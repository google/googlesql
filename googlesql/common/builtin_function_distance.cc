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
#include <utility>
#include <vector>

#include "googlesql/common/builtin_function_internal.h"
#include "googlesql/common/builtins_output_properties.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/bind_front.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

// This constant represents the index of the third argument `options` of
// approximate distance functions. Only the `options` argument allows a
// Type to be defined through BuiltinFunctionOptions.
static constexpr int kOptionsArgIdx = 2;

static std::string CheckApproximateDistanceFnProtoArguments(
    absl::string_view function_name, const FunctionSignature& matched_signature,
    absl::Span<const InputArgumentType> arguments) {
  ABSL_CHECK_EQ(arguments.size(), matched_signature.arguments().size());  // Crash OK
  if (arguments.size() > 2 &&
      matched_signature.argument(kOptionsArgIdx).type()->IsProto()) {
    if (!arguments[2].is_literal_for_constness()) {
      return absl::StrCat("Argument `options` of function ", function_name,
                          " must be a literal");
    }
  }
  return "";
}

static std::string CheckApproximateDistanceFnJsonArguments(
    absl::string_view function_name, const FunctionSignature& matched_signature,
    absl::Span<const InputArgumentType> arguments) {
  ABSL_CHECK_EQ(arguments.size(), matched_signature.arguments().size());  // Crash OK
  if (arguments.size() > 2 &&
      matched_signature.argument(kOptionsArgIdx).type()->IsJson()) {
    if (!arguments[2].is_literal_for_constness()) {
      return absl::StrCat("Argument `options` of function ", function_name,
                          " must be a JSON literal");
    }
  }
  return "";
}

static absl::Status MaybeAddApproximateDistanceFunctionProtoSignature(
    const BuiltinFunctionOptions& options, absl::string_view fn_name,
    FunctionSignatureId id, const int arg_idx, const ArrayType* vector_type,
    std::vector<FunctionSignatureOnHeap>& signatures,
    BuiltinsOutputProperties& output_properties) {
  // Mark this signature as one that supports a supplied Type.
  output_properties.MarkSupportsSuppliedArgumentType(id, arg_idx);
  // Check if a Type was actually supplied in `options`.
  if (auto it = options.argument_types.find({id, arg_idx});
      it != options.argument_types.end()) {
    const Type* proto_type = it->second;
    if (!proto_type->IsProto()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Supplied argument type for the `options` argument of function ",
          fn_name, " must be a proto"));
    }
    FunctionArgumentType proto_options_arg = FunctionArgumentType(
        proto_type, FunctionArgumentTypeOptions(FunctionArgumentType::REQUIRED)
                        .set_argument_name("options", kNamedOnly)
                        .set_must_be_constant_expression());
    signatures.push_back(
        {types::DoubleType(),
         {vector_type, vector_type, proto_options_arg},
         id,
         FunctionSignatureOptions().set_constraints(absl::bind_front(
             &CheckApproximateDistanceFnProtoArguments, fn_name))});
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status GetDistanceFunctions(TypeFactory* type_factory,
                                  const BuiltinFunctionOptions& options,
                                  NameToFunctionMap* functions,
                                  BuiltinsOutputProperties& output_properties) {
  std::vector<StructType::StructField> input_struct_fields_int64 = {
      {"key", types::Int64Type()}, {"value", types::DoubleType()}};
  const StructType* struct_int64 = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(
      type_factory->MakeStructType({input_struct_fields_int64}, &struct_int64));
  const ArrayType* array_struct_int64_key_type;
  GOOGLESQL_RETURN_IF_ERROR(
      type_factory->MakeArrayType(struct_int64, &array_struct_int64_key_type));

  std::vector<StructType::StructField> input_struct_fields_string = {
      {"key", types::StringType()}, {"value", types::DoubleType()}};
  const StructType* struct_string = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeStructType({input_struct_fields_string},
                                               &struct_string));
  const ArrayType* array_struct_string_key_type;
  GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeArrayType(struct_string,
                                              &array_struct_string_key_type));

  FunctionOptions function_options;
  std::vector<FunctionSignatureOnHeap> cosine_signatures = {
      {types::DoubleType(),
       {types::DoubleArrayType(), types::DoubleArrayType()},
       FN_COSINE_DISTANCE_DENSE_DOUBLE},
      {types::DoubleType(),
       {array_struct_int64_key_type, array_struct_int64_key_type},
       FN_COSINE_DISTANCE_SPARSE_INT64},
      {types::DoubleType(),
       {array_struct_string_key_type, array_struct_string_key_type},
       FN_COSINE_DISTANCE_SPARSE_STRING}};

  if (options.language_options.LanguageFeatureEnabled(
          FEATURE_ENABLE_FLOAT_DISTANCE_FUNCTIONS)) {
    cosine_signatures.push_back(
        {types::DoubleType(),
         {types::FloatArrayType(), types::FloatArrayType()},
         FN_COSINE_DISTANCE_DENSE_FLOAT});
  }

  InsertFunction(functions, options, "cosine_distance", Function::SCALAR,
                 cosine_signatures, function_options);

  FunctionArgumentType json_options_arg = FunctionArgumentType(
      types::JsonType(),
      FunctionArgumentTypeOptions(FunctionArgumentType::REQUIRED)
          .set_argument_name("options", kNamedOnly));

  std::vector<FunctionSignatureOnHeap> approx_cosine_signatures = {
      {types::DoubleType(),
       {types::DoubleArrayType(), types::DoubleArrayType()},
       FN_APPROX_COSINE_DISTANCE_DOUBLE},
      {types::DoubleType(),
       {types::DoubleArrayType(), types::DoubleArrayType(), json_options_arg},
       FN_APPROX_COSINE_DISTANCE_DOUBLE_WITH_JSON_OPTIONS,
       FunctionSignatureOptions().set_constraints(
           absl::bind_front(&CheckApproximateDistanceFnJsonArguments,
                            "APPROX_COSINE_DISTANCE"))},
      {types::DoubleType(),
       {types::FloatArrayType(), types::FloatArrayType()},
       FN_APPROX_COSINE_DISTANCE_FLOAT},
      {types::DoubleType(),
       {types::FloatArrayType(), types::FloatArrayType(), json_options_arg},
       FN_APPROX_COSINE_DISTANCE_FLOAT_WITH_JSON_OPTIONS,
       FunctionSignatureOptions().set_constraints(
           absl::bind_front(&CheckApproximateDistanceFnJsonArguments,
                            "APPROX_COSINE_DISTANCE"))}};

  GOOGLESQL_RETURN_IF_ERROR(MaybeAddApproximateDistanceFunctionProtoSignature(
      options, "APPROX_COSINE_DISTANCE",
      FN_APPROX_COSINE_DISTANCE_DOUBLE_WITH_PROTO_OPTIONS, kOptionsArgIdx,
      types::DoubleArrayType(), approx_cosine_signatures, output_properties));
  GOOGLESQL_RETURN_IF_ERROR(MaybeAddApproximateDistanceFunctionProtoSignature(
      options, "APPROX_COSINE_DISTANCE",
      FN_APPROX_COSINE_DISTANCE_FLOAT_WITH_PROTO_OPTIONS, kOptionsArgIdx,
      types::FloatArrayType(), approx_cosine_signatures, output_properties));

  InsertFunction(functions, options, "approx_cosine_distance", Function::SCALAR,
                 approx_cosine_signatures, /*function_options=*/{});

  std::vector<FunctionSignatureOnHeap> euclidean_signatures = {
      {types::DoubleType(),
       {types::DoubleArrayType(), types::DoubleArrayType()},
       FN_EUCLIDEAN_DISTANCE_DENSE_DOUBLE},
      {types::DoubleType(),
       {array_struct_int64_key_type, array_struct_int64_key_type},
       FN_EUCLIDEAN_DISTANCE_SPARSE_INT64},
      {types::DoubleType(),
       {array_struct_string_key_type, array_struct_string_key_type},
       FN_EUCLIDEAN_DISTANCE_SPARSE_STRING}};

  if (options.language_options.LanguageFeatureEnabled(
          FEATURE_ENABLE_FLOAT_DISTANCE_FUNCTIONS)) {
    euclidean_signatures.push_back(
        {types::DoubleType(),
         {types::FloatArrayType(), types::FloatArrayType()},
         FN_EUCLIDEAN_DISTANCE_DENSE_FLOAT});
  }

  InsertFunction(functions, options, "euclidean_distance", Function::SCALAR,
                 euclidean_signatures, function_options);

  std::vector<FunctionSignatureOnHeap> approx_euclidean_signatures = {
      {types::DoubleType(),
       {types::DoubleArrayType(), types::DoubleArrayType()},
       FN_APPROX_EUCLIDEAN_DISTANCE_DOUBLE},
      {types::DoubleType(),
       {types::DoubleArrayType(), types::DoubleArrayType(), json_options_arg},
       FN_APPROX_EUCLIDEAN_DISTANCE_DOUBLE_WITH_JSON_OPTIONS,
       FunctionSignatureOptions().set_constraints(
           absl::bind_front(&CheckApproximateDistanceFnJsonArguments,
                            "APPROX_EUCLIDEAN_DISTANCE"))},
      {types::DoubleType(),
       {types::FloatArrayType(), types::FloatArrayType()},
       FN_APPROX_EUCLIDEAN_DISTANCE_FLOAT},
      {types::DoubleType(),
       {types::FloatArrayType(), types::FloatArrayType(), json_options_arg},
       FN_APPROX_EUCLIDEAN_DISTANCE_FLOAT_WITH_JSON_OPTIONS,
       FunctionSignatureOptions().set_constraints(
           absl::bind_front(&CheckApproximateDistanceFnJsonArguments,
                            "APPROX_EUCLIDEAN_DISTANCE"))}};

  GOOGLESQL_RETURN_IF_ERROR(MaybeAddApproximateDistanceFunctionProtoSignature(
      options, "APPROX_EUCLIDEAN_DISTANCE",
      FN_APPROX_EUCLIDEAN_DISTANCE_DOUBLE_WITH_PROTO_OPTIONS, kOptionsArgIdx,
      types::DoubleArrayType(), approx_euclidean_signatures,
      output_properties));
  GOOGLESQL_RETURN_IF_ERROR(MaybeAddApproximateDistanceFunctionProtoSignature(
      options, "APPROX_EUCLIDEAN_DISTANCE",
      FN_APPROX_EUCLIDEAN_DISTANCE_FLOAT_WITH_PROTO_OPTIONS, kOptionsArgIdx,
      types::FloatArrayType(), approx_euclidean_signatures, output_properties));

  InsertFunction(functions, options, "approx_euclidean_distance",
                 Function::SCALAR, approx_euclidean_signatures,
                 /*function_options=*/{});

  // Lambdas for a common error message amongst function rewriters.
  auto null_element_err_msg_base = [](absl::string_view name) {
    return absl::Substitute(
        "Cannot compute $0 with a NULL element, since it is unclear if NULLs "
        "should be ignored, counted as a zero value, or another "
        "interpretation.",
        name);
  };

  // Lambda with argument-checking SQL common to distance function rewriters.
  auto distance_fn_rewrite_sql = [&null_element_err_msg_base](
                                     absl::string_view name,
                                     absl::string_view sql) {
    return absl::Substitute(R"sql(
      CASE
        WHEN input_array_1 IS NULL OR input_array_2 IS NULL
          THEN NULL
        WHEN ARRAY_LENGTH(input_array_1) = 0 AND ARRAY_LENGTH(input_array_2) = 0
          THEN CAST(0 AS FLOAT64)
        WHEN ARRAY_LENGTH(input_array_1) != ARRAY_LENGTH(input_array_2)
          THEN ERROR(FORMAT(
            "Array arguments to %s must have equal length. The given arrays have lengths of %d and %d",
            "$0", ARRAY_LENGTH(input_array_1), ARRAY_LENGTH(input_array_2)))
        ELSE
          $1
          WHERE
            IF(e1 IS NULL, ERROR(FORMAT(
              "%s The NULL element was found in the first array argument at OFFSET %d",
              "$2", index)), TRUE) AND
            IF(input_array_2[OFFSET(index)] IS NULL, ERROR(FORMAT(
              "%s The NULL element was found in the second array argument at OFFSET %d",
              "$2", index)), TRUE))
        END
    )sql",
                            name, sql, null_element_err_msg_base(name));
  };

  // Lambda for defining named arguments for distance function rewriters.
  auto distance_fn_named_arg = [](const Type* arg_type,
                                  absl::string_view name) {
    return FunctionArgumentType(
        arg_type,
        FunctionArgumentTypeOptions().set_argument_name(name, kPositionalOnly));
  };

  // Use a Rewriter for DOT_PRODUCT.
  std::string dot_product_sql = distance_fn_rewrite_sql("DOT_PRODUCT", R"sql(
      (SELECT
            SUM(
              CAST(e1 AS FLOAT64) *
              CAST(input_array_2[OFFSET(index)] AS FLOAT64))
          FROM UNNEST(input_array_1) AS e1 WITH OFFSET index
    )sql");

  FunctionSignatureOptions dot_product_signature_options =
      SetDefinitionForInlining(dot_product_sql, true)
          .AddRequiredLanguageFeature(FEATURE_DOT_PRODUCT);

  std::vector<FunctionSignatureOnHeap> dot_product_signatures = {
      {types::DoubleType(),
       {distance_fn_named_arg(types::Int64ArrayType(), "input_array_1"),
        distance_fn_named_arg(types::Int64ArrayType(), "input_array_2")},
       FN_DOT_PRODUCT_INT64,
       dot_product_signature_options},
      {types::DoubleType(),
       {distance_fn_named_arg(types::FloatArrayType(), "input_array_1"),
        distance_fn_named_arg(types::FloatArrayType(), "input_array_2")},
       FN_DOT_PRODUCT_FLOAT,
       dot_product_signature_options},
      {types::DoubleType(),
       {distance_fn_named_arg(types::DoubleArrayType(), "input_array_1"),
        distance_fn_named_arg(types::DoubleArrayType(), "input_array_2")},
       FN_DOT_PRODUCT_DOUBLE,
       dot_product_signature_options}};

  InsertFunction(functions, options, "dot_product", Function::SCALAR,
                 dot_product_signatures, function_options);

  std::vector<FunctionSignatureOnHeap> approx_dot_product_signatures = {
      {types::DoubleType(),
       {types::Int64ArrayType(), types::Int64ArrayType()},
       FN_APPROX_DOT_PRODUCT_INT64},
      {types::DoubleType(),
       {types::Int64ArrayType(), types::Int64ArrayType(), json_options_arg},
       FN_APPROX_DOT_PRODUCT_INT64_WITH_JSON_OPTIONS,
       FunctionSignatureOptions().set_constraints(absl::bind_front(
           &CheckApproximateDistanceFnJsonArguments, "APPROX_DOT_PRODUCT"))},
      {types::DoubleType(),
       {types::FloatArrayType(), types::FloatArrayType()},
       FN_APPROX_DOT_PRODUCT_FLOAT},
      {types::DoubleType(),
       {types::FloatArrayType(), types::FloatArrayType(), json_options_arg},
       FN_APPROX_DOT_PRODUCT_FLOAT_WITH_JSON_OPTIONS,
       FunctionSignatureOptions().set_constraints(absl::bind_front(
           &CheckApproximateDistanceFnJsonArguments, "APPROX_DOT_PRODUCT"))},
      {types::DoubleType(),
       {types::DoubleArrayType(), types::DoubleArrayType()},
       FN_APPROX_DOT_PRODUCT_DOUBLE},
      {types::DoubleType(),
       {types::DoubleArrayType(), types::DoubleArrayType(), json_options_arg},
       FN_APPROX_DOT_PRODUCT_DOUBLE_WITH_JSON_OPTIONS,
       FunctionSignatureOptions().set_constraints(absl::bind_front(
           &CheckApproximateDistanceFnJsonArguments, "APPROX_DOT_PRODUCT"))}};

  GOOGLESQL_RETURN_IF_ERROR(MaybeAddApproximateDistanceFunctionProtoSignature(
      options, "APPROX_DOT_PRODUCT",
      FN_APPROX_DOT_PRODUCT_INT64_WITH_PROTO_OPTIONS, kOptionsArgIdx,
      types::Int64ArrayType(), approx_dot_product_signatures,
      output_properties));
  GOOGLESQL_RETURN_IF_ERROR(MaybeAddApproximateDistanceFunctionProtoSignature(
      options, "APPROX_DOT_PRODUCT",
      FN_APPROX_DOT_PRODUCT_FLOAT_WITH_PROTO_OPTIONS, kOptionsArgIdx,
      types::FloatArrayType(), approx_dot_product_signatures,
      output_properties));
  GOOGLESQL_RETURN_IF_ERROR(MaybeAddApproximateDistanceFunctionProtoSignature(
      options, "APPROX_DOT_PRODUCT",
      FN_APPROX_DOT_PRODUCT_DOUBLE_WITH_PROTO_OPTIONS, kOptionsArgIdx,
      types::DoubleArrayType(), approx_dot_product_signatures,
      output_properties));

  InsertFunction(functions, options, "approx_dot_product", Function::SCALAR,
                 approx_dot_product_signatures, /*function_options=*/{});

  // Use a Rewriter for MANHATTAN_DISTANCE.
  std::string manhattan_distance_sql =
      distance_fn_rewrite_sql("MANHATTAN_DISTANCE", R"sql(
      (SELECT
            SUM(ABS(
              CAST(e1 AS FLOAT64) -
              CAST(input_array_2[OFFSET(index)] AS FLOAT64)))
          FROM UNNEST(input_array_1) AS e1 WITH OFFSET index
    )sql");

  FunctionSignatureOptions manhattan_distance_signature_options =
      SetDefinitionForInlining(manhattan_distance_sql, true)
          .AddRequiredLanguageFeature(FEATURE_MANHATTAN_DISTANCE);

  std::vector<FunctionSignatureOnHeap> manhattan_distance_signatures = {
      {types::DoubleType(),
       {distance_fn_named_arg(types::Int64ArrayType(), "input_array_1"),
        distance_fn_named_arg(types::Int64ArrayType(), "input_array_2")},
       FN_MANHATTAN_DISTANCE_INT64,
       manhattan_distance_signature_options},
      {types::DoubleType(),
       {distance_fn_named_arg(types::FloatArrayType(), "input_array_1"),
        distance_fn_named_arg(types::FloatArrayType(), "input_array_2")},
       FN_MANHATTAN_DISTANCE_FLOAT,
       manhattan_distance_signature_options},
      {types::DoubleType(),
       {distance_fn_named_arg(types::DoubleArrayType(), "input_array_1"),
        distance_fn_named_arg(types::DoubleArrayType(), "input_array_2")},
       FN_MANHATTAN_DISTANCE_DOUBLE,
       manhattan_distance_signature_options}};

  InsertFunction(functions, options, "manhattan_distance", Function::SCALAR,
                 manhattan_distance_signatures, function_options);

  // Lambda with argument-checking SQL common to norm function rewriters.
  auto norm_fn_rewrite_sql = [&null_element_err_msg_base](
                                 absl::string_view name,
                                 absl::string_view sql) {
    return absl::Substitute(R"sql(
      CASE
        WHEN input_array IS NULL
          THEN NULL
        WHEN ARRAY_LENGTH(input_array) = 0
          THEN CAST(0 AS FLOAT64)
        ELSE
          $0
          WHERE
            IF(e IS NULL, ERROR(FORMAT(
              "%s The NULL element was found in the array argument at OFFSET %d",
              "$1", index)), TRUE))
        END
    )sql",
                            sql, null_element_err_msg_base(name));
  };

  // Use a Rewriter for L1_NORM.
  std::string l1_norm_sql = norm_fn_rewrite_sql("L1_NORM", R"sql(
      (SELECT SUM(ABS(CAST(e AS FLOAT64)))
       FROM UNNEST(input_array) AS e WITH OFFSET index
    )sql");

  FunctionSignatureOptions l1_norm_signature_options =
      SetDefinitionForInlining(l1_norm_sql, true)
          .AddRequiredLanguageFeature(FEATURE_L1_NORM);

  std::vector<FunctionSignatureOnHeap> l1_norm_signatures = {
      {types::DoubleType(),
       {distance_fn_named_arg(types::Int64ArrayType(), "input_array")},
       FN_L1_NORM_INT64,
       l1_norm_signature_options},
      {types::DoubleType(),
       {distance_fn_named_arg(types::FloatArrayType(), "input_array")},
       FN_L1_NORM_FLOAT,
       l1_norm_signature_options},
      {types::DoubleType(),
       {distance_fn_named_arg(types::DoubleArrayType(), "input_array")},
       FN_L1_NORM_DOUBLE,
       l1_norm_signature_options}};

  InsertFunction(functions, options, "l1_norm", Function::SCALAR,
                 l1_norm_signatures, function_options);

  // Use a Rewriter for L2_NORM.
  std::string l2_norm_sql = norm_fn_rewrite_sql("L2_NORM", R"sql(
      (SELECT SQRT(SUM(CAST(e AS FLOAT64) * CAST(e AS FLOAT64)))
       FROM UNNEST(input_array) AS e WITH OFFSET index
    )sql");

  FunctionSignatureOptions l2_norm_signature_options =
      SetDefinitionForInlining(l2_norm_sql, true)
          .AddRequiredLanguageFeature(FEATURE_L2_NORM);

  std::vector<FunctionSignatureOnHeap> l2_norm_signatures = {
      {types::DoubleType(),
       {distance_fn_named_arg(types::Int64ArrayType(), "input_array")},
       FN_L2_NORM_INT64,
       l2_norm_signature_options},
      {types::DoubleType(),
       {distance_fn_named_arg(types::FloatArrayType(), "input_array")},
       FN_L2_NORM_FLOAT,
       l2_norm_signature_options},
      {types::DoubleType(),
       {distance_fn_named_arg(types::DoubleArrayType(), "input_array")},
       FN_L2_NORM_DOUBLE,
       l2_norm_signature_options}};

  InsertFunction(functions, options, "l2_norm", Function::SCALAR,
                 l2_norm_signatures, function_options);

  return absl::OkStatus();
}

}  // namespace googlesql
