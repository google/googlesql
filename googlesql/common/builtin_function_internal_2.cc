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

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/builtin_function_internal.h"
#include "googlesql/proto/options.pb.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/functions/bitwise_agg_mode.pb.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/functional/bind_front.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

class AnalyzerOptions;

static FunctionSignatureOptions SetRewriter(ResolvedASTRewrite rewriter) {
  return FunctionSignatureOptions().set_rewrite_options(
      FunctionSignatureRewriteOptions().set_rewriter(rewriter));
}

template <typename ArgumentType>
static bool AllArgumentsHaveType(const std::vector<ArgumentType>& arguments) {
  for (const ArgumentType& arg : arguments) {
    if (arg.type() == nullptr) {
      return false;
    }
  }
  return true;
}

// Supports 'ArgumentType' of either InputArgumentType or FunctionArgumentType.
//
// Example return values:
//   DATE_TIME_PART FROM TIMESTAMP
//   DATE FROM TIMESTAMP
//   TIME FROM TIMESTAMP
//   DATETIME FROM TIMESTAMP
//   DATE_TIME_PART FROM TIMESTAMP AT TIME ZONE STRING
//   DATETIME FROM TIMESTAMP [AT TIME ZONE STRING]
//
// 'include_bracket' indicates whether or not the 'AT TIME ZONE' argument
// is enclosed in brackets to indicate that the clause is optional.
// The input 'arguments' must be a valid signature for EXTRACT.
//
// If 'explicit_datepart_name' is non-empty, then the signature must not
// have a date part argument.  Otherwise, the signature must have a date
// part argument.
//
// For $extract, the date part argument is present in 'arguments', and
// 'explicit_datepart_name' is empty.
//
// For $extract_date, $extract_time, and $extract_datetime, the date part
// argument is *not* present in 'arguments', and 'explicit_datepart_name'
// is non-empty.
template <class ArgumentType>
static std::string GetExtractFunctionSignatureString(
    absl::string_view explicit_datepart_name,
    const std::vector<ArgumentType>& arguments, ProductMode product_mode,
    bool include_bracket) {
  if (arguments.empty()) {
    return "Must provide at least 1 argument";
  }
  if (!AllArgumentsHaveType(arguments)) {
    return "Unexpected types";
  }
  // The 0th argument is the one we are extracting the date part from.
  const std::string source_type_string =
      arguments[0].UserFacingName(product_mode);
  std::string datepart_string;
  std::string timezone_string;
  if (explicit_datepart_name.empty()) {
    // The date part argument is present in 'arguments', so arguments[1]
    // is the date part and arguments[2] (if present) is the time zone.
    //
    // ABSL_DCHECK validated - given the non-standard function call syntax for
    // EXTRACT, the parser enforces 2 or 3 arguments in the language.
    // Expected invariant - the 1th argument is the date part argument.
    ABSL_DCHECK(arguments[1].type()->Equivalent(types::DatePartEnumType()));

    datepart_string = arguments[1].UserFacingName(product_mode);
    if (arguments.size() == 3) {
      timezone_string = arguments[2].UserFacingName(product_mode);
    }
  } else {
    // The date part is populated from 'explicit_datepart_name' and the
    // date part argument is not present in 'arguments', so arguments[1]
    // (if present) is the time zone.
    //
    // ABSL_DCHECK validated - given the non-standard function call syntax for
    // EXTRACT, the parser enforces 2 or 3 arguments in the language and
    // the date part argument has been omitted from this signature (i.e.,
    // $extract_date, etc.).
    ABSL_DCHECK(arguments.size() == 1 || arguments.size() == 2) << arguments.size();
    datepart_string = explicit_datepart_name;
    // If present, the 1th argument is the optional timezone argument.
    if (arguments.size() == 2) {
      timezone_string = arguments[1].UserFacingName(product_mode);
    }
  }

  std::string out;
  absl::StrAppend(
      &out, datepart_string, " FROM ", source_type_string,
      (timezone_string.empty()
           ? ""
           : absl::StrCat(" ", (include_bracket ? "[" : ""), "AT TIME ZONE ",
                          timezone_string, (include_bracket ? "]" : ""))));
  return out;
}

static std::string NoMatchingSignatureForExtractFunction(
    absl::string_view explicit_datepart_name,
    absl::string_view qualified_function_name,
    const std::vector<InputArgumentType>& arguments, ProductMode product_mode) {
  if (arguments.empty()) {
    return "No matching signature for function EXTRACT with no arguments";
  }
  if (explicit_datepart_name.empty() && arguments.size() != 2 &&
      arguments.size() != 3) {
    return absl::StrCat("No matching signature for function EXTRACT with ",
                        arguments.size(), " argument",
                        arguments.size() == 1 ? "" : "s");
  }
  std::string msg =
      "No matching signature for function EXTRACT for argument types: ";
  absl::StrAppend(&msg, GetExtractFunctionSignatureString(
                            explicit_datepart_name, arguments, product_mode,
                            /*include_bracket=*/false));
  return msg;
}

static std::string ExtractSignatureText(
    absl::string_view explicit_datepart_name,
    const LanguageOptions& language_options, const Function& function,
    const FunctionSignature& signature) {
  return absl::StrCat(
      "EXTRACT(",
      GetExtractFunctionSignatureString(
          explicit_datepart_name, signature.arguments(),
          language_options.product_mode(), true /* include_bracket */),
      ")");
}

void GetDatetimeExtractFunctions(TypeFactory* type_factory,
                                 const GoogleSQLBuiltinFunctionOptions& options,
                                 NameToFunctionMap* functions) {
  const Type* date_type = type_factory->get_date();
  const Type* datetime_type = type_factory->get_datetime();
  const Type* time_type = type_factory->get_time();
  const Type* timestamp_type = type_factory->get_timestamp();
  const Type* int64_type = type_factory->get_int64();
  const Type* datepart_type = types::DatePartEnumType();
  const Type* string_type = type_factory->get_string();
  const Type* interval_type = type_factory->get_interval();

  const Function::Mode SCALAR = Function::SCALAR;
  const FunctionArgumentType::ArgumentCardinality OPTIONAL =
      FunctionArgumentType::OPTIONAL;

  // EXTRACT functions.
  const Type* extract_type = int64_type;

  InsertSimpleFunction(
      functions, options, "$extract", SCALAR,
      {{extract_type, {date_type, datepart_type}, FN_EXTRACT_FROM_DATE},
       {extract_type,
        {timestamp_type, datepart_type, {string_type, OPTIONAL}},
        FN_EXTRACT_FROM_TIMESTAMP},
       {extract_type, {datetime_type, datepart_type}, FN_EXTRACT_FROM_DATETIME},
       {extract_type, {time_type, datepart_type}, FN_EXTRACT_FROM_TIME},
       {int64_type, {interval_type, datepart_type}, FN_EXTRACT_FROM_INTERVAL}},
      FunctionOptions()
          .set_supports_safe_error_mode(false)
          .set_pre_resolution_argument_constraint(
              &CheckExtractPreResolutionArguments)
          .set_post_resolution_argument_constraint(
              &CheckExtractPostResolutionArguments)
          .set_no_matching_signature_callback(
              absl::bind_front(&NoMatchingSignatureForExtractFunction,
                               /*explicit_datepart_name=*/""))
          .set_signature_text_callback(absl::bind_front(
              &ExtractSignatureText, /*explicit_datepart_name=*/""))
          .set_get_sql_callback(&ExtractFunctionSQL));

  InsertSimpleFunction(
      functions, options, "$extract_date", SCALAR,
      {{date_type,
        {timestamp_type, {string_type, OPTIONAL}},
        FN_EXTRACT_DATE_FROM_TIMESTAMP},
       {date_type, {datetime_type}, FN_EXTRACT_DATE_FROM_DATETIME}},
      FunctionOptions()
          .set_supports_safe_error_mode(false)
          .set_pre_resolution_argument_constraint(
              &CheckExtractPreResolutionArguments)
          .set_sql_name("extract")
          .set_no_matching_signature_callback(
              absl::bind_front(&NoMatchingSignatureForExtractFunction, "DATE"))
          .set_signature_text_callback(absl::bind_front(
              &ExtractSignatureText, /*explicit_datepart_name=*/"DATE"))
          .set_get_sql_callback(
              absl::bind_front(ExtractDateOrTimeFunctionSQL, "DATE")));

  InsertSimpleFunction(
      functions, options, "$extract_time", SCALAR,
      {{time_type,
        {timestamp_type, {string_type, OPTIONAL}},
        FN_EXTRACT_TIME_FROM_TIMESTAMP},
       {time_type, {datetime_type}, FN_EXTRACT_TIME_FROM_DATETIME}},
      FunctionOptions()
          .set_supports_safe_error_mode(false)
          .set_pre_resolution_argument_constraint(
              &CheckExtractPreResolutionArguments)
          .set_sql_name("extract")
          .set_no_matching_signature_callback(
              absl::bind_front(&NoMatchingSignatureForExtractFunction, "TIME"))
          .set_signature_text_callback(absl::bind_front(
              &ExtractSignatureText, /*explicit_datepart_name=*/"TIME"))
          .set_get_sql_callback(
              absl::bind_front(ExtractDateOrTimeFunctionSQL, "TIME")));

  InsertSimpleFunction(
      functions, options, "$extract_datetime", SCALAR,
      {{datetime_type,
        {timestamp_type, {string_type, OPTIONAL}},
        FN_EXTRACT_DATETIME_FROM_TIMESTAMP}},
      FunctionOptions()
          .set_supports_safe_error_mode(false)
          .set_pre_resolution_argument_constraint(
              &CheckExtractPreResolutionArguments)
          .set_sql_name("extract")
          .set_no_matching_signature_callback(absl::bind_front(
              &NoMatchingSignatureForExtractFunction, "DATETIME"))
          .set_signature_text_callback(absl::bind_front(
              &ExtractSignatureText, /*explicit_datepart_name=*/"DATETIME"))
          .set_get_sql_callback(
              absl::bind_front(ExtractDateOrTimeFunctionSQL, "DATETIME")));
}

namespace {

std::string NoStringLiterals(const FunctionSignature& matched_signature,
                             absl::Span<const InputArgumentType> arguments) {
  for (int i = 0; i < arguments.size(); ++i) {
    const InputArgumentType& argument = arguments[i];
    if (argument.is_literal() && argument.type()->IsString()) {
      return absl::StrCat("Argument ", i + 1,
                          ": no string literal can be provided");
    }
  }
  return "";
}

}  // namespace

void GetDatetimeConversionFunctions(
    TypeFactory* type_factory, const GoogleSQLBuiltinFunctionOptions& options,
    NameToFunctionMap* functions) {
  const Type* date_type = type_factory->get_date();
  const Type* timestamp_type = type_factory->get_timestamp();
  const Type* datetime_type = type_factory->get_datetime();
  const Type* int64_type = type_factory->get_int64();
  const Type* uint64_type = type_factory->get_uint64();
  const Type* string_type = type_factory->get_string();

  const Function::Mode SCALAR = Function::SCALAR;
  const FunctionArgumentType::ArgumentCardinality OPTIONAL =
      FunctionArgumentType::OPTIONAL;

  // Conversion functions from integer/string/date/timestamp to
  // date/timestamp.
  InsertFunction(functions, options, "date_from_unix_date", SCALAR,
                 {{date_type, {int64_type}, FN_DATE_FROM_UNIX_DATE}});

  FunctionSignatureOptions date_time_constructor_options =
      FunctionSignatureOptions()
          .set_constraints(&NoStringLiterals)
          .AddRequiredLanguageFeature(FEATURE_DATE_TIME_CONSTRUCTORS);
  InsertFunction(functions, options, "date", SCALAR,
                 {
                     {date_type,
                      {timestamp_type, {string_type, OPTIONAL}},
                      FN_DATE_FROM_TIMESTAMP},
                     {date_type, {datetime_type}, FN_DATE_FROM_DATETIME},
                     {date_type,
                      {int64_type, int64_type, int64_type},
                      FN_DATE_FROM_YEAR_MONTH_DAY},
                     {date_type,
                      {date_type},
                      FN_DATE_FROM_DATE,
                      date_time_constructor_options},
                     {date_type,
                      {string_type},
                      FN_DATE_FROM_STRING,
                      date_time_constructor_options},
                 });

  FunctionSignatureOptions timestamp_from_unix_uint64_options =
      FunctionSignatureOptions().AddRequiredLanguageFeature(
          FEATURE_TIMESTAMP_FROM_UNIX_FUNCTIONS_WITH_UINT64);

  InsertFunction(
      functions, options, "timestamp_from_unix_seconds", SCALAR,
      {
          {timestamp_type, {int64_type}, FN_TIMESTAMP_FROM_UNIX_SECONDS_INT64},
          {timestamp_type,
           {timestamp_type},
           FN_TIMESTAMP_FROM_UNIX_SECONDS_TIMESTAMP},
          {timestamp_type,
           {uint64_type},
           FN_TIMESTAMP_FROM_UNIX_SECONDS_UINT64,
           timestamp_from_unix_uint64_options},
      });
  InsertFunction(
      functions, options, "timestamp_from_unix_millis", SCALAR,
      {
          {timestamp_type, {int64_type}, FN_TIMESTAMP_FROM_UNIX_MILLIS_INT64},
          {timestamp_type,
           {timestamp_type},
           FN_TIMESTAMP_FROM_UNIX_MILLIS_TIMESTAMP},
          {timestamp_type,
           {uint64_type},
           FN_TIMESTAMP_FROM_UNIX_MILLIS_UINT64,
           timestamp_from_unix_uint64_options},
      });
  InsertFunction(
      functions, options, "timestamp_from_unix_micros", SCALAR,
      {
          {timestamp_type, {int64_type}, FN_TIMESTAMP_FROM_UNIX_MICROS_INT64},
          {timestamp_type,
           {timestamp_type},
           FN_TIMESTAMP_FROM_UNIX_MICROS_TIMESTAMP},
          {timestamp_type,
           {uint64_type},
           FN_TIMESTAMP_FROM_UNIX_MICROS_UINT64,
           timestamp_from_unix_uint64_options},
      });
  InsertFunction(functions, options, "timestamp", SCALAR,
                 {{timestamp_type,
                   {string_type, {string_type, OPTIONAL}},
                   FN_TIMESTAMP_FROM_STRING},
                  {timestamp_type,
                   {date_type, {string_type, OPTIONAL}},
                   FN_TIMESTAMP_FROM_DATE},
                  {timestamp_type,
                   {datetime_type, {string_type, OPTIONAL}},
                   FN_TIMESTAMP_FROM_DATETIME},
                  {timestamp_type,
                   {timestamp_type},
                   FN_TIMESTAMP_FROM_TIMESTAMP,
                   FunctionSignatureOptions().AddRequiredLanguageFeature(
                       FEATURE_DATE_TIME_CONSTRUCTORS)}});
  InsertSimpleFunction(
      functions, options, "timestamp_seconds", SCALAR,
      {{timestamp_type, {int64_type}, FN_TIMESTAMP_FROM_INT64_SECONDS}});
  InsertSimpleFunction(
      functions, options, "timestamp_millis", SCALAR,
      {{timestamp_type, {int64_type}, FN_TIMESTAMP_FROM_INT64_MILLIS}});
  InsertSimpleFunction(
      functions, options, "timestamp_micros", SCALAR,
      {{timestamp_type, {int64_type}, FN_TIMESTAMP_FROM_INT64_MICROS}});

  const Type* unix_date_type = int64_type;

  // Conversion functions from date/timestamp to integer and string.
  InsertSimpleFunction(functions, options, "unix_date", SCALAR,
                       {{unix_date_type, {date_type}, FN_UNIX_DATE}});

  InsertSimpleFunction(
      functions, options, "unix_seconds", SCALAR,
      {{int64_type, {timestamp_type}, FN_UNIX_SECONDS_FROM_TIMESTAMP}});
  InsertSimpleFunction(
      functions, options, "unix_millis", SCALAR,
      {{int64_type, {timestamp_type}, FN_UNIX_MILLIS_FROM_TIMESTAMP}});
  InsertSimpleFunction(
      functions, options, "unix_micros", SCALAR,
      {{int64_type, {timestamp_type}, FN_UNIX_MICROS_FROM_TIMESTAMP}});
}

void GetTimeAndDatetimeConstructionAndConversionFunctions(
    TypeFactory* type_factory, const GoogleSQLBuiltinFunctionOptions& options,
    NameToFunctionMap* functions) {
  const Type* date_type = type_factory->get_date();
  const Type* datetime_type = type_factory->get_datetime();
  const Type* time_type = type_factory->get_time();
  const Type* timestamp_type = type_factory->get_timestamp();
  const Type* int64_type = type_factory->get_int64();
  const Type* string_type = type_factory->get_string();

  const Function::Mode SCALAR = Function::SCALAR;
  const FunctionArgumentType::ArgumentCardinality OPTIONAL =
      FunctionArgumentType::OPTIONAL;

  FunctionOptions time_and_datetime_function_options =
      FunctionOptions().AddRequiredLanguageFeature(FEATURE_CIVIL_TIME);
  FunctionSignatureOptions date_time_constructor_options =
      FunctionSignatureOptions().AddRequiredLanguageFeature(
          FEATURE_DATE_TIME_CONSTRUCTORS);

  InsertFunction(functions, options, "time", SCALAR,
                 {{time_type,
                   {
                       int64_type,  // hour
                       int64_type,  // minute
                       int64_type,  // second
                   },
                   FN_TIME_FROM_HOUR_MINUTE_SECOND},
                  {time_type,
                   {
                       timestamp_type,           // timestamp
                       {string_type, OPTIONAL},  // timezone
                   },
                   FN_TIME_FROM_TIMESTAMP},
                  {time_type,
                   {
                       datetime_type,  // datetime
                   },
                   FN_TIME_FROM_DATETIME},
                  {time_type,
                   {time_type},
                   FN_TIME_FROM_TIME,
                   date_time_constructor_options}},
                 time_and_datetime_function_options);

  InsertFunction(functions, options, "datetime", SCALAR,
                 {{datetime_type,
                   {
                       int64_type,  // year
                       int64_type,  // month
                       int64_type,  // day
                       int64_type,  // hour
                       int64_type,  // minute
                       int64_type,  // second
                   },
                   FN_DATETIME_FROM_YEAR_MONTH_DAY_HOUR_MINUTE_SECOND},
                  {datetime_type,
                   {
                       date_type,  // date
                       time_type,  // time
                   },
                   FN_DATETIME_FROM_DATE_AND_TIME},
                  {datetime_type,
                   {
                       timestamp_type,           // timestamp
                       {string_type, OPTIONAL},  // timezone
                   },
                   FN_DATETIME_FROM_TIMESTAMP},
                  {datetime_type,
                   {
                       date_type,  // date
                   },
                   FN_DATETIME_FROM_DATE},
                  {datetime_type,
                   {datetime_type},
                   FN_DATETIME_FROM_DATETIME,
                   date_time_constructor_options},
                  {datetime_type,
                   {string_type},
                   FN_DATETIME_FROM_STRING,
                   date_time_constructor_options}},
                 time_and_datetime_function_options);
}

void GetDatetimeCurrentFunctions(TypeFactory* type_factory,
                                 const GoogleSQLBuiltinFunctionOptions& options,
                                 NameToFunctionMap* functions) {
  const Type* date_type = type_factory->get_date();
  const Type* timestamp_type = type_factory->get_timestamp();
  const Type* string_type = type_factory->get_string();

  const Function::Mode SCALAR = Function::SCALAR;
  const FunctionArgumentType::ArgumentCardinality OPTIONAL =
      FunctionArgumentType::OPTIONAL;

  FunctionOptions function_is_stable;
  function_is_stable.set_volatility(FunctionEnums::STABLE);

  InsertSimpleFunction(
      functions, options, "current_date", SCALAR,
      {{date_type, {{string_type, OPTIONAL}}, FN_CURRENT_DATE}},
      function_is_stable);
  InsertSimpleFunction(functions, options, "current_timestamp", SCALAR,
                       {{timestamp_type, {}, FN_CURRENT_TIMESTAMP}},
                       function_is_stable);

  const Type* datetime_type = type_factory->get_datetime();
  const Type* time_type = type_factory->get_time();
  FunctionOptions require_civil_time_types(function_is_stable);
  require_civil_time_types.AddRequiredLanguageFeature(FEATURE_CIVIL_TIME);
  InsertSimpleFunction(
      functions, options, "current_datetime", SCALAR,
      {{datetime_type, {{string_type, OPTIONAL}}, FN_CURRENT_DATETIME}},
      require_civil_time_types);
  InsertSimpleFunction(
      functions, options, "current_time", SCALAR,
      {{time_type, {{string_type, OPTIONAL}}, FN_CURRENT_TIME}},
      require_civil_time_types);
}

// Disallows string literals and query parameters from matching signature in
// arguments at specified positions.
template <int arg_index1, int arg_index2 = -1>
std::string NoLiteralOrParameterString(
    const FunctionSignature& matched_signature,
    absl::Span<const InputArgumentType> arguments) {
  for (int i = 0; i < arguments.size(); i++) {
    if (i != arg_index1 && i != arg_index2) {
      continue;
    }
    const auto& argument = arguments[i];
    if ((argument.is_literal() || argument.is_query_parameter()) &&
        argument.type()->IsString()) {
      return absl::StrCat(
          "Argument ", i + 1,
          ": no literal or query parameter string can be provided");
    }
  }
  return "";
}

FunctionOptions SetDateAddSubFunctionOptions(
    const GoogleSQLBuiltinFunctionOptions& options,
    absl::string_view function_name, absl::string_view alias_name) {
  FunctionOptions function_options;
  function_options
      .set_no_matching_signature_callback(
          &NoMatchingSignatureForDateOrTimeAddOrSubFunction)
      .set_pre_resolution_argument_constraint(
          absl::bind_front(&CheckDateDatetimeTimestampAddSubArguments,
                           std::string(function_name)))
      .set_get_sql_callback(absl::bind_front(&DateAddOrSubFunctionSQL,
                                             std::string(function_name)));
  if (options.language_options.LanguageFeatureEnabled(
          FEATURE_ALIASES_FOR_STRING_AND_DATE_FUNCTIONS)) {
    function_options.set_alias_name(alias_name);
  }
  return function_options;
}

void GetDatetimeAddSubFunctions(TypeFactory* type_factory,
                                const GoogleSQLBuiltinFunctionOptions& options,
                                NameToFunctionMap* functions) {
  const Type* date_type = type_factory->get_date();
  const Type* datetime_type = type_factory->get_datetime();
  const Type* time_type = type_factory->get_time();
  const Type* timestamp_type = type_factory->get_timestamp();
  const Type* int64_type = type_factory->get_int64();
  const Type* datepart_type = types::DatePartEnumType();

  const Function::Mode SCALAR = Function::SCALAR;

  FunctionOptions require_civil_time_types;
  require_civil_time_types.AddRequiredLanguageFeature(FEATURE_CIVIL_TIME);

  FunctionSignatureOptions extended_datetime_signatures =
      FunctionSignatureOptions()
          .AddRequiredLanguageFeature(FEATURE_EXTENDED_DATE_TIME_SIGNATURES)
          .set_is_aliased_signature(true)
          .set_constraints(&NoLiteralOrParameterString<0>);

  InsertFunction(
      functions, options, "date_add", SCALAR,
      {
          {date_type, {date_type, int64_type, datepart_type}, FN_DATE_ADD_DATE},
          {datetime_type,
           {datetime_type, int64_type, datepart_type},
           FN_DATETIME_ADD,
           extended_datetime_signatures},
          {timestamp_type,
           {timestamp_type, int64_type, datepart_type},
           FN_TIMESTAMP_ADD,
           extended_datetime_signatures},
      },
      SetDateAddSubFunctionOptions(options, "DATE_ADD", "adddate"));

  InsertFunction(
      functions, options, "datetime_add", SCALAR,
      {
          {datetime_type,
           {datetime_type, int64_type, datepart_type},
           FN_DATETIME_ADD},
          {timestamp_type,
           {timestamp_type, int64_type, datepart_type},
           FN_TIMESTAMP_ADD,
           extended_datetime_signatures},
      },
      require_civil_time_types.Copy()
          .set_no_matching_signature_callback(
              &NoMatchingSignatureForDateOrTimeAddOrSubFunction)
          .set_pre_resolution_argument_constraint(absl::bind_front(
              &CheckDateDatetimeTimestampAddSubArguments, "DATETIME_ADD"))
          .set_get_sql_callback(
              absl::bind_front(&DateAddOrSubFunctionSQL, "DATETIME_ADD")));

  InsertSimpleFunction(
      functions, options, "time_add", SCALAR,
      {
          {time_type, {time_type, int64_type, datepart_type}, FN_TIME_ADD},
      },
      require_civil_time_types.Copy()
          .set_no_matching_signature_callback(
              &NoMatchingSignatureForDateOrTimeAddOrSubFunction)
          .set_pre_resolution_argument_constraint(
              absl::bind_front(&CheckTimeAddSubArguments, "TIME_ADD"))
          .set_get_sql_callback(
              absl::bind_front(&DateAddOrSubFunctionSQL, "TIME_ADD")));

  InsertFunction(
      functions, options, "timestamp_add", SCALAR,
      {
          {timestamp_type,
           {timestamp_type, int64_type, datepart_type},
           FN_TIMESTAMP_ADD},
          {datetime_type,
           {datetime_type, int64_type, datepart_type},
           FN_DATETIME_ADD,
           extended_datetime_signatures},
      },
      FunctionOptions()
          .set_no_matching_signature_callback(
              &NoMatchingSignatureForDateOrTimeAddOrSubFunction)
          .set_pre_resolution_argument_constraint(absl::bind_front(
              &CheckDateDatetimeTimestampAddSubArguments, "TIMESTAMP_ADD"))
          .set_get_sql_callback(
              absl::bind_front(&DateAddOrSubFunctionSQL, "TIMESTAMP_ADD")));

  InsertFunction(
      functions, options, "date_sub", SCALAR,
      {
          {date_type, {date_type, int64_type, datepart_type}, FN_DATE_SUB_DATE},
          {datetime_type,
           {datetime_type, int64_type, datepart_type},
           FN_DATETIME_SUB,
           extended_datetime_signatures},
          {timestamp_type,
           {timestamp_type, int64_type, datepart_type},
           FN_TIMESTAMP_SUB,
           extended_datetime_signatures},
      },
      SetDateAddSubFunctionOptions(options, "DATE_SUB", "subdate"));

  InsertFunction(
      functions, options, "datetime_sub", SCALAR,
      {
          {datetime_type,
           {datetime_type, int64_type, datepart_type},
           FN_DATETIME_SUB},
          {timestamp_type,
           {timestamp_type, int64_type, datepart_type},
           FN_TIMESTAMP_SUB,
           extended_datetime_signatures},
      },
      require_civil_time_types.Copy()
          .set_no_matching_signature_callback(
              &NoMatchingSignatureForDateOrTimeAddOrSubFunction)
          .set_pre_resolution_argument_constraint(absl::bind_front(
              &CheckDateDatetimeTimestampAddSubArguments, "DATETIME_SUB"))
          .set_get_sql_callback(
              absl::bind_front(&DateAddOrSubFunctionSQL, "DATETIME_SUB")));

  InsertSimpleFunction(
      functions, options, "time_sub", SCALAR,
      {
          {time_type, {time_type, int64_type, datepart_type}, FN_TIME_SUB},
      },
      require_civil_time_types.Copy()
          .set_no_matching_signature_callback(
              &NoMatchingSignatureForDateOrTimeAddOrSubFunction)
          .set_pre_resolution_argument_constraint(
              absl::bind_front(&CheckTimeAddSubArguments, "TIME_SUB"))
          .set_get_sql_callback(
              absl::bind_front(&DateAddOrSubFunctionSQL, "TIME_SUB")));

  InsertFunction(
      functions, options, "timestamp_sub", SCALAR,
      {
          {timestamp_type,
           {timestamp_type, int64_type, datepart_type},
           FN_TIMESTAMP_SUB},
          {datetime_type,
           {datetime_type, int64_type, datepart_type},
           FN_DATETIME_SUB,
           extended_datetime_signatures},
      },
      FunctionOptions()
          .set_no_matching_signature_callback(
              &NoMatchingSignatureForDateOrTimeAddOrSubFunction)
          .set_pre_resolution_argument_constraint(absl::bind_front(
              &CheckDateDatetimeTimestampAddSubArguments, "TIMESTAMP_SUB"))
          .set_get_sql_callback(
              absl::bind_front(&DateAddOrSubFunctionSQL, "TIMESTAMP_SUB")));

  constexpr absl::string_view kAddMonthsDateSQL = R"sql(
      IF(
        date_arg IS NULL OR months_arg IS NULL,
        NULL,
        IF(
          date_arg = LAST_DAY(date_arg, MONTH),
          LAST_DAY(DATE_ADD(date_arg, INTERVAL months_arg MONTH)),
          DATE_ADD(date_arg, INTERVAL months_arg MONTH)))
  )sql";

  constexpr absl::string_view kAddMonthsDatetimeSQL = R"sql(
      IF(
        datetime_arg IS NULL OR months_arg IS NULL,
        NULL,
        IF(
          -- Note thats LAST_DAY(DATETIME) yields a DATE, not a DATETIME.
          DATE(datetime_arg) = LAST_DAY(datetime_arg, MONTH),
          DATETIME(
            LAST_DAY(DATETIME_ADD(datetime_arg, INTERVAL months_arg MONTH)),
            TIME(datetime_arg)),
          DATETIME_ADD(datetime_arg, INTERVAL months_arg MONTH)))
  )sql";

  InsertFunction(
      functions, options, "add_months", SCALAR,
      {
          {date_type,
           {FunctionArgumentType(
                date_type, FunctionArgumentTypeOptions().set_argument_name(
                               "date_arg", kPositionalOnly)),
            FunctionArgumentType(
                int64_type, FunctionArgumentTypeOptions().set_argument_name(
                                "months_arg", kPositionalOnly))},
           FN_ADD_MONTHS_DATE,
           SetDefinitionForInlining(kAddMonthsDateSQL)},
          {datetime_type,
           {FunctionArgumentType(
                datetime_type, FunctionArgumentTypeOptions().set_argument_name(
                                   "datetime_arg", kPositionalOnly)),
            FunctionArgumentType(
                int64_type, FunctionArgumentTypeOptions().set_argument_name(
                                "months_arg", kPositionalOnly))},
           FN_ADD_MONTHS_DATETIME,
           SetDefinitionForInlining(kAddMonthsDatetimeSQL)
               .AddRequiredLanguageFeature(FEATURE_CIVIL_TIME)},
      },
      FunctionOptions().AddRequiredLanguageFeature(
          FEATURE_ADDITIONAL_DATE_TIME_FUNCTIONS));
}

void GetDatetimeDiffTruncLastNextFunctions(
    TypeFactory* type_factory, const GoogleSQLBuiltinFunctionOptions& options,
    NameToFunctionMap* functions) {
  const Type* date_type = type_factory->get_date();
  const Type* datetime_type = type_factory->get_datetime();
  const Type* time_type = type_factory->get_time();
  const Type* timestamp_type = type_factory->get_timestamp();
  const Type* int64_type = type_factory->get_int64();
  const Type* string_type = type_factory->get_string();
  const Type* datepart_type = types::DatePartEnumType();
  const Type* double_type = type_factory->get_double();

  const Function::Mode SCALAR = Function::SCALAR;
  const FunctionArgumentType::ArgumentCardinality OPTIONAL =
      FunctionArgumentType::OPTIONAL;

  FunctionOptions require_civil_time_types;
  require_civil_time_types.AddRequiredLanguageFeature(FEATURE_CIVIL_TIME);

  const Type* diff_type = int64_type;

  FunctionSignatureOptions extended_datetime_signatures =
      FunctionSignatureOptions()
          .AddRequiredLanguageFeature(FEATURE_EXTENDED_DATE_TIME_SIGNATURES)
          .set_is_aliased_signature(true)
          .set_constraints(&NoLiteralOrParameterString<0, 1>);

  InsertFunction(
      functions, options, "date_diff", SCALAR,
      {
          {diff_type, {date_type, date_type, datepart_type}, FN_DATE_DIFF_DATE},
          {int64_type,
           {datetime_type, datetime_type, datepart_type},
           FN_DATETIME_DIFF,
           extended_datetime_signatures},
          {int64_type,
           {timestamp_type, timestamp_type, datepart_type},
           FN_TIMESTAMP_DIFF,
           extended_datetime_signatures},
      },
      FunctionOptions().set_pre_resolution_argument_constraint(absl::bind_front(
          &CheckDateDatetimeTimeTimestampDiffArguments, "DATE_DIFF")));

  InsertFunction(
      functions, options, "datetime_diff", SCALAR,
      {
          {int64_type,
           {datetime_type, datetime_type, datepart_type},
           FN_DATETIME_DIFF},
          {int64_type,
           {timestamp_type, timestamp_type, datepart_type},
           FN_TIMESTAMP_DIFF,
           extended_datetime_signatures},
      },
      require_civil_time_types.Copy().set_post_resolution_argument_constraint(
          [](const FunctionSignature& /*signature*/,
             const std::vector<InputArgumentType>& arguments,
             const LanguageOptions& language_options) {
            return CheckDateDatetimeTimeTimestampDiffArguments(
                "DATETIME_DIFF", arguments, language_options);
          }));

  InsertSimpleFunction(
      functions, options, "time_diff", SCALAR,
      {
          {int64_type, {time_type, time_type, datepart_type}, FN_TIME_DIFF},
      },
      require_civil_time_types.Copy().set_pre_resolution_argument_constraint(
          absl::bind_front(&CheckDateDatetimeTimeTimestampDiffArguments,
                           "TIME_DIFF")));

  InsertFunction(functions, options, "timestamp_diff", SCALAR,
                 {
                     {int64_type,
                      {timestamp_type, timestamp_type, datepart_type},
                      FN_TIMESTAMP_DIFF},
                     {int64_type,
                      {datetime_type, datetime_type, datepart_type},
                      FN_DATETIME_DIFF,
                      extended_datetime_signatures},
                 },
                 FunctionOptions().set_post_resolution_argument_constraint(
                     [](const FunctionSignature& /*signature*/,
                        const std::vector<InputArgumentType>& arguments,
                        const LanguageOptions& language_options) {
                       return CheckDateDatetimeTimeTimestampDiffArguments(
                           "TIMESTAMP_DIFF", arguments, language_options);
                     }));

  InsertFunction(
      functions, options, "date_trunc", SCALAR,
      {
          {date_type, {date_type, datepart_type}, FN_DATE_TRUNC_DATE},
          {datetime_type,
           {datetime_type, datepart_type},
           FN_DATETIME_TRUNC,
           extended_datetime_signatures},
          {timestamp_type,
           {timestamp_type, datepart_type, {string_type, OPTIONAL}},
           FN_TIMESTAMP_TRUNC,
           extended_datetime_signatures},
      },
      FunctionOptions().set_pre_resolution_argument_constraint(absl::bind_front(
          &CheckDateDatetimeTimeTimestampTruncArguments, "DATE_TRUNC")));

  InsertFunction(
      functions, options, "datetime_trunc", SCALAR,
      {
          {datetime_type, {datetime_type, datepart_type}, FN_DATETIME_TRUNC},
          {timestamp_type,
           {timestamp_type, datepart_type, {string_type, OPTIONAL}},
           FN_TIMESTAMP_TRUNC,
           extended_datetime_signatures},
      },
      require_civil_time_types.Copy().set_pre_resolution_argument_constraint(
          absl::bind_front(&CheckDateDatetimeTimeTimestampTruncArguments,
                           "DATETIME_TRUNC")));

  InsertSimpleFunction(
      functions, options, "time_trunc", SCALAR,
      {{time_type, {time_type, datepart_type}, FN_TIME_TRUNC}},
      require_civil_time_types.Copy().set_pre_resolution_argument_constraint(
          absl::bind_front(&CheckDateDatetimeTimeTimestampTruncArguments,
                           "TIME_TRUNC")));

  InsertFunction(
      functions, options, "timestamp_trunc", SCALAR,
      {
          {timestamp_type,
           {timestamp_type, datepart_type, {string_type, OPTIONAL}},
           FN_TIMESTAMP_TRUNC},
          {datetime_type,
           {datetime_type, datepart_type},
           FN_DATETIME_TRUNC,
           extended_datetime_signatures},
      },
      FunctionOptions().set_pre_resolution_argument_constraint(absl::bind_front(
          &CheckDateDatetimeTimeTimestampTruncArguments, "TIMESTAMP_TRUNC")));

  if (options.language_options.LanguageFeatureEnabled(
          FEATURE_ADDITIONAL_STRING_FUNCTIONS) &&
      options.language_options.LanguageFeatureEnabled(FEATURE_CIVIL_TIME)) {
    InsertSimpleFunction(
        functions, options, "last_day", SCALAR,
        {{date_type, {date_type, {datepart_type, OPTIONAL}}, FN_LAST_DAY_DATE},
         {date_type,
          {datetime_type, {datepart_type, OPTIONAL}},
          FN_LAST_DAY_DATETIME}},
        FunctionOptions().set_pre_resolution_argument_constraint(
            absl::bind_front(&CheckLastDayArguments, "LAST_DAY")));
  }

  constexpr absl::string_view kNextDayDateSQL = R"sql(
    CASE
      WHEN date_arg IS NULL OR dow_arg IS NULL
        THEN NULL
      WHEN LOWER(dow_arg) IN ('mon', 'monday')
        THEN DATE_TRUNC(DATE_ADD(date_arg, INTERVAL 1 WEEK), WEEK(MONDAY))
      WHEN LOWER(dow_arg) IN ('tue', 'tuesday')
        THEN DATE_TRUNC(DATE_ADD(date_arg, INTERVAL 1 WEEK), WEEK(TUESDAY))
      WHEN LOWER(dow_arg) IN ('wed', 'wednesday')
        THEN DATE_TRUNC(DATE_ADD(date_arg, INTERVAL 1 WEEK), WEEK(WEDNESDAY))
      WHEN LOWER(dow_arg) IN ('thu', 'thursday')
        THEN DATE_TRUNC(DATE_ADD(date_arg, INTERVAL 1 WEEK), WEEK(THURSDAY))
      WHEN LOWER(dow_arg) IN ('fri', 'friday')
        THEN DATE_TRUNC(DATE_ADD(date_arg, INTERVAL 1 WEEK), WEEK(FRIDAY))
      WHEN LOWER(dow_arg) IN ('sat', 'saturday')
        THEN DATE_TRUNC(DATE_ADD(date_arg, INTERVAL 1 WEEK), WEEK(SATURDAY))
      WHEN LOWER(dow_arg) IN ('sun', 'sunday')
        THEN DATE_TRUNC(DATE_ADD(date_arg, INTERVAL 1 WEEK), WEEK(SUNDAY))
      ELSE
        ERROR(CONCAT('Unknown day of week: ', dow_arg))
      END
  )sql";

  constexpr absl::string_view kNextDayDatetimeSQL = R"sql(
    NEXT_DAY(DATE(datetime_arg), dow_arg)
  )sql";

  InsertFunction(
      functions, options, "next_day", SCALAR,
      {
          {date_type,
           {FunctionArgumentType(
                date_type, FunctionArgumentTypeOptions().set_argument_name(
                               "date_arg", kPositionalOnly)),
            FunctionArgumentType(
                string_type, FunctionArgumentTypeOptions().set_argument_name(
                                 "dow_arg", kPositionalOnly))},
           FN_NEXT_DAY_DATE,
           SetDefinitionForInlining(kNextDayDateSQL)},
          {date_type,
           {FunctionArgumentType(
                datetime_type, FunctionArgumentTypeOptions().set_argument_name(
                                   "datetime_arg", kPositionalOnly)),
            FunctionArgumentType(
                string_type, FunctionArgumentTypeOptions().set_argument_name(
                                 "dow_arg", kPositionalOnly))},
           FN_NEXT_DAY_DATETIME,
           SetDefinitionForInlining(kNextDayDatetimeSQL)
               .AddRequiredLanguageFeature(FEATURE_CIVIL_TIME)},
      },
      FunctionOptions().AddRequiredLanguageFeature(
          FEATURE_ADDITIONAL_DATE_TIME_FUNCTIONS));

  constexpr absl::string_view kMonthsBetweenDateSQL = R"sql(
    WITH(
      d1 AS EXTRACT(DAY FROM date1),
      d2 AS EXTRACT(DAY FROM date2),
      (
        -- Compute whole month portion ignoring day, then add
        -- in the fractional part, if any.
        DATE_DIFF(date1, date2, MONTH) +
        IF(d1 = d2 OR
           (LAST_DAY(date1) = date1 AND
            LAST_DAY(date2) = date2),
           -- Same day or both EOM, no fractional part.
           0,
           -- Otherwise compute fractional portion, may be negative.
           (d1 - d2) / 31)))
  )sql";

  constexpr absl::string_view kMonthsBetweenDatetimeSQL = R"sql(
    WITH(
      d1 AS EXTRACT(DAY FROM datetime1),
      d2 AS EXTRACT(DAY FROM datetime2),
      t1 AS EXTRACT(TIME FROM datetime1),
      t2 AS EXTRACT(TIME FROM datetime2),
      nanos_per_day AS 1000*1000*1000*60*60*24,
      (
        -- Compute whole month portion ignoring day and time, then add
        -- in the fractional part, if any.
        DATETIME_DIFF(datetime1, datetime2, MONTH) +
        IF(d1 = d2 OR
           (LAST_DAY(datetime1) = EXTRACT(DATE FROM datetime1) AND
            LAST_DAY(datetime2) = EXTRACT(DATE FROM datetime2)),
           -- Same day or both EOM, no fractional part.
           0,
           -- Otherwise compute fractional portion, may be negative.
           (d1 - d2 + TIME_DIFF(t1,t2,NANOSECOND)/nanos_per_day) / 31)))
  )sql";

  InsertFunction(
      functions, options, "months_between", SCALAR,
      {
          {double_type,
           {FunctionArgumentType(
                date_type, FunctionArgumentTypeOptions().set_argument_name(
                               "date1", kPositionalOnly)),
            FunctionArgumentType(
                date_type, FunctionArgumentTypeOptions().set_argument_name(
                               "date2", kPositionalOnly))},
           FN_MONTHS_BETWEEN_DATE,
           SetDefinitionForInlining(kMonthsBetweenDateSQL)},
          {double_type,
           {FunctionArgumentType(
                datetime_type, FunctionArgumentTypeOptions().set_argument_name(
                                   "datetime1", kPositionalOnly)),
            FunctionArgumentType(
                datetime_type, FunctionArgumentTypeOptions().set_argument_name(
                                   "datetime2", kPositionalOnly))},
           FN_MONTHS_BETWEEN_DATETIME,
           SetDefinitionForInlining(kMonthsBetweenDatetimeSQL)
               .AddRequiredLanguageFeature(FEATURE_CIVIL_TIME)},
      },
      FunctionOptions().AddRequiredLanguageFeature(
          FEATURE_ADDITIONAL_DATE_TIME_FUNCTIONS));
}

void GetDatetimeBucketFunctions(TypeFactory* type_factory,
                                const GoogleSQLBuiltinFunctionOptions& options,
                                NameToFunctionMap* functions) {
  if (!options.language_options.LanguageFeatureEnabled(
          FEATURE_TIME_BUCKET_FUNCTIONS) ||
      !options.language_options.LanguageFeatureEnabled(FEATURE_INTERVAL_TYPE)) {
    return;
  }
  const Type* timestamp_type = type_factory->get_timestamp();
  const Type* datetime_type = type_factory->get_datetime();
  const Type* date_type = type_factory->get_date();
  const Type* interval_type = type_factory->get_interval();

  const Function::Mode SCALAR = Function::SCALAR;
  const FunctionArgumentType::ArgumentCardinality OPTIONAL =
      FunctionArgumentType::OPTIONAL;

  FunctionSignatureOptions extended_datetime_signatures =
      FunctionSignatureOptions()
          .AddRequiredLanguageFeature(FEATURE_EXTENDED_DATE_TIME_SIGNATURES)
          .set_is_aliased_signature(true)
          .set_constraints(&NoLiteralOrParameterString<0, 2>);

  InsertFunction(
      functions, options, "date_bucket", SCALAR,
      {
          {date_type,
           {date_type, interval_type, {date_type, OPTIONAL}},
           FN_DATE_BUCKET},
          {datetime_type,
           {datetime_type, interval_type, {datetime_type, OPTIONAL}},
           FN_DATETIME_BUCKET,
           extended_datetime_signatures},
          {timestamp_type,
           {timestamp_type, interval_type, {timestamp_type, OPTIONAL}},
           FN_TIMESTAMP_BUCKET,
           extended_datetime_signatures},
      });

  InsertFunction(
      functions, options, "datetime_bucket", SCALAR,
      {
          {datetime_type,
           {datetime_type, interval_type, {datetime_type, OPTIONAL}},
           FN_DATETIME_BUCKET},
          {date_type,
           {date_type, interval_type, {date_type, OPTIONAL}},
           FN_DATE_BUCKET,
           extended_datetime_signatures},
          {timestamp_type,
           {timestamp_type, interval_type, {timestamp_type, OPTIONAL}},
           FN_TIMESTAMP_BUCKET,
           extended_datetime_signatures},
      },
      FunctionOptions().AddRequiredLanguageFeature(FEATURE_CIVIL_TIME));
  InsertFunction(
      functions, options, "timestamp_bucket", SCALAR,
      {
          {timestamp_type,
           {timestamp_type, interval_type, {timestamp_type, OPTIONAL}},
           FN_TIMESTAMP_BUCKET},
          {datetime_type,
           {datetime_type, interval_type, {datetime_type, OPTIONAL}},
           FN_DATETIME_BUCKET,
           extended_datetime_signatures},
          {date_type,
           {date_type, interval_type, {date_type, OPTIONAL}},
           FN_DATE_BUCKET,
           extended_datetime_signatures},
      });
}

static absl::Status CheckParseTimestampPrecisionArgumentValue(
    const FunctionSignature& signature,
    absl::Span<const InputArgumentType> arguments,
    const LanguageOptions& language_options) {
  if (signature.context_id() != FN_PARSE_TIMESTAMP_WITH_PRECISION &&
      signature.context_id() !=
          FN_PARSE_TIMESTAMP_WITH_PRECISION_AND_TIMEZONE) {
    return absl::OkStatus();
  }

  GOOGLESQL_RET_CHECK_GE(arguments.size(), 3);
  GOOGLESQL_RET_CHECK_LE(arguments.size(), 4);

  std::string precision_error =
      "Precision argument must be an INT64 literal with a value of ";
  if (language_options.LanguageFeatureEnabled(FEATURE_TIMESTAMP_PICOS)) {
    absl::StrAppend(&precision_error, "0, 3, 6, 9, or 12");
  } else if (language_options.LanguageFeatureEnabled(FEATURE_TIMESTAMP_NANOS)) {
    absl::StrAppend(&precision_error, "0, 3, 6, or 9");
  } else {
    absl::StrAppend(&precision_error, "0, 3, or 6");
  }

  const InputArgumentType& precision_arg = arguments[2];
  GOOGLESQL_RET_CHECK(precision_arg.type()->IsInt64()) << precision_arg.DebugString();

  if (!precision_arg.is_literal() || precision_arg.is_null()) {
    return absl::InvalidArgumentError(precision_error);
  }

  const Value* value = precision_arg.literal_value();
  GOOGLESQL_RET_CHECK(value != nullptr);
  GOOGLESQL_RET_CHECK(value->type()->IsInt64()) << value->DebugString();
  switch (value->int64_value()) {
    case 0:
    case 3:
    case 6:
      break;
    case 9:
      if (!language_options.LanguageFeatureEnabled(FEATURE_TIMESTAMP_NANOS)) {
        return absl::InvalidArgumentError(precision_error);
      }
      break;
    case 12:
      if (!language_options.LanguageFeatureEnabled(FEATURE_TIMESTAMP_PICOS)) {
        return absl::InvalidArgumentError(precision_error);
      }
      break;
    default:
      return absl::InvalidArgumentError(precision_error);
  }
  return absl::OkStatus();
}

void GetDatetimeFormatFunctions(TypeFactory* type_factory,
                                const GoogleSQLBuiltinFunctionOptions& options,
                                NameToFunctionMap* functions) {
  const Type* date_type = type_factory->get_date();
  const Type* datetime_type = type_factory->get_datetime();
  const Type* time_type = type_factory->get_time();
  const Type* timestamp_type = type_factory->get_timestamp();
  const Type* string_type = type_factory->get_string();

  const Function::Mode SCALAR = Function::SCALAR;
  const FunctionArgumentType::ArgumentCardinality OPTIONAL =
      FunctionArgumentType::OPTIONAL;

  FunctionOptions require_civil_time_types;
  require_civil_time_types.AddRequiredLanguageFeature(FEATURE_CIVIL_TIME);

  FunctionSignatureOptions extended_datetime_signatures =
      FunctionSignatureOptions()
          .AddRequiredLanguageFeature(FEATURE_EXTENDED_DATE_TIME_SIGNATURES)
          .set_is_aliased_signature(true)
          .set_constraints(&NoLiteralOrParameterString<1>);

  InsertFunction(functions, options, "format_date", SCALAR,
                 {{string_type, {string_type, date_type}, FN_FORMAT_DATE},
                  {string_type,
                   {string_type, datetime_type},
                   FN_FORMAT_DATETIME,
                   extended_datetime_signatures},
                  {string_type,
                   {string_type, timestamp_type, {string_type, OPTIONAL}},
                   FN_FORMAT_TIMESTAMP,
                   extended_datetime_signatures}});
  InsertFunction(
      functions, options, "format_datetime", SCALAR,
      {{string_type, {string_type, datetime_type}, FN_FORMAT_DATETIME},
       {string_type,
        {string_type, timestamp_type, {string_type, OPTIONAL}},
        FN_FORMAT_TIMESTAMP,
        extended_datetime_signatures}},
      require_civil_time_types.Copy());
  InsertSimpleFunction(
      functions, options, "format_time", SCALAR,
      {{string_type, {string_type, time_type}, FN_FORMAT_TIME}},
      require_civil_time_types.Copy());
  InsertFunction(functions, options, "format_timestamp", SCALAR,
                 {
                     {string_type,
                      {string_type, timestamp_type, {string_type, OPTIONAL}},
                      FN_FORMAT_TIMESTAMP},
                     {string_type,
                      {string_type, datetime_type},
                      FN_FORMAT_DATETIME,
                      extended_datetime_signatures},
                 });

  InsertSimpleFunction(
      functions, options, "parse_date", SCALAR,
      {{date_type, {string_type, string_type}, FN_PARSE_DATE}});
  InsertSimpleFunction(
      functions, options, "parse_datetime", SCALAR,
      {{datetime_type, {string_type, string_type}, FN_PARSE_DATETIME}},
      require_civil_time_types.Copy());
  InsertSimpleFunction(functions, options, "parse_time", SCALAR,
                       {{time_type, {string_type, string_type}, FN_PARSE_TIME}},
                       require_civil_time_types.Copy());

  InsertFunction(
      functions, options, "parse_timestamp", SCALAR,
      {{timestamp_type,
        {string_type, string_type, {string_type, OPTIONAL}},
        FN_PARSE_TIMESTAMP},
       {timestamp_type,
        {string_type,
         string_type,
         {types::Int64Type(), FunctionArgumentTypeOptions()
                                  .set_must_be_analysis_constant(true)
                                  .set_argument_name("precision", kNamedOnly)}},
        FN_PARSE_TIMESTAMP_WITH_PRECISION,
        FunctionSignatureOptions().AddRequiredLanguageFeature(
            FEATURE_PARSE_TIMESTAMP_WITH_PRECISION_AND_TIMEZONE)},
       {timestamp_type,
        {string_type,
         string_type,
         {types::Int64Type(), FunctionArgumentTypeOptions()
                                  .set_must_be_analysis_constant(true)
                                  .set_argument_name("precision", kNamedOnly)},
         {string_type, FunctionArgumentTypeOptions().set_argument_name(
                           "timezone", kNamedOnly)}},
        FN_PARSE_TIMESTAMP_WITH_PRECISION_AND_TIMEZONE,
        FunctionSignatureOptions().AddRequiredLanguageFeature(
            FEATURE_PARSE_TIMESTAMP_WITH_PRECISION_AND_TIMEZONE)}},
      FunctionOptions().set_post_resolution_argument_constraint(
          &CheckParseTimestampPrecisionArgumentValue));
}

void GetDatetimeFunctions(TypeFactory* type_factory,
                          const GoogleSQLBuiltinFunctionOptions& options,
                          NameToFunctionMap* functions) {
  GetDatetimeConversionFunctions(type_factory, options, functions);
  GetDatetimeCurrentFunctions(type_factory, options, functions);
  GetDatetimeExtractFunctions(type_factory, options, functions);
  GetDatetimeFormatFunctions(type_factory, options, functions);
  GetDatetimeAddSubFunctions(type_factory, options, functions);
  GetDatetimeDiffTruncLastNextFunctions(type_factory, options, functions);
  GetDatetimeBucketFunctions(type_factory, options, functions);

  GetTimeAndDatetimeConstructionAndConversionFunctions(type_factory, options,
                                                       functions);
}

namespace {

std::string IntervalConstructorSQL(absl::Span<const std::string> inputs) {
  ABSL_DCHECK_EQ(inputs.size(), 2);
  return absl::StrFormat("INTERVAL %s %s", inputs[0], inputs[1]);
}

}  // namespace

void GetIntervalFunctions(TypeFactory* type_factory,
                          const GoogleSQLBuiltinFunctionOptions& options,
                          NameToFunctionMap* functions) {
  const Type* interval_type = type_factory->get_interval();
  const Type* int64_type = type_factory->get_int64();
  const Type* datepart_type = types::DatePartEnumType();
  const Function::Mode SCALAR = Function::SCALAR;

  InsertSimpleFunction(
      functions, options, "$interval", SCALAR,
      {{interval_type, {int64_type, datepart_type}, FN_INTERVAL_CONSTRUCTOR}},
      FunctionOptions().set_get_sql_callback(&IntervalConstructorSQL));

  auto with_name = [](absl::string_view name) {
    return FunctionArgumentTypeOptions(FunctionArgumentType::OPTIONAL)
        .set_default(Value::Int64(0))
        .set_argument_name(name, kPositionalOrNamed);
  };

  InsertFunction(
      functions, options, "make_interval", SCALAR,
      {{interval_type,
        {
            {int64_type, with_name("year")},
            {int64_type, with_name("month")},
            {int64_type, with_name("day")},
            {int64_type, with_name("hour")},
            {int64_type, with_name("minute")},
            {int64_type, with_name("second")},
        },
        FN_MAKE_INTERVAL}},
      FunctionOptions().AddRequiredLanguageFeature(FEATURE_INTERVAL_TYPE));

  InsertFunction(functions, options, "justify_hours", SCALAR,
                 {{interval_type, {interval_type}, FN_JUSTIFY_HOURS}});

  InsertFunction(functions, options, "justify_days", SCALAR,
                 {{interval_type, {interval_type}, FN_JUSTIFY_DAYS}});

  InsertFunction(functions, options, "justify_interval", SCALAR,
                 {{interval_type, {interval_type}, FN_JUSTIFY_INTERVAL}});

  InsertFunction(functions, options, "to_seconds_interval", SCALAR,
                 {{interval_type, {interval_type}, FN_TO_SECONDS_INTERVAL}});
}

void GetArithmeticFunctions(TypeFactory* type_factory,
                            const GoogleSQLBuiltinFunctionOptions& options,
                            NameToFunctionMap* functions) {
  const Type* int32_type = type_factory->get_int32();
  const Type* int64_type = type_factory->get_int64();
  const Type* uint64_type = type_factory->get_uint64();
  const Type* float_type = type_factory->get_float();
  const Type* double_type = type_factory->get_double();
  const Type* numeric_type = type_factory->get_numeric();
  const Type* bignumeric_type = type_factory->get_bignumeric();
  const Type* date_type = type_factory->get_date();
  const Type* timestamp_type = type_factory->get_timestamp();
  const Type* datetime_type = type_factory->get_datetime();
  const Type* time_type = type_factory->get_time();
  const Type* interval_type = type_factory->get_interval();

  const Function::Mode SCALAR = Function::SCALAR;

  FunctionSignatureOptions has_floating_point_argument;
  has_floating_point_argument.set_constraints(&CheckHasFloatingPointArgument);
  FunctionSignatureOptions has_numeric_type_argument;
  has_numeric_type_argument.set_constraints(&CheckHasNumericTypeArgument);
  FunctionSignatureOptions has_bignumeric_type_argument;
  has_bignumeric_type_argument.set_constraints(&CheckHasBigNumericTypeArgument);
  FunctionSignatureOptions has_interval_type_argument;
  has_interval_type_argument.set_constraints(&CheckHasIntervalTypeArgument);
  FunctionSignatureOptions date_arithmetics_options =
      FunctionSignatureOptions().AddRequiredLanguageFeature(
          FEATURE_DATE_ARITHMETICS);
  FunctionSignatureOptions interval_options =
      FunctionSignatureOptions().AddRequiredLanguageFeature(
          FEATURE_INTERVAL_TYPE);
  FunctionSignatureOptions civil_time_options =
      FunctionSignatureOptions().AddRequiredLanguageFeature(FEATURE_CIVIL_TIME);

  // Note that the '$' prefix is used in function names for those that do not
  // support function call syntax.  Otherwise, syntax like ADD(<op1>, <op2>)
  // would be implicitly supported.

  // Note that these arithmetic operators (+, -, *, /, <unary minus>) have
  // related SAFE versions (SAFE_ADD, SAFE_SUBTRACT, etc.) that must have
  // the same signatures as these operators.
  InsertFunction(
      functions, options, "$add", SCALAR,
      {
          {int64_type, {int64_type, int64_type}, FN_ADD_INT64},
          {uint64_type, {uint64_type, uint64_type}, FN_ADD_UINT64},
          {double_type,
           {double_type, double_type},
           FN_ADD_DOUBLE,
           has_floating_point_argument},
          {numeric_type,
           {numeric_type, numeric_type},
           FN_ADD_NUMERIC,
           has_numeric_type_argument},
          {bignumeric_type,
           {bignumeric_type, bignumeric_type},
           FN_ADD_BIGNUMERIC,
           has_bignumeric_type_argument},
          {date_type,
           {date_type, int64_type},
           FN_ADD_DATE_INT64,
           date_arithmetics_options},
          {date_type,
           {int64_type, date_type},
           FN_ADD_INT64_DATE,
           date_arithmetics_options},
          {timestamp_type,
           {timestamp_type, interval_type},
           FN_ADD_TIMESTAMP_INTERVAL},
          {timestamp_type,
           {interval_type, timestamp_type},
           FN_ADD_INTERVAL_TIMESTAMP},
          {datetime_type,
           {date_type, interval_type},
           FN_ADD_DATE_INTERVAL,
           civil_time_options},
          {datetime_type,
           {interval_type, date_type},
           FN_ADD_INTERVAL_DATE,
           civil_time_options},
          {datetime_type,
           {datetime_type, interval_type},
           FN_ADD_DATETIME_INTERVAL},
          {datetime_type,
           {interval_type, datetime_type},
           FN_ADD_INTERVAL_DATETIME},
          {interval_type,
           {interval_type, interval_type},
           FN_ADD_INTERVAL_INTERVAL},
      },
      FunctionOptions()
          .set_supports_safe_error_mode(false)
          .set_sql_name("+")
          .set_get_sql_callback(absl::bind_front(&InfixFunctionSQL, "+")));

  InsertFunction(
      functions, options, "$subtract", SCALAR,
      {
          {int64_type, {int64_type, int64_type}, FN_SUBTRACT_INT64},
          {int64_type, {uint64_type, uint64_type}, FN_SUBTRACT_UINT64},
          {numeric_type,
           {numeric_type, numeric_type},
           FN_SUBTRACT_NUMERIC,
           has_numeric_type_argument},
          {bignumeric_type,
           {bignumeric_type, bignumeric_type},
           FN_SUBTRACT_BIGNUMERIC,
           has_bignumeric_type_argument},
          {double_type,
           {double_type, double_type},
           FN_SUBTRACT_DOUBLE,
           has_floating_point_argument},
          {date_type,
           {date_type, int64_type},
           FN_SUBTRACT_DATE_INT64,
           date_arithmetics_options},
          {interval_type,
           {date_type, date_type},
           FN_SUBTRACT_DATE,
           interval_options},
          {interval_type,
           {timestamp_type, timestamp_type},
           FN_SUBTRACT_TIMESTAMP,
           interval_options},
          {interval_type,
           {datetime_type, datetime_type},
           FN_SUBTRACT_DATETIME,
           interval_options},
          {interval_type,
           {time_type, time_type},
           FN_SUBTRACT_TIME,
           interval_options},
          {timestamp_type,
           {timestamp_type, interval_type},
           FN_SUBTRACT_TIMESTAMP_INTERVAL},
          {datetime_type,
           {date_type, interval_type},
           FN_SUBTRACT_DATE_INTERVAL,
           civil_time_options},
          {datetime_type,
           {datetime_type, interval_type},
           FN_SUBTRACT_DATETIME_INTERVAL},
          {interval_type,
           {interval_type, interval_type},
           FN_SUBTRACT_INTERVAL_INTERVAL},
      },
      FunctionOptions()
          .set_supports_safe_error_mode(false)
          .set_sql_name("-")
          .set_get_sql_callback(absl::bind_front(&InfixFunctionSQL, "-")));

  InsertFunction(
      functions, options, "$divide", SCALAR,
      {{double_type, {double_type, double_type}, FN_DIVIDE_DOUBLE},
       {numeric_type,
        {numeric_type, numeric_type},
        FN_DIVIDE_NUMERIC,
        has_numeric_type_argument},
       {bignumeric_type,
        {bignumeric_type, bignumeric_type},
        FN_DIVIDE_BIGNUMERIC,
        has_bignumeric_type_argument},
       {interval_type, {interval_type, int64_type}, FN_DIVIDE_INTERVAL_INT64}},
      FunctionOptions()
          .set_supports_safe_error_mode(false)
          .set_sql_name("/")
          .set_get_sql_callback(absl::bind_front(&InfixFunctionSQL, "/")));

  InsertFunction(
      functions, options, "$multiply", SCALAR,
      {{int64_type, {int64_type, int64_type}, FN_MULTIPLY_INT64},
       {uint64_type, {uint64_type, uint64_type}, FN_MULTIPLY_UINT64},
       {double_type,
        {double_type, double_type},
        FN_MULTIPLY_DOUBLE,
        has_floating_point_argument},
       {numeric_type,
        {numeric_type, numeric_type},
        FN_MULTIPLY_NUMERIC,
        has_numeric_type_argument},
       {bignumeric_type,
        {bignumeric_type, bignumeric_type},
        FN_MULTIPLY_BIGNUMERIC,
        has_bignumeric_type_argument},
       {interval_type, {interval_type, int64_type}, FN_MULTIPLY_INTERVAL_INT64},
       {interval_type,
        {int64_type, interval_type},
        FN_MULTIPLY_INT64_INTERVAL}},
      FunctionOptions()
          .set_supports_safe_error_mode(false)
          .set_sql_name("*")
          .set_get_sql_callback(absl::bind_front(&InfixFunctionSQL, "*")));

  // We do not allow arguments to be coerced because we don't want unary
  // minus to apply to UINT32/UINT64 by implicitly matching the INT64/DOUBLE
  // signatures.
  InsertFunction(
      functions, options, "$unary_minus", SCALAR,
      {{int32_type, {int32_type}, FN_UNARY_MINUS_INT32},
       {int64_type, {int64_type}, FN_UNARY_MINUS_INT64},
       {float_type, {float_type}, FN_UNARY_MINUS_FLOAT},
       {double_type, {double_type}, FN_UNARY_MINUS_DOUBLE},
       {numeric_type,
        {numeric_type},
        FN_UNARY_MINUS_NUMERIC,
        has_numeric_type_argument},
       {bignumeric_type,
        {bignumeric_type},
        FN_UNARY_MINUS_BIGNUMERIC,
        has_bignumeric_type_argument},
       {interval_type,
        {interval_type},
        FN_UNARY_MINUS_INTERVAL,
        has_interval_type_argument}},
      FunctionOptions()
          .set_supports_safe_error_mode(false)
          .set_sql_name("-")
          .set_arguments_are_coercible(false)
          .set_get_sql_callback(absl::bind_front(&PreUnaryFunctionSQL, "-")));
}

void GetBitwiseFunctions(TypeFactory* type_factory,
                         const GoogleSQLBuiltinFunctionOptions& options,
                         NameToFunctionMap* functions) {
  const Type* int32_type = type_factory->get_int32();
  const Type* int64_type = type_factory->get_int64();
  const Type* uint32_type = type_factory->get_uint32();
  const Type* uint64_type = type_factory->get_uint64();
  const Type* bytes_type = type_factory->get_bytes();

  const Function::Mode SCALAR = Function::SCALAR;

  InsertSimpleFunction(
      functions, options, "$bitwise_not", SCALAR,
      {{int32_type, {int32_type}, FN_BITWISE_NOT_INT32},
       {int64_type, {int64_type}, FN_BITWISE_NOT_INT64},
       {uint32_type, {uint32_type}, FN_BITWISE_NOT_UINT32},
       {uint64_type, {uint64_type}, FN_BITWISE_NOT_UINT64},
       {bytes_type, {bytes_type}, FN_BITWISE_NOT_BYTES}},
      FunctionOptions()
          .set_supports_safe_error_mode(false)
          .set_sql_name("~")
          .set_get_sql_callback(absl::bind_front(&PreUnaryFunctionSQL, "~")));
  InsertSimpleFunction(
      functions, options, "$bitwise_or", SCALAR,
      {{int32_type, {int32_type, int32_type}, FN_BITWISE_OR_INT32},
       {int64_type, {int64_type, int64_type}, FN_BITWISE_OR_INT64},
       {uint32_type, {uint32_type, uint32_type}, FN_BITWISE_OR_UINT32},
       {uint64_type, {uint64_type, uint64_type}, FN_BITWISE_OR_UINT64},
       {bytes_type, {bytes_type, bytes_type}, FN_BITWISE_OR_BYTES}},
      FunctionOptions()
          .set_supports_safe_error_mode(false)
          .set_sql_name("|")
          .set_pre_resolution_argument_constraint(
              absl::bind_front(&CheckBitwiseOperatorArgumentsHaveSameType, "|"))
          .set_get_sql_callback(absl::bind_front(&InfixFunctionSQL, "|")));
  InsertSimpleFunction(
      functions, options, "$bitwise_xor", SCALAR,
      {{int32_type, {int32_type, int32_type}, FN_BITWISE_XOR_INT32},
       {int64_type, {int64_type, int64_type}, FN_BITWISE_XOR_INT64},
       {uint32_type, {uint32_type, uint32_type}, FN_BITWISE_XOR_UINT32},
       {uint64_type, {uint64_type, uint64_type}, FN_BITWISE_XOR_UINT64},
       {bytes_type, {bytes_type, bytes_type}, FN_BITWISE_XOR_BYTES}},
      FunctionOptions()
          .set_supports_safe_error_mode(false)
          .set_sql_name("^")
          .set_pre_resolution_argument_constraint(
              absl::bind_front(&CheckBitwiseOperatorArgumentsHaveSameType, "^"))
          .set_get_sql_callback(absl::bind_front(&InfixFunctionSQL, "^")));
  InsertSimpleFunction(
      functions, options, "$bitwise_and", SCALAR,
      {{int32_type, {int32_type, int32_type}, FN_BITWISE_AND_INT32},
       {int64_type, {int64_type, int64_type}, FN_BITWISE_AND_INT64},
       {uint32_type, {uint32_type, uint32_type}, FN_BITWISE_AND_UINT32},
       {uint64_type, {uint64_type, uint64_type}, FN_BITWISE_AND_UINT64},
       {bytes_type, {bytes_type, bytes_type}, FN_BITWISE_AND_BYTES}},
      FunctionOptions()
          .set_supports_safe_error_mode(false)
          .set_sql_name("&")
          .set_pre_resolution_argument_constraint(
              absl::bind_front(&CheckBitwiseOperatorArgumentsHaveSameType, "&"))
          .set_get_sql_callback(absl::bind_front(&InfixFunctionSQL, "&")));
  InsertSimpleFunction(
      functions, options, "$bitwise_left_shift", SCALAR,
      {{int32_type, {int32_type, int64_type}, FN_BITWISE_LEFT_SHIFT_INT32},
       {int64_type, {int64_type, int64_type}, FN_BITWISE_LEFT_SHIFT_INT64},
       {uint32_type, {uint32_type, int64_type}, FN_BITWISE_LEFT_SHIFT_UINT32},
       {uint64_type, {uint64_type, int64_type}, FN_BITWISE_LEFT_SHIFT_UINT64},
       {bytes_type, {bytes_type, int64_type}, FN_BITWISE_LEFT_SHIFT_BYTES}},
      FunctionOptions()
          .set_supports_safe_error_mode(false)
          .set_sql_name("<<")
          .set_pre_resolution_argument_constraint(absl::bind_front(
              &CheckBitwiseOperatorFirstArgumentIsIntegerOrBytes, "<<"))
          .set_get_sql_callback(absl::bind_front(&InfixFunctionSQL, "<<")));
  InsertSimpleFunction(
      functions, options, "$bitwise_right_shift", SCALAR,
      {{int32_type, {int32_type, int64_type}, FN_BITWISE_RIGHT_SHIFT_INT32},
       {int64_type, {int64_type, int64_type}, FN_BITWISE_RIGHT_SHIFT_INT64},
       {uint32_type, {uint32_type, int64_type}, FN_BITWISE_RIGHT_SHIFT_UINT32},
       {uint64_type, {uint64_type, int64_type}, FN_BITWISE_RIGHT_SHIFT_UINT64},
       {bytes_type, {bytes_type, int64_type}, FN_BITWISE_RIGHT_SHIFT_BYTES}},
      FunctionOptions()
          .set_supports_safe_error_mode(false)
          .set_sql_name(">>")
          .set_pre_resolution_argument_constraint(absl::bind_front(
              &CheckBitwiseOperatorFirstArgumentIsIntegerOrBytes, ">>"))
          .set_get_sql_callback(absl::bind_front(&InfixFunctionSQL, ">>")));
  InsertSimpleFunction(functions, options, "bit_count", SCALAR,
                       {{int64_type, {int32_type}, FN_BIT_COUNT_INT32},
                        {int64_type, {int64_type}, FN_BIT_COUNT_INT64},
                        {int64_type, {uint64_type}, FN_BIT_COUNT_UINT64},
                        {int64_type, {bytes_type}, FN_BIT_COUNT_BYTES}});
}

namespace {

void GetAnyAggregateFunctions(TypeFactory* type_factory,
                              const GoogleSQLBuiltinFunctionOptions& options,
                              NameToFunctionMap* functions) {
  InsertSimpleFunction(functions, options, "any_value", Function::AGGREGATE,
                       {{ARG_TYPE_ANY_1, {ARG_TYPE_ANY_1}, FN_ANY_VALUE}},
                       DefaultAggregateFunctionOptions());
}

void GetCountFunctions(TypeFactory* type_factory,
                       const GoogleSQLBuiltinFunctionOptions& options,
                       NameToFunctionMap* functions) {
  const Type* int64_type = type_factory->get_int64();
  const Type* bool_type = type_factory->get_bool();

  InsertFunction(functions, options, "count", Function::AGGREGATE,
                 {{int64_type, {ARG_TYPE_ANY_1}, FN_COUNT}},
                 DefaultAggregateFunctionOptions());

  InsertSimpleFunction(functions, options, "$count_star", Function::AGGREGATE,
                       {{int64_type, {}, FN_COUNT_STAR}},
                       DefaultAggregateFunctionOptions()
                           .set_sql_name("count(*)")
                           .set_get_sql_callback(&CountStarFunctionSQL));

  InsertFunction(functions, options, "countif", Function::AGGREGATE,
                 {{int64_type, {bool_type}, FN_COUNTIF}},
                 DefaultAggregateFunctionOptions());
}

void GetSumAvgFunctions(TypeFactory* type_factory,
                        const GoogleSQLBuiltinFunctionOptions& options,
                        NameToFunctionMap* functions) {
  const Type* const int64_type = type_factory->get_int64();
  const Type* const uint64_type = type_factory->get_uint64();
  const Type* const double_type = type_factory->get_double();
  const Type* const numeric_type = type_factory->get_numeric();
  const Type* const bignumeric_type = type_factory->get_bignumeric();
  const Type* const interval_type = type_factory->get_interval();

  FunctionSignatureOptions has_numeric_type_argument;
  has_numeric_type_argument.set_constraints(&CheckHasNumericTypeArgument);
  FunctionSignatureOptions has_bignumeric_type_argument;
  has_bignumeric_type_argument.set_constraints(&CheckHasBigNumericTypeArgument);

  // Let INT32 -> INT64, UINT32 -> UINT64, and FLOAT -> DOUBLE.
  InsertFunction(functions, options, "sum", Function::AGGREGATE,
                 {{int64_type, {int64_type}, FN_SUM_INT64},
                  {uint64_type, {uint64_type}, FN_SUM_UINT64},
                  {double_type, {double_type}, FN_SUM_DOUBLE},
                  {numeric_type,
                   {numeric_type},
                   FN_SUM_NUMERIC,
                   has_numeric_type_argument},
                  {bignumeric_type,
                   {bignumeric_type},
                   FN_SUM_BIGNUMERIC,
                   has_bignumeric_type_argument},
                  {interval_type, {interval_type}, FN_SUM_INTERVAL}},
                 DefaultAggregateFunctionOptions());

  InsertFunction(functions, options, "avg", Function::AGGREGATE,
                 {{double_type, {int64_type}, FN_AVG_INT64},
                  {double_type, {uint64_type}, FN_AVG_UINT64},
                  {double_type, {double_type}, FN_AVG_DOUBLE},
                  {numeric_type,
                   {numeric_type},
                   FN_AVG_NUMERIC,
                   has_numeric_type_argument},
                  {bignumeric_type,
                   {bignumeric_type},
                   FN_AVG_BIGNUMERIC,
                   has_bignumeric_type_argument},
                  {interval_type, {interval_type}, FN_AVG_INTERVAL}},
                 DefaultAggregateFunctionOptions());
}

void GetBitwiseAggregateFunctions(
    TypeFactory* type_factory, const GoogleSQLBuiltinFunctionOptions& options,
    NameToFunctionMap* functions) {
  const Type* const int32_type = type_factory->get_int32();
  const Type* const int64_type = type_factory->get_int64();
  const Type* const uint32_type = type_factory->get_uint32();
  const Type* const uint64_type = type_factory->get_uint64();
  const Type* const bytes_type = type_factory->get_bytes();

  const Type* bitwise_agg_mode_type = types::BitwiseAggModeEnumType();
  const FunctionArgumentType bitwise_agg_mode_arg(
      bitwise_agg_mode_type,
      FunctionArgumentTypeOptions(FunctionArgumentType::OPTIONAL)
          .set_default(
              Value::Enum(bitwise_agg_mode_type->AsEnum(),
                          static_cast<int>(functions::BitwiseAggEnums::STRICT)))
          .set_argument_name("mode", kNamedOnly));

  const FunctionSignatureOptions bitwise_agg_bytes_options =
      FunctionSignatureOptions().AddRequiredLanguageFeature(
          FEATURE_BITWISE_AGGREGATE_BYTES_SIGNATURES);

  InsertFunction(functions, options, "bit_and", Function::AGGREGATE,
                 {{int32_type, {int32_type}, FN_BIT_AND_INT32},
                  {int64_type, {int64_type}, FN_BIT_AND_INT64},
                  {uint32_type, {uint32_type}, FN_BIT_AND_UINT32},
                  {uint64_type, {uint64_type}, FN_BIT_AND_UINT64},
                  {bytes_type,
                   {bytes_type, bitwise_agg_mode_arg},
                   FN_BIT_AND_BYTES,
                   bitwise_agg_bytes_options}},
                 DefaultAggregateFunctionOptions());

  InsertFunction(functions, options, "bit_or", Function::AGGREGATE,
                 {{int32_type, {int32_type}, FN_BIT_OR_INT32},
                  {int64_type, {int64_type}, FN_BIT_OR_INT64},
                  {uint32_type, {uint32_type}, FN_BIT_OR_UINT32},
                  {uint64_type, {uint64_type}, FN_BIT_OR_UINT64},
                  {bytes_type,
                   {bytes_type, bitwise_agg_mode_arg},
                   FN_BIT_OR_BYTES,
                   bitwise_agg_bytes_options}},
                 DefaultAggregateFunctionOptions());

  InsertFunction(functions, options, "bit_xor", Function::AGGREGATE,
                 {{int32_type, {int32_type}, FN_BIT_XOR_INT32},
                  {int64_type, {int64_type}, FN_BIT_XOR_INT64},
                  {uint32_type, {uint32_type}, FN_BIT_XOR_UINT32},
                  {uint64_type, {uint64_type}, FN_BIT_XOR_UINT64},
                  {bytes_type,
                   {bytes_type, bitwise_agg_mode_arg},
                   FN_BIT_XOR_BYTES,
                   bitwise_agg_bytes_options}},
                 DefaultAggregateFunctionOptions());
}

void GetLogicalAggregateFunctions(
    TypeFactory* type_factory, const GoogleSQLBuiltinFunctionOptions& options,
    NameToFunctionMap* functions) {
  const Type* const bool_type = type_factory->get_bool();
  InsertFunction(functions, options, "logical_and", Function::AGGREGATE,
                 {{bool_type, {bool_type}, FN_LOGICAL_AND}},
                 DefaultAggregateFunctionOptions());

  InsertFunction(functions, options, "logical_or", Function::AGGREGATE,
                 {{bool_type, {bool_type}, FN_LOGICAL_OR}},
                 DefaultAggregateFunctionOptions());
}

void GetMinMaxFunctions(TypeFactory* type_factory,
                        const GoogleSQLBuiltinFunctionOptions& options,
                        NameToFunctionMap* functions) {
  // Resolution will verify that argument types are valid (not proto, struct,
  // or array).
  InsertFunction(
      functions, options, "min", Function::AGGREGATE,
      {{ARG_TYPE_ANY_1,
        {ARG_TYPE_ANY_1},
        FN_MIN,
        FunctionSignatureOptions().set_uses_operation_collation()}},
      DefaultAggregateFunctionOptions().set_pre_resolution_argument_constraint(
          absl::bind_front(&CheckMinMaxArguments, "MIN")));

  InsertFunction(
      functions, options, "max", Function::AGGREGATE,
      {{ARG_TYPE_ANY_1,
        {ARG_TYPE_ANY_1},
        FN_MAX,
        FunctionSignatureOptions().set_uses_operation_collation()}},
      DefaultAggregateFunctionOptions().set_pre_resolution_argument_constraint(
          absl::bind_front(&CheckMinMaxArguments, "MAX")));
}

void GetGroupingFunctions(TypeFactory* type_factory,
                          const GoogleSQLBuiltinFunctionOptions& options,
                          NameToFunctionMap* functions) {
  const Type* const int64_type = type_factory->get_int64();

  if (options.language_options.LanguageFeatureEnabled(
          FEATURE_GROUPING_BUILTIN)) {
    InsertFunction(
        functions, options, "grouping", Function::AGGREGATE,
        {{int64_type, {ARG_TYPE_ANY_1}, FN_GROUPING}},
        DefaultAggregateFunctionOptions()
            .set_post_resolution_argument_constraint(
                absl::bind_front(&CheckArgumentsSupportGrouping, "Grouping"))
            .set_supports_safe_error_mode(false)
            .set_supports_over_clause(false)
            .set_supports_distinct_modifier(false)
            .set_supports_window_framing(false)
            .set_window_ordering_support(FunctionOptions::ORDER_UNSUPPORTED)
            .set_supports_having_modifier(false)
            .set_supports_group_by_modifier(false)
            .set_supports_where_modifier(false)
            .set_supports_having_filter_modifier(false));
  }
}

void GetMergeFunctions(TypeFactory* type_factory,
                       const GoogleSQLBuiltinFunctionOptions& options,
                       NameToFunctionMap* functions) {
  const Type* const string_type = type_factory->get_string();
  const Type* const bytes_type = type_factory->get_bytes();

  FunctionArgumentTypeOptions non_null_non_agg;
  non_null_non_agg.set_is_not_aggregate();
  non_null_non_agg.set_must_be_non_null();

  InsertFunction(
      functions, options, "string_agg", Function::AGGREGATE,
      {{string_type, {string_type}, FN_STRING_AGG_STRING},
       // Resolution will verify that the second argument must be a literal.
       {string_type,
        {string_type, {string_type, non_null_non_agg}},
        FN_STRING_AGG_DELIM_STRING},
       // Bytes inputs
       {bytes_type, {bytes_type}, FN_STRING_AGG_BYTES},
       // Resolution will verify that the second argument must be a literal.
       {bytes_type,
        {bytes_type, {bytes_type, non_null_non_agg}},
        FN_STRING_AGG_DELIM_BYTES}},
      DefaultAggregateFunctionOptions()
          .set_supports_order_by(true)
          .set_supports_limit(true));

  InsertFunction(
      functions, options, "array_agg", Function::AGGREGATE,
      {{{ARG_ARRAY_TYPE_ANY_1,
         FunctionArgumentTypeOptions().set_uses_array_element_for_collation()},
        {ARG_TYPE_ANY_1},
        FN_ARRAY_AGG}},
      DefaultAggregateFunctionOptions()
          .set_pre_resolution_argument_constraint(&CheckArrayAggArguments)
          .set_supports_null_handling_modifier(true)
          .set_supports_order_by(true)
          .set_supports_limit(true));

  InsertSimpleFunction(
      functions, options, "array_concat_agg", Function::AGGREGATE,
      {{ARG_ARRAY_TYPE_ANY_1, {ARG_ARRAY_TYPE_ANY_1}, FN_ARRAY_CONCAT_AGG}},
      DefaultAggregateFunctionOptions()
          .set_pre_resolution_argument_constraint(&CheckArrayConcatArguments)
          .set_supports_order_by(true)
          .set_supports_limit(true));
}

void GetPercentileFunctions(TypeFactory* type_factory,
                            const GoogleSQLBuiltinFunctionOptions& options,
                            NameToFunctionMap* functions) {
  const Type* const double_type = type_factory->get_double();
  const Type* const numeric_type = type_factory->get_numeric();
  const Type* const bignumeric_type = type_factory->get_bignumeric();

  FunctionSignatureOptions all_args_are_numeric_or_bignumeric;
  all_args_are_numeric_or_bignumeric.set_constraints(
      &CheckAllArgumentsHaveNumericOrBigNumericType);
  FunctionArgumentTypeOptions non_null_non_agg_between_0_and_1;
  non_null_non_agg_between_0_and_1.set_is_not_aggregate();
  non_null_non_agg_between_0_and_1.set_must_be_non_null();
  non_null_non_agg_between_0_and_1.set_min_value(0);
  non_null_non_agg_between_0_and_1.set_max_value(1);

  FunctionArgumentTypeOptions comparable;
  comparable.set_must_support_ordering();

  const FunctionOptions disallowed_order_and_frame_allowed_null_handling =
      FunctionOptions(FunctionOptions::ORDER_UNSUPPORTED,
                      /*window_framing_support_in=*/false)
          .set_supports_null_handling_modifier(true)
          .set_supports_distinct_modifier(false);

  InsertFunction(
      functions, options, "percentile_cont", Function::AGGREGATE,
      {
          {double_type,
           {double_type, {double_type, non_null_non_agg_between_0_and_1}},
           FN_PERCENTILE_CONT},
          {numeric_type,
           {numeric_type, {numeric_type, non_null_non_agg_between_0_and_1}},
           FN_PERCENTILE_CONT_NUMERIC,
           all_args_are_numeric_or_bignumeric},
          {bignumeric_type,
           {bignumeric_type,
            {bignumeric_type, non_null_non_agg_between_0_and_1}},
           FN_PERCENTILE_CONT_BIGNUMERIC,
           all_args_are_numeric_or_bignumeric},
      },
      disallowed_order_and_frame_allowed_null_handling);

  InsertFunction(
      functions, options, "percentile_disc", Function::AGGREGATE,
      {{ARG_TYPE_ANY_1,
        {{ARG_TYPE_ANY_1, comparable},
         {double_type, non_null_non_agg_between_0_and_1}},
        FN_PERCENTILE_DISC,
        FunctionSignatureOptions().set_uses_operation_collation()},
       {ARG_TYPE_ANY_1,
        {{ARG_TYPE_ANY_1, comparable},
         {numeric_type, non_null_non_agg_between_0_and_1}},
        FN_PERCENTILE_DISC_NUMERIC,
        FunctionSignatureOptions()
            .set_uses_operation_collation()
            .set_constraints(&CheckLastArgumentHasNumericOrBigNumericType)},
       {ARG_TYPE_ANY_1,
        {{ARG_TYPE_ANY_1, comparable},
         {bignumeric_type, non_null_non_agg_between_0_and_1}},
        FN_PERCENTILE_DISC_BIGNUMERIC,
        FunctionSignatureOptions()
            .set_uses_operation_collation()
            .set_constraints(&CheckLastArgumentHasNumericOrBigNumericType)}},
      disallowed_order_and_frame_allowed_null_handling);
}

}  // namespace

void GetAggregateFunctions(TypeFactory* type_factory,
                           const GoogleSQLBuiltinFunctionOptions& options,
                           NameToFunctionMap* functions) {
  GetAnyAggregateFunctions(type_factory, options, functions);
  GetCountFunctions(type_factory, options, functions);
  GetSumAvgFunctions(type_factory, options, functions);
  GetBitwiseAggregateFunctions(type_factory, options, functions);
  GetLogicalAggregateFunctions(type_factory, options, functions);
  GetMinMaxFunctions(type_factory, options, functions);
  GetGroupingFunctions(type_factory, options, functions);
  GetMergeFunctions(type_factory, options, functions);
  GetPercentileFunctions(type_factory, options, functions);
}

// Compute the output annotations for TOP_COUNT and TOP_SUM.
// The signatures are of the form F(T1, T2) -> ARRAY<STRUCT<T1, T2>>
static absl::StatusOr<const AnnotationMap*> ComputeAnnotationsForTopStruct(
    const ResolvedFunctionCallBase& function_call, TypeFactory& type_factory) {
  GOOGLESQL_RET_CHECK_GE(function_call.argument_list_size(), 2);

  // Simply combine the argument annotations into a struct.
  GOOGLESQL_RET_CHECK(function_call.type()->IsArray());
  GOOGLESQL_RET_CHECK(function_call.type()->AsArray()->element_type()->IsStruct());
  GOOGLESQL_RET_CHECK_EQ(
      function_call.type()->AsArray()->element_type()->AsStruct()->num_fields(),
      2);
  std::unique_ptr<AnnotationMap> annotation_map =
      AnnotationMap::Create(function_call.type());
  GOOGLESQL_RET_CHECK(annotation_map->IsStructMap());
  GOOGLESQL_RET_CHECK_EQ(annotation_map->AsStructMap()->num_fields(), 1);

  AnnotationMap* element_annotation_map =
      annotation_map->AsStructMap()->mutable_field(0);
  GOOGLESQL_RET_CHECK(element_annotation_map->IsStructMap());
  GOOGLESQL_RET_CHECK_EQ(element_annotation_map->AsStructMap()->num_fields(), 2);

  // Propagate the array element collations to the elements of the result array.
  for (int i = 0; i < function_call.argument_list_size(); ++i) {
    const AnnotationMap* arg_annotation_map =
        function_call.argument_list(i)->type_annotation_map();
    if (arg_annotation_map != nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(element_annotation_map->AsStructMap()->CloneIntoField(
          i, arg_annotation_map));
    }
  }
  if (annotation_map->Empty()) {
    // Use nullptr rather than an empty annotation map when there are no
    // annotations.
    return nullptr;
  }
  return type_factory.TakeOwnership(std::move(annotation_map));
}

void GetApproxFunctions(TypeFactory* type_factory,
                        const GoogleSQLBuiltinFunctionOptions& options,
                        NameToFunctionMap* functions) {
  const Type* int64_type = type_factory->get_int64();
  const Type* uint64_type = type_factory->get_uint64();
  const Type* double_type = type_factory->get_double();
  const Type* numeric_type = type_factory->get_numeric();
  const Type* bignumeric_type = type_factory->get_bignumeric();

  const Function::Mode AGGREGATE = Function::AGGREGATE;

  // By default, all built-in aggregate functions can be used as analytic
  // functions.
  FunctionOptions aggregate_analytic_function_options =
      DefaultAggregateFunctionOptions();

  FunctionArgumentTypeOptions comparable;
  comparable.set_must_support_ordering();

  FunctionArgumentTypeOptions supports_grouping;
  supports_grouping.set_must_support_grouping();

  FunctionArgumentTypeOptions non_null_positive_non_agg;
  non_null_positive_non_agg.set_is_not_aggregate();
  non_null_positive_non_agg.set_must_be_non_null();
  non_null_positive_non_agg.set_min_value(1);

  FunctionSignatureOptions has_numeric_type_argument;
  has_numeric_type_argument.set_constraints(&CheckHasNumericTypeArgument);
  FunctionSignatureOptions has_bignumeric_type_argument;
  has_bignumeric_type_argument.set_constraints(&CheckHasBigNumericTypeArgument);

  InsertFunction(functions, options, "approx_count_distinct", AGGREGATE,
                 {{int64_type,
                   {{ARG_TYPE_ANY_1, supports_grouping}},
                   FN_APPROX_COUNT_DISTINCT,
                   FunctionSignatureOptions().set_uses_operation_collation()}},
                 aggregate_analytic_function_options);

  // Temporarily disallow collated arguments for approx_quantiles,
  // approx_top_count and approx_top_sum functions.
  // TODO: Support approx_quantiles, approx_top_count and
  // approx_top_sum functions with collated arguments.
  InsertFunction(
      functions, options, "approx_quantiles", AGGREGATE,
      {{ARG_ARRAY_TYPE_ANY_1,
        {{ARG_TYPE_ANY_1, comparable}, {int64_type, non_null_positive_non_agg}},
        FN_APPROX_QUANTILES,
        FunctionSignatureOptions()
            .set_uses_operation_collation()
            .set_rejects_collation()}},
      DefaultAggregateFunctionOptions().set_supports_null_handling_modifier(
          true));

  InsertFunction(
      functions, options, "approx_top_count", AGGREGATE,
      {{ARG_TYPE_ARBITRARY,  // Return type will be overridden.
        {{ARG_TYPE_ANY_1, supports_grouping},
         {int64_type, non_null_positive_non_agg}},
        FN_APPROX_TOP_COUNT,
        FunctionSignatureOptions()
            .set_uses_operation_collation()
            .set_rejects_collation()
            .set_compute_result_annotations_callback(
                &ComputeAnnotationsForTopStruct)}},
      DefaultAggregateFunctionOptions().set_compute_result_type_callback(
          absl::bind_front(&ComputeResultTypeForTopStruct, "count")));

  InsertFunction(
      functions, options, "approx_top_sum", AGGREGATE,
      {{ARG_TYPE_ARBITRARY,  // Return type will be overridden.
        {{ARG_TYPE_ANY_1, supports_grouping},
         int64_type,
         {int64_type, non_null_positive_non_agg}},
        FN_APPROX_TOP_SUM_INT64,
        FunctionSignatureOptions()
            .set_uses_operation_collation()
            .set_rejects_collation()
            .set_compute_result_annotations_callback(
                &ComputeAnnotationsForTopStruct)},
       {ARG_TYPE_ARBITRARY,  // Return type will be overridden.
        {{ARG_TYPE_ANY_1, supports_grouping},
         uint64_type,
         {int64_type, non_null_positive_non_agg}},
        FN_APPROX_TOP_SUM_UINT64,
        FunctionSignatureOptions()
            .set_uses_operation_collation()
            .set_rejects_collation()
            .set_compute_result_annotations_callback(
                &ComputeAnnotationsForTopStruct)},
       {ARG_TYPE_ARBITRARY,  // Return type will be overridden.
        {{ARG_TYPE_ANY_1, supports_grouping},
         double_type,
         {int64_type, non_null_positive_non_agg}},
        FN_APPROX_TOP_SUM_DOUBLE,
        FunctionSignatureOptions()
            .set_uses_operation_collation()
            .set_rejects_collation()
            .set_compute_result_annotations_callback(
                &ComputeAnnotationsForTopStruct)},
       {ARG_TYPE_ARBITRARY,  // Return type will be overridden.
        {{ARG_TYPE_ANY_1, supports_grouping},
         numeric_type,
         {int64_type, non_null_positive_non_agg}},
        FN_APPROX_TOP_SUM_NUMERIC,
        has_numeric_type_argument.set_uses_operation_collation()
            .set_rejects_collation()
            .set_compute_result_annotations_callback(
                &ComputeAnnotationsForTopStruct)},
       {ARG_TYPE_ARBITRARY,  // Return type will be overridden.
        {{ARG_TYPE_ANY_1, supports_grouping},
         bignumeric_type,
         {int64_type, non_null_positive_non_agg}},
        FN_APPROX_TOP_SUM_BIGNUMERIC,
        has_bignumeric_type_argument.set_uses_operation_collation()
            .set_rejects_collation()
            .set_compute_result_annotations_callback(
                &ComputeAnnotationsForTopStruct)}},
      DefaultAggregateFunctionOptions().set_compute_result_type_callback(
          absl::bind_front(&ComputeResultTypeForTopStruct, "sum")));
}

void GetStatisticalFunctions(TypeFactory* type_factory,
                             const GoogleSQLBuiltinFunctionOptions& options,
                             NameToFunctionMap* functions) {
  const Type* double_type = type_factory->get_double();
  const Type* numeric_type = type_factory->get_numeric();
  const Type* bignumeric_type = type_factory->get_bignumeric();
  const Function::Mode AGGREGATE = Function::AGGREGATE;

  FunctionSignatureOptions has_numeric_type_argument;
  FunctionSignatureOptions has_bignumeric_type_argument;
  has_numeric_type_argument.set_constraints(&CheckHasNumericTypeArgument);
  has_bignumeric_type_argument.set_constraints(&CheckHasBigNumericTypeArgument);

  // Support statistical functions:
  // CORR, COVAR_POP, COVAR_SAMP,
  // STDDEV_POP, STDDEV_SAMP, STDDEV (alias for STDDEV_SAMP),
  // VAR_POP, VAR_SAMP, VARIANCE (alias for VAR_SAMP)
  // in both modes: aggregate and analytic.
  InsertFunction(functions, options, "corr", AGGREGATE,
                 {{double_type,
                   {numeric_type, numeric_type},
                   FN_CORR_NUMERIC,
                   has_numeric_type_argument},
                  {double_type,
                   {bignumeric_type, bignumeric_type},
                   FN_CORR_BIGNUMERIC,
                   has_bignumeric_type_argument},
                  {double_type, {double_type, double_type}, FN_CORR}},
                 DefaultAggregateFunctionOptions());
  InsertFunction(functions, options, "covar_pop", AGGREGATE,
                 {{double_type,
                   {numeric_type, numeric_type},
                   FN_COVAR_POP_NUMERIC,
                   has_numeric_type_argument},
                  {double_type,
                   {bignumeric_type, bignumeric_type},
                   FN_COVAR_POP_BIGNUMERIC,
                   has_bignumeric_type_argument},
                  {double_type, {double_type, double_type}, FN_COVAR_POP}},
                 DefaultAggregateFunctionOptions());
  InsertFunction(functions, options, "covar_samp", AGGREGATE,
                 {{double_type,
                   {numeric_type, numeric_type},
                   FN_COVAR_SAMP_NUMERIC,
                   has_numeric_type_argument},
                  {double_type,
                   {bignumeric_type, bignumeric_type},
                   FN_COVAR_SAMP_BIGNUMERIC,
                   has_bignumeric_type_argument},
                  {double_type, {double_type, double_type}, FN_COVAR_SAMP}},
                 DefaultAggregateFunctionOptions());

  // Unary functions
  InsertFunction(functions, options, "stddev_pop", AGGREGATE,
                 {{double_type,
                   {numeric_type},
                   FN_STDDEV_POP_NUMERIC,
                   has_numeric_type_argument},
                  {double_type,
                   {bignumeric_type},
                   FN_STDDEV_POP_BIGNUMERIC,
                   has_bignumeric_type_argument},
                  {double_type, {double_type}, FN_STDDEV_POP}},
                 DefaultAggregateFunctionOptions());
  InsertFunction(functions, options, "stddev_samp", AGGREGATE,
                 {{double_type,
                   {numeric_type},
                   FN_STDDEV_SAMP_NUMERIC,
                   has_numeric_type_argument},
                  {double_type,
                   {bignumeric_type},
                   FN_STDDEV_SAMP_BIGNUMERIC,
                   has_bignumeric_type_argument},
                  {double_type, {double_type}, FN_STDDEV_SAMP}},
                 DefaultAggregateFunctionOptions().set_alias_name("stddev"));
  InsertFunction(functions, options, "var_pop", AGGREGATE,
                 {{double_type,
                   {numeric_type},
                   FN_VAR_POP_NUMERIC,
                   has_numeric_type_argument},
                  {double_type,
                   {bignumeric_type},
                   FN_VAR_POP_BIGNUMERIC,
                   has_bignumeric_type_argument},
                  {double_type, {double_type}, FN_VAR_POP}},
                 DefaultAggregateFunctionOptions());
  InsertFunction(functions, options, "var_samp", AGGREGATE,
                 {{double_type,
                   {numeric_type},
                   FN_VAR_SAMP_NUMERIC,
                   has_numeric_type_argument},
                  {double_type,
                   {bignumeric_type},
                   FN_VAR_SAMP_BIGNUMERIC,
                   has_bignumeric_type_argument},
                  {double_type, {double_type}, FN_VAR_SAMP}},
                 DefaultAggregateFunctionOptions().set_alias_name("variance"));
}

void GetAnalyticFunctions(TypeFactory* type_factory,
                          const GoogleSQLBuiltinFunctionOptions& options,
                          NameToFunctionMap* functions) {
  const Type* bool_type = type_factory->get_bool();
  const Type* int64_type = type_factory->get_int64();
  const Type* double_type = type_factory->get_double();
  const Function::Mode ANALYTIC = Function::ANALYTIC;

  const FunctionArgumentType::ArgumentCardinality OPTIONAL =
      FunctionArgumentType::OPTIONAL;

  const FunctionOptions::WindowOrderSupport ORDER_OPTIONAL =
      FunctionOptions::ORDER_OPTIONAL;
  const FunctionOptions::WindowOrderSupport ORDER_REQUIRED =
      FunctionOptions::ORDER_REQUIRED;

  const FunctionOptions analytic_function_options =
      FunctionOptions().AddRequiredLanguageFeature(FEATURE_ANALYTIC_FUNCTIONS);
  const FunctionOptions optional_order_disallowed_frame =
      FunctionOptions(analytic_function_options)
          .set_window_ordering_support(ORDER_OPTIONAL)
          .set_supports_window_framing(false)
          .set_supports_over_clause(true);
  const FunctionOptions required_order_disallowed_frame =
      FunctionOptions(analytic_function_options)
          .set_window_ordering_support(ORDER_REQUIRED)
          .set_supports_window_framing(false)
          .set_supports_over_clause(true);
  const FunctionOptions required_order_allowed_frame =
      FunctionOptions(analytic_function_options)
          .set_window_ordering_support(ORDER_REQUIRED)
          .set_supports_window_framing(true)
          .set_supports_over_clause(true);
  const FunctionOptions required_order_allowed_frame_and_null_handling =
      FunctionOptions(required_order_allowed_frame)
          .set_supports_null_handling_modifier(true);

  FunctionArgumentTypeOptions non_null;
  non_null.set_must_be_non_null();

  FunctionArgumentTypeOptions non_null_positive_non_agg;
  non_null_positive_non_agg.set_is_not_aggregate();
  non_null_positive_non_agg.set_must_be_non_null();
  non_null_positive_non_agg.set_min_value(1);

  FunctionArgumentTypeOptions optional_non_null_non_agg;
  optional_non_null_non_agg.set_cardinality(OPTIONAL);
  optional_non_null_non_agg.set_is_not_aggregate();
  optional_non_null_non_agg.set_must_be_non_null();

  InsertSimpleFunction(functions, options, "dense_rank", ANALYTIC,
                       {{int64_type, {}, FN_DENSE_RANK}},
                       required_order_disallowed_frame);
  InsertSimpleFunction(functions, options, "rank", ANALYTIC,
                       {{int64_type, {}, FN_RANK}},
                       required_order_disallowed_frame);
  InsertSimpleFunction(functions, options, "percent_rank", ANALYTIC,
                       {{double_type, {}, FN_PERCENT_RANK}},
                       required_order_disallowed_frame);
  InsertSimpleFunction(functions, options, "cume_dist", ANALYTIC,
                       {{double_type, {}, FN_CUME_DIST}},
                       required_order_disallowed_frame);
  InsertFunction(
      functions, options, "ntile", ANALYTIC,
      {{int64_type, {{int64_type, non_null_positive_non_agg}}, FN_NTILE}},
      required_order_disallowed_frame);

  InsertSimpleFunction(functions, options, "row_number", ANALYTIC,
                       {{int64_type, {}, FN_ROW_NUMBER}},
                       optional_order_disallowed_frame);

  FunctionArgumentType is_first_arg(
      int64_type, FunctionArgumentTypeOptions().set_must_be_constant(true));

  InsertFunction(functions, options, "is_first", ANALYTIC,
                 {{bool_type,
                   {{is_first_arg}},
                   FN_IS_FIRST,
                   SetRewriter(REWRITE_IS_FIRST_IS_LAST_FUNCTION)}},
                 optional_order_disallowed_frame);
  InsertFunction(functions, options, "is_last", ANALYTIC,
                 {{bool_type,
                   {{is_first_arg}},
                   FN_IS_LAST,
                   SetRewriter(REWRITE_IS_FIRST_IS_LAST_FUNCTION)}},
                 optional_order_disallowed_frame);

  // The optional arguments will be populated in the resolved tree.
  // The second offset argument defaults to 1, and the third default
  // argument defaults to NULL.
  InsertFunction(
      functions, options, "lead", ANALYTIC,
      {{ARG_TYPE_ANY_1,
        {ARG_TYPE_ANY_1,
         {int64_type, optional_non_null_non_agg},
         // If present, the third argument must be a constant or parameter.
         // TODO: b/18709755: Give an error if it isn't.
         {ARG_TYPE_ANY_1, OPTIONAL}},
        FN_LEAD}},
      required_order_disallowed_frame);
  InsertFunction(
      functions, options, "lag", ANALYTIC,
      {{ARG_TYPE_ANY_1,
        {ARG_TYPE_ANY_1,
         {int64_type, optional_non_null_non_agg},
         // If present, the third argument must be a constant or parameter.
         // TODO: b/18709755: Give an error if it isn't.
         {ARG_TYPE_ANY_1, OPTIONAL}},
        FN_LAG}},
      required_order_disallowed_frame);

  InsertFunction(
      functions, options, "first_value", ANALYTIC,
      {{ARG_TYPE_ANY_1, {{ARG_TYPE_ANY_1, non_null}}, FN_FIRST_VALUE}},
      required_order_allowed_frame_and_null_handling);
  InsertFunction(
      functions, options, "last_value", ANALYTIC,
      {{ARG_TYPE_ANY_1, {{ARG_TYPE_ANY_1, non_null}}, FN_LAST_VALUE}},
      required_order_allowed_frame_and_null_handling);
  InsertFunction(functions, options, "nth_value", ANALYTIC,
                 {{ARG_TYPE_ANY_1,
                   {ARG_TYPE_ANY_1, {int64_type, non_null_positive_non_agg}},
                   FN_NTH_VALUE}},
                 required_order_allowed_frame_and_null_handling);
}

void GetLogicFunctions(TypeFactory* type_factory,
                       const GoogleSQLBuiltinFunctionOptions& options,
                       NameToFunctionMap* functions) {
  const Type* bool_type = type_factory->get_bool();

  const Function::Mode SCALAR = Function::SCALAR;

  const FunctionArgumentType::ArgumentCardinality REPEATED =
      FunctionArgumentType::REPEATED;

  InsertSimpleFunction(functions, options, "$is_null", SCALAR,
                       {{bool_type, {ARG_TYPE_ANY_1}, FN_IS_NULL}},
                       FunctionOptions()
                           .set_supports_safe_error_mode(false)
                           .set_get_sql_callback(absl::bind_front(
                               &PostUnaryFunctionSQL, " IS NULL")));
  InsertSimpleFunction(functions, options, "$is_true", SCALAR,
                       {{bool_type, {bool_type}, FN_IS_TRUE}},
                       FunctionOptions()
                           .set_supports_safe_error_mode(false)
                           .set_get_sql_callback(absl::bind_front(
                               &PostUnaryFunctionSQL, " IS TRUE")));
  InsertSimpleFunction(functions, options, "$is_false", SCALAR,
                       {{bool_type, {bool_type}, FN_IS_FALSE}},
                       FunctionOptions()
                           .set_supports_safe_error_mode(false)
                           .set_get_sql_callback(absl::bind_front(
                               &PostUnaryFunctionSQL, " IS FALSE")));

  InsertSimpleFunction(
      functions, options, "$and", SCALAR,
      {{bool_type, {bool_type, {bool_type, REPEATED}}, FN_AND}},
      FunctionOptions()
          .set_supports_safe_error_mode(false)
          .set_get_sql_callback(absl::bind_front(&InfixFunctionSQL, "AND")));
  InsertSimpleFunction(functions, options, "$not", SCALAR,
                       {{bool_type, {bool_type}, FN_NOT}},
                       FunctionOptions()
                           .set_supports_safe_error_mode(false)
                           .set_get_sql_callback(
                               absl::bind_front(&PreUnaryFunctionSQL, "NOT ")));
  InsertSimpleFunction(
      functions, options, "$or", SCALAR,
      {{bool_type, {bool_type, {bool_type, REPEATED}}, FN_OR}},
      FunctionOptions()
          .set_supports_safe_error_mode(false)
          .set_get_sql_callback(absl::bind_front(&InfixFunctionSQL, "OR")));
}

}  // namespace googlesql
