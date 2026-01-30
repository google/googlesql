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

#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

#include "google/protobuf/timestamp.pb.h"
#include "google/protobuf/wrappers.pb.h"
#include "google/type/date.pb.h"
#include "google/type/timeofday.pb.h"
#include "googlesql/common/builtin_function_internal.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/functions/rank_type.pb.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "absl/base/no_destructor.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {

void GetHllCountFunctions(TypeFactory* type_factory,
                          const GoogleSQLBuiltinFunctionOptions& options,
                          NameToFunctionMap* functions) {
  const Type* bytes_type = type_factory->get_bytes();
  const Type* int64_type = type_factory->get_int64();
  const Type* uint64_type = type_factory->get_uint64();
  const Type* string_type = type_factory->get_string();
  const Type* numeric_type = type_factory->get_numeric();
  const Type* bignumeric_type = type_factory->get_bignumeric();

  constexpr Function::Mode AGGREGATE = Function::AGGREGATE;
  constexpr Function::Mode SCALAR = Function::SCALAR;
  constexpr FunctionArgumentType::ArgumentCardinality OPTIONAL =
      FunctionArgumentType::OPTIONAL;

  FunctionSignatureOptions has_numeric_type_argument;
  has_numeric_type_argument.set_constraints(&CheckHasNumericTypeArgument);

  FunctionSignatureOptions has_bignumeric_type_argument;
  has_bignumeric_type_argument.set_constraints(&CheckHasBigNumericTypeArgument);

  // The second argument must be an integer literal between 10 and 24,
  // and cannot be NULL.
  FunctionArgumentTypeOptions hll_init_arg;
  hll_init_arg.set_is_not_aggregate();
  hll_init_arg.set_must_be_non_null();
  hll_init_arg.set_cardinality(OPTIONAL);
  hll_init_arg.set_min_value(10);
  hll_init_arg.set_max_value(24);

  InsertSimpleNamespaceFunction(
      functions, options, "hll_count", "merge", AGGREGATE,
      {{int64_type, {bytes_type}, FN_HLL_COUNT_MERGE}},
      DefaultAggregateFunctionOptions());
  InsertSimpleNamespaceFunction(
      functions, options, "hll_count", "extract", SCALAR,
      {{int64_type, {bytes_type}, FN_HLL_COUNT_EXTRACT}});
  InsertNamespaceFunction(
      functions, options, "hll_count", "init", AGGREGATE,
      {{bytes_type,
        {int64_type, {int64_type, hll_init_arg}},
        FN_HLL_COUNT_INIT_INT64},
       {bytes_type,
        {uint64_type, {int64_type, hll_init_arg}},
        FN_HLL_COUNT_INIT_UINT64},
       {bytes_type,
        {numeric_type, {int64_type, hll_init_arg}},
        FN_HLL_COUNT_INIT_NUMERIC,
        has_numeric_type_argument},
       {bytes_type,
        {bignumeric_type, {int64_type, hll_init_arg}},
        FN_HLL_COUNT_INIT_BIGNUMERIC,
        has_bignumeric_type_argument},
       {bytes_type,
        {string_type, {int64_type, hll_init_arg}},
        FN_HLL_COUNT_INIT_STRING,
        FunctionSignatureOptions().set_uses_operation_collation()},
       {bytes_type,
        {bytes_type, {int64_type, hll_init_arg}},
        FN_HLL_COUNT_INIT_BYTES}},
      DefaultAggregateFunctionOptions());
  InsertSimpleNamespaceFunction(
      functions, options, "hll_count", "merge_partial", AGGREGATE,
      {{bytes_type, {bytes_type}, FN_HLL_COUNT_MERGE_PARTIAL}},
      DefaultAggregateFunctionOptions());
}

void GetKllQuantilesFunctions(TypeFactory* type_factory,
                              const GoogleSQLBuiltinFunctionOptions& options,
                              NameToFunctionMap* functions) {
  const Type* int64_type = type_factory->get_int64();
  const Type* uint64_type = type_factory->get_uint64();
  const Type* double_type = type_factory->get_double();
  const Type* bytes_type = type_factory->get_bytes();
  const Type* int64_array_type = types::Int64ArrayType();
  const Type* uint64_array_type = types::Uint64ArrayType();
  const Type* double_array_type = types::DoubleArrayType();

  constexpr Function::Mode AGGREGATE = Function::AGGREGATE;
  constexpr Function::Mode SCALAR = Function::SCALAR;
  constexpr FunctionArgumentType::ArgumentCardinality OPTIONAL =
      FunctionArgumentType::OPTIONAL;

  // By default, all built-in aggregate functions can be used as analytic
  // functions, and all the KllQuantilesFunctions do not allow external usage.
  FunctionOptions
      aggregate_analytic_function_options_and_not_allow_external_usage =
          DefaultAggregateFunctionOptions().set_allow_external_usage(false);

  const FunctionOptions scalar_function_options_disallow_external_usage =
      FunctionOptions().set_allow_external_usage(false);

  // The optional second argument of 'init', the approximation precision or
  // inverse epsilon ('inv_eps'), must be an integer >= 1 and cannot be NULL.
  FunctionArgumentTypeOptions init_inv_eps_arg;
  init_inv_eps_arg.set_is_not_aggregate();
  init_inv_eps_arg.set_must_be_non_null();
  init_inv_eps_arg.set_cardinality(OPTIONAL);

  bool use_float64_name = options.language_options.LanguageFeatureEnabled(
      FEATURE_KLL_FLOAT64_PRIMARY_WITH_DOUBLE_ALIAS);
  auto make_function_name = [use_float64_name](absl::string_view name_prefix) {
    return absl::StrCat(name_prefix, use_float64_name ? "_float64" : "_double");
  };
  bool is_internal_dialect =
      options.language_options.product_mode() == PRODUCT_INTERNAL;
  bool add_double_alias = use_float64_name && is_internal_dialect;
  // Only enable external usage when float64 is the primary function name.
  bool allow_external_usage = use_float64_name;
  auto prepare_function_options = [add_double_alias, allow_external_usage](
                                      FunctionOptions options,
                                      absl::string_view name_prefix) {
    if (add_double_alias) {
      options.set_alias_name(absl::StrCat(name_prefix, "_double"));
    }

    if (allow_external_usage) {
      options.set_allow_external_usage(true);
    }
    return options;
  };

  // Init functions include a weight parameter only if NAMED_ARGUMENTS
  // enabled.
  if (options.language_options.LanguageFeatureEnabled(
          googlesql::FEATURE_KLL_WEIGHTS)) {
    // Explicitly set default value for precision (detailed in (broken link))
    init_inv_eps_arg.set_default(Value::Int64(1000));

    // There is an additional optional argument for input weights.
    FunctionArgumentTypeOptions init_weights_arg;
    init_weights_arg.set_cardinality(OPTIONAL);
    init_weights_arg.set_argument_name("weight", kNamedOnly);
    init_weights_arg.set_default(Value::Int64(1));

    // Init functions with weight parameter
    InsertNamespaceFunction(functions, options, "kll_quantiles", "init_int64",
                            AGGREGATE,
                            {{bytes_type,
                              {int64_type,
                               {int64_type, init_inv_eps_arg},
                               {int64_type, init_weights_arg}},
                              FN_KLL_QUANTILES_INIT_INT64}},
                            DefaultAggregateFunctionOptions());
    InsertNamespaceFunction(
        functions, options, "kll_quantiles", "init_uint64", AGGREGATE,
        {{bytes_type,
          {uint64_type,
           {int64_type, init_inv_eps_arg},
           {int64_type, init_weights_arg}},
          FN_KLL_QUANTILES_INIT_UINT64}},
        aggregate_analytic_function_options_and_not_allow_external_usage);
    InsertNamespaceFunction(
        functions, options, "kll_quantiles", make_function_name("init"),
        AGGREGATE,
        {{bytes_type,
          {double_type,
           {int64_type, init_inv_eps_arg},
           {int64_type, init_weights_arg}},
          FN_KLL_QUANTILES_INIT_DOUBLE}},
        prepare_function_options(
            aggregate_analytic_function_options_and_not_allow_external_usage,
            "init"));
  } else {
    // init functions with no weight parameter
    InsertNamespaceFunction(functions, options, "kll_quantiles", "init_int64",
                            AGGREGATE,
                            {{bytes_type,
                              {int64_type, {int64_type, init_inv_eps_arg}},
                              FN_KLL_QUANTILES_INIT_INT64}},
                            DefaultAggregateFunctionOptions());
    InsertNamespaceFunction(
        functions, options, "kll_quantiles", "init_uint64", AGGREGATE,
        {{bytes_type,
          {uint64_type, {int64_type, init_inv_eps_arg}},
          FN_KLL_QUANTILES_INIT_UINT64}},
        aggregate_analytic_function_options_and_not_allow_external_usage);
    InsertNamespaceFunction(
        functions, options, "kll_quantiles", make_function_name("init"),
        AGGREGATE,
        {{bytes_type,
          {double_type, {int64_type, init_inv_eps_arg}},
          FN_KLL_QUANTILES_INIT_DOUBLE}},
        prepare_function_options(
            aggregate_analytic_function_options_and_not_allow_external_usage,
            "init"));
  }

  // Merge_partial
  InsertSimpleNamespaceFunction(
      functions, options, "kll_quantiles", "merge_partial", AGGREGATE,
      {{bytes_type, {bytes_type}, FN_KLL_QUANTILES_MERGE_PARTIAL}},
      DefaultAggregateFunctionOptions());

  // The second argument of aggregate function 'merge', the number of
  // equidistant quantiles that should be returned; must be a non-aggregate
  //  integer >= 1 and cannot be NULL.
  FunctionArgumentTypeOptions num_quantiles_merge_arg;
  num_quantiles_merge_arg.set_is_not_aggregate();
  num_quantiles_merge_arg.set_must_be_non_null();
  num_quantiles_merge_arg.set_min_value(1);

  // Merge
  InsertNamespaceFunction(
      functions, options, "kll_quantiles", "merge_int64", AGGREGATE,
      {{int64_array_type,
        // TODO: b/219883981 - Add support for interpolation option for all
        // merge/ extract/merge_point/extract_point functions if the feature
        // gets prioritized.
        {bytes_type, {int64_type, num_quantiles_merge_arg}},
        FN_KLL_QUANTILES_MERGE_INT64}},
      DefaultAggregateFunctionOptions());
  InsertNamespaceFunction(
      functions, options, "kll_quantiles", "merge_uint64", AGGREGATE,
      {{uint64_array_type,
        {bytes_type, {int64_type, num_quantiles_merge_arg}},
        FN_KLL_QUANTILES_MERGE_UINT64}},
      aggregate_analytic_function_options_and_not_allow_external_usage);
  InsertNamespaceFunction(
      functions, options, "kll_quantiles", make_function_name("merge"),
      AGGREGATE,
      {{double_array_type,
        {bytes_type, {int64_type, num_quantiles_merge_arg}},
        FN_KLL_QUANTILES_MERGE_DOUBLE}},
      prepare_function_options(
          aggregate_analytic_function_options_and_not_allow_external_usage,
          "merge"));

  // The second argument of scalar function 'extract', the number of
  // equidistant quantiles that should be returned; must be an integer >= 1 and
  // cannot be NULL.
  FunctionArgumentTypeOptions num_quantiles_extract_arg;
  num_quantiles_extract_arg.set_must_be_non_null();
  num_quantiles_extract_arg.set_min_value(1);

  // Extract
  InsertNamespaceFunction(
      functions, options, "kll_quantiles", "extract_int64", SCALAR,
      {{int64_array_type,
        {bytes_type, {int64_type, num_quantiles_extract_arg}},
        FN_KLL_QUANTILES_EXTRACT_INT64}},
      FunctionOptions());
  InsertNamespaceFunction(
      functions, options, "kll_quantiles", "extract_uint64", SCALAR,
      {{uint64_array_type,
        {bytes_type, {int64_type, num_quantiles_extract_arg}},
        FN_KLL_QUANTILES_EXTRACT_UINT64}},
      scalar_function_options_disallow_external_usage);
  InsertNamespaceFunction(
      functions, options, "kll_quantiles", make_function_name("extract"),
      SCALAR,
      {{double_array_type,
        {bytes_type, {int64_type, num_quantiles_extract_arg}},
        FN_KLL_QUANTILES_EXTRACT_DOUBLE}},
      prepare_function_options(scalar_function_options_disallow_external_usage,
                               "extract"));

  // The second argument of aggregate function 'merge_point', phi, must be a
  // non-aggregate double in [0, 1] and cannot be null.
  FunctionArgumentTypeOptions phi_merge_arg;
  phi_merge_arg.set_is_not_aggregate();
  phi_merge_arg.set_must_be_non_null();
  phi_merge_arg.set_min_value(0);
  phi_merge_arg.set_max_value(1);

  // Merge_point
  InsertNamespaceFunction(functions, options, "kll_quantiles",
                          "merge_point_int64", AGGREGATE,
                          {{int64_type,
                            {bytes_type, {double_type, phi_merge_arg}},
                            FN_KLL_QUANTILES_MERGE_POINT_INT64}},
                          DefaultAggregateFunctionOptions());
  InsertNamespaceFunction(
      functions, options, "kll_quantiles", "merge_point_uint64", AGGREGATE,
      {{uint64_type,
        {bytes_type, {double_type, phi_merge_arg}},
        FN_KLL_QUANTILES_MERGE_POINT_UINT64}},
      aggregate_analytic_function_options_and_not_allow_external_usage);
  InsertNamespaceFunction(
      functions, options, "kll_quantiles", make_function_name("merge_point"),
      AGGREGATE,
      {{double_type,
        {bytes_type, {double_type, phi_merge_arg}},
        FN_KLL_QUANTILES_MERGE_POINT_DOUBLE}},
      prepare_function_options(
          aggregate_analytic_function_options_and_not_allow_external_usage,
          "merge_point"));

  // The second argument of scalar function 'extract_point', phi, must be a
  // double in [0, 1] and cannot be null.
  FunctionArgumentTypeOptions phi_extract_arg;
  phi_extract_arg.set_must_be_non_null();
  phi_extract_arg.set_min_value(0);
  phi_extract_arg.set_max_value(1);

  // Extract_point
  InsertNamespaceFunction(functions, options, "kll_quantiles",
                          "extract_point_int64", SCALAR,
                          {{int64_type,
                            {bytes_type, {double_type, phi_extract_arg}},
                            FN_KLL_QUANTILES_EXTRACT_POINT_INT64}},
                          FunctionOptions());
  InsertNamespaceFunction(functions, options, "kll_quantiles",
                          "extract_point_uint64", SCALAR,
                          {{uint64_type,
                            {bytes_type, {double_type, phi_extract_arg}},
                            FN_KLL_QUANTILES_EXTRACT_POINT_UINT64}},
                          scalar_function_options_disallow_external_usage);
  InsertNamespaceFunction(
      functions, options, "kll_quantiles", make_function_name("extract_point"),
      SCALAR,
      {{double_type,
        {bytes_type, {double_type, phi_extract_arg}},
        FN_KLL_QUANTILES_EXTRACT_POINT_DOUBLE}},
      prepare_function_options(scalar_function_options_disallow_external_usage,
                               "extract_point"));

  if (options.language_options.LanguageFeatureEnabled(
          FEATURE_KLL_QUANTILES_EXTRACT_RELATIVE_RANK)) {
    // Merge_relative_rank
    FunctionArgumentTypeOptions value_arg_option(
        FunctionArgumentType::REQUIRED);
    value_arg_option.set_argument_name("value", kNamedOnly);
    auto rank_type = types::RankTypeEnumType();
    if (rank_type.ok()) {
      FunctionArgumentType rank_type_arg(
          rank_type.value(),
          FunctionArgumentTypeOptions(FunctionArgumentType::OPTIONAL)
              .set_default(Value::Enum(
                  rank_type.value()->AsEnum(),
                  static_cast<int>(
                      googlesql::functions::RankTypeEnums::FRACTION_LESS_THAN)))
              .set_argument_name("rank_type", kNamedOnly));

      InsertNamespaceFunction(
          functions, options, "kll_quantiles", "merge_relative_rank_int64",
          AGGREGATE,
          {{double_type,
            {bytes_type, {int64_type, value_arg_option}, rank_type_arg},
            FN_KLL_QUANTILES_MERGE_RELATIVE_RANK_INT64}},
          DefaultAggregateFunctionOptions());
      InsertNamespaceFunction(
          functions, options, "kll_quantiles", "merge_relative_rank_uint64",
          AGGREGATE,
          {{double_type,
            {bytes_type, {uint64_type, value_arg_option}, rank_type_arg},
            FN_KLL_QUANTILES_MERGE_RELATIVE_RANK_UINT64}},
          aggregate_analytic_function_options_and_not_allow_external_usage);
      // Insert function for double type. This function inserts
      // "merge_relative_rank_double" or "merge_relative_rank_float64" based on
      // flag FEATURE_KLL_FLOAT64_PRIMARY_WITH_DOUBLE_ALIAS
      InsertNamespaceFunction(
          functions, options, "kll_quantiles",
          make_function_name("merge_relative_rank"), AGGREGATE,
          {{double_type,
            {bytes_type, {double_type, value_arg_option}, rank_type_arg},
            FN_KLL_QUANTILES_MERGE_RELATIVE_RANK_DOUBLE}},
          prepare_function_options(
              aggregate_analytic_function_options_and_not_allow_external_usage,
              "merge_relative_rank"));

      // Extract_relative_rank
      InsertNamespaceFunction(
          functions, options, "kll_quantiles", "extract_relative_rank_int64",
          SCALAR,
          {{double_type,
            {bytes_type, {int64_type, value_arg_option}, rank_type_arg},
            FN_KLL_QUANTILES_EXTRACT_RELATIVE_RANK_INT64}},
          FunctionOptions());
      InsertNamespaceFunction(
          functions, options, "kll_quantiles", "extract_relative_rank_uint64",
          SCALAR,
          {{double_type,
            {bytes_type, {uint64_type, value_arg_option}, rank_type_arg},
            FN_KLL_QUANTILES_EXTRACT_RELATIVE_RANK_UINT64}},
          scalar_function_options_disallow_external_usage);
      // Insert function for double type. This function inserts
      // "extract_relative_rank_double" or "extract_relative_rank_float64" based
      // on flag FEATURE_KLL_FLOAT64_PRIMARY_WITH_DOUBLE_ALIAS
      InsertNamespaceFunction(
          functions, options, "kll_quantiles",
          make_function_name("extract_relative_rank"), SCALAR,
          {{double_type,
            {bytes_type, {double_type, value_arg_option}, rank_type_arg},
            FN_KLL_QUANTILES_EXTRACT_RELATIVE_RANK_DOUBLE}},
          prepare_function_options(
              scalar_function_options_disallow_external_usage,
              "extract_relative_rank"));
    } else {
      ABSL_LOG(ERROR) << "Failed to get rank type enum type: " << rank_type.status();
    }
  }
}

}  // namespace googlesql
