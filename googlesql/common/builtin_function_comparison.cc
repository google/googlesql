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

#include <string>
#include <vector>

#include "googlesql/common/builtin_function_internal.h"
#include "googlesql/common/errors.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/input_argument_type.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "absl/functional/bind_front.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

// If string literal is compared against bytes, then we want the error message
// to be more specific and helpful.
static constexpr absl::string_view kErrorMessageCompareStringLiteralToBytes =
    ". STRING and BYTES are different types that are not directly "
    "comparable. To write a BYTES literal, use a b-prefixed literal "
    "such as b'bytes value'";

std::string NoMatchingSignatureForComparisonOperator(
    absl::string_view operator_name,
    absl::Span<const InputArgumentType> arguments, ProductMode product_mode) {
  std::string error_message =
      Function::GetGenericNoMatchingFunctionSignatureErrorMessage(
          operator_name, arguments, product_mode);
  if (arguments.size() > 1 &&
      IsStringLiteralComparedToBytes(arguments[0], arguments[1])) {
    absl::StrAppend(&error_message, kErrorMessageCompareStringLiteralToBytes);
  }
  return error_message;
}

void GetEqualityFunctions(TypeFactory* type_factory,
                          const GoogleSQLBuiltinFunctionOptions& options,
                          NameToFunctionMap* functions) {
  const Type* const bool_type = type_factory->get_bool();
  const Type* const int64_type = type_factory->get_int64();
  const Type* const uint64_type = type_factory->get_uint64();

  InsertFunction(
      functions, options, "$equal", Function::SCALAR,
      {
          {bool_type,
           {ARG_TYPE_ANY_1, ARG_TYPE_ANY_1},
           FN_EQUAL,
           FunctionSignatureOptions().set_uses_operation_collation()},
          {bool_type, {int64_type, uint64_type}, FN_EQUAL_INT64_UINT64},
          {bool_type, {uint64_type, int64_type}, FN_EQUAL_UINT64_INT64},
          // TODO: Remove these signatures.
          {bool_type,
           {ARG_TYPE_GRAPH_NODE, ARG_TYPE_GRAPH_NODE},
           FN_EQUAL_GRAPH_NODE,
           FunctionSignatureOptions()
               .set_is_hidden(true)
               .set_uses_operation_collation()
               .AddRequiredLanguageFeature(LanguageFeature::FEATURE_SQL_GRAPH)},
          {bool_type,
           {ARG_TYPE_GRAPH_EDGE, ARG_TYPE_GRAPH_EDGE},
           FN_EQUAL_GRAPH_EDGE,
           FunctionSignatureOptions()
               .set_is_hidden(true)
               .set_uses_operation_collation()
               .AddRequiredLanguageFeature(LanguageFeature::FEATURE_SQL_GRAPH)},
      },
      FunctionOptions()
          .set_supports_safe_error_mode(false)
          .set_sql_name("=")
          .set_post_resolution_argument_constraint(
              absl::bind_front(&CheckArgumentsSupportEquality, "Equality"))
          .set_no_matching_signature_callback(
              &NoMatchingSignatureForComparisonOperator)
          .set_get_sql_callback(absl::bind_front(&InfixFunctionSQL, "=")));

  InsertFunction(
      functions, options, "$not_equal", Function::SCALAR,
      {
          {bool_type,
           {ARG_TYPE_ANY_1, ARG_TYPE_ANY_1},
           FN_NOT_EQUAL,
           FunctionSignatureOptions().set_uses_operation_collation()},
          {bool_type, {int64_type, uint64_type}, FN_NOT_EQUAL_INT64_UINT64},
          {bool_type, {uint64_type, int64_type}, FN_NOT_EQUAL_UINT64_INT64},
          // TODO: Remove these signatures.
          {bool_type,
           {ARG_TYPE_GRAPH_NODE, ARG_TYPE_GRAPH_NODE},
           FN_NOT_EQUAL_GRAPH_NODE,
           FunctionSignatureOptions()
               .set_is_hidden(true)
               .set_uses_operation_collation()
               .AddRequiredLanguageFeature(LanguageFeature::FEATURE_SQL_GRAPH)},
          {bool_type,
           {ARG_TYPE_GRAPH_EDGE, ARG_TYPE_GRAPH_EDGE},
           FN_NOT_EQUAL_GRAPH_EDGE,
           FunctionSignatureOptions()
               .set_is_hidden(true)
               .set_uses_operation_collation()
               .AddRequiredLanguageFeature(LanguageFeature::FEATURE_SQL_GRAPH)},
      },
      FunctionOptions()
          .set_supports_safe_error_mode(false)
          .set_sql_name("!=")
          .set_post_resolution_argument_constraint(
              absl::bind_front(&CheckArgumentsSupportEquality, "Inequality"))
          .set_no_matching_signature_callback(
              &NoMatchingSignatureForComparisonOperator)
          .set_get_sql_callback(absl::bind_front(&InfixFunctionSQL, "!=")));
}

void GetDistinctFunctions(TypeFactory* type_factory,
                          const GoogleSQLBuiltinFunctionOptions& options,
                          NameToFunctionMap* functions) {
  const Type* const bool_type = type_factory->get_bool();
  const Type* const int64_type = type_factory->get_int64();
  const Type* const uint64_type = type_factory->get_uint64();

  // Add $is_distinct_from/$is_not_distinct_from functions to the catalog
  // unconditionally so that rewriters can generate calls to them, even if the
  // IS NOT DISTINCT FROM syntax is not supported at the query level. The pivot
  // rewriter makes use of this.
  InsertFunction(
      functions, options, "$is_distinct_from", Function::SCALAR,
      {{bool_type,
        {ARG_TYPE_ANY_1, ARG_TYPE_ANY_1},
        FN_DISTINCT,
        FunctionSignatureOptions().set_uses_operation_collation()},
       {bool_type, {int64_type, uint64_type}, FN_DISTINCT_INT64_UINT64},
       {bool_type, {uint64_type, int64_type}, FN_DISTINCT_UINT64_INT64}},
      FunctionOptions()
          .set_sql_name("IS DISTINCT FROM")
          .set_get_sql_callback(
              absl::bind_front(&InfixFunctionSQL, "IS DISTINCT FROM"))
          .set_supports_safe_error_mode(false)
          .set_post_resolution_argument_constraint(
              absl::bind_front(&CheckArgumentsSupportGrouping, "Grouping")));

  InsertFunction(
      functions, options, "$is_not_distinct_from", Function::SCALAR,
      {{bool_type,
        {ARG_TYPE_ANY_1, ARG_TYPE_ANY_1},
        FN_NOT_DISTINCT,
        FunctionSignatureOptions().set_uses_operation_collation()},
       {bool_type, {int64_type, uint64_type}, FN_NOT_DISTINCT_INT64_UINT64},
       {bool_type, {uint64_type, int64_type}, FN_NOT_DISTINCT_UINT64_INT64}},
      FunctionOptions()
          .set_sql_name("IS NOT DISTINCT FROM")
          .set_get_sql_callback(
              absl::bind_front(&InfixFunctionSQL, "IS NOT DISTINCT FROM"))
          .set_supports_safe_error_mode(false)
          .set_post_resolution_argument_constraint(
              absl::bind_front(&CheckArgumentsSupportGrouping, "Grouping")));
}

void GetInequalityFunctions(TypeFactory* type_factory,
                            const GoogleSQLBuiltinFunctionOptions& options,
                            NameToFunctionMap* functions) {
  const Type* const bool_type = type_factory->get_bool();
  const Type* const int64_type = type_factory->get_int64();
  const Type* const uint64_type = type_factory->get_uint64();

  InsertFunction(
      functions, options, "$less", Function::SCALAR,
      {{bool_type,
        {ARG_TYPE_ANY_1, ARG_TYPE_ANY_1},
        FN_LESS,
        FunctionSignatureOptions().set_uses_operation_collation()},
       {bool_type, {int64_type, uint64_type}, FN_LESS_INT64_UINT64},
       {bool_type, {uint64_type, int64_type}, FN_LESS_UINT64_INT64}},
      FunctionOptions()
          .set_supports_safe_error_mode(false)
          .set_post_resolution_argument_constraint(
              absl::bind_front(&CheckArgumentsSupportComparison, "Less than"))
          .set_no_matching_signature_callback(
              &NoMatchingSignatureForComparisonOperator)
          .set_sql_name("<")
          .set_get_sql_callback(absl::bind_front(&InfixFunctionSQL, "<")));

  InsertFunction(
      functions, options, "$less_or_equal", Function::SCALAR,
      {{bool_type,
        {ARG_TYPE_ANY_1, ARG_TYPE_ANY_1},
        FN_LESS_OR_EQUAL,
        FunctionSignatureOptions().set_uses_operation_collation()},
       {bool_type, {int64_type, uint64_type}, FN_LESS_OR_EQUAL_INT64_UINT64},
       {bool_type, {uint64_type, int64_type}, FN_LESS_OR_EQUAL_UINT64_INT64}},
      FunctionOptions()
          .set_supports_safe_error_mode(false)
          .set_post_resolution_argument_constraint(
              absl::bind_front(&CheckArgumentsSupportComparison, "Less than"))
          .set_no_matching_signature_callback(
              &NoMatchingSignatureForComparisonOperator)
          .set_sql_name("<=")
          .set_get_sql_callback(absl::bind_front(&InfixFunctionSQL, "<=")));

  InsertFunction(
      functions, options, "$greater_or_equal", Function::SCALAR,
      {{bool_type,
        {ARG_TYPE_ANY_1, ARG_TYPE_ANY_1},
        FN_GREATER_OR_EQUAL,
        FunctionSignatureOptions().set_uses_operation_collation()},
       {bool_type, {int64_type, uint64_type}, FN_GREATER_OR_EQUAL_INT64_UINT64},
       {bool_type,
        {uint64_type, int64_type},
        FN_GREATER_OR_EQUAL_UINT64_INT64}},
      FunctionOptions()
          .set_supports_safe_error_mode(false)
          .set_post_resolution_argument_constraint(absl::bind_front(
              &CheckArgumentsSupportComparison, "Greater than"))
          .set_no_matching_signature_callback(
              &NoMatchingSignatureForComparisonOperator)
          .set_sql_name(">=")
          .set_get_sql_callback(absl::bind_front(&InfixFunctionSQL, ">=")));

  InsertFunction(
      functions, options, "$greater", Function::SCALAR,
      {{bool_type,
        {ARG_TYPE_ANY_1, ARG_TYPE_ANY_1},
        FN_GREATER,
        FunctionSignatureOptions().set_uses_operation_collation()},
       {bool_type, {int64_type, uint64_type}, FN_GREATER_INT64_UINT64},
       {bool_type, {uint64_type, int64_type}, FN_GREATER_UINT64_INT64}},
      FunctionOptions()
          .set_supports_safe_error_mode(false)
          .set_post_resolution_argument_constraint(absl::bind_front(
              &CheckArgumentsSupportComparison, "Greater than"))
          .set_no_matching_signature_callback(
              &NoMatchingSignatureForComparisonOperator)
          .set_sql_name(">")
          .set_get_sql_callback(absl::bind_front(&InfixFunctionSQL, ">")));
}

std::string BetweenFunctionSQL(absl::Span<const std::string> inputs) {
  ABSL_DCHECK_EQ(inputs.size(), 3);
  return absl::StrCat("(", inputs[0], ") BETWEEN (", inputs[1], ") AND (",
                      inputs[2], ")");
}

void GetBetweenFunctions(TypeFactory* type_factory,
                         const GoogleSQLBuiltinFunctionOptions& options,
                         NameToFunctionMap* functions) {
  const Type* const bool_type = type_factory->get_bool();
  const Type* const int64_type = type_factory->get_int64();
  const Type* const uint64_type = type_factory->get_uint64();

  // Historically, BETWEEN had only one function signature where all
  // arguments must be coercible to the same type.  The implication is that
  // if arguments could not be coerced to the same type then resolving BETWEEN
  // would fail, including cases for BETWEEN with INT64 and UINT64 arguments
  // which logically should be ok.  To support such cases, additional
  // signatures have been added (and those signatures are protected by a
  // LanguageFeature so that engines can opt-in to those signatures when
  // ready).
  //
  // Note that we cannot rewrite BETWEEN into '>=' and '<=' because BETWEEN
  // has run-once semantics for each of its input expressions.
  if (!options.language_options.LanguageFeatureEnabled(
          FEATURE_BETWEEN_UINT64_INT64)) {
    InsertFunction(
        functions, options, "$between", Function::SCALAR,
        {{bool_type,
          {ARG_TYPE_ANY_1, ARG_TYPE_ANY_1, ARG_TYPE_ANY_1},
          FN_BETWEEN,
          FunctionSignatureOptions().set_uses_operation_collation()}},
        FunctionOptions()
            .set_supports_safe_error_mode(false)
            .set_post_resolution_argument_constraint(
                absl::bind_front(&CheckArgumentsSupportComparison, "BETWEEN"))
            .set_get_sql_callback(&BetweenFunctionSQL));
  } else {
    InsertFunction(
        functions, options, "$between", Function::SCALAR,
        {{bool_type,
          {int64_type, uint64_type, uint64_type},
          FN_BETWEEN_INT64_UINT64_UINT64},
         {bool_type,
          {uint64_type, int64_type, uint64_type},
          FN_BETWEEN_UINT64_INT64_UINT64},
         {bool_type,
          {uint64_type, uint64_type, int64_type},
          FN_BETWEEN_UINT64_UINT64_INT64},
         {bool_type,
          {uint64_type, int64_type, int64_type},
          FN_BETWEEN_UINT64_INT64_INT64},
         {bool_type,
          {int64_type, uint64_type, int64_type},
          FN_BETWEEN_INT64_UINT64_INT64},
         {bool_type,
          {int64_type, int64_type, uint64_type},
          FN_BETWEEN_INT64_INT64_UINT64},
         {bool_type,
          {ARG_TYPE_ANY_1, ARG_TYPE_ANY_1, ARG_TYPE_ANY_1},
          FN_BETWEEN,
          FunctionSignatureOptions().set_uses_operation_collation()}},
        FunctionOptions()
            .set_supports_safe_error_mode(false)
            .set_post_resolution_argument_constraint(
                absl::bind_front(&CheckArgumentsSupportComparison, "BETWEEN"))
            .set_get_sql_callback(&BetweenFunctionSQL));
  }
}

std::string NoMatchingSignatureForLikeExprFunction(
    const std::string& qualified_function_name,
    const std::vector<InputArgumentType>& arguments, ProductMode product_mode) {
  if (arguments.empty()) {
    return "Unexpected missing of the first argument (the LHS) in a LIKE "
           "expression";
  }
  bool is_string_literal_compared_to_bytes = false;
  const InputArgumentType& lhs_argument = arguments[0];
  InputArgumentTypeSet rhs_argument_set;
  for (int idx = 1; idx < arguments.size(); ++idx) {
    rhs_argument_set.Insert(arguments[idx]);
    is_string_literal_compared_to_bytes |=
        IsStringLiteralComparedToBytes(lhs_argument, arguments[idx]);
  }
  std::string function_name =
      (absl::StrContains(qualified_function_name, "ALL"))
          ? qualified_function_name
          : "operator LIKE ANY|SOME";
  std::string error_message = absl::StrCat(
      "No matching signature for ", function_name, " for argument types ",
      lhs_argument.DebugString(), " and ", rhs_argument_set.ToString());
  if (is_string_literal_compared_to_bytes) {
    absl::StrAppend(&error_message, kErrorMessageCompareStringLiteralToBytes);
  }
  return error_message;
}

std::string LikeAnyAllFunctionSQL(absl::string_view display_name,
                                  absl::Span<const std::string> inputs) {
  ABSL_DCHECK_GT(inputs.size(), 1);
  std::vector<std::string> like_list(inputs.begin() + 1, inputs.end());
  return absl::StrCat(inputs[0], " ", display_name, " (",
                      absl::StrJoin(like_list, ", "), ")");
}

void GetLikeFunctions(TypeFactory* type_factory,
                      const GoogleSQLBuiltinFunctionOptions& options,
                      NameToFunctionMap* functions) {
  const Type* const bool_type = type_factory->get_bool();
  const Type* const byte_type = type_factory->get_bytes();
  const Type* const string_type = type_factory->get_string();

  InsertFunction(
      functions, options, "$like", Function::SCALAR,
      {{bool_type,
        {string_type, string_type},
        FN_STRING_LIKE,
        FunctionSignatureOptions().set_uses_operation_collation()},
       {bool_type, {byte_type, byte_type}, FN_BYTE_LIKE}},
      FunctionOptions()
          .set_supports_safe_error_mode(false)
          .set_no_matching_signature_callback(
              &NoMatchingSignatureForComparisonOperator)
          .set_get_sql_callback(absl::bind_front(&InfixFunctionSQL, "LIKE")));

  if (options.language_options.LanguageFeatureEnabled(
          FEATURE_LIKE_ANY_SOME_ALL)) {
    // Supports both LIKE ANY and LIKE SOME.
    InsertFunction(
        functions, options, "$like_any", Function::SCALAR,
        {{bool_type,
          {string_type, {string_type, FunctionArgumentType::REPEATED}},
          FN_STRING_LIKE_ANY,
          FunctionSignatureOptions().set_uses_operation_collation()},
         {bool_type,
          {byte_type, {byte_type, FunctionArgumentType::REPEATED}},
          FN_BYTE_LIKE_ANY}},
        FunctionOptions()
            .set_supports_safe_error_mode(false)
            .set_post_resolution_argument_constraint(
                absl::bind_front(&CheckArgumentsSupportEquality, "LIKE ANY"))
            .set_no_matching_signature_callback(
                &NoMatchingSignatureForLikeExprFunction)
            .set_sql_name("like any")
            .set_hide_supported_signatures(true)
            .set_get_sql_callback(
                absl::bind_front(&LikeAnyAllFunctionSQL, "LIKE ANY")));

    InsertFunction(
        functions, options, "$like_all", Function::SCALAR,
        {{bool_type,
          {string_type, {string_type, FunctionArgumentType::REPEATED}},
          FN_STRING_LIKE_ALL,
          FunctionSignatureOptions().set_uses_operation_collation()},
         {bool_type,
          {byte_type, {byte_type, FunctionArgumentType::REPEATED}},
          FN_BYTE_LIKE_ALL}},
        FunctionOptions()
            .set_supports_safe_error_mode(false)
            .set_post_resolution_argument_constraint(
                absl::bind_front(&CheckArgumentsSupportEquality, "LIKE ALL"))
            .set_no_matching_signature_callback(
                &NoMatchingSignatureForLikeExprFunction)
            .set_sql_name("like all")
            .set_hide_supported_signatures(true)
            .set_get_sql_callback(
                absl::bind_front(&LikeAnyAllFunctionSQL, "LIKE ALL")));

    InsertFunction(
        functions, options, "$not_like_any", Function::SCALAR,
        {{bool_type,
          {string_type, {string_type, FunctionArgumentType::REPEATED}},
          FN_STRING_NOT_LIKE_ANY,
          FunctionSignatureOptions().set_uses_operation_collation()},
         {bool_type,
          {byte_type, {byte_type, FunctionArgumentType::REPEATED}},
          FN_BYTE_NOT_LIKE_ANY}},
        FunctionOptions()
            .set_supports_safe_error_mode(false)
            .set_post_resolution_argument_constraint(absl::bind_front(
                &CheckArgumentsSupportEquality, "NOT LIKE ANY"))
            .set_no_matching_signature_callback(
                &NoMatchingSignatureForLikeExprFunction)
            .set_sql_name("not like any")
            .set_hide_supported_signatures(true)
            .set_get_sql_callback(
                absl::bind_front(&LikeAnyAllFunctionSQL, "NOT LIKE ANY")));

    InsertFunction(
        functions, options, "$not_like_all", Function::SCALAR,
        {{bool_type,
          {string_type, {string_type, FunctionArgumentType::REPEATED}},
          FN_STRING_NOT_LIKE_ALL,
          FunctionSignatureOptions().set_uses_operation_collation()},
         {bool_type,
          {byte_type, {byte_type, FunctionArgumentType::REPEATED}},
          FN_BYTE_NOT_LIKE_ALL}},
        FunctionOptions()
            .set_supports_safe_error_mode(false)
            .set_post_resolution_argument_constraint(absl::bind_front(
                &CheckArgumentsSupportEquality, "NOT LIKE ALL"))
            .set_no_matching_signature_callback(
                &NoMatchingSignatureForLikeExprFunction)
            .set_sql_name("not like all")
            .set_hide_supported_signatures(true)
            .set_get_sql_callback(
                absl::bind_front(&LikeAnyAllFunctionSQL, "NOT LIKE ALL")));
  }
}

absl::Status CheckLikeExprArrayArguments(
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options) {
  GOOGLESQL_RET_CHECK_EQ(arguments.size(), 2);
  if (!arguments[1].type()->IsArray()) {
    return MakeSqlError() << "Second argument of LIKE ANY|SOME|ALL UNNEST must "
                             "be an array but was "
                          << arguments[1].DebugString();
  }
  return absl::OkStatus();
}

std::string NoMatchingSignatureForLikeExprArrayFunction(
    const std::string& qualified_function_name,
    absl::Span<const InputArgumentType> arguments, ProductMode product_mode) {
  if (arguments.size() != 2) {
    return "Incorrect number of arguments for a LIKE expression.";
  }
  std::string function_name =
      (absl::StrContains(qualified_function_name, "ALL"))
          ? qualified_function_name
          : "operator LIKE ANY|SOME UNNEST";
  std::string error_message =
      Function::GetGenericNoMatchingFunctionSignatureErrorMessage(
          function_name, arguments, product_mode);
  const InputArgumentType& lhs_arg = arguments[0];
  const InputArgumentType& rhs_arg = arguments[1];
  bool is_string_literal_compared_to_bytes = false;
  if (rhs_arg.type() != nullptr && lhs_arg.type() != nullptr &&
      rhs_arg.type()->IsArray()) {
    const Type* element_type = rhs_arg.type()->AsArray()->element_type();
    if ((lhs_arg.type()->IsString() && lhs_arg.is_literal() &&
         element_type->IsBytes()) ||
        (lhs_arg.type()->IsBytes() && rhs_arg.is_literal() &&
         element_type->IsString())) {
      is_string_literal_compared_to_bytes = true;
    }
  }
  if (is_string_literal_compared_to_bytes) {
    absl::StrAppend(&error_message, kErrorMessageCompareStringLiteralToBytes);
  }
  return error_message;
}

std::string LikeAnyAllArrayFunctionSQL(absl::string_view display_name,
                                       absl::Span<const std::string> inputs) {
  ABSL_DCHECK_EQ(inputs.size(), 2);
  return absl::StrCat(inputs[0], " ", display_name, "(", inputs[1], ")");
}

absl::Status GetLikeArrayFunctions(
    TypeFactory* type_factory, const GoogleSQLBuiltinFunctionOptions& options,
    NameToFunctionMap* functions) {
  const Type* const bool_type = type_factory->get_bool();
  const Type* const byte_type = type_factory->get_bytes();
  const Type* const string_type = type_factory->get_string();
  const ArrayType* array_string_type;
  GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeArrayType(string_type, &array_string_type));
  const ArrayType* array_byte_type;
  GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeArrayType(byte_type, &array_byte_type));

  if (options.language_options.LanguageFeatureEnabled(
          FEATURE_LIKE_ANY_SOME_ALL_ARRAY)) {
    // Supports both LIKE ANY and LIKE SOME arrays.
    InsertFunction(
        functions, options, "$like_any_array", Function::SCALAR,
        {{bool_type,
          {string_type,
           {array_string_type, FunctionArgumentTypeOptions()
                                   .set_uses_array_element_for_collation()}},
          FN_STRING_ARRAY_LIKE_ANY,
          FunctionSignatureOptions().set_uses_operation_collation()},
         {bool_type, {byte_type, array_byte_type}, FN_BYTE_ARRAY_LIKE_ANY}},
        FunctionOptions()
            .set_supports_safe_error_mode(false)
            .set_pre_resolution_argument_constraint(
                // Verifies for <expr> LIKE ANY|SOME UNNEST(<array_expr>)
                // * Argument to UNNEST is an array.
                // * <expr> and elements of <array_expr> are comparable.
                &CheckLikeExprArrayArguments)
            .set_no_matching_signature_callback(
                &NoMatchingSignatureForLikeExprArrayFunction)
            .set_sql_name("like any unnest")
            .set_hide_supported_signatures(true)
            .set_get_sql_callback(absl::bind_front(&LikeAnyAllArrayFunctionSQL,
                                                   "LIKE ANY UNNEST")));

    InsertFunction(
        functions, options, "$like_all_array", Function::SCALAR,
        {{bool_type,
          {string_type,
           {array_string_type, FunctionArgumentTypeOptions()
                                   .set_uses_array_element_for_collation()}},
          FN_STRING_ARRAY_LIKE_ALL,
          FunctionSignatureOptions().set_uses_operation_collation()},
         {bool_type, {byte_type, array_byte_type}, FN_BYTE_ARRAY_LIKE_ALL}},
        FunctionOptions()
            .set_supports_safe_error_mode(false)
            .set_pre_resolution_argument_constraint(
                // Verifies for <expr> LIKE ALL UNNEST(<array_expr>)
                // * Argument to UNNEST is an array.
                // * <expr> and elements of <array_expr> are comparable.
                &CheckLikeExprArrayArguments)
            .set_no_matching_signature_callback(
                &NoMatchingSignatureForLikeExprArrayFunction)
            .set_sql_name("like all unnest")
            .set_hide_supported_signatures(true)
            .set_get_sql_callback(absl::bind_front(&LikeAnyAllArrayFunctionSQL,
                                                   "LIKE ALL UNNEST")));

    InsertFunction(
        functions, options, "$not_like_any_array", Function::SCALAR,
        {{bool_type,
          {string_type,
           {array_string_type, FunctionArgumentTypeOptions()
                                   .set_uses_array_element_for_collation()}},
          FN_STRING_ARRAY_NOT_LIKE_ANY,
          FunctionSignatureOptions().set_uses_operation_collation()},
         {bool_type, {byte_type, array_byte_type}, FN_BYTE_ARRAY_NOT_LIKE_ANY}},
        FunctionOptions()
            .set_supports_safe_error_mode(false)
            .set_pre_resolution_argument_constraint(
                // Verifies for <expr> NOT LIKE ANY|SOME UNNEST(<array_expr>)
                // * Argument to UNNEST is an array.
                // * <expr> and elements of <array_expr> are comparable.
                &CheckLikeExprArrayArguments)
            .set_no_matching_signature_callback(
                &NoMatchingSignatureForLikeExprArrayFunction)
            .set_sql_name("not like any unnest")
            .set_hide_supported_signatures(true)
            .set_get_sql_callback(absl::bind_front(&LikeAnyAllArrayFunctionSQL,
                                                   "NOT LIKE ANY UNNEST")));

    InsertFunction(
        functions, options, "$not_like_all_array", Function::SCALAR,
        {{bool_type,
          {string_type,
           {array_string_type, FunctionArgumentTypeOptions()
                                   .set_uses_array_element_for_collation()}},
          FN_STRING_ARRAY_NOT_LIKE_ALL,
          FunctionSignatureOptions().set_uses_operation_collation()},
         {bool_type, {byte_type, array_byte_type}, FN_BYTE_ARRAY_NOT_LIKE_ALL}},
        FunctionOptions()
            .set_supports_safe_error_mode(false)
            .set_pre_resolution_argument_constraint(
                // Verifies for <expr> NOT LIKE ALL UNNEST(<array_expr>)
                // * Argument to UNNEST is an array.
                // * <expr> and elements of <array_expr> are comparable.
                &CheckLikeExprArrayArguments)
            .set_no_matching_signature_callback(
                &NoMatchingSignatureForLikeExprArrayFunction)
            .set_sql_name("not like all unnest")
            .set_hide_supported_signatures(true)
            .set_get_sql_callback(absl::bind_front(&LikeAnyAllArrayFunctionSQL,
                                                   "NOT LIKE ALL UNNEST")));
  }
  return absl::OkStatus();
}

// For the
//
//   <expr> [NOT] IN UNNEST(<array_expr>)
//   <expr> <equality-operator> {ANY|SOME|ALL} UNNEST(<array_expr>)
//
// forms we want to verify that the argument to UNNEST is an array,
// and that <expr> and the elements of <array_expr> support equality.
absl::Status CheckQuantifiedComparisonArrayArgumentsEquality(
    absl::string_view display_name,
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options) {
  GOOGLESQL_RET_CHECK_EQ(arguments.size(), 2);

  // Query parameters have unknown types at this point, so we cannot see if
  // they are arrays and what their element types are.  That will happen
  // during resolution.
  bool array_is_untyped_parameter = arguments[1].is_untyped_query_parameter();

  if (arguments[1].type() == nullptr ||
      (!array_is_untyped_parameter && !arguments[1].type()->IsArray())) {
    return MakeSqlError() << "Second argument of " << display_name
                          << " must be an array but was "
                          << arguments[1].UserFacingName(
                                 language_options.product_mode());
  }

  if (arguments[0].type() == nullptr ||
      !arguments[0].type()->SupportsEquality(language_options)) {
    return MakeSqlError() << "First argument to " << display_name << " of type "
                          << arguments[0].DebugString()
                          << " does not support equality comparison";
  }

  if (!array_is_untyped_parameter &&
      !arguments[1].type()->AsArray()->element_type()->SupportsEquality(
          language_options)) {
    return MakeSqlError()
           << "Second argument to " << display_name << " of type "
           << arguments[1].DebugString()
           << " is not supported because array element type is not "
              "equality comparable";
  }

  return absl::OkStatus();
}

// For the
//
//   <expr> <ordering-operator> {ANY|SOME|ALL} UNNEST(<array_expr>)
//
// forms we want to verify that the argument to UNNEST is an array,
// and that <expr> and the elements of <array_expr> support ordering.
//
// Note that ordering is a stricter requirement than equality,
// so operators like >= only need to check for ordering, not
// both ordering and equality.
absl::Status CheckQuantifiedComparisonArrayArgumentsOrdering(
    absl::string_view display_name,
    const std::vector<InputArgumentType>& arguments,
    const LanguageOptions& language_options) {
  GOOGLESQL_RET_CHECK_EQ(arguments.size(), 2);

  // Query parameters have unknown types at this point, so we cannot see if
  // they are arrays and what their element types are.  That will happen
  // during resolution.
  bool array_is_untyped_parameter = arguments[1].is_untyped_query_parameter();

  if (arguments[1].type() == nullptr ||
      (!array_is_untyped_parameter && !arguments[1].type()->IsArray())) {
    return MakeSqlError() << "Second argument of " << display_name
                          << " must be an array but was "
                          << arguments[1].UserFacingName(
                                 language_options.product_mode());
  }

  if (arguments[0].type() == nullptr ||
      !arguments[0].type()->SupportsOrdering(language_options, nullptr)) {
    return MakeSqlError() << "First argument to " << display_name << " of type "
                          << arguments[0].DebugString()
                          << " does not support ordering";
  }

  if (!array_is_untyped_parameter &&
      !arguments[1].type()->AsArray()->element_type()->SupportsOrdering(
          language_options, nullptr)) {
    return MakeSqlError()
           << "Second argument to " << display_name << " of type "
           << arguments[1].DebugString()
           << " is not supported because array element type is not orderable";
  }

  return absl::OkStatus();
}

std::string NoMatchingSignatureForQuantifiedComparisonListFunction(
    absl::string_view qualified_function_name,
    absl::Span<const InputArgumentType> arguments, ProductMode product_mode) {
  if (arguments.empty()) {
    return Function::GetGenericNoMatchingFunctionSignatureErrorMessage(
        qualified_function_name, arguments, product_mode);
  }
  bool is_string_literal_compared_to_bytes = false;
  const InputArgumentType& lhs_argument = arguments[0];
  InputArgumentTypeSet rhs_argument_set;
  for (int idx = 1; idx < arguments.size(); ++idx) {
    rhs_argument_set.Insert(arguments[idx]);
    is_string_literal_compared_to_bytes |=
        IsStringLiteralComparedToBytes(lhs_argument, arguments[idx]);
  }
  std::string error_message =
      absl::StrCat("No matching signature for ", qualified_function_name,
                   " for argument types ", lhs_argument.DebugString(), " and ",
                   rhs_argument_set.ToString());
  if (is_string_literal_compared_to_bytes) {
    absl::StrAppend(&error_message, kErrorMessageCompareStringLiteralToBytes);
  }
  return error_message;
}

std::string NoMatchingSignatureForQuantifiedComparisonArrayFunction(
    absl::string_view qualified_function_name,
    absl::Span<const InputArgumentType> arguments, ProductMode product_mode) {
  std::string error_message =
      Function::GetGenericNoMatchingFunctionSignatureErrorMessage(
          qualified_function_name, arguments, product_mode);
  if (arguments.size() < 2) {
    return error_message;
  }

  const InputArgumentType& lhs_arg = arguments[0];
  const InputArgumentType& rhs_arg = arguments[1];
  bool is_string_literal_compared_to_bytes = false;
  // The rhs can be an untyped or an array type. This is enforced in
  // the pre_resolution_argument_constraint.
  if (rhs_arg.type() != nullptr && lhs_arg.type() != nullptr &&
      rhs_arg.type()->IsArray()) {
    const Type* element_type = rhs_arg.type()->AsArray()->element_type();
    if ((lhs_arg.type()->IsString() && lhs_arg.is_literal() &&
         element_type->IsBytes()) ||
        (lhs_arg.type()->IsBytes() && rhs_arg.is_literal() &&
         element_type->IsString())) {
      is_string_literal_compared_to_bytes = true;
    }
  }
  if (is_string_literal_compared_to_bytes) {
    absl::StrAppend(&error_message, kErrorMessageCompareStringLiteralToBytes);
  }
  return error_message;
}

std::string QuantifiedComparisonListFunctionSQL(
    absl::string_view display_name, absl::Span<const std::string> inputs) {
  ABSL_DCHECK_GT(inputs.size(), 1);
  std::vector<std::string> in_list(inputs.begin() + 1, inputs.end());
  return absl::StrCat("(", inputs[0], ") ", display_name, " (",
                      absl::StrJoin(in_list, ", "), ")");
}

std::string QuantifiedComparisonArrayFunctionSQL(
    absl::string_view display_name, const std::vector<std::string>& inputs) {
  ABSL_DCHECK_EQ(inputs.size(), 2);
  return absl::StrCat("(", inputs[0], ") ", display_name, "(", inputs[1], ")");
}

void GetInFunctions(TypeFactory* type_factory,
                    const GoogleSQLBuiltinFunctionOptions& options,
                    NameToFunctionMap* functions) {
  const Type* const bool_type = type_factory->get_bool();

  // TODO: Do we want to support IN for non-compatible integers, i.e.,
  // '<uint64col> IN (<int32col>, <int64col>)'?
  InsertFunction(
      functions, options, "$in", Function::SCALAR,
      {{bool_type,
        {ARG_TYPE_ANY_1, {ARG_TYPE_ANY_1, FunctionArgumentType::REPEATED}},
        FN_IN,
        FunctionSignatureOptions().set_uses_operation_collation()}},
      FunctionOptions()
          .set_supports_safe_error_mode(false)
          .set_post_resolution_argument_constraint(
              absl::bind_front(&CheckArgumentsSupportEquality, "IN"))
          .set_no_matching_signature_callback(
              &NoMatchingSignatureForQuantifiedComparisonListFunction)
          .set_hide_supported_signatures(true)
          .set_get_sql_callback(
              absl::bind_front(&QuantifiedComparisonListFunctionSQL, "IN")));

  // TODO: Do we want to support:
  //   '<uint64col>' IN UNNEST(<int64_array>)'?
  InsertFunction(
      functions, options, "$in_array", Function::SCALAR,
      {{bool_type,
        {ARG_TYPE_ANY_1,
         {ARG_ARRAY_TYPE_ANY_1, FunctionArgumentTypeOptions()
                                    .set_uses_array_element_for_collation()}},
        FN_IN_ARRAY,
        FunctionSignatureOptions().set_uses_operation_collation(true)}},
      FunctionOptions()
          .set_supports_safe_error_mode(false)
          .set_pre_resolution_argument_constraint(absl::bind_front(
              &CheckQuantifiedComparisonArrayArgumentsEquality, "IN UNNEST"))
          .set_no_matching_signature_callback(
              &NoMatchingSignatureForQuantifiedComparisonArrayFunction)
          .set_sql_name("in unnest")
          .set_hide_supported_signatures(true)
          .set_get_sql_callback(absl::bind_front(
              &QuantifiedComparisonArrayFunctionSQL, "IN UNNEST")));
}

void GetQuantifiedComparisonFunctions(
    TypeFactory* type_factory, const GoogleSQLBuiltinFunctionOptions& options,
    NameToFunctionMap* functions) {
  const Type* const bool_type = type_factory->get_bool();

  FunctionSignatureOptions quantified_comparison_options =
      FunctionSignatureOptions()
          .set_uses_operation_collation()
          .AddRequiredLanguageFeature(FEATURE_GENERAL_QUANTIFIED_COMPARISONS);

  struct FnInfo {
    absl::string_view name;
    FunctionSignatureId id;
    absl::string_view display_name;
    absl::string_view operator_name;
    bool is_equality = false;
  };
  FnInfo list_forms[] = {
      // Equality.
      {"$eq_any", FN_EQ_ANY, "= ANY", "=", true},
      {"$eq_all", FN_EQ_ALL, "= ALL", "=", true},
      {"$ne_any", FN_NE_ANY, "!= ANY", "!=", true},
      {"$ne_all", FN_NE_ALL, "!= ALL", "!=", true},

      // Ordering.
      {"$lt_any", FN_LT_ANY, "< ANY", "<"},
      {"$lt_all", FN_LT_ALL, "< ALL", "<"},
      {"$le_any", FN_LE_ANY, "<= ANY", "<="},
      {"$le_all", FN_LE_ALL, "<= ALL", "<="},
      {"$gt_any", FN_GT_ANY, "> ANY", ">"},
      {"$gt_all", FN_GT_ALL, "> ALL", ">"},
      {"$ge_any", FN_GE_ANY, ">= ANY", ">="},
      {"$ge_all", FN_GE_ALL, ">= ALL", ">="},
  };
  FnInfo array_forms[] = {
      // Equality.
      {"$eq_any_array", FN_EQ_ANY_ARRAY, "= ANY UNNEST", "=", true},
      {"$eq_all_array", FN_EQ_ALL_ARRAY, "= ALL UNNEST", "=", true},
      {"$ne_any_array", FN_NE_ANY_ARRAY, "!= ANY UNNEST", "!=", true},
      {"$ne_all_array", FN_NE_ALL_ARRAY, "!= ALL UNNEST", "!=", true},

      // Ordering.
      {"$lt_any_array", FN_LT_ANY_ARRAY, "< ANY UNNEST", "<"},
      {"$lt_all_array", FN_LT_ALL_ARRAY, "< ALL UNNEST", "<"},
      {"$le_any_array", FN_LE_ANY_ARRAY, "<= ANY UNNEST", "<="},
      {"$le_all_array", FN_LE_ALL_ARRAY, "<= ALL UNNEST", "<="},
      {"$gt_any_array", FN_GT_ANY_ARRAY, "> ANY UNNEST", ">"},
      {"$gt_all_array", FN_GT_ALL_ARRAY, "> ALL UNNEST", ">"},
      {"$ge_any_array", FN_GE_ANY_ARRAY, ">= ANY UNNEST", ">="},
      {"$ge_all_array", FN_GE_ALL_ARRAY, ">= ALL UNNEST", ">="},
  };

  for (const auto& fi : list_forms) {
    InsertFunction(
        functions, options, fi.name, Function::SCALAR,
        {{bool_type,
          {ARG_TYPE_ANY_1, {ARG_TYPE_ANY_1, FunctionArgumentType::REPEATED}},
          fi.id,
          quantified_comparison_options}},
        FunctionOptions()
            .set_supports_safe_error_mode(false)
            .set_post_resolution_argument_constraint(absl::bind_front(
                fi.is_equality ? &CheckArgumentsSupportEquality
                               : &CheckArgumentsSupportComparison,
                fi.operator_name))
            .set_no_matching_signature_callback(
                &NoMatchingSignatureForQuantifiedComparisonListFunction)
            .set_hide_supported_signatures(true)
            .set_sql_name(fi.display_name)
            .set_get_sql_callback(absl::bind_front(
                &QuantifiedComparisonListFunctionSQL, fi.display_name)));
  }
  for (auto& fi : array_forms) {
    InsertFunction(
        functions, options, fi.name, Function::SCALAR,
        {{bool_type,
          {ARG_TYPE_ANY_1,
           {ARG_ARRAY_TYPE_ANY_1, FunctionArgumentTypeOptions()
                                      .set_uses_array_element_for_collation()}},
          fi.id,
          quantified_comparison_options}},
        FunctionOptions()
            .set_supports_safe_error_mode(false)
            .set_pre_resolution_argument_constraint(absl::bind_front(
                fi.is_equality
                    ? &CheckQuantifiedComparisonArrayArgumentsEquality
                    : &CheckQuantifiedComparisonArrayArgumentsOrdering,
                fi.display_name))
            .set_no_matching_signature_callback(
                &NoMatchingSignatureForQuantifiedComparisonArrayFunction)
            .set_hide_supported_signatures(true)
            .set_sql_name(fi.display_name)
            .set_get_sql_callback(absl::bind_front(
                &QuantifiedComparisonArrayFunctionSQL, fi.display_name)));
  }
}

}  // namespace

absl::Status GetComparisonFunctions(
    TypeFactory* type_factory, const GoogleSQLBuiltinFunctionOptions& options,
    NameToFunctionMap* functions) {
  GetEqualityFunctions(type_factory, options, functions);
  GetDistinctFunctions(type_factory, options, functions);
  GetInequalityFunctions(type_factory, options, functions);
  GetBetweenFunctions(type_factory, options, functions);
  GetLikeFunctions(type_factory, options, functions);
  GOOGLESQL_RETURN_IF_ERROR(GetLikeArrayFunctions(type_factory, options, functions));
  GetInFunctions(type_factory, options, functions);
  GetQuantifiedComparisonFunctions(type_factory, options, functions);
  return absl::OkStatus();
}

}  // namespace googlesql
