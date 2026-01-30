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

#include "googlesql/reference_impl/algebrizer.h"

#include <algorithm>
#include <bitset>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <stack>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/base/arena.h"
#include "googlesql/base/atomic_sequence_num.h"
#include "googlesql/base/logging.h"
#include "googlesql/analyzer/expr_resolver_helper.h"
#include "googlesql/common/aggregate_null_handling.h"
#include "googlesql/public/annotation/collation.h"
#include "googlesql/public/constant_evaluator.h"
#include "googlesql/public/sql_constant.h"
#include "googlesql/public/types/measure_type.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/rewrite_utils.h"
#include "googlesql/common/measure_utils.h"
#include "googlesql/common/thread_stack.h"
#include "googlesql/public/anonymization_utils.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/cast.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/collator.h"
#include "googlesql/public/evaluator_table_iterator.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/functions/array_zip_mode.pb.h"
#include "googlesql/public/functions/differential_privacy.pb.h"
#include "googlesql/public/functions/json.h"
#include "googlesql/public/functions/match_recognize/compiled_pattern.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/proto_util.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/sql_function.h"
#include "googlesql/public/sql_tvf.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/templated_sql_function.h"
#include "googlesql/public/templated_sql_tvf.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/reference_impl/common.h"
#include "googlesql/reference_impl/evaluation.h"
#include "googlesql/reference_impl/function.h"
#include "googlesql/reference_impl/operator.h"
#include "googlesql/reference_impl/parameters.h"
#include "googlesql/reference_impl/proto_util.h"
#include "googlesql/reference_impl/tuple.h"
#include "googlesql/reference_impl/tvf_evaluation.h"
#include "googlesql/reference_impl/type_helpers.h"
#include "googlesql/reference_impl/uda_evaluation.h"
#include "googlesql/reference_impl/variable_generator.h"
#include "googlesql/reference_impl/variable_id.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "googlesql/resolved_ast/resolved_ast_visitor.h"
#include "googlesql/resolved_ast/resolved_collation.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/resolved_ast/serialization.pb.h"
#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/base/no_destructor.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"

using googlesql::types::BoolType;

namespace googlesql {

#define RETURN_ERROR_IF_OUT_OF_STACK_SPACE()                                   \
  GOOGLESQL_RETURN_IF_NOT_ENOUGH_STACK(                                        \
      "Out of stack space due to deeply nested query expression during query " \
      "algebrization")

namespace {

constexpr FunctionSignatureId kCollationSupportedAnalyticFunctions[] = {
    FN_COUNT,
    FN_MIN,
    FN_MAX,
    FN_PERCENTILE_DISC,
    FN_PERCENTILE_DISC_NUMERIC,
    FN_PERCENTILE_DISC_BIGNUMERIC};

absl::Status CheckHints(
    absl::Span<const std::unique_ptr<const ResolvedOption>> hint_list) {
  for (const auto& hint : hint_list) {
    // Ignore all hints meant for a specific different engine.
    if (!hint->qualifier().empty() && hint->qualifier() != "reference_impl") {
      continue;
    }
    // All hints are currently unsupported in the reference_impl, so we
    // always give an error.
    return ::googlesql_base::InvalidArgumentErrorBuilder()
           << "Unsupported hint: " << hint->qualifier()
           << (hint->qualifier().empty() ? "" : ".") << hint->name();
  }
  return absl::OkStatus();
}

std::unique_ptr<ExtendedCompositeCastEvaluator>
GetExtendedCastEvaluatorFromResolvedCast(const ResolvedCast* cast) {
  if (cast->extended_cast() != nullptr) {
    std::vector<ConversionEvaluator> evaluators;
    for (const auto& extended_conversion :
         cast->extended_cast()->element_list()) {
      evaluators.push_back(
          ConversionEvaluator::Create(extended_conversion->from_type(),
                                      extended_conversion->to_type(),
                                      extended_conversion->function())
              .value());
    }

    return std::make_unique<ExtendedCompositeCastEvaluator>(
        std::move(evaluators));
  }

  return {};
}

bool IsAnalyticFunctionCollationSupported(const FunctionSignature& signature) {
  return std::find(std::begin(kCollationSupportedAnalyticFunctions),
                   std::end(kCollationSupportedAnalyticFunctions),
                   signature.context_id()) !=
         std::end(kCollationSupportedAnalyticFunctions);
}

// Converts ResolvedGroupingSet to an int64 grouping set id.
absl::StatusOr<int64_t> ConvertGroupingSetToGroupingSetId(
    const ResolvedGroupingSet* node,
    const absl::flat_hash_map<std::string, int>& key_to_index_map) {
  int64_t grouping_set = 0;
  for (const std::unique_ptr<const ResolvedColumnRef>& column_ref :
       node->group_by_column_list()) {
    const int* key_index =
        googlesql_base::FindOrNull(key_to_index_map, column_ref->column().name());
    GOOGLESQL_RET_CHECK_NE(key_index, nullptr);
    grouping_set |= (1ll << *key_index);
  }
  return grouping_set;
}

// Converts ResolvedCube to a list of int64 grouping set ids.
absl::StatusOr<std::vector<int64_t>> ConvertCubeToGroupingSetIds(
    const ResolvedCube* cube,
    const absl::flat_hash_map<std::string, int>& key_to_index_map) {
  std::vector<int64_t> grouping_sets;

  int cube_size = cube->cube_column_list_size();
  // This is the hard limit to avoid bitset overflow, the actual maximum number
  // of grouping sets allowed is specified by AggregateOp::kMaxGroupingSet.
  if (cube_size > 31) {
    return absl::InvalidArgumentError(
        "Cube can not have more than 31 elements");
  }
  // CUBE with n columns generates 2^N grouping sets
  uint32_t expanded_grouping_set_size = (1u << cube_size);
  grouping_sets.reserve((expanded_grouping_set_size));
  // Though expanded_grouping_set_size is an uint32_t, but the check above makes
  // sure uint32_t is smaller than or equal to 2^31, in which case,
  // expanded_grouping_set_size - 1 is still in the range of int.
  for (int i = expanded_grouping_set_size - 1; i >= 0; --i) {
    int64_t current_grouping_set = 0;
    std::bitset<32> grouping_set_bit(i);
    for (int column_index = 0; column_index < cube_size; ++column_index) {
      if (!grouping_set_bit.test(column_index)) {
        continue;
      }
      for (const std::unique_ptr<const ResolvedColumnRef>& column_ref :
           cube->cube_column_list(column_index)->column_list()) {
        const int* key_index =
            googlesql_base::FindOrNull(key_to_index_map, column_ref->column().name());
        GOOGLESQL_RET_CHECK_NE(key_index, nullptr);
        current_grouping_set |= (1ll << *key_index);
      }
    }
    grouping_sets.push_back(current_grouping_set);
  }
  return grouping_sets;
}

// Converts ResolvedRollup to a list of int64 grouping set ids.
absl::StatusOr<std::vector<int64_t>> ConvertRollupToGroupingSetIds(
    const ResolvedRollup* rollup,
    const absl::flat_hash_map<std::string, int>& key_to_index_map) {
  std::vector<int64_t> grouping_sets;
  // ROLLUP with n columns generates n+1 grouping sets
  grouping_sets.reserve(rollup->rollup_column_list_size() + 1);
  // Add the empty grouping set to the grouping sets list.
  grouping_sets.push_back(0);
  int64_t current_grouping_set = 0;
  for (const std::unique_ptr<const ResolvedGroupingSetMultiColumn>&
           multi_column : rollup->rollup_column_list()) {
    for (const std::unique_ptr<const ResolvedColumnRef>& column_ref :
         multi_column->column_list()) {
      const int* key_index =
          googlesql_base::FindOrNull(key_to_index_map, column_ref->column().name());
      GOOGLESQL_RET_CHECK_NE(key_index, nullptr);
      current_grouping_set |= (1ll << *key_index);
    }
    grouping_sets.push_back(current_grouping_set);
  }
  // Prefer to output the grouping sets from more to less granular levels of
  // subtotals, e.g. (a, b, c), (a, b), (a), and then ().
  std::reverse(grouping_sets.begin(), grouping_sets.end());
  return grouping_sets;
}

absl::Status GroupingSetIdsCartesianProduct(
    const std::vector<std::vector<int64_t>>& grouping_set_ids_multiplier,
    int current_multiplier_index, int64_t product_grouping_set_id,
    std::vector<int64_t>& output_grouping_set_ids) {
  if (current_multiplier_index >= grouping_set_ids_multiplier.size()) {
    output_grouping_set_ids.push_back(product_grouping_set_id);
    return absl::OkStatus();
  }
  for (int64_t current_grouping_set_id :
       grouping_set_ids_multiplier[current_multiplier_index]) {
    GOOGLESQL_RETURN_IF_ERROR(GroupingSetIdsCartesianProduct(
        grouping_set_ids_multiplier, current_multiplier_index + 1,
        product_grouping_set_id | current_grouping_set_id,
        output_grouping_set_ids));
  }
  return absl::OkStatus();
}

// Converts ResolvedGroupingSet to a list of int64 grouping set ids.
absl::StatusOr<std::vector<int64_t>> ConvertGroupingSetToGroupingSetIds(
    const ResolvedGroupingSetBase* grouping_set_base,
    const absl::flat_hash_map<std::string, int>& key_to_index_map,
    const int64_t max_grouping_sets) {
  if (grouping_set_base->Is<ResolvedGroupingSet>()) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        int64_t grouping_set_id,
        ConvertGroupingSetToGroupingSetId(
            grouping_set_base->GetAs<ResolvedGroupingSet>(), key_to_index_map));
    return std::vector<int64_t>({grouping_set_id});
  } else if (grouping_set_base->Is<ResolvedCube>()) {
    return ConvertCubeToGroupingSetIds(grouping_set_base->GetAs<ResolvedCube>(),
                                       key_to_index_map);
  } else if (grouping_set_base->Is<ResolvedRollup>()) {
    return ConvertRollupToGroupingSetIds(
        grouping_set_base->GetAs<ResolvedRollup>(), key_to_index_map);
  } else if (grouping_set_base->Is<ResolvedGroupingSetList>()) {
    std::vector<int64_t> grouping_set_ids_list;
    for (const std::unique_ptr<const ResolvedGroupingSetBase>& grouping_set :
         grouping_set_base->GetAs<ResolvedGroupingSetList>()->elem_list()) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::vector<int64_t> grouping_set_ids,
          ConvertGroupingSetToGroupingSetIds(
              grouping_set.get(), key_to_index_map, max_grouping_sets));
      absl::c_move(grouping_set_ids, std::back_inserter(grouping_set_ids_list));
    }
    if (grouping_set_ids_list.size() > max_grouping_sets) {
      return absl::InvalidArgumentError(
          absl::StrFormat("When expanding GroupingSetList, too many "
                          "grouping sets, at most %d grouping sets are allowed",
                          max_grouping_sets));
    }
    return grouping_set_ids_list;
  }
  // This is a ResolvedGroupingSetProduct node.
  GOOGLESQL_RET_CHECK(grouping_set_base->Is<ResolvedGroupingSetProduct>())
      << "Unsupported grouping set base type: "
      << grouping_set_base->DebugString();
  size_t total_grouping_sets = 1;
  std::vector<std::vector<int64_t>> grouping_set_ids_multiplier;
  for (const std::unique_ptr<const ResolvedGroupingSetBase>& grouping_set :
       grouping_set_base->GetAs<ResolvedGroupingSetProduct>()->input_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::vector<int64_t> grouping_set_ids,
        ConvertGroupingSetToGroupingSetIds(grouping_set.get(), key_to_index_map,
                                           max_grouping_sets));
    grouping_set_ids_multiplier.push_back(grouping_set_ids);

    if (total_grouping_sets > max_grouping_sets / grouping_set_ids.size()) {
      return absl::InvalidArgumentError(
          absl::StrFormat("When expanding GroupingSetProduct, too many "
                          "grouping sets, at most %d grouping sets are allowed",
                          max_grouping_sets));
    }
    total_grouping_sets *= grouping_set_ids.size();
  }

  // Do cartesian product of the grouping set ids.
  std::vector<int64_t> grouping_sets;
  int64_t tmp_grouping_set_id = 0;
  GOOGLESQL_RETURN_IF_ERROR(GroupingSetIdsCartesianProduct(
      grouping_set_ids_multiplier, 0, tmp_grouping_set_id, grouping_sets));
  return grouping_sets;
}

}  // namespace

Algebrizer::Algebrizer(const LanguageOptions& language_options,
                       const AlgebrizerOptions& algebrizer_options,
                       TypeFactory* type_factory, Parameters* parameters,
                       ParameterMap* column_map,
                       SystemVariablesAlgebrizerMap* system_variables_map)
    : language_options_(language_options),
      algebrizer_options_(algebrizer_options),
      column_to_variable_(std::make_unique<ColumnToVariableMapping>(
          std::make_unique<VariableGenerator>())),
      variable_gen_(column_to_variable_->variable_generator()),
      parameters_(parameters),
      column_map_(column_map),
      system_variables_map_(system_variables_map),
      type_factory_(type_factory) {}

absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeCast(
    const ResolvedCast* cast) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> arg,
                   AlgebrizeExpression(cast->expr()));
  std::unique_ptr<ValueExpr> format;
  if (cast->format() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(format, AlgebrizeExpression(cast->format()));
  }
  std::unique_ptr<ValueExpr> time_zone;
  if (cast->time_zone()) {
    GOOGLESQL_ASSIGN_OR_RETURN(time_zone, AlgebrizeExpression(cast->time_zone()));
  }

  // For extended conversions extended_cast will contain a conversion function
  // that implements a conversion. Extended conversions in reference
  // implementation are tested in public/types/extended_type_test.cc.
  std::unique_ptr<ExtendedCompositeCastEvaluator> extended_evaluator =
      GetExtendedCastEvaluatorFromResolvedCast(cast);
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> function_call,
                   BuiltinScalarFunction::CreateCast(
                       language_options_, cast->type(), std::move(arg),
                       std::move(format), std::move(time_zone),
                       cast->type_modifiers(), cast->return_null_on_error(),
                       ResolvedFunctionCallBase::DEFAULT_ERROR_MODE,
                       std::move(extended_evaluator)));
  return function_call;
}

absl::StatusOr<std::unique_ptr<InlineLambdaExpr>> Algebrizer::AlgebrizeLambda(
    const ResolvedInlineLambda* lambda) {
  GOOGLESQL_RET_CHECK_NE(lambda, nullptr);
  // Access 'parameters' to suppress the resolver check for non-accessed
  // expressions.
  for (const auto& parameter : lambda->parameter_list()) {
    parameter->column();
  }

  // Allocate and collect variables for lambda arguments.
  std::vector<VariableId> lambda_arg_vars;
  lambda_arg_vars.reserve(lambda->argument_list_size());
  for (int i = 0; i < lambda->argument_list_size(); i++) {
    lambda_arg_vars.push_back(column_to_variable_->AssignNewVariableToColumn(
        lambda->argument_list(i)));
  }

  // Algebrize lambda body
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> lambda_body,
                   AlgebrizeExpression(lambda->body()));

  return InlineLambdaExpr::Create(lambda_arg_vars, std::move(lambda_body));
}

namespace {
constexpr absl::string_view kCollatedFunctionNamePostfix = "_with_collation";
// Creates function name and arguments with respect to the collations in
// <collation_list> given original function name <function> and <arguments>.
absl::Status GetCollatedFunctionNameAndArguments(
    absl::string_view function_name,
    std::vector<std::unique_ptr<AlgebraArg>> arguments,
    absl::Span<const ResolvedCollation> collation_list,
    const LanguageOptions& language_options,
    std::string* collated_function_name,
    std::vector<std::unique_ptr<AlgebraArg>>* collated_arguments) {
  // So far we only support functions that takes a single collator.
  GOOGLESQL_RET_CHECK(collation_list.size() == 1)
      << "The collation_list can only contain one element for function "
      << function_name;
  GOOGLESQL_ASSIGN_OR_RETURN(std::string collation_name,
                   GetCollationNameFromResolvedCollation(collation_list[0]));

  if (function_name == "$greater" || function_name == "$greater_or_equal" ||
      function_name == "$less" || function_name == "$less_or_equal" ||
      function_name == "$equal" || function_name == "$not_equal" ||
      function_name == "$between" || function_name == "$in" ||
      function_name == "$is_distinct_from" ||
      function_name == "$is_not_distinct_from") {
    // For any binary comparison function listed here, we use
    // CollationKey(left/right_argument, collation_name) as collated argument
    // for original left/right argument respectively. The
    // <collated_function_name> is the same as <function_name>.
    *collated_function_name = function_name;
    for (int i = 0; i < arguments.size(); ++i) {
      std::vector<std::unique_ptr<AlgebraArg>> collation_key_args;
      collation_key_args.reserve(2);
      collation_key_args.push_back(std::move(arguments[i]));
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> collation_name_expr,
                       ConstExpr::Create(Value::String(collation_name)));
      collation_key_args.push_back(
          std::make_unique<ExprArg>(std::move(collation_name_expr)));
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> collation_key,
                       BuiltinScalarFunction::CreateCall(
                           FunctionKind::kCollationKey, language_options,
                           types::StringType(), std::move(collation_key_args)));
      collated_arguments->push_back(
          std::make_unique<ExprArg>(std::move(collation_key)));
    }
  } else if (function_name == "replace" || function_name == "split" ||
             function_name == "strpos" || function_name == "instr" ||
             function_name == "starts_with" || function_name == "ends_with" ||
             function_name == "$like" || function_name == "$like_any" ||
             function_name == "$not_like_any" || function_name == "$like_all" ||
             function_name == "$not_like_all" ||
             function_name == "$like_any_array" ||
             function_name == "$not_like_any_array" ||
             function_name == "$like_all_array" ||
             function_name == "$not_like_all_array") {
    // For string functions whose collated version take an extra collator
    // argument, we insert the <collation_name> as a String argument at the
    // beginning of the <arguments> vector. We append the postfix to
    // <function_name> to get <collated_function_name>, and use a leading '$'
    // to indicate it is a internal function.
    *collated_function_name =
        absl::StrCat(function_name, kCollatedFunctionNamePostfix);
    if (!absl::StartsWith(*collated_function_name, "$")) {
      *collated_function_name = absl::StrCat("$", *collated_function_name);
    }
    *collated_arguments = std::move(arguments);
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> collation_name_expr,
                     ConstExpr::Create(Value::String(collation_name)));
    collated_arguments->insert(
        collated_arguments->cbegin(),
        std::make_unique<ExprArg>(std::move(collation_name_expr)));
  } else if (function_name == "$case_with_value" ||
             function_name == "$in_array" || function_name == "array_min" ||
             function_name == "array_max" || function_name == "array_offset" ||
             function_name == "array_find" ||
             function_name == "array_offsets" ||
             function_name == "array_find_all" ||
             function_name == "array_includes" ||
             function_name == "array_includes_all" ||
             function_name == "array_includes_any") {
    // For these functions, we do not make changes to arguments or the function
    // name here. The arguments may be transformed at a later stage.
    // TODO: Replace hard-coded function name list with builtin
    // function signature signal against collation.
    *collated_function_name = function_name;
    *collated_arguments = std::move(arguments);
  } else {
    return ::googlesql_base::InvalidArgumentErrorBuilder()
           << "Collation is not supported for function: " << function_name;
  }
  return absl::OkStatus();
}

}  // namespace

struct EvaluatorState {
  std::shared_ptr<ValueExpr> algebrized_tree;
  std::vector<std::string> argument_names;
};

absl::StatusOr<EvaluatorState> MakeUdfEvaluator(
    const ResolvedExpr* expr, std::vector<std::string> argument_names,
    LanguageOptions language_options, AlgebrizerOptions algebrizer_options,
    TypeFactory* type_factory) {
  Parameters parameter_variables;
  ParameterMap column_map;
  SystemVariablesAlgebrizerMap algebrizer_system_variables;
  std::unique_ptr<ValueExpr> algebrized_tree;
  GOOGLESQL_RETURN_IF_ERROR(Algebrizer::AlgebrizeExpression(
      language_options, algebrizer_options, type_factory, expr,
      &algebrized_tree, &parameter_variables, &column_map,
      &algebrizer_system_variables));
  return EvaluatorState{std::move(algebrized_tree), argument_names};
}

namespace {
// A mapping of googlesql::FunctionSignatureId to FunctionKind, to be used in
// the algebrizer when matching on the function name is not precise enough.
// TODO: Can we unify FunctionSignatureId and FunctionKind?
static const absl::flat_hash_map<int64_t, FunctionKind>&
GetBuiltinFunctionSignatureIdToKindMap() {
  static const absl::NoDestructor<absl::flat_hash_map<int64_t, FunctionKind>>
      kBuiltinFunctionSignatureIdToKindMap(
          absl::flat_hash_map<int64_t, FunctionKind>({
              {FN_JSON_SUBSCRIPT_STRING, FunctionKind::kJsonSubscript},
              {FN_JSON_SUBSCRIPT_INT64, FunctionKind::kJsonSubscript},
              {FN_MAP_SUBSCRIPT, FunctionKind::kMapSubscript},
              {FN_MAP_SUBSCRIPT_WITH_KEY, FunctionKind::kMapSubscriptWithKey},
              {FN_MAP_SAFE_SUBSCRIPT_WITH_KEY,
               FunctionKind::kMapSafeSubscriptWithKey},
              {FN_MAP_REPLACE_KV_PAIRS, FunctionKind::kMapReplaceKeyValuePairs},
              {FN_MAP_REPLACE_K_REPEATED_V_LAMBDA,
               FunctionKind::kMapReplaceKeysAndLambda},
          }));
  return *kBuiltinFunctionSignatureIdToKindMap;
};

// Helper function to convert a series of AlgebraArg to ValueExpr.
// TODO: b/359716173: Remove this function once all usages of ValueExpr to
// represent function arguments are migrated to AlgebraArg.
static absl::StatusOr<std::vector<std::unique_ptr<ValueExpr>>>
ConvertAlgebraArgsToValueExprs(
    std::vector<std::unique_ptr<AlgebraArg>>&& arguments) {
  std::vector<std::unique_ptr<ValueExpr>> converted_arguments;
  converted_arguments.reserve(arguments.size());
  for (auto& e : arguments) {
    GOOGLESQL_RET_CHECK(e->value_expr() != nullptr) << absl::StrFormat(
        "The argument %s is not a value expression.", e->DebugString());

    converted_arguments.push_back(e->release_value_expr());
  }
  return converted_arguments;
}

// Returns true if the function call is a GoogleSQL builtin function with an
// argument of extended type.
// This is a helper function to determine whether to use the generic evaluator
// for the function call. Extended types are not supported in the built-in
// evaluators so the custom evaluator should be used instead.
static bool IsBuiltinFunctionWithExtendedTypeEvaluator(
    const ResolvedFunctionCallBase* function_call) {
  if (function_call->function()->GetFunctionEvaluatorFactory() == nullptr &&
      function_call->function()->GetAggregateFunctionEvaluatorFactory() ==
          nullptr) {
    return false;
  }
  if (function_call->function()->IsGoogleSQLBuiltin()) {
    for (const auto& arg : function_call->argument_list()) {
      if (arg->type()->IsExtendedType()) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

ABSL_ATTRIBUTE_NOINLINE
absl::StatusOr<std::vector<std::unique_ptr<AlgebraArg>>>
Algebrizer::AlgebrizeFunctionArgs(
  const ResolvedFunctionCall* function_call) {
  bool use_generic_args = function_call->generic_argument_list_size() > 0;
  int num_arguments = use_generic_args
                          ? function_call->generic_argument_list_size()
                          : function_call->argument_list_size();
  std::vector<std::unique_ptr<AlgebraArg>> arguments;
  for (int i = 0; i < num_arguments; ++i) {
    const ResolvedExpr* argument_expr = nullptr;

    if (use_generic_args) {
      const ResolvedFunctionArgument* function_arg =
          function_call->generic_argument_list(i);

      if (function_arg->expr() != nullptr) {
        argument_expr = function_arg->expr();
      } else if (function_arg->sequence() != nullptr) {
        // ResolvedSequence is algebrized to a string constant containing the
        // sequence name.
        const ResolvedSequence* sequence = function_arg->sequence();
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> sequence_name,
                         ConstExpr::Create(Value::StringValue(absl::StrCat(
                             "_sequence_", sequence->sequence()->FullName()))));
        arguments.push_back(
            std::make_unique<ExprArg>(std::move(sequence_name)));
      } else if (function_arg->inline_lambda() != nullptr) {
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<InlineLambdaExpr> lambda,
                         AlgebrizeLambda(function_arg->inline_lambda()));
        arguments.push_back(
            std::make_unique<InlineLambdaArg>(std::move(lambda)));
      } else {
        return ::googlesql_base::UnimplementedErrorBuilder()
               << "Unimplemented generic function argument: "
               << function_arg->node_kind_string();
      }
      // TODO: Store the argument aliases in `AlgebraArg`s.
      // Currently we just access this field because no functions need to access
      // argument aliases in the reference implementation.
      function_arg->argument_alias();
    } else {
      argument_expr = function_call->argument_list(i);
    }

    if (argument_expr == nullptr) {
      continue;
    }

    GOOGLESQL_ASSIGN_OR_RETURN(auto argument, AlgebrizeExpression(argument_expr));
    arguments.push_back(std::make_unique<ExprArg>(std::move(argument)));
  }
  return std::move(arguments);
}

ABSL_ATTRIBUTE_NOINLINE
absl::StatusOr<std::unique_ptr<ValueExpr>>
Algebrizer::AlgebrizeFunctionCallWithAlgebriedArgsAndEvalFactory(
    const ResolvedFunctionCall* function_call,
    std::vector<std::unique_ptr<AlgebraArg>> arguments) {
  std::string name =
      function_call->function()->FullName(/*include_group=*/false);
  GOOGLESQL_RET_CHECK(function_call->function()->GetFunctionEvaluatorFactory() !=
            nullptr);
  auto callback = function_call->function()->GetFunctionEvaluatorFactory();
  // An evaluator is already defined for this function.
  GOOGLESQL_ASSIGN_OR_RETURN(auto eval_impl, callback(function_call->signature()));
  GOOGLESQL_RET_CHECK(eval_impl != nullptr)
      << "NULL evaluator returned for user-defined function " << name;
  ContextAwareFunctionEvaluator evaluator =
      [evaluator = eval_impl](const absl::Span<const Value> arguments,
                              EvaluationContext& context) {
        return evaluator(arguments);
      };
  return ScalarFunctionCallExpr::Create(
      std::make_unique<UserDefinedScalarFunction>(evaluator,
                                                  function_call->type(), name),
      std::move(arguments), function_call->error_mode());
}

ABSL_ATTRIBUTE_NOINLINE
absl::StatusOr<std::unique_ptr<ValueExpr>>
Algebrizer::AlgebrizeSqlFunctionCallWithAlgebrizedArgs(
    const ResolvedFunctionCall* function_call,
    std::vector<std::unique_ptr<AlgebraArg>> arguments) {
  const ResolvedExpr* expr = nullptr;
  std::vector<std::string> argument_names;
  // Non templated UDF
  if (function_call->function()->Is<SQLFunction>()) {
    const SQLFunction* sql_function =
        function_call->function()->GetAs<SQLFunction>();
    expr = sql_function->FunctionExpression();
    for (const std::string& arg_name : sql_function->GetArgumentNames()) {
      argument_names.push_back(arg_name);
    }
  }

  // Templated UDF
  if (function_call->function()->Is<TemplatedSQLFunction>()) {
    const TemplatedSQLFunction* templated_sql_function =
        function_call->function()->GetAs<TemplatedSQLFunction>();
    GOOGLESQL_RET_CHECK(
        function_call->function_call_info()->Is<TemplatedSQLFunctionCall>());
    expr = function_call->function_call_info()
               ->GetAs<TemplatedSQLFunctionCall>()
               ->expr();
    for (const std::string& arg_name :
         templated_sql_function->GetArgumentNames()) {
      argument_names.push_back(arg_name);
    }
  }
  // Create an evaluator that executes this UDF by algebrizing the UDF
  // function body `expr`, then evaluating the result using an
  // EvaluationContext that contains mapped function argument references
  // by name according to `argument_names`.
  const LanguageOptions& language_options = this->language_options_;
  const AlgebrizerOptions& algebrizer_options = this->algebrizer_options_;
  TypeFactory* type_factory = this->type_factory_;
  ContextAwareFunctionEvaluator udf_evaluator(
      [evaluator_state = MakeUdfEvaluator(
           expr, argument_names, language_options, algebrizer_options,
           type_factory)](const absl::Span<const Value> args,
                          EvaluationContext& context) -> absl::StatusOr<Value> {
        GOOGLESQL_ASSIGN_OR_RETURN(EvaluatorState state, evaluator_state);
        // Create a local EvaluationContext for evaluating the function
        // body, it is important that the local context uses the same
        // EvaluationOptions as the outer context, since this contains
        // settings that enable some non determinism checks.
        std::unique_ptr<EvaluationContext> local_context =
            context.MakeChildContext();
        GOOGLESQL_RET_CHECK(state.argument_names.size() == args.size());
        if (!args.empty()) {
          for (int i = 0; i < state.argument_names.size(); ++i) {
            GOOGLESQL_RETURN_IF_ERROR(local_context->AddFunctionArgumentRef(
                state.argument_names[i], args[i]));
          }
        }

        // Evaluate the algebrized tree and return the result, or
        // propagate the error status.
        TupleSlot result;
        absl::Status status;
        GOOGLESQL_RETURN_IF_ERROR(state.algebrized_tree->SetSchemasForEvaluation({}));
        if (!state.algebrized_tree->EvalSimple(
                /*params=*/{}, local_context.get(), &result, &status)) {
          GOOGLESQL_RET_CHECK(!status.ok());
          return status;
        }
        GOOGLESQL_RET_CHECK(status.ok());
        GOOGLESQL_RETURN_IF_ERROR(local_context->VerifyNotAborted());
        return result.value();
      });
  return ScalarFunctionCallExpr::Create(
      std::make_unique<UserDefinedScalarFunction>(
          udf_evaluator, function_call->type(),
          function_call->function()->FullName(/*include_group=*/false)),
      std::move(arguments), function_call->error_mode());
}

ABSL_ATTRIBUTE_NOINLINE
absl::StatusOr<std::unique_ptr<ValueExpr>>
Algebrizer::AlgebrizeBuiltinFunctionCallWithAlgebrizedArguments(
    const ResolvedFunctionCall* function_call,
  std::vector<std::unique_ptr<AlgebraArg>> arguments) {
  std::string name =
      function_call->function()->FullName(/*include_group=*/false);

  // If <collation_list> is present, we replace original <arguments> with
  // new arguments with respect to the collations.
  if (!function_call->collation_list().empty()) {
    std::string collated_name;
    std::vector<std::unique_ptr<AlgebraArg>> collated_arguments;
    GOOGLESQL_RETURN_IF_ERROR(GetCollatedFunctionNameAndArguments(
        name, std::move(arguments), function_call->collation_list(),
        language_options_, &collated_name, &collated_arguments));
    name = collated_name;
    arguments = std::move(collated_arguments);
  }

  const auto& kSignatureIdToKindMap = GetBuiltinFunctionSignatureIdToKindMap();

  FunctionKind kind;
  if (name == "$not_equal") {
    GOOGLESQL_ASSIGN_OR_RETURN(std::vector<std::unique_ptr<ValueExpr>> argument_exprs,
                     ConvertAlgebraArgsToValueExprs(std::move(arguments)));
    return AlgebrizeNotEqual(std::move(argument_exprs));
  } else if (name == "$greater") {
    kind = FunctionKind::kLess;
    std::swap(arguments[0], arguments[1]);
  } else if (name == "$greater_or_equal") {
    kind = FunctionKind::kLessOrEqual;
    std::swap(arguments[0], arguments[1]);
  } else if (name == "typeof") {
    GOOGLESQL_ASSIGN_OR_RETURN(std::vector<std::unique_ptr<ValueExpr>> argument_exprs,
                     ConvertAlgebraArgsToValueExprs(std::move(arguments)));
    return CreateTypeofExpr(std::move(argument_exprs));
  } else if (name == "if") {
    GOOGLESQL_ASSIGN_OR_RETURN(std::vector<std::unique_ptr<ValueExpr>> argument_exprs,
                     ConvertAlgebraArgsToValueExprs(std::move(arguments)));
    return AlgebrizeIf(function_call->type(), std::move(argument_exprs));
  } else if (name == "iferror") {
    GOOGLESQL_ASSIGN_OR_RETURN(std::vector<std::unique_ptr<ValueExpr>> argument_exprs,
                     ConvertAlgebraArgsToValueExprs(std::move(arguments)));
    return AlgebrizeIfError(std::move(argument_exprs));
  } else if (name == "iserror") {
    GOOGLESQL_ASSIGN_OR_RETURN(std::vector<std::unique_ptr<ValueExpr>> argument_exprs,
                     ConvertAlgebraArgsToValueExprs(std::move(arguments)));
    return AlgebrizeIsError(std::move(argument_exprs));
  } else if (name == "ifnull" || name == "zeroifnull") {
    GOOGLESQL_ASSIGN_OR_RETURN(std::vector<std::unique_ptr<ValueExpr>> argument_exprs,
                     ConvertAlgebraArgsToValueExprs(std::move(arguments)));
    return AlgebrizeIfNull(function_call->type(), std::move(argument_exprs));
  } else if (name == "nullif" || name == "nullifzero") {
    GOOGLESQL_ASSIGN_OR_RETURN(std::vector<std::unique_ptr<ValueExpr>> argument_exprs,
                     ConvertAlgebraArgsToValueExprs(std::move(arguments)));
    return AlgebrizeNullIf(function_call->type(), std::move(argument_exprs));
  } else if (name == "nulliferror") {
    GOOGLESQL_ASSIGN_OR_RETURN(std::vector<std::unique_ptr<ValueExpr>> argument_exprs,
                     ConvertAlgebraArgsToValueExprs(std::move(arguments)));
    return CreateNullIfErrorExpr(std::move(argument_exprs));
  } else if (name == "coalesce") {
    GOOGLESQL_ASSIGN_OR_RETURN(std::vector<std::unique_ptr<ValueExpr>> argument_exprs,
                     ConvertAlgebraArgsToValueExprs(std::move(arguments)));
    return AlgebrizeCoalesce(function_call->type(), std::move(argument_exprs));
  } else if (name == "$case_no_value") {
    GOOGLESQL_ASSIGN_OR_RETURN(std::vector<std::unique_ptr<ValueExpr>> argument_exprs,
                     ConvertAlgebraArgsToValueExprs(std::move(arguments)));
    return AlgebrizeCaseNoValue(function_call->type(),
                                std::move(argument_exprs));
  } else if (name == "$case_with_value") {
    GOOGLESQL_ASSIGN_OR_RETURN(std::vector<std::unique_ptr<ValueExpr>> argument_exprs,
                     ConvertAlgebraArgsToValueExprs(std::move(arguments)));
    return AlgebrizeCaseWithValue(function_call->type(),
                                  std::move(argument_exprs),
                                  function_call->collation_list());
  } else if (name == "$with_side_effects") {
    GOOGLESQL_ASSIGN_OR_RETURN(std::vector<std::unique_ptr<ValueExpr>> argument_exprs,
                     ConvertAlgebraArgsToValueExprs(std::move(arguments)));
    return AlgebrizeWithSideEffects(std::move(argument_exprs));
  } else if (name == "$in") {
    GOOGLESQL_ASSIGN_OR_RETURN(std::vector<std::unique_ptr<ValueExpr>> argument_exprs,
                     ConvertAlgebraArgsToValueExprs(std::move(arguments)));
    return AlgebrizeIn(function_call->type(), std::move(argument_exprs));
  } else if (name == "$between") {
    GOOGLESQL_ASSIGN_OR_RETURN(std::vector<std::unique_ptr<ValueExpr>> argument_exprs,
                     ConvertAlgebraArgsToValueExprs(std::move(arguments)));
    return AlgebrizeBetween(function_call->type(), std::move(argument_exprs));
  } else if (name == "$make_array") {
    GOOGLESQL_ASSIGN_OR_RETURN(std::vector<std::unique_ptr<ValueExpr>> argument_exprs,
                     ConvertAlgebraArgsToValueExprs(std::move(arguments)));
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> new_array_expr,
                     NewArrayExpr::Create(function_call->type()->AsArray(),
                                          std::move(argument_exprs)));
    return new_array_expr;
  } else if (name == "$in_array") {
    const std::vector<ResolvedCollation>& collation_list =
        function_call->collation_list();
    GOOGLESQL_RET_CHECK_LE(collation_list.size(), 1);
    GOOGLESQL_ASSIGN_OR_RETURN(std::vector<std::unique_ptr<ValueExpr>> argument_exprs,
                     ConvertAlgebraArgsToValueExprs(std::move(arguments)));
    return AlgebrizeInArray(
        std::move(argument_exprs[0]), std::move(argument_exprs[1]),
        collation_list.empty() ? ResolvedCollation() : collation_list[0]);
  } else if (name == "float64") {
    kind = FunctionKind::kDouble;
  } else if (auto mapped_kind = kSignatureIdToKindMap.find(
                 function_call->signature().context_id());
             mapped_kind != kSignatureIdToKindMap.end()) {
    GOOGLESQL_RET_CHECK(function_call->function()->IsGoogleSQLBuiltin());
    kind = mapped_kind->second;
  } else {
    absl::StatusOr<FunctionKind> status_or_kind =
        BuiltinFunctionCatalog::GetKindByName(name);
    if (!status_or_kind.ok()) {
      return status_or_kind.status();
    }
    kind = status_or_kind.value();
  }

  static const auto* const kScalarArrayFunctions =
      new absl::flat_hash_set<absl::string_view>{
          "array_min",          "array_max",         "array_offset",
          "array_find",         "array_offsets",     "array_find_all",
          "array_includes_all", "array_includes_any"};
  // TODO: Refactor out AlgebrizeScalarArrayFunctionWithCollation
  // to provide a standard solution to scalar function with collation.
  if (kScalarArrayFunctions->contains(name) ||
      // ARRAY_INCLUDES takes this path only with the non-lambda version
      (function_call->function()->IsGoogleSQLBuiltin(FN_ARRAY_INCLUDES) &&
       function_call->signature().context_id() == FN_ARRAY_INCLUDES)) {
    return AlgebrizeScalarArrayFunctionWithCollation(
        kind, function_call->type(), name, std::move(arguments),
        function_call->collation_list());
  }

  BuiltinScalarFunctionCallOptions function_call_options = {
  };
  return BuiltinScalarFunction::CreateCall(
      kind, language_options_, function_call->type(), std::move(arguments),
      function_call->error_mode(), function_call_options);
}

absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeFunctionCall(
    const ResolvedFunctionCall* function_call) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  const Function* function = function_call->function();

  if (function_call->error_mode() ==
          ResolvedFunctionCallBase::SAFE_ERROR_MODE &&
      !function->SupportsSafeErrorMode()) {
    return ::googlesql_base::InvalidArgumentErrorBuilder()
           << "Function " << function->FullName(/*include_group=*/false)
           << "does not support SAFE error mode";
  }
  GOOGLESQL_ASSIGN_OR_RETURN(std::vector<std::unique_ptr<AlgebraArg>> arguments,
                   AlgebrizeFunctionArgs(function_call));

  if (function->GetFunctionEvaluatorFactory() != nullptr &&
      (!function->IsGoogleSQLBuiltin() ||
       IsBuiltinFunctionWithExtendedTypeEvaluator(function_call))) {
    return AlgebrizeFunctionCallWithAlgebriedArgsAndEvalFactory(
        function_call, std::move(arguments));
  }

  if (function->Is<SQLFunction>() || function->Is<TemplatedSQLFunction>()) {
    return AlgebrizeSqlFunctionCallWithAlgebrizedArgs(function_call,
                                                      std::move(arguments));
  }

  // User-defined functions and extended type built-in functions.
  if (!function->IsGoogleSQLBuiltin()) {
    return ::googlesql_base::InvalidArgumentErrorBuilder()
           << "User-defined function "
           << function->FullName(/*include_group=*/false)
           << " has no evaluator. Use FunctionOptions to supply one.";
  }

  return AlgebrizeBuiltinFunctionCallWithAlgebrizedArguments(
  function_call, std::move(arguments));
}

// CASE WHEN w1 THEN t1 ELSE e END =
//     IfExpr(w1, t1, e)
// CASE WHEN w1 THEN t1 WHEN w2 THEN t2 ELSE e END =
//     IfExpr(w1, t1, IfExpr(w2, t2, e))
// etc.
absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeCaseNoValue(
    const Type* output_type, std::vector<std::unique_ptr<ValueExpr>> args) {
  GOOGLESQL_RET_CHECK_LE(2, args.size());
  bool has_else = args.size() % 2 == 1;
  int i = args.size() - 1;
  GOOGLESQL_ASSIGN_OR_RETURN(auto null_expr, ConstExpr::Create(Value::Null(output_type)));
  std::unique_ptr<ValueExpr> result(has_else ? std::move(args[i--])
                                             : std::move(null_expr));
  while (i > 0) {
    std::unique_ptr<ValueExpr> then = std::move(args[i--]);
    std::unique_ptr<ValueExpr> when = std::move(args[i--]);
    GOOGLESQL_ASSIGN_OR_RETURN(result, IfExpr::Create(std::move(when), std::move(then),
                                            std::move(result)));
  }
  GOOGLESQL_RET_CHECK_EQ(-1, i);
  return result;
}

// CASE v WHEN w1 THEN t1 ELSE e END =
//     If(v=w1, t1, e)
// CASE v WHEN w1 THEN t1 WHEN w2 THEN t2 ELSE e END =
//     WithExpr(x:=v, IfExpr(x=w1, t1, IfExpr(x=w2, t2, e)))
// etc.
absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeCaseWithValue(
    const Type* output_type, std::vector<std::unique_ptr<ValueExpr>> args,
    absl::Span<const ResolvedCollation> collation_list) {
  GOOGLESQL_RET_CHECK_LE(2, args.size());
  bool has_else = args.size() % 2 == 0;
  int i = args.size() - 1;
  GOOGLESQL_ASSIGN_OR_RETURN(auto null_expr, ConstExpr::Create(Value::Null(output_type)));
  std::unique_ptr<ValueExpr> result(has_else ? std::move(args[i--])
                                             : std::move(null_expr));
  // Empty x means we don't need WithExpr, i.e., we have a single WHEN/THEN.
  const VariableId x =
      args.size() > 4 ? variable_gen_->GetNewVariableName("x") : VariableId();
  while (i > 0) {
    std::unique_ptr<ValueExpr> then = std::move(args[i--]);
    std::unique_ptr<ValueExpr> when = std::move(args[i--]);

    std::unique_ptr<ValueExpr> value;
    if (x.is_valid()) {
      GOOGLESQL_ASSIGN_OR_RETURN(value, DerefExpr::Create(x, args[0]->output_type()));
    } else {
      value = std::move(args[0]);
    }

    std::vector<std::unique_ptr<ValueExpr>> cond_args;
    cond_args.push_back(std::move(value));
    cond_args.push_back(std::move(when));

    if (!collation_list.empty()) {
      std::string collated_name;
      std::vector<std::unique_ptr<AlgebraArg>> collated_cond_arguments;
      GOOGLESQL_RETURN_IF_ERROR(GetCollatedFunctionNameAndArguments(
          "$equal", ConvertValueExprsToAlgebraArgs(std::move(cond_args)),
          collation_list, language_options_, &collated_name,
          &collated_cond_arguments));
      GOOGLESQL_RET_CHECK_EQ(collated_name, "$equal");
      GOOGLESQL_ASSIGN_OR_RETURN(cond_args, ConvertAlgebraArgsToValueExprs(
                                      std::move(collated_cond_arguments)));
    }

    GOOGLESQL_ASSIGN_OR_RETURN(auto cond,
                     BuiltinScalarFunction::CreateCall(
                         FunctionKind::kEqual, language_options_, BoolType(),
                         ConvertValueExprsToAlgebraArgs(std::move(cond_args)),
                         ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));
    GOOGLESQL_ASSIGN_OR_RETURN(result, IfExpr::Create(std::move(cond), std::move(then),
                                            std::move(result)));
  }
  GOOGLESQL_RET_CHECK_EQ(0, i);
  if (x.is_valid()) {
    std::vector<std::unique_ptr<ExprArg>> expr_args;
    expr_args.push_back(std::make_unique<ExprArg>(x, std::move(args[0])));
    GOOGLESQL_ASSIGN_OR_RETURN(result,
                     WithExpr::Create(std::move(expr_args), std::move(result)));
  }
  return result;
}

// a != b  ->  !(a = b)
absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeNotEqual(
    std::vector<std::unique_ptr<ValueExpr>> args) {
  GOOGLESQL_RET_CHECK_EQ(2, args.size());
  GOOGLESQL_ASSIGN_OR_RETURN(auto equal,
                   BuiltinScalarFunction::CreateCall(
                       FunctionKind::kEqual, language_options_, BoolType(),
                       ConvertValueExprsToAlgebraArgs(std::move(args)),
                       ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));

  std::vector<std::unique_ptr<ValueExpr>> not_args;
  not_args.push_back(std::move(equal));

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> function_call,
                   BuiltinScalarFunction::CreateCall(
                       FunctionKind::kNot, language_options_, BoolType(),
                       ConvertValueExprsToAlgebraArgs(std::move(not_args)),
                       ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));
  return function_call;
}

// If(v0, v1, v2) = IfExpr(v0, v1, v2)
absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeIf(
    const Type* output_type, std::vector<std::unique_ptr<ValueExpr>> args) {
  GOOGLESQL_RET_CHECK_EQ(3, args.size());
  return IfExpr::Create(std::move(args[0]), std::move(args[1]),
                        std::move(args[2]));
}

// Iferror(v0, v1) = IfErrorExpr(v0, v1)
absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeIfError(
    std::vector<std::unique_ptr<ValueExpr>> args) {
  GOOGLESQL_RET_CHECK_EQ(2, args.size());
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> if_error_expr,
                   IfErrorExpr::Create(std::move(args[0]), std::move(args[1])));
  return if_error_expr;
}

// Iserror(v0) = IsErrorExpr(v0)
absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeIsError(
    std::vector<std::unique_ptr<ValueExpr>> args) {
  GOOGLESQL_RET_CHECK_EQ(args.size(), 1);
  return IsErrorExpr::Create(std::move(args[0]));
}

// $with_side_effects(v0, v1) = WithSideEffectsExpr(v0, v1)
absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeWithSideEffects(
    std::vector<std::unique_ptr<ValueExpr>> args) {
  GOOGLESQL_RET_CHECK_EQ(args.size(), 2);
  return WithSideEffectsExpr::Create(std::move(args[0]), std::move(args[1]));
}

// Generates a ConstExpr with the value of 0 based on the input type. Only
// the following types are supported: INT32, INT64, UINT32, UINT64, FLOAT,
// DOUBLE, NUMERIC, BIGNUMERIC.
static absl::StatusOr<std::unique_ptr<ValueExpr>> CreateTypedZero(
    const Type* type) {
  switch (type->kind()) {
    case TYPE_INT32:
      return ConstExpr::Create(Value::Int32(0));
    case TYPE_INT64:
      return ConstExpr::Create(Value::Int64(0));
    case TYPE_UINT32:
      return ConstExpr::Create(Value::Uint32(0));
    case TYPE_UINT64:
      return ConstExpr::Create(Value::Uint64(0));
    case TYPE_FLOAT:
      return ConstExpr::Create(Value::Float(0));
    case TYPE_DOUBLE:
      return ConstExpr::Create(Value::Double(0));
    case TYPE_NUMERIC:
      return ConstExpr::Create(Value::Numeric(NumericValue(0)));
    case TYPE_BIGNUMERIC:
      return ConstExpr::Create(Value::BigNumeric(BigNumericValue(0)));
    default: {
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected argument type in CreateTypedZero: "
                       << type->DebugString();
    }
  }
}

// IfNull(v0, v1) = WithExpr(x:=v0, IfExpr(IsNull(x), v1, x))
absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeIfNull(
    const Type* output_type, std::vector<std::unique_ptr<ValueExpr>> args) {
  // If ZeroIfNull is being invoked, we augment args with the value 0 based on
  // the appropriate type.
  // ZeroIfNull = IfNull(v0, 0)
  if (args.size() == 1) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> zero,
                     CreateTypedZero(output_type));
    args.push_back(std::move(zero));
  }
  GOOGLESQL_RET_CHECK_EQ(2, args.size());
  const VariableId x = variable_gen_->GetNewVariableName("x");

  GOOGLESQL_ASSIGN_OR_RETURN(auto deref_x, DerefExpr::Create(x, output_type));

  std::vector<std::unique_ptr<ValueExpr>> is_null_args;
  is_null_args.push_back(std::move(deref_x));

  GOOGLESQL_ASSIGN_OR_RETURN(auto is_null,
                   BuiltinScalarFunction::CreateCall(
                       FunctionKind::kIsNull, language_options_, BoolType(),
                       ConvertValueExprsToAlgebraArgs(std::move(is_null_args)),
                       ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));

  GOOGLESQL_ASSIGN_OR_RETURN(auto deref_x_again, DerefExpr::Create(x, output_type));

  GOOGLESQL_ASSIGN_OR_RETURN(auto if_op,
                   IfExpr::Create(std::move(is_null), std::move(args[1]),
                                  std::move(deref_x_again)));

  std::vector<std::unique_ptr<ExprArg>> let_assign;
  let_assign.push_back(std::make_unique<ExprArg>(x, std::move(args[0])));

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> with_expr,
                   WithExpr::Create(std::move(let_assign), std::move(if_op)));
  return with_expr;
}

// NullIf(v0, v1) = WithExpr(x:=v0, IfExpr(x=v1, NULL, x))
absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeNullIf(
    const Type* output_type, std::vector<std::unique_ptr<ValueExpr>> args) {
  // If NullIfZero is being invoked, we augment args with the value 0 based on
  // the appropriate type.
  // NullIfZero = NullIf(v0, 0)
  if (args.size() == 1) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> zero,
                     CreateTypedZero(output_type));
    args.push_back(std::move(zero));
  }
  GOOGLESQL_RET_CHECK_EQ(2, args.size());
  const VariableId x = variable_gen_->GetNewVariableName("x");

  GOOGLESQL_ASSIGN_OR_RETURN(auto deref_x, DerefExpr::Create(x, output_type));

  std::vector<std::unique_ptr<ValueExpr>> equal_args;
  equal_args.push_back(std::move(deref_x));
  equal_args.push_back(std::move(args[1]));

  GOOGLESQL_ASSIGN_OR_RETURN(auto equal,
                   BuiltinScalarFunction::CreateCall(
                       FunctionKind::kEqual, language_options_, BoolType(),
                       ConvertValueExprsToAlgebraArgs(std::move(equal_args)),
                       ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));
  GOOGLESQL_ASSIGN_OR_RETURN(auto null_constant,
                   ConstExpr::Create(Value::Null(output_type)));
  GOOGLESQL_ASSIGN_OR_RETURN(auto deref_x_again, DerefExpr::Create(x, output_type));
  GOOGLESQL_ASSIGN_OR_RETURN(auto if_op,
                   IfExpr::Create(std::move(equal), std::move(null_constant),
                                  std::move(deref_x_again)));

  std::vector<std::unique_ptr<ExprArg>> let_assign;
  let_assign.push_back(std::make_unique<ExprArg>(x, std::move(args[0])));

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> with_expr,
                   WithExpr::Create(std::move(let_assign), std::move(if_op)));
  return with_expr;
}

// Coalesce(v1, v2) = WithExpr(x:=v1, IfExpr(IsNull(x), v2, x))
// Coalesce(v1, v2, ...) = Coalesce(v1, Coalesce(v2, ...))
absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeCoalesce(
    const Type* output_type, std::vector<std::unique_ptr<ValueExpr>> args) {
  GOOGLESQL_RET_CHECK_LE(1, args.size());
  int i = args.size() - 1;
  std::unique_ptr<ValueExpr> result = std::move(args[i--]);
  while (i >= 0) {
    const VariableId x = variable_gen_->GetNewVariableName("x");

    GOOGLESQL_ASSIGN_OR_RETURN(auto deref_x, DerefExpr::Create(x, output_type));

    std::vector<std::unique_ptr<ValueExpr>> is_null_args;
    is_null_args.push_back(std::move(deref_x));

    GOOGLESQL_ASSIGN_OR_RETURN(
        auto is_null,
        BuiltinScalarFunction::CreateCall(
            FunctionKind::kIsNull, language_options_, BoolType(),
            ConvertValueExprsToAlgebraArgs(std::move(is_null_args)),
            ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));
    GOOGLESQL_ASSIGN_OR_RETURN(auto deref_x_again, DerefExpr::Create(x, output_type));
    GOOGLESQL_ASSIGN_OR_RETURN(auto if_op,
                     IfExpr::Create(std::move(is_null), std::move(result),
                                    std::move(deref_x_again)));

    std::vector<std::unique_ptr<ExprArg>> let_assign;
    let_assign.push_back(std::make_unique<ExprArg>(x, std::move(args[i--])));

    GOOGLESQL_ASSIGN_OR_RETURN(result,
                     WithExpr::Create(std::move(let_assign), std::move(if_op)));
  }
  GOOGLESQL_RET_CHECK_EQ(-1, i);
  return result;
}

// In(v, v1, v2, ...) = WithExpr(x:=v, Or(x=v1, x=v2, ...))
absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeIn(
    const Type* output_type, std::vector<std::unique_ptr<ValueExpr>> args) {
  GOOGLESQL_RET_CHECK_GE(args.size(), 2);
  const VariableId x = variable_gen_->GetNewVariableName("x");
  std::vector<std::unique_ptr<ValueExpr>> or_args;
  for (int i = 0; i < args.size() - 1; ++i) {
    GOOGLESQL_ASSIGN_OR_RETURN(auto deref_x, DerefExpr::Create(x, output_type));

    std::vector<std::unique_ptr<ValueExpr>> or_arg_args;
    or_arg_args.push_back(std::move(deref_x));
    or_arg_args.push_back(std::move(args[i + 1]));

    GOOGLESQL_ASSIGN_OR_RETURN(auto or_arg,
                     BuiltinScalarFunction::CreateCall(
                         FunctionKind::kEqual, language_options_, BoolType(),
                         ConvertValueExprsToAlgebraArgs(std::move(or_arg_args)),
                         ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));
    or_args.push_back(std::move(or_arg));
  }
  GOOGLESQL_ASSIGN_OR_RETURN(auto or_op,
                   BuiltinScalarFunction::CreateCall(
                       FunctionKind::kOr, language_options_, BoolType(),
                       ConvertValueExprsToAlgebraArgs(std::move(or_args)),
                       ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));

  std::vector<std::unique_ptr<ExprArg>> let_assign;
  let_assign.push_back(std::make_unique<ExprArg>(x, std::move(args[0])));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> with_expr,
                   WithExpr::Create(std::move(let_assign), std::move(or_op)));
  return with_expr;
}

// Between(v, min, max) = WithExpr(x:=v, And(min<=x, x<=max))
absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeBetween(
    const Type* output_type, std::vector<std::unique_ptr<ValueExpr>> args) {
  GOOGLESQL_RET_CHECK_EQ(args.size(), 3);
  const VariableId x = variable_gen_->GetNewVariableName("x");

  GOOGLESQL_ASSIGN_OR_RETURN(auto deref_x, DerefExpr::Create(x, output_type));
  std::vector<std::unique_ptr<ValueExpr>> first_le_args;
  first_le_args.push_back(std::move(args[1]));
  first_le_args.push_back(std::move(deref_x));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto first_le,
      BuiltinScalarFunction::CreateCall(
          FunctionKind::kLessOrEqual, language_options_, BoolType(),
          ConvertValueExprsToAlgebraArgs(std::move(first_le_args)),
          ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));

  GOOGLESQL_ASSIGN_OR_RETURN(auto deref_x_again, DerefExpr::Create(x, output_type));
  std::vector<std::unique_ptr<ValueExpr>> second_le_args;
  second_le_args.push_back(std::move(deref_x_again));
  second_le_args.push_back(std::move(args[2]));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto second_le,
      BuiltinScalarFunction::CreateCall(
          FunctionKind::kLessOrEqual, language_options_, BoolType(),
          ConvertValueExprsToAlgebraArgs(std::move(second_le_args)),
          ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));

  std::vector<std::unique_ptr<ValueExpr>> and_args;
  and_args.push_back(std::move(first_le));
  and_args.push_back(std::move(second_le));

  GOOGLESQL_ASSIGN_OR_RETURN(auto and_op,
                   BuiltinScalarFunction::CreateCall(
                       FunctionKind::kAnd, language_options_, BoolType(),
                       ConvertValueExprsToAlgebraArgs(std::move(and_args)),
                       ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));

  std::vector<std::unique_ptr<ExprArg>> let_assign;
  let_assign.push_back(std::make_unique<ExprArg>(x, std::move(args[0])));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> with_expr,
                   WithExpr::Create(std::move(let_assign), std::move(and_op)));
  return with_expr;
}

namespace {
static absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeResolvedCollation(
    const ResolvedCollation& collation, TypeFactory* type_factory) {
  const ProtoType* proto_type;
  GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeProtoType(
      ResolvedCollationProto::descriptor(), &proto_type));
  ResolvedCollationProto proto;
  GOOGLESQL_RETURN_IF_ERROR(collation.Serialize(&proto));
  absl::Cord cord;
  GOOGLESQL_RET_CHECK(proto.SerializePartialToCord(&cord));
  Value resolved_collation_proto_value = Value::Proto(proto_type, cord);
  return ConstExpr::Create(resolved_collation_proto_value);
}
}  // namespace

absl::StatusOr<std::unique_ptr<AggregateArg>> Algebrizer::AlgebrizeAggregateFn(
    const VariableId& variable,
    std::optional<AnonymizationOptions> anonymization_options,
    std::unique_ptr<ValueExpr> filter, const ResolvedExpr* expr,
    const VariableId& side_effects_variable) {
  GOOGLESQL_RET_CHECK(expr->node_kind() == RESOLVED_AGGREGATE_FUNCTION_CALL ||
            expr->node_kind() == RESOLVED_ANALYTIC_FUNCTION_CALL)
      << expr->node_kind_string();
  const ResolvedNonScalarFunctionCallBase* aggregate_function =
      expr->GetAs<ResolvedNonScalarFunctionCallBase>();

  std::vector<std::unique_ptr<KeyArg>> inner_grouping_keys;
  std::vector<std::unique_ptr<AggregateArg>> inner_aggregators;
  if (aggregate_function->Is<ResolvedAggregateFunctionCall>()) {
    const ResolvedAggregateFunctionCall* aggregate_function_call =
        aggregate_function->GetAs<ResolvedAggregateFunctionCall>();
    for (const auto& computed_column :
         aggregate_function_call->group_by_list()) {
      const VariableId inner_grouping_key_variable_name =
          column_to_variable_->AssignNewVariableToColumn(
              computed_column->column());
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> inner_grouping_key,
                       AlgebrizeExpression(computed_column->expr()));
      inner_grouping_keys.push_back(std::make_unique<KeyArg>(
          inner_grouping_key_variable_name, std::move(inner_grouping_key)));
      if (CollationAnnotation::ExistsIn(
              computed_column->expr()->type_annotation_map())) {
        GOOGLESQL_RET_CHECK(computed_column->expr()->type_annotation_map() != nullptr);
        GOOGLESQL_ASSIGN_OR_RETURN(ResolvedCollation resolved_collation,
                         ResolvedCollation::MakeResolvedCollation(
                             *computed_column->expr()->type_annotation_map()));
        GOOGLESQL_ASSIGN_OR_RETURN(
            std::unique_ptr<ValueExpr> group_by_collation,
            AlgebrizeResolvedCollation(resolved_collation, type_factory_));
        inner_grouping_keys.back()->set_collation(
            std::move(group_by_collation));
      }
    }
    for (const auto& computed_column_base :
         aggregate_function_call->group_by_aggregate_list()) {
      VariableId side_effects_variable;
      if (computed_column_base->Is<ResolvedDeferredComputedColumn>()) {
        auto deferred =
            computed_column_base->GetAs<ResolvedDeferredComputedColumn>();
        side_effects_variable = column_to_variable_->AssignNewVariableToColumn(
            deferred->side_effect_column());
      }
      const VariableId inner_aggregate_column_variable_name =
          column_to_variable_->AssignNewVariableToColumn(
              computed_column_base->column());
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::unique_ptr<AggregateArg> inner_agg,
          AlgebrizeAggregateFn(inner_aggregate_column_variable_name,
                               anonymization_options, /*filter=*/nullptr,
                               computed_column_base->expr(),
                               side_effects_variable));
      inner_aggregators.push_back(std::move(inner_agg));
    }
  }

  std::vector<std::unique_ptr<ValueExpr>> arguments;
  const ResolvedExpr* measure_expr = nullptr;
  for (int i = 0; i < aggregate_function->argument_list_size(); ++i) {
    const ResolvedExpr* argument_expr = aggregate_function->argument_list(i);
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> argument,
                     AlgebrizeExpression(argument_expr));
    if (argument_expr->type()->IsMeasureType()) {
      GOOGLESQL_RET_CHECK(measure_expr == nullptr);
      ResolvedColumn measure_column_arg;
      if (argument_expr->Is<ResolvedColumnRef>()) {
        measure_column_arg =
            argument_expr->GetAs<ResolvedColumnRef>()->column();
      } else if (argument_expr->Is<ResolvedSubqueryExpr>()) {
        GOOGLESQL_RET_CHECK_EQ(
            argument_expr->GetAs<ResolvedSubqueryExpr>()->subquery_type(),
            ResolvedSubqueryExpr::SCALAR);
        measure_column_arg = argument_expr->GetAs<ResolvedSubqueryExpr>()
                                 ->subquery()
                                 ->column_list(0);
      } else {
        return absl::UnimplementedError(
            absl::StrCat("Unexpected argument for AGG function. Argument: ",
                         argument_expr->DebugString()));
      }

      GOOGLESQL_ASSIGN_OR_RETURN(measure_expr, measure_column_to_expr_.GetMeasureExpr(
                                         measure_column_arg));
    }
    arguments.push_back(std::move(argument));
  }

  return AlgebrizeAggregateFnWithAlgebrizedArguments(
      variable, anonymization_options, std::move(filter), expr,
      std::move(arguments), std::move(inner_grouping_keys),
      std::move(inner_aggregators), side_effects_variable, /*order_by_keys=*/{},
      measure_expr);
}

absl::StatusOr<VariableId> Algebrizer::AddUdaArgumentVariable(
    absl::string_view argument_name) {
  GOOGLESQL_RET_CHECK(!aggregate_args_map_.contains(argument_name));
  VariableId variable =
      variable_gen_->GetNewVariableName(std::string(argument_name));
  aggregate_args_map_[argument_name] = variable;
  return variable;
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeUdaOrMeasureCall(
    const AnonymizationOptions* anonymization_options,
    const ResolvedExpr& function_expr,
    const std::vector<const ResolvedExpr*>& aggregate_exprs,
    const ResolvedColumnList& aggregate_expr_columns,
    std::function<absl::StatusOr<std::unique_ptr<RelationalOp>>(Algebrizer&)>
        create_input_op,
    const LanguageOptions& language_options,
    const AlgebrizerOptions& algebrizer_options, TypeFactory* type_factory) {
  Parameters parameters;
  ParameterMap column_map;
  SystemVariablesAlgebrizerMap system_variables_map;
  Algebrizer algebrizer(language_options, algebrizer_options, type_factory,
                        &parameters, &column_map, &system_variables_map);

  // Construct the input op for the UDA or Measure call.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> input,
                   create_input_op(algebrizer));

  // Algebrize each aggregate expression and assign new variables.
  GOOGLESQL_RET_CHECK_EQ(aggregate_exprs.size(), aggregate_expr_columns.size());
  std::vector<std::unique_ptr<AggregateArg>> algebrized_aggregate_exprs;
  algebrized_aggregate_exprs.reserve(aggregate_exprs.size());
  for (int i = 0; i < aggregate_exprs.size(); ++i) {
    const VariableId agg_variable_name =
        algebrizer.column_to_variable_->AssignNewVariableToColumn(
            aggregate_expr_columns[i]);
    std::optional<AnonymizationOptions> anonymization_opts;
    if (anonymization_options != nullptr) {
      anonymization_opts = *anonymization_options;
    }
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<AggregateArg> agg,
                     algebrizer.AlgebrizeAggregateFn(
                         agg_variable_name, anonymization_opts,
                         /*filter=*/nullptr, aggregate_exprs[i],
                         /*side_effects_variable=*/VariableId()));
    algebrized_aggregate_exprs.push_back(std::move(agg));
  }

  // Construct an AggregateOp that computes all the top-level aggregates
  // in the UDA or measure.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<AggregateOp> agg_op,
                   AggregateOp::Create(
                       /*keys=*/{}, std::move(algebrized_aggregate_exprs),
                       std::move(input), /*grouping_sets=*/{}));

  // Construct a compute op which augments the output of AggregateOp with an
  // additional entry representing the algebrized UDA function expression.
  std::vector<std::unique_ptr<ExprArg>> arguments;
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> algebrized_tree,
                   algebrizer.AlgebrizeExpression(&function_expr));
  arguments.push_back(std::make_unique<ExprArg>(std::move(algebrized_tree)));
  GOOGLESQL_ASSIGN_OR_RETURN(auto compute_op,
                   ComputeOp::Create(std::move(arguments), std::move(agg_op)));

  return compute_op;
}

absl::StatusOr<std::unique_ptr<AggregateFunctionBody>>
Algebrizer::CreateCallbackUserDefinedAggregateFn(
    const ResolvedNonScalarFunctionCallBase* aggregate_function,
    const AnonymizationOptions* anonymization_options) {
  // Check the evaluator against the function signature to make sure it's
  // not null.
  auto callback =
      aggregate_function->function()->GetAggregateFunctionEvaluatorFactory();
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<AggregateFunctionEvaluator> evaluator,
                   callback(aggregate_function->signature()));

  const std::string name = aggregate_function->function()->FullName(false);
  GOOGLESQL_RET_CHECK(evaluator != nullptr)
      << "NULL evaluator returned for user-defined aggregate function " << name;

  return MakeUserDefinedAggregateFunction(
      callback, aggregate_function->signature(), type_factory_, name,
      /*ignores_null=*/evaluator->IgnoresNulls());
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::CreateInputOpForUdaCall(
    absl::Span<const UdaArgumentInfo> argument_infos) {
  std::vector<VariableId> variables;
  for (const UdaArgumentInfo& argument_info : argument_infos) {
    GOOGLESQL_ASSIGN_OR_RETURN(VariableId variable,
                     AddUdaArgumentVariable(argument_info.argument_name));
    variables.push_back(variable);
  }
  return RowsForUdaOp::Create(std::move(variables));
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::CreateInputOpForMeasureCall(const MeasureType* measure_type) {
  GOOGLESQL_RET_CHECK(!measure_variable_.is_valid());
  GOOGLESQL_RET_CHECK(measure_type_ == nullptr);
  measure_type_ = measure_type;
  measure_variable_ = variable_gen_->GetNewVariableName("measure");
  return GrainLockingOp::Create(measure_variable_);
}

absl::StatusOr<std::unique_ptr<AggregateFunctionBody>>
Algebrizer::CreateTemplatedUserDefinedAggregateFn(
    const ResolvedNonScalarFunctionCallBase* aggregate_function,
    std::vector<std::unique_ptr<ValueExpr>>& arguments,
    const AnonymizationOptions* anonymization_options) {
  const TemplatedSQLFunction* templated_sql_function =
      aggregate_function->function()->GetAs<TemplatedSQLFunction>();
  GOOGLESQL_RET_CHECK(aggregate_function->Is<ResolvedAggregateFunctionCall>());
  const ResolvedAggregateFunctionCall* aggregate_function_call =
      aggregate_function->GetAs<ResolvedAggregateFunctionCall>();
  GOOGLESQL_RET_CHECK(aggregate_function_call->function_call_info()
                ->Is<TemplatedSQLFunctionCall>());

  const ResolvedExpr* function_expr =
      aggregate_function_call->function_call_info()
          ->GetAs<TemplatedSQLFunctionCall>()
          ->expr();

  std::vector<const ResolvedExpr*> aggregate_exprs;
  ResolvedColumnList aggregate_expr_columns;
  for (const std::unique_ptr<const ResolvedComputedColumn>& agg_expr :
       aggregate_function_call->function_call_info()
           ->GetAs<TemplatedSQLFunctionCall>()
           ->aggregate_expression_list()) {
    aggregate_exprs.push_back(agg_expr->expr());
    aggregate_expr_columns.push_back(agg_expr->column());
  }

  GOOGLESQL_RET_CHECK_EQ(templated_sql_function->GetArgumentNames().size(),
               aggregate_function->signature().arguments().size());
  std::vector<UdaArgumentInfo> argument_infos;
  for (int i = 0; i < templated_sql_function->GetArgumentNames().size(); ++i) {
    bool is_aggregate = !aggregate_function->signature()
                             .arguments()[i]
                             .options()
                             .is_not_aggregate();
    argument_infos.push_back(
        {.argument_name = templated_sql_function->GetArgumentNames()[i],
         .is_aggregate = is_aggregate,
         .expr = is_aggregate ? nullptr : arguments[i].get()});
  }

  // Create a custom UDA evaluator
  const FunctionSignature& signature = aggregate_function->signature();
  const LanguageOptions& language_options = this->language_options_;
  const AlgebrizerOptions& algebrizer_options = this->algebrizer_options_;
  TypeFactory* type_factory = this->type_factory_;
  AggregateFunctionEvaluatorFactory AggregateFn =
      [signature, anonymization_options, function_expr, aggregate_exprs,
       aggregate_expr_columns, argument_infos = std::move(argument_infos),
       language_options, algebrizer_options,
       type_factory](const FunctionSignature& sig)
      -> absl::StatusOr<std::unique_ptr<AggregateFunctionEvaluator>> {
    auto create_input_op_callback = [argument_infos](Algebrizer& algebrizer) {
      return algebrizer.CreateInputOpForUdaCall(argument_infos);
    };
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> algebrized_uda,
                     AlgebrizeUdaOrMeasureCall(
                         anonymization_options, *function_expr, aggregate_exprs,
                         aggregate_expr_columns, create_input_op_callback,
                         language_options, algebrizer_options, type_factory));
    return MakeUserDefinedAggregateFunctionEvaluator(std::move(algebrized_uda),
                                                     std::move(argument_infos));
  };

  const std::string name = aggregate_function->function()->FullName(false);
  return MakeUserDefinedAggregateFunction(
      AggregateFn, aggregate_function->signature(), type_factory_, name,
      /*ignores_null=*/false);
}

absl::StatusOr<std::unique_ptr<AggregateFunctionBody>>
Algebrizer::CreateNonTemplatedUserDefinedAggregateFn(
    const ResolvedNonScalarFunctionCallBase* aggregate_function,
    std::vector<std::unique_ptr<ValueExpr>>& arguments,
    const AnonymizationOptions* anonymization_options) {
  const SQLFunction* sql_function =
      aggregate_function->function()->GetAs<SQLFunction>();
  std::vector<const ResolvedExpr*> aggregate_exprs;
  ResolvedColumnList aggregate_expr_columns;
  for (const std::unique_ptr<const ResolvedComputedColumn>& agg_expr :
       *sql_function->aggregate_expression_list()) {
    aggregate_exprs.push_back(agg_expr->expr());
    aggregate_expr_columns.push_back(agg_expr->column());
  }
  const ResolvedExpr* function_expr = sql_function->FunctionExpression();

  GOOGLESQL_RET_CHECK_EQ(sql_function->GetArgumentNames().size(),
               aggregate_function->signature().arguments().size());
  std::vector<UdaArgumentInfo> argument_infos;
  for (int i = 0; i < sql_function->GetArgumentNames().size(); ++i) {
    bool is_aggregate = !aggregate_function->signature()
                             .arguments()[i]
                             .options()
                             .is_not_aggregate();
    argument_infos.push_back(
        {.argument_name = sql_function->GetArgumentNames()[i],
         .is_aggregate = is_aggregate,
         .expr = is_aggregate ? nullptr : arguments[i].get()});
  }

  // Create a custom UDA evaluator
  const FunctionSignature& signature = aggregate_function->signature();
  const LanguageOptions& language_options = this->language_options_;
  const AlgebrizerOptions& algebrizer_options = this->algebrizer_options_;
  TypeFactory* type_factory = this->type_factory_;
  AggregateFunctionEvaluatorFactory AggregateFn =
      [signature, anonymization_options, function_expr, aggregate_exprs,
       aggregate_expr_columns, argument_infos = std::move(argument_infos),
       language_options, algebrizer_options,
       type_factory](const FunctionSignature& sig)
      -> absl::StatusOr<std::unique_ptr<AggregateFunctionEvaluator>> {
    auto create_input_op_callback = [argument_infos](Algebrizer& algebrizer) {
      return algebrizer.CreateInputOpForUdaCall(argument_infos);
    };
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> algebrized_uda,
                     AlgebrizeUdaOrMeasureCall(
                         anonymization_options, *function_expr, aggregate_exprs,
                         aggregate_expr_columns, create_input_op_callback,
                         language_options, algebrizer_options, type_factory));
    return MakeUserDefinedAggregateFunctionEvaluator(std::move(algebrized_uda),
                                                     std::move(argument_infos));
  };

  const std::string name = aggregate_function->function()->FullName(false);
  return MakeUserDefinedAggregateFunction(
      AggregateFn, aggregate_function->signature(), type_factory_, name,
      /*ignores_null=*/false);
}

absl::StatusOr<std::unique_ptr<AggregateFunctionBody>>
Algebrizer::CreateUserDefinedAggregateFn(
    const ResolvedNonScalarFunctionCallBase* aggregate_function,
    std::vector<std::unique_ptr<ValueExpr>>& arguments,
    const AnonymizationOptions* anonymization_options) {
  if (aggregate_function->function()->GetAggregateFunctionEvaluatorFactory() !=
      nullptr) {
    return CreateCallbackUserDefinedAggregateFn(aggregate_function,
                                                anonymization_options);
  }
  if (aggregate_function->function()->Is<SQLFunction>()) {
    return CreateNonTemplatedUserDefinedAggregateFn(
        aggregate_function, arguments, anonymization_options);
  }
  if (aggregate_function->function()->Is<TemplatedSQLFunction>()) {
    return CreateTemplatedUserDefinedAggregateFn(aggregate_function, arguments,
                                                 anonymization_options);
  }
  return ::googlesql_base::InvalidArgumentErrorBuilder()
         << "User-defined function "
         << aggregate_function->function()->FullName(false)
         << " has no evaluator. "
         << "Use FunctionOptions to supply one.";
}

absl::StatusOr<std::unique_ptr<AggregateFunctionBody>>
Algebrizer::CreateMeasureAggregateFn(
    const ResolvedNonScalarFunctionCallBase* aggregate_function,
    std::vector<std::unique_ptr<ValueExpr>>& arguments,
    const ResolvedExpr* measure_expr) {
  GOOGLESQL_RET_CHECK_NE(measure_expr, nullptr);
  GOOGLESQL_RET_CHECK_EQ(arguments.size(), 1);
  GOOGLESQL_RET_CHECK(
      !aggregate_function->function()->GetAggregateFunctionEvaluatorFactory());
  const ValueExpr* measure_expr_arg = arguments[0].get();
  GOOGLESQL_RET_CHECK(measure_expr_arg->output_type()->IsMeasureType());
  const MeasureType* measure_type =
      measure_expr_arg->output_type()->AsMeasure();

  const LanguageOptions& language_options = this->language_options_;
  const AlgebrizerOptions& algebrizer_options = this->algebrizer_options_;
  TypeFactory* type_factory = this->type_factory_;

  // Create a lambda function to algebrize the measure expression and returns an
  // `AggregateFunctionEvaluator`. It's worth noting that this lambda function
  // can be invoked multiple times, and may be called after the algebrizer is
  // destroyed. Hence, any captured variables must be copied, or be point to
  // objects that outlive the algebrizer.
  AggregateFunctionEvaluatorFactory measure_aggregate_fn =
      [measure_expr, measure_type, language_options, algebrizer_options,
       type_factory](const FunctionSignature& unused)
      -> absl::StatusOr<std::unique_ptr<AggregateFunctionEvaluator>> {
    if (!algebrizer_options.max_seen_column_id) {
      return absl::InvalidArgumentError(
          "max_seen_column_id must be initialized in order to algebrize "
          "measure expressions");
    }
    if (*algebrizer_options.max_seen_column_id <= 0) {
      return absl::InvalidArgumentError("max_seen_column_id must be positive");
    }
    // Use a local `IdStringPool` and `ColumnFactory` to avoid lifetime issues.
    std::shared_ptr<googlesql_base::UnsafeArena> arena =
        std::make_shared<googlesql_base::UnsafeArena>(/*block_size=*/4096);
    IdStringPool id_pool(arena);
    googlesql_base::SequenceNumber sequence_number;
    // Use a starting column id greater than the max seen column id
    // to avoid conflicts with other columns.
    ColumnFactory column_factory(*algebrizer_options.max_seen_column_id,
                                 id_pool, sequence_number);

    // Copy the measure expression, and remap column ids within it to use new
    // column ids allocated by `column_factory`. Since the measure expression
    // was analyzed in a different context, it's column ids will be invalid in
    // the current query.
    ColumnReplacementMap column_replacement_map;
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> rewritten_measure_expr,
        CopyResolvedASTAndRemapColumns(*measure_expr, column_factory,
                                       column_replacement_map));

    std::vector<std::unique_ptr<const ResolvedComputedColumnBase>>
        extracted_aggregates;
    GOOGLESQL_ASSIGN_OR_RETURN(
        rewritten_measure_expr,
        ExtractTopLevelAggregates(std::move(rewritten_measure_expr),
                                  extracted_aggregates, column_factory));
    // Update the max seen column id to the max column id allocated by
    // `column_factory`.
    *algebrizer_options.max_seen_column_id = column_factory.max_column_id();

    // The algebrization code takes raw pointers to resolved aggregate
    // functions and their corresponding columns. Extract them from the
    // `extracted_aggregates` vector.
    std::vector<const ResolvedExpr*> extracted_aggregates_raw_ptrs;
    ResolvedColumnList extracted_aggregates_columns;
    for (const std::unique_ptr<const ResolvedComputedColumnBase>& agg :
         extracted_aggregates) {
      extracted_aggregates_raw_ptrs.push_back(agg->expr());
      extracted_aggregates_columns.push_back(agg->column());
    }

    auto create_grain_locking_op = [measure_type](Algebrizer& algebrizer) {
      return algebrizer.CreateInputOpForMeasureCall(measure_type);
    };

    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> algebrized_measure,
                     AlgebrizeUdaOrMeasureCall(
                         /*anonymization_options=*/nullptr,
                         *rewritten_measure_expr, extracted_aggregates_raw_ptrs,
                         extracted_aggregates_columns, create_grain_locking_op,
                         language_options, algebrizer_options, type_factory));

    return MakeUserDefinedAggregateFunctionEvaluator(
        std::move(algebrized_measure), /*argument_infos=*/{});
  };

  return MakeUserDefinedAggregateFunction(
      measure_aggregate_fn, aggregate_function->signature(), type_factory_,
      aggregate_function->function()->FullName(false), /*ignores_null=*/false);
}

namespace {

// Converts the AnonymizationOptions to a STRUCT value. The STRUCT contains
// option values that will be used when evaluating anonymization aggregate
// functions. Fields should be accessed via their case-sensitive names.
absl::StatusOr<Value> AnonymizationOptionsToStruct(
    const AnonymizationOptions& anonymization_options,
    TypeFactory* type_factory) {
  const StructType* anon_struct_type;
  GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeStructType(
      {
          {"epsilon", types::DoubleType()},
          {"max_groups_contributed", types::Int64Type()},
      },
      &anon_struct_type));
  return Value::MakeStruct(
      anon_struct_type,
      {
          anonymization_options.epsilon.value_or(Value::NullDouble()),
          anonymization_options.max_groups_contributed.value_or(
              Value::NullInt64()),
      });
}

absl::Status RemoveAnonymizationEpsilonNamedArgument(
    const FunctionSignature& signature,
    std::vector<std::unique_ptr<ValueExpr>>& arguments) {
  GOOGLESQL_RET_CHECK_EQ(signature.arguments().size(), arguments.size());
  auto signature_it = signature.arguments().begin();
  auto arguments_it = arguments.begin();
  while (signature_it != signature.arguments().end() &&
         arguments_it != arguments.end()) {
    if ((*signature_it).has_argument_name() &&
        googlesql_base::CaseEqual((*signature_it).argument_name(), "epsilon")) {
      arguments.erase(arguments_it);
      return absl::OkStatus();
    }
    ++signature_it;
    ++arguments_it;
  }
  return absl::OkStatus();
}

bool IsCountDistinctAggFunctionWithStringArg(const FunctionKind& kind) {
  return kind == FunctionKind::kApproxCountDistinct;
}

absl::StatusOr<std::unique_ptr<ScalarFunctionCallExpr>> CreateConjunction(
    std::unique_ptr<ValueExpr> left, std::unique_ptr<ValueExpr> right,
    const LanguageOptions& language_options) {
  GOOGLESQL_RET_CHECK(left->output_type()->IsBool());
  GOOGLESQL_RET_CHECK(right->output_type()->IsBool());
  std::vector<std::unique_ptr<ValueExpr>> and_args;
  and_args.push_back(std::move(left));
  and_args.push_back(std::move(right));
  return BuiltinScalarFunction::CreateCall(
      FunctionKind::kAnd, language_options, BoolType(),
      ConvertValueExprsToAlgebraArgs(std::move(and_args)),
      ResolvedFunctionCallBase::DEFAULT_ERROR_MODE);
}

}  // namespace

absl::StatusOr<std::unique_ptr<ValueExpr>>
Algebrizer::AlgebrizeAggregateWhereFilterExpr(
    const ResolvedExpr* expr, std::unique_ptr<ValueExpr> filter) {
  if (expr->Is<ResolvedAnalyticFunctionCall>() ||
      expr->Is<ResolvedAggregateFunctionCall>()) {
    const ResolvedExpr* where_expr =
        expr->GetAs<ResolvedNonScalarFunctionCallBase>()->where_expr();
    if (where_expr != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(auto local_filter, AlgebrizeExpression(where_expr));
      if (filter == nullptr) {
        return local_filter;
      } else {
        return CreateConjunction(std::move(filter), std::move(local_filter),
                                 language_options_);
      }
    }
  }
  return filter;
}

absl::StatusOr<std::unique_ptr<ValueExpr>>
Algebrizer::AlgebrizeAggregateHavingFilterExpr(const ResolvedExpr* expr) {
  if (expr->Is<ResolvedAggregateFunctionCall>()) {
    const ResolvedAggregateFunctionCall* aggregate_func =
        expr->GetAs<ResolvedAggregateFunctionCall>();
    if (aggregate_func->having_expr() != nullptr) {
      return AlgebrizeExpression(aggregate_func->having_expr());
    }
  }
  return nullptr;
}

absl::StatusOr<std::unique_ptr<AggregateArg>>
Algebrizer::AlgebrizeAggregateFnWithAlgebrizedArguments(
    const VariableId& variable,
    std::optional<AnonymizationOptions> anonymization_options,
    std::unique_ptr<ValueExpr> filter, const ResolvedExpr* expr,
    std::vector<std::unique_ptr<ValueExpr>> arguments,
    std::vector<std::unique_ptr<KeyArg>> inner_grouping_keys,
    std::vector<std::unique_ptr<AggregateArg>> inner_aggregators,
    const VariableId& side_effects_variable,
    std::vector<std::unique_ptr<KeyArg>> order_by_keys,
    const ResolvedExpr* measure_expr) {
  GOOGLESQL_RET_CHECK(expr->node_kind() == RESOLVED_AGGREGATE_FUNCTION_CALL ||
            expr->node_kind() == RESOLVED_ANALYTIC_FUNCTION_CALL)
      << expr->node_kind_string();
  const ResolvedNonScalarFunctionCallBase* aggregate_function =
      expr->GetAs<ResolvedNonScalarFunctionCallBase>();
  const std::string name = aggregate_function->function()->FullName(false);

  if (anonymization_options.has_value()) {
    // Overwrite `epsilon` for this specific call.
    if (anonymization_options->epsilon_assigner != nullptr) {
      const ResolvedFunctionCallBase* function_call =
          expr->GetAs<ResolvedFunctionCallBase>();
      GOOGLESQL_ASSIGN_OR_RETURN(
          double epsilon,
          anonymization_options->epsilon_assigner->GetEpsilonForFunction(
              function_call));
      anonymization_options->epsilon = Value::Double(epsilon);
      // The FunctionEpsilonAssigner combined the `epsilon` argument on the
      // aggregation and the `epsilon` option on the DP scan. We therefore
      // remove the epsilon named argument here as the effective epsilon will be
      // added to the struct that is appended to the arguments list.
      GOOGLESQL_RETURN_IF_ERROR(RemoveAnonymizationEpsilonNamedArgument(
          function_call->signature(), arguments));
    }

    // Append struct containing `epsilon` and `max_groups_contributed` to the
    // arguments.
    GOOGLESQL_ASSIGN_OR_RETURN(Value anon_options_struct,
                     AnonymizationOptionsToStruct(anonymization_options.value(),
                                                  type_factory_));
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> arg,
                     ConstExpr::Create(anon_options_struct));
    arguments.push_back(std::move(arg));
  }

  FunctionKind kind;
  int num_input_fields;
  if (aggregate_function->function()->IsGoogleSQLBuiltin()) {
    if (name == "$count_star") {
      kind = FunctionKind::kCount;
      num_input_fields = 0;
    } else {
      GOOGLESQL_ASSIGN_OR_RETURN(kind, BuiltinFunctionCatalog::GetKindByName(name));
      switch (kind) {
        case FunctionKind::kApproxTopSum:
        case FunctionKind::kCorr:
        case FunctionKind::kCovarPop:
        case FunctionKind::kCovarSamp:
          num_input_fields = 2;
          break;
        default:
          num_input_fields = 1;
          break;
      }
    }
  } else {
    // User defined functions
    kind = FunctionKind::kInvalid;
    num_input_fields = static_cast<int>(arguments.size());
  }
  GOOGLESQL_RET_CHECK_LE(num_input_fields, arguments.size()) << name;

  const Type* input_type;
  switch (num_input_fields) {
    case 0:
      input_type = types::EmptyStructType();
      break;
    case 1:
      input_type = arguments[0]->output_type();
      break;
    default: {
      std::vector<StructType::StructField> fields;
      fields.reserve(num_input_fields);
      for (int i = 0; i < num_input_fields; ++i) {
        fields.push_back({"", arguments[i]->output_type()});
      }
      const StructType* struct_type;
      GOOGLESQL_RET_CHECK_OK(type_factory_->MakeStructType(fields, &struct_type));
      input_type = struct_type;
      break;
    }
  }

  std::unique_ptr<ValueExpr> having_expr;
  AggregateArg::HavingModifierKind having_kind = AggregateArg::kHavingNone;
  std::unique_ptr<ValueExpr> limit;
  if (expr->node_kind() == RESOLVED_AGGREGATE_FUNCTION_CALL) {
    // TODO: make sure keys are correct here
    const ResolvedAggregateFunctionCall* resolved_aggregate_func =
        expr->GetAs<ResolvedAggregateFunctionCall>();

    if (resolved_aggregate_func->having_modifier() != nullptr &&
        resolved_aggregate_func->having_modifier()->having_expr() != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          having_expr,
          AlgebrizeExpression(
              resolved_aggregate_func->having_modifier()->having_expr()));
      if (resolved_aggregate_func->having_modifier()->kind() ==
          ResolvedAggregateHavingModifier::MAX) {
        having_kind = AggregateArg::kHavingMax;
      } else {
        having_kind = AggregateArg::kHavingMin;
      }
    }

    // TODO: Support sort keys with collation for aggregate
    // functions.
    GOOGLESQL_RET_CHECK(order_by_keys.empty() ||
              resolved_aggregate_func->order_by_item_list().empty())
        << "Both order_by_by_key_override and order_by_item_list cannot be "
        << "non-empty.";
    if (!resolved_aggregate_func->order_by_item_list().empty()) {
      absl::flat_hash_map<int, VariableId> column_to_id_map;
      // It is safe to remove correlated column references, because they
      // are constant and do not affect the order of the input to
      // the aggregate function.
      GOOGLESQL_RETURN_IF_ERROR(AlgebrizeOrderByItems(
          true /* drop_correlated_columns */, false /* create_new_ids */,
          resolved_aggregate_func->order_by_item_list(), &column_to_id_map,
          &order_by_keys));
    }
    if (resolved_aggregate_func->limit() != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(limit,
                       AlgebrizeExpression(resolved_aggregate_func->limit()));
    }
  }

  // Algebrize the WHERE filtering expression for the aggregate function call.
  GOOGLESQL_ASSIGN_OR_RETURN(filter,
                   AlgebrizeAggregateWhereFilterExpr(expr, std::move(filter)));
  // Algebrize the HAVING filtering expression for the aggregate function call.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> having_filter,
                   AlgebrizeAggregateHavingFilterExpr(expr));

  std::unique_ptr<AggregateFunctionBody> function;
  const Type* type = aggregate_function->type();

  // Extended types are not supported in the reference implementation for
  // builtin functions. Set kind to kInvalid to use the provided evaluator.
  if (IsBuiltinFunctionWithExtendedTypeEvaluator(aggregate_function)) {
    kind = FunctionKind::kInvalid;
  }

  switch (kind) {
    case FunctionKind::kInvalid: {
      // User defined functions
      const AnonymizationOptions* anonymization_options_ptr = nullptr;
      if (anonymization_options.has_value()) {
        anonymization_options_ptr = &(*anonymization_options);
      }
      GOOGLESQL_ASSIGN_OR_RETURN(
          function, CreateUserDefinedAggregateFn(aggregate_function, arguments,
                                                 anonymization_options_ptr));
      break;
    }
    case FunctionKind::kMeasureAgg: {
      // Measure AGG function. Requires special handling.
      GOOGLESQL_RET_CHECK(!anonymization_options.has_value());
      GOOGLESQL_ASSIGN_OR_RETURN(function,
                       CreateMeasureAggregateFn(aggregate_function, arguments,
                                                measure_expr));
      break;
    }
    case FunctionKind::kCorr:
    case FunctionKind::kCovarPop:
    case FunctionKind::kCovarSamp:
      {
        function = std::make_unique<BinaryStatFunction>(kind, type, input_type);
        break;
      }
    default: {
      GOOGLESQL_RET_CHECK(aggregate_function->function()->IsGoogleSQLBuiltin());
      function = std::make_unique<BuiltinAggregateFunction>(
          kind, type, num_input_fields, input_type,
          IgnoresNullArguments(aggregate_function));
      break;
    }
  }

  const AggregateArg::Distinctness distinctness =
      (aggregate_function->distinct() ||
       IsCountDistinctAggFunctionWithStringArg(kind))
          ? AggregateArg::kDistinct
          : AggregateArg::kAll;

  if (!aggregate_function->collation_list().empty() &&
      distinctness != AggregateArg::kDistinct && kind != FunctionKind::kMin &&
      kind != FunctionKind::kMax) {
    return ::googlesql_base::InvalidArgumentErrorBuilder()
           << "Collation is not supported for aggregate function " << name
           << " without DISTINCT";
  }

  return AggregateArg::Create(
      variable, std::move(function), std::move(arguments), distinctness,
      std::move(having_expr), having_kind, std::move(order_by_keys),
      std::move(limit), /*group_rows_subquery=*/nullptr,
      std::move(inner_grouping_keys), std::move(inner_aggregators),
      aggregate_function->error_mode(), std::move(filter),
      std::move(having_filter), aggregate_function->collation_list(),
      side_effects_variable);
}

absl::StatusOr<std::unique_ptr<NewStructExpr>> Algebrizer::AlgebrizeMakeStruct(
    const ResolvedMakeStruct* make_struct) {
  ABSL_DCHECK(make_struct->type()->IsStruct());
  const StructType* struct_type = make_struct->type()->AsStruct();

  // Build a list of arguments.
  std::vector<std::unique_ptr<ExprArg>> arguments;
  ABSL_DCHECK_EQ(struct_type->num_fields(), make_struct->field_list_size());
  for (int i = 0; i < struct_type->num_fields(); ++i) {
    const ResolvedExpr* field_expr = make_struct->field_list()[i].get();
    ABSL_DCHECK(field_expr->type()->Equals(struct_type->field(i).type));
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> algebrized_field_expr,
                     AlgebrizeExpression(field_expr));
    // Record the field value.
    arguments.push_back(
        std::make_unique<ExprArg>(std::move(algebrized_field_expr)));
  }
  // Build the row value.
  return NewStructExpr::Create(struct_type, std::move(arguments));
}

absl::StatusOr<std::unique_ptr<FieldValueExpr>>
Algebrizer::AlgebrizeGetStructField(
    const ResolvedGetStructField* get_struct_field) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> value,
                   AlgebrizeExpression(get_struct_field->expr()));
  return FieldValueExpr::Create(get_struct_field->field_idx(),
                                std::move(value));
}

// Convert the GetJsonField into a JSON_QUERY function call.
absl::StatusOr<std::unique_ptr<ScalarFunctionCallExpr>>
Algebrizer::AlgebrizeGetJsonField(const ResolvedGetJsonField* get_json_field) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> input,
                   AlgebrizeExpression(get_json_field->expr()));
  std::string json_path =
      absl::StrCat("$.", functions::ConvertJSONPathTokenToSqlStandardMode(
                             get_json_field->field_name()));

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ConstExpr> json_path_expr,
                   ConstExpr::Create(googlesql::values::String(json_path)));

  std::vector<std::unique_ptr<ValueExpr>> arguments;
  arguments.push_back(std::move(input));
  arguments.push_back(std::move(json_path_expr));

  return BuiltinScalarFunction::CreateCall(
      FunctionKind::kJsonQuery, language_options_, get_json_field->type(),
      ConvertValueExprsToAlgebraArgs(std::move(arguments)),
      ResolvedFunctionCallBase::DEFAULT_ERROR_MODE);
}

static ProtoFieldAccessInfo CreateProtoFieldAccessInfo(
    const ResolvedGetProtoField& get_proto_field) {
  ProtoFieldAccessInfo info;
  info.field_info.descriptor = get_proto_field.field_descriptor();
  info.field_info.format = get_proto_field.format();
  info.field_info.type = get_proto_field.type();
  info.field_info.get_has_bit = get_proto_field.get_has_bit();
  info.field_info.default_value = get_proto_field.default_value();

  info.return_default_value_when_unset =
      get_proto_field.return_default_value_when_unset();

  return info;
}

absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeGetProtoField(
    const ResolvedGetProtoField* get_proto_field) {
  // Represent 'get_proto_field' as 'base_expression'.'path[0]'.'path[1]'. ...
  std::vector<
      std::variant<const ResolvedGetProtoField*, const ResolvedGetStructField*>>
      path;
  const ResolvedExpr* base_expression = get_proto_field;
  while (base_expression->node_kind() == RESOLVED_GET_PROTO_FIELD ||
         base_expression->node_kind() == RESOLVED_GET_STRUCT_FIELD) {
    // Build 'path' in reverse order as we traverse down 'base_expression'.
    if (base_expression->node_kind() == RESOLVED_GET_PROTO_FIELD) {
      const ResolvedGetProtoField* get_field =
          base_expression->GetAs<ResolvedGetProtoField>();
      path.push_back(get_field);
      base_expression = get_field->expr();
    } else {
      GOOGLESQL_RET_CHECK_EQ(base_expression->node_kind(), RESOLVED_GET_STRUCT_FIELD);
      const ResolvedGetStructField* get_field =
          base_expression->GetAs<ResolvedGetStructField>();
      path.push_back(get_field);
      base_expression = get_field->expr();
    }
  }
  // Put 'path' in the correct order.
  std::reverse(path.begin(), path.end());

  const ResolvedNodeKind node_kind = base_expression->node_kind();
  if (algebrizer_options_.consolidate_proto_field_accesses) {
    switch (node_kind) {
      case RESOLVED_COLUMN_REF:
        // In this case we can optimize by caching. See GetProtoFieldExpr in
        // operator.h for details
      case RESOLVED_PARAMETER:
        // Another case where we can optimize.
      case RESOLVED_EXPRESSION_COLUMN:
        // Essentially the same thing as a parameter.        .
        return AlgebrizeGetProtoFieldOfPath(base_expression, path);
      default:
        break;
    }
  }

  // No optimization. Just produce a chain of GetProtoFieldExpr nodes on top of
  // 'base_expression.<struct_fields>' with no opportunity to share state with
  // other chains. In particular, this means that for a query like: SELECT
  // proto.repeated_field[0].field, proto.repeated_field[0].field, we will only
  // extract proto.repeated_field once, but we will extract the "[0].field"
  // portion twice.
  //
  // To do this, first we write the field path as
  // base_expression.<struct_fields>.<proto_fields>, and then we set
  // 'base_expression' to point to base_expression.<struct_fields>. Then we
  // handle <proto_fields> below.
  std::vector<const ResolvedGetProtoField*> proto_field_path;
  proto_field_path.reserve(path.size());
  bool seen_get_proto_field = false;
  for (const auto& get_field : path) {
    if (seen_get_proto_field) {
      GOOGLESQL_RET_CHECK(
          std::holds_alternative<const ResolvedGetProtoField*>(get_field));
      proto_field_path.push_back(
          std::get<const ResolvedGetProtoField*>(get_field));
    } else if (std::holds_alternative<const ResolvedGetProtoField*>(
                   get_field)) {
      seen_get_proto_field = true;
      proto_field_path.push_back(
          std::get<const ResolvedGetProtoField*>(get_field));
    } else {
      GOOGLESQL_RET_CHECK(
          std::holds_alternative<const ResolvedGetStructField*>(get_field));
      base_expression = std::get<const ResolvedGetStructField*>(get_field);
    }
  }
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> base_value_expr,
                   AlgebrizeExpression(base_expression));

  std::unique_ptr<GetProtoFieldExpr> last_get_proto_field_expr;
  bool first = true;
  for (const ResolvedGetProtoField* get_proto_field : proto_field_path) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ProtoFieldRegistry> registry,
                     MakeProtoFieldRegistry(/*id=*/std::nullopt));
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<ProtoFieldReader> field_reader,
        MakeProtoFieldReader(
            /*id=*/std::nullopt, CreateProtoFieldAccessInfo(*get_proto_field),
            registry.get()));

    GOOGLESQL_ASSIGN_OR_RETURN(
        last_get_proto_field_expr,
        GetProtoFieldExpr::Create(first ? std::move(base_value_expr)
                                        : std::move(last_get_proto_field_expr),
                                  field_reader.get()));
    last_get_proto_field_expr->set_owned_reader(std::move(field_reader),
                                                std::move(registry));
    first = false;
  }
  return last_get_proto_field_expr;
}

absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeFlatten(
    const ResolvedFlatten* flatten) {
  flattened_arg_input_.push(std::make_unique<const Value*>());
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> expr,
                   AlgebrizeExpression(flatten->expr()));
  std::vector<std::unique_ptr<ValueExpr>> get_fields;
  for (const auto& node : flatten->get_field_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> get_field,
                     AlgebrizeExpression(node.get()));
    get_fields.push_back(std::move(get_field));
  }
  GOOGLESQL_RET_CHECK(!flattened_arg_input_.empty());
  auto flattened_arg = std::move(flattened_arg_input_.top());
  flattened_arg_input_.pop();
  return FlattenExpr::Create(flatten->type(), std::move(expr),
                             std::move(get_fields), std::move(flattened_arg));
}

absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeFlattenedArg(
    const ResolvedFlattenedArg* flattened_arg) {
  GOOGLESQL_RET_CHECK(!flattened_arg_input_.empty());
  return FlattenedArgExpr::Create(flattened_arg->type(),
                                  flattened_arg_input_.top().get());
}

absl::StatusOr<std::unique_ptr<ValueExpr>>
Algebrizer::AlgebrizeGetProtoFieldOfPath(
    const ResolvedExpr* column_or_param_expr,
    absl::Span<const std::variant<const ResolvedGetProtoField*,
                                  const ResolvedGetStructField*>>
        path) {
  SharedProtoFieldPath column_and_field_path;
  switch (column_or_param_expr->node_kind()) {
    case RESOLVED_COLUMN_REF:
      column_and_field_path.column_or_param = ColumnOrParameter(
          column_or_param_expr->GetAs<ResolvedColumnRef>()->column());
      break;
    case RESOLVED_PARAMETER:
      column_and_field_path.column_or_param =
          ColumnOrParameter(*column_or_param_expr->GetAs<ResolvedParameter>());
      break;
    case RESOLVED_EXPRESSION_COLUMN:
      column_and_field_path.column_or_param = ColumnOrParameter(
          *column_or_param_expr->GetAs<ResolvedExpressionColumn>());
      break;
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected node kind: "
                       << ResolvedNodeKind_Name(
                              column_or_param_expr->node_kind())
                       << " in AlgebrizeGetProtoFieldOfPath()";
  }

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> base_expr,
                   AlgebrizeExpression(column_or_param_expr));
  for (const auto& element : path) {
    if (std::holds_alternative<const ResolvedGetProtoField*>(element)) {
      const ResolvedGetProtoField* get_proto_field =
          std::get<const ResolvedGetProtoField*>(element);
      ProtoFieldRegistry* registry =
          googlesql_base::FindPtrOrNull(proto_field_registry_map_, column_and_field_path);
      std::unique_ptr<ProtoFieldRegistry> owned_registry;
      if (registry == nullptr) {
        GOOGLESQL_ASSIGN_OR_RETURN(owned_registry,
                         MakeProtoFieldRegistry(column_and_field_path));
        registry = owned_registry.get();
      }

      ProtoFieldAccessInfo access_info =
          CreateProtoFieldAccessInfo(*get_proto_field);
      column_and_field_path.field_path.push_back(
          ProtoOrStructField(ProtoOrStructField::PROTO_FIELD,
                             access_info.field_info.descriptor->number()));

      ProtoFieldReader* reader =
          access_info.field_info.get_has_bit
              ? nullptr
              : googlesql_base::FindPtrOrNull(get_proto_field_reader_map_,
                                   column_and_field_path);
      std::unique_ptr<ProtoFieldReader> owned_reader;
      if (reader == nullptr) {
        GOOGLESQL_ASSIGN_OR_RETURN(
            owned_reader,
            MakeProtoFieldReader(column_and_field_path, access_info, registry));
        reader = owned_reader.get();
      }

      GOOGLESQL_ASSIGN_OR_RETURN(auto new_proto_field_expr,
                       GetProtoFieldExpr::Create(std::move(base_expr), reader));
      new_proto_field_expr->set_owned_reader(std::move(owned_reader),
                                             std::move(owned_registry));
      base_expr = std::move(new_proto_field_expr);
    } else {
      GOOGLESQL_RET_CHECK(std::holds_alternative<const ResolvedGetStructField*>(element));
      const ResolvedGetStructField* get_struct_field =
          std::get<const ResolvedGetStructField*>(element);
      column_and_field_path.field_path.push_back(ProtoOrStructField(
          ProtoOrStructField::STRUCT_FIELD, get_struct_field->field_idx()));
      GOOGLESQL_ASSIGN_OR_RETURN(base_expr,
                       FieldValueExpr::Create(get_struct_field->field_idx(),
                                              std::move(base_expr)));
    }
  }

  return base_expr;
}

absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeSubqueryExpr(
    const ResolvedSubqueryExpr* subquery_expr) {
  // Access 'parameters' to suppress the resolver check for non-accessed
  // expressions.
  for (const auto& parameter : subquery_expr->parameter_list()) {
    parameter->column();
  }
  // We will restore 'column_to_variable_' after algebrizing the subquery
  // to avoid any side-effect caused by the subquery.
  absl::Cleanup restore_original_column_to_variable_map =
      [this, original_column_to_variable = column_to_variable_->map()] {
        column_to_variable_->set_map(original_column_to_variable);
      };
  GOOGLESQL_RETURN_IF_ERROR(CheckHints(subquery_expr->hint_list()));
  const ResolvedScan* scan = subquery_expr->subquery();
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> relation, AlgebrizeScan(scan));

  const ResolvedColumnList& output_columns = scan->column_list();
  switch (subquery_expr->subquery_type()) {
    case ResolvedSubqueryExpr::EXISTS: {
      // In theory, we don't need a new expression for this, and can instead do
      // some function of SingleValueExpr(LimitOp(<relation>, LIMIT 1)). The
      // problem is that if the relation has more than one row, the LimitOp will
      // mark the operation as non-deterministic which will prevent random query
      // testing of this feature.
      GOOGLESQL_ASSIGN_OR_RETURN(auto subquery_valueop,
                       ExistsExpr::Create(std::move(relation)));
      return std::unique_ptr<ValueExpr>(std::move(subquery_valueop));
    }
    case ResolvedSubqueryExpr::SCALAR: {
      // A single column which may be a struct or an array.
      ABSL_DCHECK_EQ(output_columns.size(), 1);
      const VariableId& var =
          column_to_variable_->GetVariableNameFromColumn(output_columns[0]);
      GOOGLESQL_ASSIGN_OR_RETURN(auto deref,
                       DerefExpr::Create(var, output_columns[0].type()));
      GOOGLESQL_ASSIGN_OR_RETURN(
          auto single_value_expr,
          SingleValueExpr::Create(std::move(deref), std::move(relation)));
      return std::unique_ptr<ValueExpr>(std::move(single_value_expr));
    }
    case ResolvedSubqueryExpr::ARRAY: {
      // Either a single scalar column or a struct column.
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::unique_ptr<ValueExpr> nest_expr,
          NestSingleColumnRelation(output_columns, std::move(relation),
                                   /*is_with_table=*/false));
      return nest_expr;
    }
    case ResolvedSubqueryExpr::IN:
    case ResolvedSubqueryExpr::LIKE_ANY:
    case ResolvedSubqueryExpr::LIKE_ALL:
    case ResolvedSubqueryExpr::NOT_LIKE_ANY:
    case ResolvedSubqueryExpr::NOT_LIKE_ALL: {
      GOOGLESQL_RET_CHECK_EQ(1, scan->column_list().size());
      const VariableId haystack_var =
          column_to_variable_->GetVariableNameFromColumn(
              scan->column_list()[0]);
      // Restore 'column_to_variable_' before algebrizing the IN expression,
      // because the expression cannot reference columns produced by the
      // IN subquery.
      std::move(restore_original_column_to_variable_map).Invoke();
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> in_value,
                       AlgebrizeExpression(subquery_expr->in_expr()));
      return AlgebrizeInLikeAnyLikeAllRelation(
          std::move(in_value), subquery_expr->subquery_type(), haystack_var,
          std::move(relation), subquery_expr->in_collation());
    }
    default:
      return ::googlesql_base::InternalErrorBuilder()
             << "Unknown type of resolved subquery: "
             << subquery_expr->subquery_type();
  }
}

absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeWithExpr(
    const ResolvedWithExpr* with_expr) {
  std::vector<std::unique_ptr<ExprArg>> assignments;
  for (int i = 0; i < with_expr->assignment_list_size(); ++i) {
    const VariableId assigned_var =
        column_to_variable_->AssignNewVariableToColumn(
            with_expr->assignment_list(i)->column());
    GOOGLESQL_ASSIGN_OR_RETURN(
        auto assigned_value,
        AlgebrizeExpression(with_expr->assignment_list(i)->expr()));
    assignments.emplace_back(
        std::make_unique<ExprArg>(assigned_var, std::move(assigned_value)));
    GOOGLESQL_RETURN_IF_ERROR(measure_column_to_expr_.TrackMeasureColumnsRenamedByExpr(
        with_expr->assignment_list(i)->column(),
        *with_expr->assignment_list(i)->expr()));
  }
  GOOGLESQL_ASSIGN_OR_RETURN(auto expr, AlgebrizeExpression(with_expr->expr()));
  return WithExpr::Create(std::move(assignments), std::move(expr));
}

absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeInArray(
    std::unique_ptr<ValueExpr> in_value, std::unique_ptr<ValueExpr> array_value,
    const ResolvedCollation& collation) {
  const VariableId haystack_var =
      variable_gen_->GetNewVariableName("_in_element");
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<RelationalOp> haystack_rel,
      ArrayScanOp::Create(haystack_var, VariableId(), /* position */
                          {},                         /* fields */
                          std::move(array_value)));
  return AlgebrizeInLikeAnyLikeAllRelation(
      std::move(in_value), ResolvedSubqueryExpr::IN, haystack_var,
      std::move(haystack_rel), collation);
}

// Algebrizes (lhs [IN | LIKE ANY | LIKE ALL | NOT LIKE ANY | NOT LIKE ALL]
// haystack_rel)
//
// The core logic is to transform the expression into:
// LetOp{$_needle := lhs}(
//   SingleValueExpr(_matches,
//     AggregateOp{_matches := AGG_FN( COMPARISON_EXPR )}(haystack_rel)))
//
// Where AGG_FN and COMPARISON_EXPR depend on the subquery_type:
//
// subquery_type   | AGG_FN   | COMPARISON_EXPR
// ----------------|----------|----------------------------------
// IN              | OR_AGG   | $_needle = $haystack
// LIKE_ANY        | OR_AGG   | $_needle LIKE $haystack
// LIKE_ALL        | AND_AGG  | $_needle LIKE $haystack
// NOT_LIKE_ANY    | OR_AGG   | NOT ($_needle LIKE $haystack)
// NOT_LIKE_ALL    | AND_AGG  | NOT ($_needle LIKE $haystack)
absl::StatusOr<std::unique_ptr<ValueExpr>>
Algebrizer::AlgebrizeInLikeAnyLikeAllRelation(
    std::unique_ptr<ValueExpr> lhs,
    ResolvedSubqueryExpr::SubqueryType subquery_type,
    const VariableId& haystack_var, std::unique_ptr<RelationalOp> haystack_rel,
    const ResolvedCollation& collation) {
  FunctionKind compare_fn;
  FunctionKind aggregate_fn;
  bool negate_comparison = false;
  switch (subquery_type) {
    case ResolvedSubqueryExpr::IN:
      compare_fn = FunctionKind::kEqual;
      aggregate_fn = FunctionKind::kOrAgg;
      break;
    case ResolvedSubqueryExpr::LIKE_ANY:
      compare_fn = FunctionKind::kLike;
      aggregate_fn = FunctionKind::kOrAgg;
      break;
    case ResolvedSubqueryExpr::LIKE_ALL:
      compare_fn = FunctionKind::kLike;
      aggregate_fn = FunctionKind::kAndAgg;
      break;
    case ResolvedSubqueryExpr::NOT_LIKE_ANY:
      compare_fn = FunctionKind::kLike;
      aggregate_fn = FunctionKind::kOrAgg;
      negate_comparison = true;
      break;
    case ResolvedSubqueryExpr::NOT_LIKE_ALL:
      compare_fn = FunctionKind::kLike;
      aggregate_fn = FunctionKind::kAndAgg;
      negate_comparison = true;
      break;
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected subquery_type: " << subquery_type;
      break;
  }

  const VariableId needle_var = variable_gen_->GetNewVariableName("_needle");
  const VariableId matches_var = variable_gen_->GetNewVariableName("_matches");

  GOOGLESQL_ASSIGN_OR_RETURN(auto deref_needle_var,
                   DerefExpr::Create(needle_var, lhs->output_type()));
  GOOGLESQL_ASSIGN_OR_RETURN(auto deref_haystack_var,
                   DerefExpr::Create(haystack_var, lhs->output_type()));

  std::vector<std::unique_ptr<ValueExpr>> equal_args;
  equal_args.push_back(std::move(deref_needle_var));
  equal_args.push_back(std::move(deref_haystack_var));
  if (!collation.Empty()) {
    GOOGLESQL_RET_CHECK(compare_fn == FunctionKind::kEqual);
    std::string collated_fn_name;
    std::vector<std::unique_ptr<AlgebraArg>> collated_equal_args;
    GOOGLESQL_RETURN_IF_ERROR(GetCollatedFunctionNameAndArguments(
        "$equal", ConvertValueExprsToAlgebraArgs(std::move(equal_args)),
        {collation}, language_options_, &collated_fn_name,
        &collated_equal_args));
    GOOGLESQL_RET_CHECK_EQ(collated_fn_name, "$equal");
    GOOGLESQL_ASSIGN_OR_RETURN(equal_args, ConvertAlgebraArgsToValueExprs(
                                     std::move(collated_equal_args)));
  }

  GOOGLESQL_ASSIGN_OR_RETURN(auto equality_comparison,
                   BuiltinScalarFunction::CreateCall(
                       compare_fn, language_options_, BoolType(),
                       ConvertValueExprsToAlgebraArgs(std::move(equal_args)),
                       ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));

  if (negate_comparison) {
    std::vector<std::unique_ptr<AlgebraArg>> not_args;
    not_args.push_back(
        std::make_unique<ExprArg>(std::move(equality_comparison)));
    GOOGLESQL_ASSIGN_OR_RETURN(
        equality_comparison,
        BuiltinScalarFunction::CreateCall(FunctionKind::kNot, language_options_,
                                          BoolType(), std::move(not_args)));
  }

  std::vector<std::unique_ptr<ValueExpr>> agg_func_args;
  agg_func_args.push_back(std::move(equality_comparison));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto agg_arg,
      AggregateArg::Create(matches_var,
                           std::make_unique<BuiltinAggregateFunction>(
                               aggregate_fn, BoolType(), /*num_input_fields=*/1,
                               BoolType(), false /* ignores_null */),
                           std::move(agg_func_args)));

  std::vector<std::unique_ptr<AggregateArg>> agg_args;
  agg_args.push_back(std::move(agg_arg));

  GOOGLESQL_ASSIGN_OR_RETURN(auto agg_rel,
                   AggregateOp::Create(
                       /*keys=*/{}, std::move(agg_args),
                       std::move(haystack_rel), /*grouping_sets=*/{}));

  // Create a scalar expression from the aggregate.
  GOOGLESQL_ASSIGN_OR_RETURN(auto deref_matches_var,
                   DerefExpr::Create(matches_var, BoolType()));
  GOOGLESQL_ASSIGN_OR_RETURN(auto singleton,
                   SingleValueExpr::Create(std::move(deref_matches_var),
                                           std::move(agg_rel)));

  std::vector<std::unique_ptr<ExprArg>> let_assign;
  let_assign.push_back(std::make_unique<ExprArg>(needle_var, std::move(lhs)));
  GOOGLESQL_ASSIGN_OR_RETURN(auto with_expr, WithExpr::Create(std::move(let_assign),
                                                    std::move(singleton)));
  return std::unique_ptr<ValueExpr>(std::move(with_expr));
}

absl::StatusOr<std::unique_ptr<ValueExpr>>
Algebrizer::AlgebrizeScalarArrayFunctionWithCollation(
    FunctionKind kind, const Type* output_type, absl::string_view function_name,
    std::vector<std::unique_ptr<AlgebraArg>> converted_arguments,
    absl::Span<const ResolvedCollation> collation_list) {
  GOOGLESQL_ASSIGN_OR_RETURN(CollatorList collator_list,
                   MakeCollatorList(collation_list));

  switch (kind) {
    case FunctionKind::kArrayMin:
    case FunctionKind::kArrayMax:
      return ArrayMinMaxFunction::CreateCall(
          kind, language_options_, output_type, std::move(converted_arguments),
          ResolvedFunctionCallBase::DEFAULT_ERROR_MODE,
          std::move(collator_list));
    case FunctionKind::kArrayOffset:
    case FunctionKind::kArrayFind:
    case FunctionKind::kArrayOffsets:
    case FunctionKind::kArrayFindAll:
      return ArrayFindFunctions::CreateCall(
          kind, language_options_, output_type, std::move(converted_arguments),
          ResolvedFunctionCallBase::DEFAULT_ERROR_MODE,
          std::move(collator_list));
    case FunctionKind::kArrayIncludes:
      return ArrayIncludesFunction::CreateCall(
          language_options_, std::move(converted_arguments),
          ResolvedFunctionCallBase::DEFAULT_ERROR_MODE,
          std::move(collator_list));
    case FunctionKind::kArrayIncludesAll:
    case FunctionKind::kArrayIncludesAny:
      return ArrayIncludesArrayFunction::CreateCall(
          /*require_all=*/kind == FunctionKind::kArrayIncludesAll,
          language_options_, std::move(converted_arguments),
          ResolvedFunctionCallBase::DEFAULT_ERROR_MODE,
          std::move(collator_list));

    default:
      return ::googlesql_base::UnimplementedErrorBuilder()
             << "Unhandled scalar array function in algebrizer: "
             << function_name;
  }
}

absl::StatusOr<std::unique_ptr<ValueExpr>>
Algebrizer::AlgebrizeScalarArrayFunctionWithCollation(
    FunctionKind kind, const Type* output_type, absl::string_view function_name,
    std::vector<std::unique_ptr<ValueExpr>> args,
    absl::Span<const ResolvedCollation> collation_list) {
  std::vector<std::unique_ptr<AlgebraArg>> converted_arguments;
  converted_arguments.reserve(args.size());
  for (auto& e : args) {
    converted_arguments.push_back(std::make_unique<ExprArg>(std::move(e)));
  }

  return AlgebrizeScalarArrayFunctionWithCollation(
      kind, output_type, function_name, std::move(converted_arguments),
      collation_list);
}

absl::StatusOr<std::unique_ptr<ValueExpr>>
Algebrizer::AlgebrizeStandaloneExpression(const ResolvedExpr* expr) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> value_expr,
                   AlgebrizeExpression(expr));

  // If we have any WITH clauses, create a WithExpr that binds the names of
  // subqueries to array expressions.  WITH subqueries cannot be correlated so
  // we can attach them all in one batch at the top of the query, and that will
  // ensure we run each of them exactly once.
  if (!with_subquery_let_assignments_.empty()) {
    GOOGLESQL_ASSIGN_OR_RETURN(value_expr,
                     WithExpr::Create(std::move(with_subquery_let_assignments_),
                                      std::move(value_expr)));
  }
  // Sanity check - WITH map should be cleared as WITH clauses go out of scope.
  GOOGLESQL_RET_CHECK(with_map_.empty());

  return std::move(value_expr);
}

#define ALGEBRIZE_EXPR(class_name)                        \
  {Resolved##class_name::TYPE,                            \
   [](Algebrizer* algebrizer, const ResolvedExpr* expr) { \
     return algebrizer->Algebrize##class_name(            \
         expr->GetAs<Resolved##class_name>());            \
   }}

ABSL_ATTRIBUTE_NOINLINE
const absl::flat_hash_map<ResolvedNodeKind, Algebrizer::AlgebrizeExprFunc>*
Algebrizer::InitializeExprMap() {
  static const absl::NoDestructor<
      absl::flat_hash_map<ResolvedNodeKind, Algebrizer::AlgebrizeExprFunc>>
      kAlgebrizeExprMap({
          // (broken link) start
          ALGEBRIZE_EXPR(ArgumentRef),
          ALGEBRIZE_EXPR(ArrayAggregate),
          ALGEBRIZE_EXPR(Cast),
          ALGEBRIZE_EXPR(CatalogColumnRef),
          ALGEBRIZE_EXPR(ColumnRef),
          ALGEBRIZE_EXPR(Constant),
          ALGEBRIZE_EXPR(DMLDefault),
          ALGEBRIZE_EXPR(ExpressionColumn),
          ALGEBRIZE_EXPR(FilterField),
          ALGEBRIZE_EXPR(Flatten),
          ALGEBRIZE_EXPR(FlattenedArg),
          ALGEBRIZE_EXPR(FunctionCall),
          ALGEBRIZE_EXPR(GetJsonField),
          ALGEBRIZE_EXPR(GetProtoField),
          ALGEBRIZE_EXPR(GetProtoOneof),
          ALGEBRIZE_EXPR(GetStructField),
          ALGEBRIZE_EXPR(GraphGetElementProperty),
          ALGEBRIZE_EXPR(GraphIsLabeledPredicate),
          ALGEBRIZE_EXPR(GraphMakeElement),
          ALGEBRIZE_EXPR(Literal),
          ALGEBRIZE_EXPR(MakeProto),
          ALGEBRIZE_EXPR(MakeStruct),
          ALGEBRIZE_EXPR(Parameter),
          ALGEBRIZE_EXPR(ReplaceField),
          ALGEBRIZE_EXPR(SubqueryExpr),
          ALGEBRIZE_EXPR(SystemVariable),
          ALGEBRIZE_EXPR(UpdateConstructor),
          ALGEBRIZE_EXPR(WithExpr)
          // (broken link) end
      });
  return kAlgebrizeExprMap.get();
}
#undef ALGEBRIZE_EXPR

absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeExpression(
    const ResolvedExpr* expr) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  if (!expr->type()->IsSupportedType(language_options_)) {
    return ::googlesql_base::InvalidArgumentErrorBuilder()
           << "Type not found: "
           << expr->type()->TypeName(language_options_.product_mode());
  }
  if (expr->Is<ResolvedAggregateFunctionCall>()) {
    return absl::InternalError(
        "Aggregate function call should not be "
        "algebrized as an expression");
  }

  static const auto* expr_map = InitializeExprMap();
  auto it = expr_map->find(expr->node_kind());
  if (it == expr_map->end()) {
    return ::googlesql_base::UnimplementedErrorBuilder()
           << "Unhandled node type algebrizing an expression: "
           << expr->node_kind_string();
  }
  return it->second(this, expr);
}

absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeLiteral(
    const ResolvedLiteral* literal) {
  return ConstExpr::Create(literal->value());
}

absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeConstant(
    const ResolvedConstant* resolved_constant) {
  const Constant* constant = resolved_constant->constant();
  if (!constant->HasValue()) {
    if (!constant->Is<SQLConstant>()) {
      return ::googlesql_base::UnimplementedErrorBuilder()
             << "Unhandled constant implementation algebrizing an "
                "expression: "
             << constant->DebugString();
    }
    SQLConstant* sql_constant =
        const_cast<SQLConstant*>(constant->GetAs<SQLConstant>());

    // Algebrize the constant body expression.
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> value_expr,
                     AlgebrizeExpression(sql_constant->constant_expression()));

    // Create an ad-hoc evaluator to evaluate the algebrized body.
    // WARNING: Do not modify the `status` and `success` variables.
    EvaluationContext context{EvaluationOptions()};
    TupleSlot slot;
    {
      absl::Status status;
      bool success =
          value_expr->EvalSimple(/*params=*/{}, &context, &slot, &status);
      GOOGLESQL_RET_CHECK_EQ(status.ok(),
                   success);  // Ensure success and status agree.
      GOOGLESQL_RETURN_IF_ERROR(status);
    }

    // Set the evaluation result back to the constant to ensure the
    // run-once semantics.
    GOOGLESQL_RETURN_IF_ERROR(sql_constant->SetEvaluationResult(slot.value()));
  }
  GOOGLESQL_RET_CHECK(constant->HasValue());
  GOOGLESQL_ASSIGN_OR_RETURN(Value value, constant->GetValue());
  return ConstExpr::Create(value);
}

absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeColumnRef(
    const ResolvedColumnRef* column_ref) {
  const ResolvedColumn& column = column_ref->column();
  GOOGLESQL_ASSIGN_OR_RETURN(const VariableId variable_id,
                   column_to_variable_->LookupVariableNameForColumn(column));
  return DerefExpr::Create(variable_id, column_ref->type());
}

absl::StatusOr<std::unique_ptr<ValueExpr>>
Algebrizer::AlgebrizeCatalogColumnRef(
    const ResolvedCatalogColumnRef* column_ref) {
  const Column* catalog_column = column_ref->column();
  GOOGLESQL_RET_CHECK(catalog_column_ref_variables_.has_value());
  const auto& it = catalog_column_ref_variables_->find(catalog_column);
  GOOGLESQL_RET_CHECK(it != catalog_column_ref_variables_->end()) << absl::StrFormat(
      "Cannot find catalog column %s of type %s", catalog_column->FullName(),
      catalog_column->GetType()->DebugString());

  return DerefExpr::Create(it->second, column_ref->type());
}

absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeGetProtoOneof(
    const ResolvedGetProtoOneof* get_proto_oneof) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> input_proto,
                   AlgebrizeExpression(get_proto_oneof->expr()));
  std::vector<std::unique_ptr<ValueExpr>> arguments;
  arguments.push_back(std::move(input_proto));
  return ScalarFunctionCallExpr::Create(
      std::make_unique<ExtractOneofCaseFunction>(
          get_proto_oneof->type(), get_proto_oneof->oneof_descriptor()),
      std::move(arguments));
}

absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeSystemVariable(
    const ResolvedSystemVariable* system_variable) {
  return DerefExpr::Create(
      variable_gen_->GetVariableNameFromSystemVariable(
          system_variable->name_path(), system_variables_map_),
      system_variable->type());
}

absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeParameter(
    const ResolvedParameter* parameter) {
  const bool is_named_parameter = !parameter->name().empty();
  GOOGLESQL_RET_CHECK_EQ(is_named_parameter, parameters_->is_named())
      << "The parameter kind (named versus positional) is not consistent "
         "with the parameters provided to the statement";

  if (parameters_->is_named()) {
    return DerefExpr::Create(
        variable_gen_->GetVariableNameFromParameter(
            parameter->name(), &parameters_->named_parameters()),
        parameter->type());
  } else {
    return DerefExpr::Create(
        variable_gen_->GetVariableNameFromParameter(
            parameter->position(), &parameters_->positional_parameters()),
        parameter->type());
  }
}

absl::StatusOr<std::unique_ptr<ValueExpr>>
Algebrizer::AlgebrizeExpressionColumn(
    const ResolvedExpressionColumn* expr_column) {
  // Column passed as a parameter into an expression.
  // If `measure_variable_` is valid, then we are algebrizing a measure
  // expression, and we need to treat the ExpressionColumn as a reference to
  // a named field within the measure variable.
  if (measure_variable_.is_valid()) {
    GOOGLESQL_RET_CHECK(measure_type_ != nullptr);
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> deref_measure_column_op,
                     DerefExpr::Create(measure_variable_, measure_type_));
    return MeasureFieldValueExpr::Create(expr_column->name(),
                                         expr_column->type(),
                                         std::move(deref_measure_column_op));
  } else {
    return DerefExpr::Create(variable_gen_->GetVariableNameFromParameter(
                                 expr_column->name(), column_map_),
                             expr_column->type());
  }
}

absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeMakeProto(
    const ResolvedMakeProto* make_proto) {
  std::vector<MakeProtoFunction::FieldAndFormat> fields;
  std::vector<std::unique_ptr<ValueExpr>> arguments;
  for (const auto& field : make_proto->field_list()) {
    const google::protobuf::FieldDescriptor* field_descr = field->field_descriptor();
    GOOGLESQL_RETURN_IF_ERROR(
        ProtoUtil::CheckIsSupportedFieldFormat(field->format(), field_descr));
    fields.emplace_back(field_descr, field->format());
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> value_op,
                     AlgebrizeExpression(field->expr()));
    arguments.push_back(std::move(value_op));
  }
  return ScalarFunctionCallExpr::Create(
      std::make_unique<MakeProtoFunction>(make_proto->type()->AsProto(),
                                          fields),
      std::move(arguments));
}

absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeDMLDefault(
    const ResolvedDMLDefault* dml_default) {
  return ConstExpr::Create(Value::Null(dml_default->type()));
}

absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeFilterField(
    const ResolvedFilterField* filter_fields) {
  // <arguments> will store root object to be modified.
  std::vector<std::unique_ptr<ValueExpr>> arguments;
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> root_expr,
                   AlgebrizeExpression(filter_fields->expr()));
  arguments.push_back(std::move(root_expr));
  auto function = std::make_unique<FilterFieldsFunction>(
      filter_fields->type(), filter_fields->reset_cleared_required_fields());
  for (const auto& filter_field_arg : filter_fields->filter_field_arg_list()) {
    GOOGLESQL_RETURN_IF_ERROR(
        function->AddFieldPath(filter_field_arg->include(),
                               filter_field_arg->field_descriptor_path()));
  }
  return ScalarFunctionCallExpr::Create(std::move(function),
                                        std::move(arguments));
}

absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeArgumentRef(
    const ResolvedArgumentRef* argument_ref) {
  if (argument_ref->argument_kind() == ResolvedArgumentDef::AGGREGATE) {
    // AGGREGATE arguments in a SQL UDA
    return DerefExpr::Create(aggregate_args_map_[argument_ref->name()],
                             argument_ref->type());
  } else {
    // SCALAR and NOT_AGGREGATE arguments
    return CreateFunctionArgumentRefExpr(argument_ref->name(),
                                         argument_ref->type());
  }
}

absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeReplaceField(
    const ResolvedReplaceField* replace_fields) {
  // TODO: Modify the ResolvedAST to propagate the field paths in
  // a tree format to assist with traversal of the serialized proto.
  std::vector<ReplaceFieldsFunction::StructAndProtoPath> field_paths;
  field_paths.reserve(replace_fields->replace_field_item_list_size());
  // <arguments> will store root object to be modified as well as the new
  // field values.
  std::vector<std::unique_ptr<ValueExpr>> arguments;
  arguments.reserve(replace_fields->replace_field_item_list_size() + 1);
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> root_expr,
                   AlgebrizeExpression(replace_fields->expr()));
  arguments.push_back(std::move(root_expr));
  for (const auto& replace_field_item :
       replace_fields->replace_field_item_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> new_value,
                     AlgebrizeExpression(replace_field_item->expr()));
    arguments.push_back(std::move(new_value));
    field_paths.emplace_back(replace_field_item->struct_index_path(),
                             replace_field_item->proto_field_path());
  }
  return ScalarFunctionCallExpr::Create(
      std::make_unique<ReplaceFieldsFunction>(replace_fields->type(),
                                              field_paths),
      std::move(arguments));
}

absl::StatusOr<std::unique_ptr<ValueExpr>>
Algebrizer::AlgebrizeUpdateConstructor(
    const ResolvedUpdateConstructor* update_constructor) {
  std::vector<ReplaceFieldsFunction::StructAndProtoPath> field_paths;
  field_paths.reserve(update_constructor->update_field_item_list_size());
  // `arguments` will store root object to be modified as well as the new
  // field values.
  std::vector<std::unique_ptr<ValueExpr>> arguments;
  arguments.reserve(update_constructor->update_field_item_list_size() + 1);
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> root_expr,
                   AlgebrizeExpression(update_constructor->expr()));
  arguments.push_back(std::move(root_expr));
  for (const auto& update_field_item :
       update_constructor->update_field_item_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> new_value,
                     AlgebrizeExpression(update_field_item->expr()));
    arguments.push_back(std::move(new_value));
    field_paths.emplace_back(std::vector<int>{},
                             update_field_item->proto_field_path());
    // TODO: Ignored for now but to ensure all fields are used.
    update_field_item->operation();
  }
  return ScalarFunctionCallExpr::Create(
      std::make_unique<ReplaceFieldsFunction>(update_constructor->type(),
                                              field_paths),
      std::move(arguments));
}

// Returns the set of columns referenced by 'expr'.
static absl::StatusOr<absl::flat_hash_set<ResolvedColumn>> GetReferencedColumns(
    const ResolvedExpr* expr) {
  // ResolvedASTVisitor that records the set of referenced columns.
  class ReferencedColumnsVisitor : public ResolvedASTVisitor {
   public:
    ReferencedColumnsVisitor() = default;
    ReferencedColumnsVisitor(const ReferencedColumnsVisitor&) = delete;
    ReferencedColumnsVisitor& operator=(const ReferencedColumnsVisitor&) =
        delete;

    const absl::flat_hash_set<ResolvedColumn>& columns() const {
      return columns_;
    }

    absl::Status VisitResolvedColumnRef(
        const ResolvedColumnRef* node) override {
      columns_.insert(node->column());
      return DefaultVisit(node);
    }

   private:
    absl::flat_hash_set<ResolvedColumn> columns_;
  };

  ReferencedColumnsVisitor visitor;
  GOOGLESQL_RETURN_IF_ERROR(expr->Accept(&visitor));
  return visitor.columns();
}

// Returns true if 'expr' is known to be non-volatile (per
// FunctionEnums::VOLATILE).
static bool IsNonVolatile(const ResolvedExpr* expr) {
  switch (expr->node_kind()) {
    case RESOLVED_FUNCTION_CALL: {
      const ResolvedFunctionCall* function_call =
          expr->GetAs<ResolvedFunctionCall>();
      if (function_call->function()->function_options().volatility ==
          FunctionEnums::VOLATILE) {
        return false;
      }
      for (int i = 0; i < function_call->argument_list_size(); ++i) {
        if (!IsNonVolatile(function_call->argument_list(i))) {
          return false;
        }
      }
      return true;
    }

    case RESOLVED_GET_PROTO_FIELD:
      return IsNonVolatile(expr->GetAs<ResolvedGetProtoField>()->expr());
    case RESOLVED_GET_STRUCT_FIELD:
      return IsNonVolatile(expr->GetAs<ResolvedGetStructField>()->expr());
    case RESOLVED_CAST:
      return IsNonVolatile(expr->GetAs<ResolvedCast>()->expr());
    case RESOLVED_MAKE_PROTO: {
      for (const auto& field : expr->GetAs<ResolvedMakeProto>()->field_list()) {
        if (!IsNonVolatile(field->expr())) {
          return false;
        }
      }
      return true;
    }
    case RESOLVED_MAKE_STRUCT: {
      for (const auto& field_expr :
           expr->GetAs<ResolvedMakeStruct>()->field_list()) {
        if (!IsNonVolatile(field_expr.get())) {
          return false;
        }
      }
      return true;
    }

    case RESOLVED_EXPRESSION_COLUMN:
    case RESOLVED_LITERAL:
    case RESOLVED_CONSTANT:
    case RESOLVED_COLUMN_REF:
    case RESOLVED_PARAMETER:
    case RESOLVED_GRAPH_GET_ELEMENT_PROPERTY:
      return true;
    default:
      return false;
  }
}

absl::StatusOr<std::unique_ptr<Algebrizer::FilterConjunctInfo>>
Algebrizer::FilterConjunctInfo::Create(const ResolvedExpr* conjunct) {
  auto info = std::make_unique<FilterConjunctInfo>();
  info->kind = kOther;
  info->conjunct = conjunct;
  info->is_non_volatile = IsNonVolatile(info->conjunct);
  GOOGLESQL_ASSIGN_OR_RETURN(info->referenced_columns,
                   GetReferencedColumns(info->conjunct));

  if (conjunct->node_kind() != RESOLVED_FUNCTION_CALL) return info;

  const ResolvedFunctionCall* function_call =
      conjunct->GetAs<ResolvedFunctionCall>();
  const Function* function = function_call->function();

  info->arguments.reserve(function_call->argument_list_size());
  for (int i = 0; i < function_call->argument_list_size(); ++i) {
    info->arguments.push_back(function_call->argument_list(i));
  }

  info->argument_columns.reserve(info->arguments.size());
  for (const ResolvedExpr* argument : info->arguments) {
    GOOGLESQL_ASSIGN_OR_RETURN(absl::flat_hash_set<ResolvedColumn> columns,
                     GetReferencedColumns(argument));
    info->argument_columns.push_back(std::move(columns));
  }

  if (!function->IsGoogleSQLBuiltin()) return info;

  const std::string name = function->FullName(/*include_group=*/false);

  // Only consider conjuncts that are guaranteed to be NULL if any of their
  // arguments is NULL. Otherwise, the conjunct should go into kOther.
  // For example, `x IS NULL` and `1 IN (1, x)` evaluate to TRUE, even if x
  // is NULL.
  if (name == "$less" || name == "$less_or_equal") {
    info->kind = kLE;
  } else if (name == "$greater" || name == "$greater_or_equal") {
    info->kind = kGE;
  } else if (name == "$equal") {
    info->kind = kEquals;
  } else if (name == "$between") {
    info->kind = kBetween;
  } else if (name == "$in_array") {
    info->kind = kInArray;
  } else {
    GOOGLESQL_RET_CHECK_EQ(info->kind, kOther);
  }

  return info;
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeSingleRowScan(
    const ResolvedSingleRowScan* single_row_scan) {
  GOOGLESQL_ASSIGN_OR_RETURN(auto const_expr, ConstExpr::Create(Value::Int64(1)));
  GOOGLESQL_ASSIGN_OR_RETURN(auto enum_op, EnumerateOp::Create(std::move(const_expr)));
  return std::unique_ptr<RelationalOp>(std::move(enum_op));
}

absl::StatusOr<std::unique_ptr<ArrayScanOp>>
Algebrizer::CreateScanOfColumnsAsArray(const ResolvedColumnList& column_list,
                                       bool is_value_table,
                                       std::unique_ptr<ValueExpr> table_expr) {
  auto element_type = table_expr->output_type()->AsArray()->element_type();
  if (!is_value_table) {
    // List of fields emitted by the table.
    std::vector<std::pair<VariableId, int>> fields;
    ABSL_DCHECK_EQ(column_list.size(), element_type->AsStruct()->num_fields());
    fields.reserve(column_list.size());
    for (int i = 0; i < column_list.size(); ++i) {
      fields.emplace_back(std::make_pair(
          column_to_variable_->GetVariableNameFromColumn(column_list[i]), i));
    }
    return ArrayScanOp::Create(VariableId() /* element */,
                               VariableId() /* position */, fields,
                               std::move(table_expr));
  } else {
    // Value table, e.g., array of protos. Leave 'fields' empty.
    return ArrayScanOp::Create(
        column_to_variable_->GetVariableNameFromColumn(
            column_list[0]) /* element */,
        VariableId() /* position */,
        std::vector<std::pair<VariableId, int>>() /* fields */,
        std::move(table_expr));
  }
}

absl::StatusOr<std::unique_ptr<ArrayScanOp>>
Algebrizer::CreateScanOfTableAsArray(const ResolvedScan* scan,
                                     bool is_value_table,
                                     std::unique_ptr<ValueExpr> table_expr) {
  return CreateScanOfColumnsAsArray(scan->column_list(), is_value_table,
                                    std::move(table_expr));
}

absl::StatusOr<std::unique_ptr<RelationalOp>> Algebrizer::AlgebrizeTableScan(
    const ResolvedTableScan* table_scan,
    std::vector<FilterConjunctInfo*>* active_conjuncts) {
  std::unique_ptr<ValueExpr> system_time_expr;
  if (table_scan->for_system_time_expr() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(system_time_expr,
                     AlgebrizeExpression(table_scan->for_system_time_expr()));
  }

  // Placeholder access for ResolvedTableScan::lock_mode.
  // We don't need to acquire any locks as the reference implementation doesn't
  // support transactions.
  if (table_scan->lock_mode() != nullptr) {
    switch (table_scan->lock_mode()->strength()) {
      case ResolvedLockModeEnums::UPDATE:
        // Do nothing.
        break;
      default:
        // Shouldn't get here.
        GOOGLESQL_RET_CHECK_FAIL() << "Unsupported lock mode strength: "
                         << table_scan->lock_mode()->strength();
    }
  }

  GOOGLESQL_RETURN_IF_ERROR(measure_column_to_expr_.TrackMeasureColumnsEmittedByTableScan(
      *table_scan));

  if (algebrizer_options_.use_arrays_for_tables) {
    // Construct the list of fields of the SELECT and place a scan operator
    // over the table.  The scan is required because a table has array type.
    const std::string& table_name = table_scan->table()->Name();

    GOOGLESQL_ASSIGN_OR_RETURN(const ArrayType* table_type,
                     CreateTableArrayType(table_scan->column_list(),
                                          table_scan->table()->IsValueTable(),
                                          type_factory_));
    GOOGLESQL_ASSIGN_OR_RETURN(auto table_as_array_expr,
                     TableAsArrayExpr::Create(table_name, table_type,
                                              table_scan->column_index_list()));
    return CreateScanOfTableAsArray(table_scan,
                                    table_scan->table()->IsValueTable(),
                                    std::move(table_as_array_expr));
  } else {
    GOOGLESQL_RET_CHECK(!table_scan->read_as_row_type())
        << "Reading table as ROW type not supported";
    const ResolvedColumnList& column_list = table_scan->column_list();
    std::vector<const Column*> table_column_list =
        table_scan->table_column_list();
    const std::vector<int>& column_idx_list = table_scan->column_index_list();
    GOOGLESQL_RET_CHECK(column_idx_list.empty() || table_column_list.empty())
        << "ResolvedTableScan has both column_index_list and table_column_list";
    // If we had `column_index_list`, build `table_column_list` instead.
    if (table_column_list.empty()) {
      GOOGLESQL_RET_CHECK_EQ(column_list.size(), column_idx_list.size());
      table_column_list.reserve(column_idx_list.size());
      for (int column_idx : column_idx_list) {
        table_column_list.push_back(table_scan->table()->GetColumn(column_idx));
      }
    }
    GOOGLESQL_RET_CHECK_EQ(column_list.size(), table_column_list.size());

    // Figure out the variables.
    std::vector<VariableId> variables;
    variables.reserve(column_list.size());
    TableScanColumnInfoMap column_info_map;
    column_info_map.reserve(column_list.size());
    for (int i = 0; i < column_list.size(); ++i) {
      const ResolvedColumn& column = column_list[i];
      const VariableId variable =
          column_to_variable_->GetVariableNameFromColumn(column);
      variables.push_back(variable);
      GOOGLESQL_RET_CHECK(
          column_info_map.emplace(column, std::make_pair(variable, i)).second);
    }

    // Create ColumnFilterArgs from 'conjunct_infos'.
    std::vector<std::unique_ptr<ColumnFilterArg>> and_filters;
    if (algebrizer_options_.push_down_filters) {
      // Iterate over 'active_conjuncts' in reverse order because it's a stack.
      for (auto i = active_conjuncts->rbegin(); i != active_conjuncts->rend();
           ++i) {
        const FilterConjunctInfo& info = **i;
        GOOGLESQL_RETURN_IF_ERROR(TryAlgebrizeFilterConjunctAsColumnFilterArgs(
            column_info_map, info, &and_filters));
        // We cannot mark 'info' redundant here because EvaluatorTableIterator
        // does not guarantee that it will honor 'and_filters' (so we need to
        // keep the conjunct in a filter somewhere above it).
      }
    }

    return EvaluatorTableScanOp::Create(
        table_scan->table(), table_scan->alias(), table_column_list, variables,
        std::move(and_filters), std::move(system_time_expr));
  }
}

// Returns true if any element of 'a' is in 'b'.
static bool Intersects(const absl::flat_hash_set<ResolvedColumn>& a,
                       const absl::flat_hash_set<ResolvedColumn>& b) {
  for (const ResolvedColumn& column : a) {
    if (b.contains(column)) return true;
  }
  return false;
}

absl::Status Algebrizer::TryAlgebrizeFilterConjunctAsColumnFilterArgs(
    const TableScanColumnInfoMap& column_info_map,
    const FilterConjunctInfo& conjunct_info,
    std::vector<std::unique_ptr<ColumnFilterArg>>* and_filters) {
  if (!conjunct_info.is_non_volatile) return absl::OkStatus();

  absl::flat_hash_set<ResolvedColumn> table_columns;
  table_columns.reserve(column_info_map.size());
  for (const auto& entry : column_info_map) {
    GOOGLESQL_RET_CHECK(table_columns.insert(entry.first).second);
  }

  FilterConjunctInfo::Kind conjunct_kind = conjunct_info.kind;
  switch (conjunct_kind) {
    case FilterConjunctInfo::kLE:
    case FilterConjunctInfo::kGE:
    case FilterConjunctInfo::kEquals: {
      GOOGLESQL_RET_CHECK_EQ(conjunct_info.arguments.size(), 2);
      int left_idx = 0;
      int right_idx = 1;

      const ResolvedExpr* left_hand_side = conjunct_info.arguments[left_idx];
      const ResolvedExpr* right_hand_side = conjunct_info.arguments[right_idx];

      if (right_hand_side->node_kind() == RESOLVED_COLUMN_REF &&
          table_columns.contains(
              right_hand_side->GetAs<ResolvedColumnRef>()->column())) {
        std::swap(left_idx, right_idx);
        std::swap(left_hand_side, right_hand_side);
        if (conjunct_kind == FilterConjunctInfo::kLE) {
          conjunct_kind = FilterConjunctInfo::kGE;
        } else if (conjunct_kind == FilterConjunctInfo::kGE) {
          conjunct_kind = FilterConjunctInfo::kLE;
        }
      }

      if (left_hand_side->node_kind() != RESOLVED_COLUMN_REF) {
        return absl::OkStatus();
      }
      const ResolvedColumn& column =
          left_hand_side->GetAs<ResolvedColumnRef>()->column();
      const std::pair<VariableId, int>* variable_and_column_idx =
          googlesql_base::FindOrNull(column_info_map, column);
      if (variable_and_column_idx == nullptr) return absl::OkStatus();

      // For example, we can't push down a filter of the form column1 = column2.
      // One side has to be independent of the table row.
      if (Intersects(conjunct_info.argument_columns[right_idx],
                     table_columns)) {
        return absl::OkStatus();
      }

      if (conjunct_kind == FilterConjunctInfo::kEquals) {
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> element,
                         AlgebrizeExpression(right_hand_side));

        std::vector<std::unique_ptr<ValueExpr>> elements;
        elements.push_back(std::move(element));

        GOOGLESQL_ASSIGN_OR_RETURN(auto filter, InListColumnFilterArg::Create(
                                          variable_and_column_idx->first,
                                          variable_and_column_idx->second,
                                          std::move(elements)));
        and_filters->push_back(std::move(filter));
      } else {
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> arg,
                         AlgebrizeExpression(right_hand_side));
        const HalfUnboundedColumnFilterArg::Kind arg_kind =
            (conjunct_kind == FilterConjunctInfo::kLE
                 ? HalfUnboundedColumnFilterArg::kLE
                 : HalfUnboundedColumnFilterArg::kGE);

        GOOGLESQL_ASSIGN_OR_RETURN(auto filter, HalfUnboundedColumnFilterArg::Create(
                                          variable_and_column_idx->first,
                                          variable_and_column_idx->second,
                                          arg_kind, std::move(arg)));
        and_filters->push_back(std::move(filter));
      }
      break;
    }
    case FilterConjunctInfo::kBetween: {
      GOOGLESQL_RET_CHECK_EQ(conjunct_info.arguments.size(), 3);

      if (conjunct_info.arguments[0]->node_kind() != RESOLVED_COLUMN_REF) {
        return absl::OkStatus();
      }
      const ResolvedColumn& column =
          conjunct_info.arguments[0]->GetAs<ResolvedColumnRef>()->column();
      const std::pair<VariableId, int>* variable_and_column_idx =
          googlesql_base::FindOrNull(column_info_map, column);
      if (variable_and_column_idx == nullptr) return absl::OkStatus();

      if (Intersects(conjunct_info.argument_columns[1], table_columns)) {
        return absl::OkStatus();
      }

      if (Intersects(conjunct_info.argument_columns[2], table_columns)) {
        return absl::OkStatus();
      }

      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> lower_bound,
                       AlgebrizeExpression(conjunct_info.arguments[1]));
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> upper_bound,
                       AlgebrizeExpression(conjunct_info.arguments[2]));

      GOOGLESQL_ASSIGN_OR_RETURN(
          auto lower_bound_restriction,
          HalfUnboundedColumnFilterArg::Create(
              variable_and_column_idx->first, variable_and_column_idx->second,
              HalfUnboundedColumnFilterArg::kGE, std::move(lower_bound)));
      GOOGLESQL_ASSIGN_OR_RETURN(
          auto upper_bound_restriction,
          HalfUnboundedColumnFilterArg::Create(
              variable_and_column_idx->first, variable_and_column_idx->second,
              HalfUnboundedColumnFilterArg::kLE, std::move(upper_bound)));

      and_filters->push_back(std::move(lower_bound_restriction));
      and_filters->push_back(std::move(upper_bound_restriction));
      break;
    }
    case FilterConjunctInfo::kInArray: {
      GOOGLESQL_RET_CHECK(!conjunct_info.arguments.empty());

      if (conjunct_info.arguments[0]->node_kind() != RESOLVED_COLUMN_REF) {
        return absl::OkStatus();
      }
      const ResolvedColumn& column =
          conjunct_info.arguments[0]->GetAs<ResolvedColumnRef>()->column();
      const std::pair<VariableId, int>* variable_and_column_idx =
          googlesql_base::FindOrNull(column_info_map, column);
      if (variable_and_column_idx == nullptr) return absl::OkStatus();

      for (int i = 1; i < conjunct_info.arguments.size(); ++i) {
        if (Intersects(conjunct_info.argument_columns[i], table_columns)) {
          return absl::OkStatus();
        }
      }

      // Algebrize all the arguments except for the first one.
      std::vector<std::unique_ptr<ValueExpr>> elements;
      for (int i = 1; i < conjunct_info.arguments.size(); ++i) {
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> element,
                         AlgebrizeExpression(conjunct_info.arguments[i]));
        elements.push_back(std::move(element));
      }

      std::unique_ptr<ColumnFilterArg> filter;
      if (conjunct_kind == FilterConjunctInfo::kInArray) {
        GOOGLESQL_RET_CHECK_EQ(elements.size(), 1);
        GOOGLESQL_ASSIGN_OR_RETURN(filter, InArrayColumnFilterArg::Create(
                                     variable_and_column_idx->first,
                                     variable_and_column_idx->second,
                                     std::move(elements[0])));
      } else {
        GOOGLESQL_ASSIGN_OR_RETURN(
            filter, InListColumnFilterArg::Create(
                        variable_and_column_idx->first,
                        variable_and_column_idx->second, std::move(elements)));
      }
      and_filters->push_back(std::move(filter));
      break;
    }
    case FilterConjunctInfo::kOther:
      break;
  }

  return absl::OkStatus();
}

// Computes the number of times a WITH entry is referenced.
// Only references reachable from the main query count. For example, in this
// query:
//    WITH t1 AS (SELECT 1),
//         t2 AS (SELECT * FROM t1),
//         t3 AS (SELECT * FROM t2),
//         t4 AS (SELECT * FROM t1)
//    SELECT * FROM t4
//
//  t1 and t4 would have one reference, while t2 and t3 would have zero
//  references.
class FindWithEntryReferenceCountVisitor : public ResolvedASTVisitor {
 public:
  static absl::StatusOr<absl::flat_hash_map<std::string, int>> Run(
      const ResolvedWithScan* scan) {
    absl::flat_hash_map<std::string, int> result;
    std::stack<const ResolvedNode*> stack;
    absl::flat_hash_set<const ResolvedNode*> visited;
    stack.push(scan->query());
    while (!stack.empty()) {
      if (visited.contains(stack.top())) {
        stack.pop();
        continue;
      }
      visited.insert(stack.top());
      FindWithEntryReferenceCountVisitor visitor;
      for (const auto& with_entry : scan->with_entry_list()) {
        visitor.reference_count_[with_entry->with_query_name()] = 0;
      }
      GOOGLESQL_RETURN_IF_ERROR(stack.top()->Accept(&visitor));
      stack.pop();

      for (const auto& with_entry : scan->with_entry_list()) {
        int ref_count = visitor.reference_count_[with_entry->with_query_name()];
        if (ref_count >= 1 && !visited.contains(with_entry.get())) {
          stack.push(with_entry.get());
        }
        result[with_entry->with_query_name()] += ref_count;
      }
    }
    return result;
  }

  absl::Status DefaultVisit(const ResolvedNode* node) override {
    return node->ChildrenAccept(this);
  }

  absl::Status VisitResolvedPivotScan(const ResolvedPivotScan* scan) override {
    GOOGLESQL_RETURN_IF_ERROR(scan->input_scan()->Accept(this));

    // Make sure pivot expressions are visited for each time the expression
    // is algebrized (once per pivot value). This prevents inlining of WITH
    // tables when referenced inside of a PIVOT expression with multiple
    // pivot values. See b/191772920 for a repro.
    for (const auto& expr : scan->pivot_expr_list()) {
      for (auto it = scan->pivot_value_list().begin();
           it != scan->pivot_value_list().end(); ++it) {
        GOOGLESQL_RETURN_IF_ERROR(expr->Accept(this));
      }
    }

    // The FOR expression is evaluated only once, regardless of the number
    // of pivot values.
    GOOGLESQL_RETURN_IF_ERROR(scan->for_expr()->Accept(this));

    for (const auto& groupby : scan->group_by_list()) {
      GOOGLESQL_RETURN_IF_ERROR(groupby->Accept(this));
    }
    for (const auto& pivot_value : scan->pivot_value_list()) {
      GOOGLESQL_RETURN_IF_ERROR(pivot_value->Accept(this));
    }
    return absl::OkStatus();
  }

  absl::Status VisitResolvedWithRefScan(
      const ResolvedWithRefScan* scan) override {
    auto it = reference_count_.find(scan->with_query_name());
    if (it != reference_count_.end()) {
      ++it->second;
    }
    return absl::OkStatus();
  }

 private:
  absl::flat_hash_map<std::string, int> reference_count_;
};

absl::StatusOr<std::unique_ptr<RelationalOp>> Algebrizer::AlgebrizeWithScan(
    const ResolvedWithScan* scan) {
  // Each named subquery is nested as an array, which is then unnested when
  // referenced in other subquerieries or in the main query. Named subqueries
  // are stored in with_map_ to be used for algebrizing WithRef scans that
  // reference those subqueries.
  // Save the old with_map_ with names that are visible in the outer scope.
  const absl::flat_hash_map<std::string, ExprArg*> old_with_map = with_map_;

  // Compute how many times each WITH entry is referenced. Entries referenced
  // exactly once can be inlined, while entries not referenced at all can be
  // skipped altogether.
  std::optional<absl::flat_hash_map<std::string, int>> reference_count_by_name;
  if (algebrizer_options_.inline_with_entries) {
    GOOGLESQL_ASSIGN_OR_RETURN(reference_count_by_name,
                     FindWithEntryReferenceCountVisitor::Run(scan));
  }

  for (const auto& with_entry : scan->with_entry_list()) {
    measure_column_to_expr_.TrackWithQueryScan(*with_entry);
    if (reference_count_by_name.has_value()) {
      int ref_count =
          reference_count_by_name.value().at(with_entry->with_query_name());

      if (ref_count == 0) {
        // This WITH entry is not referenced, directly or indirectly, in the
        // main query, so we can just ignore it completely. Mark its fields as
        // "accessed" to suppress the access checks.
        with_entry->MarkFieldsAccessed();
        continue;
      }
      if (ref_count == 1) {
        // Skip for now; we'll inline the definition when it's used.
        inlined_with_entries_[with_entry->with_query_name()] =
            with_entry->with_subquery();
        continue;
      }
    }
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> subquery,
                     AlgebrizeScan(with_entry->with_subquery()));
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<ArrayNestExpr> nested_subquery,
        NestRelationInStruct(with_entry->with_subquery()->column_list(),
                             std::move(subquery),
                             /*is_with_table=*/true));
    const VariableId subquery_variable =
        variable_gen_->GetNewVariableName(with_entry->with_query_name());
    ExprArg* arg = new ExprArg(subquery_variable, std::move(nested_subquery));
    // Record a mapping from subquery name to ExprArg.
    with_map_[with_entry->with_query_name()] = arg;
    with_subquery_let_assignments_.emplace_back(arg);  // Takes ownership.
  }
  // Algebrize and nest the main query.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> nested_query,
                   AlgebrizeScan(scan->query()));
  with_map_ = old_with_map;  // Restore original state.
  return nested_query;
}

// Called only while AlgebrizeWithScan() sits in an earlier stack frame.
absl::StatusOr<std::unique_ptr<RelationalOp>> Algebrizer::AlgebrizeWithRefScan(
    const ResolvedWithRefScan* scan) {
  const std::string& query_name =
      scan->GetAs<ResolvedWithRefScan>()->with_query_name();
  auto inlined_it = inlined_with_entries_.find(query_name);
  if (inlined_it != inlined_with_entries_.end()) {
    const ResolvedScan* with_subquery_scan = inlined_it->second;
    // We have an inlined WITH entry. Algebrize it here and add the algebrized
    // subquery directly.
    //
    // Note that, to maintain correct semantics where WITH subqueries are
    // evaluated only once, it is not possible to get here unless
    // the WITH entry is referenced exactly once. Remove the WITH entry from the
    // map to help ensure that this is actually the case (and make the resultant
    // GOOGLESQL_RET_CHECK() more diagnosable if it somehow isn't).
    inlined_with_entries_.erase(inlined_it);

    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> algebrized_with_subquery,
                     AlgebrizeScan(with_subquery_scan));
    // Track measure columns renamed by the WITH ref scan after algebrizing the
    // WITH subquery.
    GOOGLESQL_RETURN_IF_ERROR(
        measure_column_to_expr_.TrackMeasureColumnsRenamedByWithRefScan(*scan));

    // Map columns from that of subquery to that of <scan>.
    std::vector<std::unique_ptr<ExprArg>> column_map;
    GOOGLESQL_RET_CHECK_EQ(scan->column_list_size(),
                 with_subquery_scan->column_list_size());
    for (int i = 0; i < scan->column_list_size(); ++i) {
      GOOGLESQL_ASSIGN_OR_RETURN(VariableId from_varid,
                       column_to_variable_->LookupVariableNameForColumn(
                           with_subquery_scan->column_list(i)));
      VariableId to_varid =
          column_to_variable_->GetVariableNameFromColumn(scan->column_list(i));
      GOOGLESQL_RET_CHECK(scan->column_list(i).type()->Equals(
          with_subquery_scan->column_list(i).type()));

      GOOGLESQL_ASSIGN_OR_RETURN(
          std::unique_ptr<DerefExpr> deref,
          DerefExpr::Create(from_varid, scan->column_list(i).type()));
      column_map.push_back(
          std::make_unique<ExprArg>(to_varid, std::move(deref)));
    }
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> compute_op,
                     ComputeOp::Create(std::move(column_map),
                                       std::move(algebrized_with_subquery)));
    return compute_op;
  }

  // WITH subquery is already algebrized. Track measure columns renamed by the
  // WITH ref scan.
  GOOGLESQL_RETURN_IF_ERROR(
      measure_column_to_expr_.TrackMeasureColumnsRenamedByWithRefScan(*scan));
  // We are referencing a pre-computed array value storing the entire table.
  const auto it = with_map_.find(query_name);
  GOOGLESQL_RET_CHECK(it != with_map_.end())
      << "Can't find query in with_map_: " << query_name;
  const ExprArg* arg = it->second;
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto deref_arg,
      DerefExpr::Create(arg->variable(), arg->value_expr()->output_type()));
  return CreateScanOfTableAsArray(scan, /*is_value_table=*/false,
                                  std::move(deref_arg));
}

absl::StatusOr<std::unique_ptr<RelationalOp>> Algebrizer::AlgebrizeArrayScan(
    const ResolvedArrayScan* array_scan,
    std::vector<FilterConjunctInfo*>* active_conjuncts) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  if (array_scan->input_scan() == nullptr) {
    GOOGLESQL_RET_CHECK(array_scan->join_expr() == nullptr);
    return AlgebrizeArrayScanWithoutJoin(array_scan, active_conjuncts);
  } else {
    const JoinOp::JoinKind join_kind =
        array_scan->is_outer() ? JoinOp::kOuterApply : JoinOp::kCrossApply;

    std::vector<ResolvedColumn> right_output_columns =
        array_scan->element_column_list();
    if (array_scan->array_offset_column() != nullptr) {
      right_output_columns.push_back(
          array_scan->array_offset_column()->column());
    }

    auto right_scan_algebrizer_cb =
        [this,
         array_scan](std::vector<FilterConjunctInfo*>* active_conjuncts_arg)
        -> absl::StatusOr<std::unique_ptr<RelationalOp>> {
      const absl::flat_hash_set<ResolvedColumn> input_columns(
          array_scan->input_scan()->column_list().begin(),
          array_scan->input_scan()->column_list().end());

      for (FilterConjunctInfo* info : *active_conjuncts_arg) {
        GOOGLESQL_RET_CHECK(!info->redundant);
        GOOGLESQL_RET_CHECK(!Intersects(info->referenced_columns, input_columns));
      }

      return AlgebrizeArrayScanWithoutJoin(array_scan, active_conjuncts_arg);
    };

    return AlgebrizeJoinScanInternal(
        join_kind, array_scan->join_expr(), array_scan->input_scan(),
        right_output_columns, right_scan_algebrizer_cb, active_conjuncts);
  }
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeArrayScanWithoutJoin(
    const ResolvedArrayScan* array_scan,
    std::vector<FilterConjunctInfo*>* active_conjuncts) {
  int element_column_count = array_scan->array_expr_list_size();
  std::vector<VariableId> element_list(element_column_count);
  std::vector<std::unique_ptr<ValueExpr>> array_list(element_column_count);
  for (int i = 0; i < element_column_count; ++i) {
    GOOGLESQL_ASSIGN_OR_RETURN(array_list[i],
                     AlgebrizeExpression(array_scan->array_expr_list(i)));
    element_list[i] = column_to_variable_->GetVariableNameFromColumn(
        array_scan->element_column_list(i));
  }

  VariableId array_position_in;
  if (array_scan->array_offset_column() != nullptr) {
    array_position_in = column_to_variable_->GetVariableNameFromColumn(
        array_scan->array_offset_column()->column());
  }

  // ARRAY_ZIP_MODE `mode` argument, which defaults to "PAD" if unspecified.
  std::unique_ptr<ValueExpr> mode;
  if (array_scan->array_zip_mode() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(mode, AlgebrizeExpression(array_scan->array_zip_mode()));
    GOOGLESQL_RET_CHECK(mode->output_type()->IsEnum());
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(
        mode, ConstExpr::Create(Value::Enum(types::ArrayZipModeEnumType(),
                                            functions::ArrayZipEnums::PAD)));
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<RelationalOp> rel_op,
      ArrayScanOp::Create(std::move(element_list), array_position_in,
                          std::move(array_list), std::move(mode)));
  return MaybeApplyFilterConjuncts(std::move(rel_op), active_conjuncts);
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeLimitOffsetScan(const ResolvedLimitOffsetScan* scan) {
  // If both limit and offset are null, then drop the scan.
  if (scan->limit() == nullptr && scan->offset() == nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> input,
                     AlgebrizeScan(scan->input_scan()));
    GOOGLESQL_RETURN_IF_ERROR(input->set_is_order_preserving(scan->is_ordered()));
    return input;
  }

  std::unique_ptr<ValueExpr> limit;
  if (scan->limit() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(limit, AlgebrizeExpression(scan->limit()));
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(limit, ConstExpr::Create(Value::NullInt64()));
  }

  std::unique_ptr<ValueExpr> offset;
  if (scan->offset() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(offset, AlgebrizeExpression(scan->offset()));
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(offset, ConstExpr::Create(Value::Int64(0)));
  }

  if (algebrizer_options_.allow_order_by_limit_operator &&
      scan->input_scan()->node_kind() == RESOLVED_ORDER_BY_SCAN) {
    const ResolvedOrderByScan* input_scan =
        scan->input_scan()->GetAs<ResolvedOrderByScan>();
    return AlgebrizeOrderByScan(input_scan, std::move(limit),
                                std::move(offset));
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> input,
                     AlgebrizeScan(scan->input_scan()));
    return LimitOp::Create(std::move(limit), std::move(offset),
                           std::move(input), scan->is_ordered());
  }
}

absl::Status Algebrizer::AddFilterConjunctsTo(
    const ResolvedExpr* expr,
    std::vector<std::unique_ptr<FilterConjunctInfo>>* conjunct_infos) {
  if (expr->node_kind() == RESOLVED_FUNCTION_CALL) {
    const ResolvedFunctionCall* function_call =
        expr->GetAs<ResolvedFunctionCall>();
    const Function* function = function_call->function();
    if (function->IsGoogleSQLBuiltin()) {
      const absl::StatusOr<FunctionKind> status_or_kind =
          BuiltinFunctionCatalog::GetKindByName(
              function->FullName(/*include_group=*/false));
      if (status_or_kind.ok() && status_or_kind.value() == FunctionKind::kAnd) {
        for (const std::unique_ptr<const ResolvedExpr>& arg :
             function_call->argument_list()) {
          GOOGLESQL_RETURN_IF_ERROR(AddFilterConjunctsTo(arg.get(), conjunct_infos));
        }
        return absl::OkStatus();
      }
    }
  }

  GOOGLESQL_ASSIGN_OR_RETURN(auto conjunct_info, FilterConjunctInfo::Create(expr));
  conjunct_infos->push_back(std::move(conjunct_info));
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<RelationalOp>> Algebrizer::AlgebrizeJoinScan(
    const ResolvedJoinScan* join_scan,
    std::vector<FilterConjunctInfo*>* active_conjuncts) {
  JoinOp::JoinKind join_kind;
  if (join_scan->is_lateral()) {
    // Mark accessed to pass access checks.
    for (const auto& col_ref : join_scan->parameter_list()) {
      col_ref->MarkFieldsAccessed();
    }

    switch (join_scan->join_type()) {
      case ResolvedJoinScan::INNER:
        join_kind = JoinOp::kCrossApply;
        break;
      case ResolvedJoinScan::LEFT:
        join_kind = JoinOp::kOuterApply;
        break;
      case ResolvedJoinScan::RIGHT:
        GOOGLESQL_RET_CHECK_FAIL() << "Lateral join cannot be RIGHT OUTER.";
      case ResolvedJoinScan::FULL:
        GOOGLESQL_RET_CHECK_FAIL() << "Lateral join cannot be FULL OUTER.";
        break;
    }
  } else {
    GOOGLESQL_RET_CHECK(join_scan->parameter_list().empty());
    switch (join_scan->join_type()) {
      case ResolvedJoinScan::INNER:
        join_kind = JoinOp::kInnerJoin;
        break;
      case ResolvedJoinScan::LEFT:
        join_kind = JoinOp::kLeftOuterJoin;
        break;
      case ResolvedJoinScan::RIGHT:
        join_kind = JoinOp::kRightOuterJoin;
        break;
      case ResolvedJoinScan::FULL:
        join_kind = JoinOp::kFullOuterJoin;
        break;
    }
  }

  const ResolvedScan* right_scan = join_scan->right_scan();
  auto right_scan_algebrizer_cb =
      [this, right_scan](std::vector<FilterConjunctInfo*>* active_conjuncts) {
        return AlgebrizeScan(right_scan, active_conjuncts);
      };
  return AlgebrizeJoinScanInternal(
      join_kind, join_scan->join_expr(), join_scan->left_scan(),
      right_scan->column_list(), right_scan_algebrizer_cb, active_conjuncts);
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeJoinScanInternal(
    JoinOp::JoinKind join_kind, const ResolvedExpr* join_expr,
    const ResolvedScan* left_scan,
    const std::vector<ResolvedColumn>& right_output_column_list,
    const ScanAlgebrizerCallback& right_scan_algebrizer_cb,
    std::vector<FilterConjunctInfo*>* active_conjuncts) {
  std::vector<std::unique_ptr<FilterConjunctInfo>> conjunct_infos;
  if (join_expr != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(AddFilterConjunctsTo(join_expr, &conjunct_infos));
  }
  const absl::flat_hash_set<ResolvedColumn> left_output_columns(
      left_scan->column_list().begin(), left_scan->column_list().end());
  const absl::flat_hash_set<ResolvedColumn> right_output_columns(
      right_output_column_list.begin(), right_output_column_list.end());
  std::vector<FilterConjunctInfo*> join_condition_conjuncts_with_push_down;
  std::vector<FilterConjunctInfo*> left_conjuncts_with_push_down;
  std::vector<FilterConjunctInfo*> right_conjuncts_with_push_down;
  if (algebrizer_options_.push_down_filters) {
    // Try to rewrite the join as an equivalent inner join to facilitate filter
    // pushdown.
    for (FilterConjunctInfo* conjunct_info : *active_conjuncts) {
      GOOGLESQL_RETURN_IF_ERROR(
          NarrowJoinKindForFilterConjunct(*conjunct_info, left_output_columns,
                                          right_output_columns, &join_kind));
    }

    // Iterate over 'active_conjuncts' to find the ones that can actually be
    // pushed down.
    for (FilterConjunctInfo* conjunct_info : *active_conjuncts) {
      bool push_down_to_join_condition;
      bool push_down_to_left_input;
      bool push_down_to_right_input;
      GOOGLESQL_RETURN_IF_ERROR(CanPushFilterConjunctIntoJoin(
          *conjunct_info, join_kind, left_output_columns, right_output_columns,
          &push_down_to_join_condition, &push_down_to_left_input,
          &push_down_to_right_input));
      if (push_down_to_join_condition) {
        join_condition_conjuncts_with_push_down.push_back(conjunct_info);
      }
      if (push_down_to_left_input) {
        left_conjuncts_with_push_down.push_back(conjunct_info);
      }
      if (push_down_to_right_input) {
        right_conjuncts_with_push_down.push_back(conjunct_info);
      }
    }
  }

  // Iterate over the new conjuncts (in reverse order, because they are in a
  // stack), determining which of them can be pushed down and which should
  // remain in the join condition.
  for (auto i = conjunct_infos.rbegin(); i != conjunct_infos.rend(); ++i) {
    FilterConjunctInfo* conjunct_info = i->get();
    bool push_down_to_left_input = false;
    bool push_down_to_right_input = false;
    if (algebrizer_options_.push_down_filters &&
        (join_kind == JoinOp::kInnerJoin || join_kind == JoinOp::kCrossApply)) {
      // For simplicity, we only support pushdowns from the join condition to
      // the join inputs for inner joins. Note that the code above handles the
      // case of WHERE clause pushdown (above the join), and also potentially
      // rewrites a left/right/full join to an inner join, so this code runs.
      GOOGLESQL_RETURN_IF_ERROR(CanPushFilterConjunctDownFromInnerJoinCondition(
          *conjunct_info, left_output_columns, right_output_columns,
          &push_down_to_left_input, &push_down_to_right_input));
    }
    if (push_down_to_left_input) {
      left_conjuncts_with_push_down.push_back(conjunct_info);
    }
    if (push_down_to_right_input) {
      right_conjuncts_with_push_down.push_back(conjunct_info);
    }
    if (!push_down_to_left_input && !push_down_to_right_input) {
      join_condition_conjuncts_with_push_down.push_back(conjunct_info);
    }
  }

  // Sanity check that there are no redundant conjuncts yet.
  for (const FilterConjunctInfo* conjunct_info : *active_conjuncts) {
    GOOGLESQL_RET_CHECK(!conjunct_info->redundant);
  }

  // Algebrize the left side of the join. Because we checked whether a
  // ConjunctInfo was non-volatile when we constructed
  // 'left_conjuncts_with_push_down', all entries in
  // 'left_conjuncts_with_push_down' are marked as redundant. However, we must
  // temporarily unmark them in order to algebrize the right side of the join,
  // because 'left_conjuncts_with_push_down' and
  // 'right_conjuncts_with_push_down' may overlap.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> left,
                   AlgebrizeScan(left_scan, &left_conjuncts_with_push_down));
  for (FilterConjunctInfo* info : left_conjuncts_with_push_down) {
    GOOGLESQL_RET_CHECK(info->redundant);
    info->redundant = false;
  }

  // Algebrize the right side of the join. Because we checked whether a
  // ConjunctInfo was non-volatile when we constructed
  // 'right_conjuncts_with_push_down', all entries in
  // 'right_conjuncts_with_push_down' are marked as redundant.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> right,
                   right_scan_algebrizer_cb(&right_conjuncts_with_push_down));

  for (const FilterConjunctInfo* info : right_conjuncts_with_push_down) {
    GOOGLESQL_RET_CHECK(info->redundant);
  }

  // Remark all entries in 'left_conjuncts_with_push_down' as redundant.
  for (FilterConjunctInfo* info : left_conjuncts_with_push_down) {
    info->redundant = true;
  }

  // Incorporate conjuncts into the hash join where allowed/possible.
  std::vector<JoinOp::HashJoinEqualityExprs> hash_join_equality_exprs;
  if (algebrizer_options_.allow_hash_join) {
    switch (join_kind) {
      case JoinOp::kInnerJoin:
      case JoinOp::kLeftOuterJoin:
      case JoinOp::kRightOuterJoin:
      case JoinOp::kFullOuterJoin:
        GOOGLESQL_RETURN_IF_ERROR(AlgebrizeJoinConditionForHashJoin(
            left_output_columns, right_output_columns,
            &join_condition_conjuncts_with_push_down,
            &hash_join_equality_exprs));
        break;
      case JoinOp::kCrossApply:
      case JoinOp::kOuterApply:
        // Hash join is not supported for correlated joins.
        break;
    }
  }

  // Algebrize all of the non-redundant remaining conjuncts for use in the join
  // condition. Iterate in reverse order to de-stackify the ordering.
  std::vector<std::unique_ptr<ValueExpr>> algebrized_conjuncts;
  for (auto i = join_condition_conjuncts_with_push_down.rbegin();
       i != join_condition_conjuncts_with_push_down.rend(); ++i) {
    FilterConjunctInfo* conjunct_info = *i;
    if (!conjunct_info->redundant) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> algebrized_conjunct,
                       AlgebrizeExpression(conjunct_info->conjunct));
      algebrized_conjuncts.push_back(std::move(algebrized_conjunct));
      conjunct_info->redundant = true;
    }
  }

  // Algebrize the join condition using the algebrized conjuncts.
  std::unique_ptr<ValueExpr> remaining_join_expr;
  if (algebrized_conjuncts.empty()) {
    GOOGLESQL_ASSIGN_OR_RETURN(remaining_join_expr,
                     ConstExpr::Create(values::Bool(true)));
  } else if (algebrized_conjuncts.size() == 1) {
    remaining_join_expr = std::move(algebrized_conjuncts[0]);
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(
        remaining_join_expr,
        BuiltinScalarFunction::CreateCall(
            FunctionKind::kAnd, language_options_, types::BoolType(),
            ConvertValueExprsToAlgebraArgs(std::move(algebrized_conjuncts)),
            ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));
  }

  // Determine the outputs.
  std::vector<std::unique_ptr<ExprArg>> left_output, right_output;
  switch (join_kind) {
    case JoinOp::kInnerJoin:
    case JoinOp::kCrossApply:
      // no NULL-extension of left or right input
      break;
    case JoinOp::kLeftOuterJoin:
    case JoinOp::kOuterApply:
      GOOGLESQL_RETURN_IF_ERROR(
          RemapJoinColumns(right_output_column_list, &right_output));
      break;
    case JoinOp::kRightOuterJoin:
      GOOGLESQL_RETURN_IF_ERROR(RemapJoinColumns(left_scan->column_list(), &left_output));
      break;
    case JoinOp::kFullOuterJoin:
      GOOGLESQL_RETURN_IF_ERROR(RemapJoinColumns(left_scan->column_list(), &left_output));
      GOOGLESQL_RETURN_IF_ERROR(
          RemapJoinColumns(right_output_column_list, &right_output));
      break;
  }

  // Algebrize the join.
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<RelationalOp> join_op,
      JoinOp::Create(join_kind, std::move(hash_join_equality_exprs),
                     std::move(remaining_join_expr), std::move(left),
                     std::move(right), std::move(left_output),
                     std::move(right_output)));

  return join_op;
}

absl::Status Algebrizer::NarrowJoinKindForFilterConjunct(
    const FilterConjunctInfo& conjunct_info,
    const absl::flat_hash_set<ResolvedColumn>& left_output_columns,
    const absl::flat_hash_set<ResolvedColumn>& right_output_columns,
    JoinOp::JoinKind* join_kind) {
  if (conjunct_info.kind == FilterConjunctInfo::kOther ||
      *join_kind == JoinOp::kInnerJoin || *join_kind == JoinOp::kCrossApply) {
    return absl::OkStatus();
  }

  bool has_left_column_arg = false;
  bool has_right_column_arg = false;

  for (const ResolvedExpr* argument : conjunct_info.arguments) {
    if (argument->node_kind() != RESOLVED_COLUMN_REF) continue;
    if (left_output_columns.contains(
            argument->GetAs<ResolvedColumnRef>()->column())) {
      has_left_column_arg = true;
    }
    if (right_output_columns.contains(
            argument->GetAs<ResolvedColumnRef>()->column())) {
      has_right_column_arg = true;
    }
  }

  switch (*join_kind) {
    case JoinOp::kInnerJoin:
    case JoinOp::kCrossApply:
      // Handled above.
      GOOGLESQL_RET_CHECK_FAIL()
          << "Unexpected join kind in TightenJoinKindForFilterConjunct(): "
          << JoinOp::JoinKindToString(*join_kind);
    case JoinOp::kLeftOuterJoin:
      // Consider a conjunct of the form f(left columns) = <right column>
      // applied above a left join. Any tuples coming out of the join that are
      // right-padded with NULLs will cause this predicate to evaluate to NULL,
      // and therefore the predicate won't apply. Thus, it is safe to rewrite
      // the join to an inner join to facilitate pushing down the filter.
      //
      // Generalizing to arbitrary non-kOther conjuncts, it is enough to know
      // that at least one of the arguments is NULL whenever all the right join
      // columns are NULL, since that case the conjunct is NULL
      //
      // TODO: With more work, we could do better here by handling more
      // cases (i.e., handle kOther and arbitrary expressions for the
      // arguments, not just columns).
      if (has_right_column_arg) {
        *join_kind = JoinOp::kInnerJoin;
      }
      return absl::OkStatus();
    case JoinOp::kOuterApply:
      // Same as left outer join.
      if (has_right_column_arg) {
        *join_kind = JoinOp::kCrossApply;
      }
      return absl::OkStatus();
    case JoinOp::kRightOuterJoin:
      // Symmetric to left outer join.
      if (has_left_column_arg) {
        *join_kind = JoinOp::kInnerJoin;
      }
      return absl::OkStatus();
    case JoinOp::kFullOuterJoin:
      // Analogous to the left/right outer join cases.
      if (has_left_column_arg && has_right_column_arg) {
        *join_kind = JoinOp::kInnerJoin;
      } else if (has_left_column_arg) {
        *join_kind = JoinOp::kLeftOuterJoin;
      } else if (has_right_column_arg) {
        *join_kind = JoinOp::kRightOuterJoin;
      }
      return absl::OkStatus();
  }
}

absl::Status Algebrizer::CanPushFilterConjunctIntoJoin(
    const FilterConjunctInfo& conjunct_info, JoinOp::JoinKind join_kind,
    const absl::flat_hash_set<ResolvedColumn>& left_output_columns,
    const absl::flat_hash_set<ResolvedColumn>& right_output_columns,
    bool* push_down_to_join_condition, bool* push_down_to_left_input,
    bool* push_down_to_right_input) {
  *push_down_to_join_condition = false;
  *push_down_to_left_input = false;
  *push_down_to_right_input = false;

  if (!conjunct_info.is_non_volatile) return absl::OkStatus();

  const bool references_left_column =
      Intersects(conjunct_info.referenced_columns, left_output_columns);
  const bool references_right_column =
      Intersects(conjunct_info.referenced_columns, right_output_columns);

  switch (join_kind) {
    case JoinOp::kInnerJoin:
    case JoinOp::kCrossApply:
      // We can freely push down any conjunct through an inner join.
      GOOGLESQL_RETURN_IF_ERROR(CanPushFilterConjunctDownFromInnerJoinCondition(
          conjunct_info, left_output_columns, right_output_columns,
          push_down_to_left_input, push_down_to_right_input));
      *push_down_to_join_condition =
          !*push_down_to_left_input && !*push_down_to_right_input;
      return absl::OkStatus();
    case JoinOp::kLeftOuterJoin:
    case JoinOp::kOuterApply:
      // Conjuncts that don't reference the right can be pushed down.
      if (!references_right_column) {
        *push_down_to_left_input = true;
      }
      return absl::OkStatus();
    case JoinOp::kRightOuterJoin:
      // Symmetric to left outer join.
      if (!references_left_column) {
        *push_down_to_right_input = true;
      }
      return absl::OkStatus();
    case JoinOp::kFullOuterJoin:
      // Analogous to left/right outer join.
      return absl::OkStatus();
  }
}

absl::Status Algebrizer::CanPushFilterConjunctDownFromInnerJoinCondition(
    const FilterConjunctInfo& conjunct_info,
    const absl::flat_hash_set<ResolvedColumn>& left_output_columns,
    const absl::flat_hash_set<ResolvedColumn>& right_output_columns,
    bool* push_down_to_left_input, bool* push_down_to_right_input) {
  *push_down_to_left_input = false;
  *push_down_to_right_input = false;

  if (!conjunct_info.is_non_volatile) return absl::OkStatus();

  const bool references_left_column =
      Intersects(conjunct_info.referenced_columns, left_output_columns);
  const bool references_right_column =
      Intersects(conjunct_info.referenced_columns, right_output_columns);

  *push_down_to_left_input = !references_right_column;
  *push_down_to_right_input = !references_left_column;

  return absl::OkStatus();
}

absl::Status Algebrizer::AlgebrizeJoinConditionForHashJoin(
    const absl::flat_hash_set<ResolvedColumn>& left_output_columns,
    const absl::flat_hash_set<ResolvedColumn>& right_output_columns,
    std::vector<FilterConjunctInfo*>* conjuncts_with_push_down,
    std::vector<JoinOp::HashJoinEqualityExprs>* hash_join_equality_exprs) {
  for (auto i = conjuncts_with_push_down->rbegin();
       i != conjuncts_with_push_down->rend(); ++i) {
    FilterConjunctInfo* conjunct_info = *i;
    GOOGLESQL_RET_CHECK(!conjunct_info->redundant);

    JoinOp::HashJoinEqualityExprs equality_exprs;
    GOOGLESQL_ASSIGN_OR_RETURN(
        const bool populated_equality_exprs,
        TryAlgebrizeFilterConjunctAsHashJoinEqualityExprs(
            *conjunct_info, left_output_columns, right_output_columns,
            hash_join_equality_exprs->size(), &equality_exprs));
    if (populated_equality_exprs) {
      hash_join_equality_exprs->push_back(std::move(equality_exprs));
      conjunct_info->redundant = true;
    }
  }

  return absl::OkStatus();
}

// Returns true if 'a' is a subset of 'b'.
static bool IsSubsetOf(const absl::flat_hash_set<ResolvedColumn>& a,
                       const absl::flat_hash_set<ResolvedColumn>& b) {
  for (const ResolvedColumn& column : a) {
    if (!b.contains(column)) return false;
  }
  return true;
}

absl::StatusOr<bool>
Algebrizer::TryAlgebrizeFilterConjunctAsHashJoinEqualityExprs(
    const FilterConjunctInfo& conjunct_info,
    const absl::flat_hash_set<ResolvedColumn>& left_output_columns,
    const absl::flat_hash_set<ResolvedColumn>& right_output_columns,
    int num_previous_equality_exprs,
    JoinOp::HashJoinEqualityExprs* equality_exprs) {
  if (!conjunct_info.is_non_volatile) return false;
  if (conjunct_info.kind != FilterConjunctInfo::kEquals) return false;

  GOOGLESQL_RET_CHECK_EQ(conjunct_info.arguments.size(), 2);
  const ResolvedExpr* first_arg = conjunct_info.arguments[0];
  const ResolvedExpr* second_arg = conjunct_info.arguments[1];
  const absl::flat_hash_set<ResolvedColumn>* first_arg_columns =
      &conjunct_info.argument_columns[0];
  const absl::flat_hash_set<ResolvedColumn>* second_arg_columns =
      &conjunct_info.argument_columns[1];

  if (IsSubsetOf(*first_arg_columns, right_output_columns) &&
      IsSubsetOf(*second_arg_columns, left_output_columns)) {
    std::swap(first_arg, second_arg);
    std::swap(first_arg_columns, second_arg_columns);
  }

  if (!IsSubsetOf(*first_arg_columns, left_output_columns)) {
    return false;
  }
  if (!IsSubsetOf(*second_arg_columns, right_output_columns)) {
    return false;
  }

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> algebrized_first_arg,
                   AlgebrizeExpression(first_arg));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> algebrized_second_arg,
                   AlgebrizeExpression(second_arg));

  const int next_var_number = num_previous_equality_exprs + 1;
  const VariableId left_var =
      variable_gen_->GetNewVariableName(absl::StrCat("a", next_var_number));
  const VariableId right_var =
      variable_gen_->GetNewVariableName(absl::StrCat("b", next_var_number));

  equality_exprs->left_expr =
      std::make_unique<ExprArg>(left_var, std::move(algebrized_first_arg));
  equality_exprs->right_expr =
      std::make_unique<ExprArg>(right_var, std::move(algebrized_second_arg));

  return true;
}
absl::Status Algebrizer::RemapJoinColumns(
    const ResolvedColumnList& columns,
    std::vector<std::unique_ptr<ExprArg>>* output) {
  absl::flat_hash_set<int> columns_seen;
  for (int i = 0; i < columns.size(); ++i) {
    if (!googlesql_base::InsertIfNotPresent(&columns_seen, columns[i].column_id())) {
      continue;  // Skip columns that we have already seen.
    }
    VariableId old_var =
        column_to_variable_->GetVariableNameFromColumn(columns[i]);
    VariableId new_var =
        column_to_variable_->AssignNewVariableToColumn(columns[i]);
    GOOGLESQL_ASSIGN_OR_RETURN(auto deref_expr,
                     DerefExpr::Create(old_var, columns[i].type()));
    output->push_back(
        std::make_unique<ExprArg>(new_var, std::move(deref_expr)));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeFilterScanInternal(
    const ResolvedExpr* filter_expr,
    const ScanAlgebrizerCallback& scan_algebrizer_cb,
    std::vector<FilterConjunctInfo*>* active_conjuncts) {
  GOOGLESQL_RET_CHECK(filter_expr != nullptr);
  std::vector<std::unique_ptr<FilterConjunctInfo>> conjunct_infos;
  GOOGLESQL_RETURN_IF_ERROR(AddFilterConjunctsTo(filter_expr, &conjunct_infos));
  // Push the new conjuncts onto 'active_conjuncts'.
  PushConjuncts(conjunct_infos, active_conjuncts);

  // Algebrize the input scan.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> input,
                   scan_algebrizer_cb(active_conjuncts));

  // Restore 'active_conjuncts'.
  GOOGLESQL_RETURN_IF_ERROR(PopConjuncts(conjunct_infos, active_conjuncts));

  GOOGLESQL_ASSIGN_OR_RETURN(auto algebrized_conjuncts,
                   AlgebrizeNonRedundantConjuncts(conjunct_infos));

  // Algebrize the filter.
  return ApplyAlgebrizedFilterConjuncts(std::move(input),
                                        std::move(algebrized_conjuncts));
}

absl::StatusOr<std::unique_ptr<RelationalOp>> Algebrizer::AlgebrizeFilterScan(
    const ResolvedFilterScan* filter_scan,
    std::vector<FilterConjunctInfo*>* active_conjuncts) {
  const ResolvedScan* input_scan = filter_scan->input_scan();
  const ResolvedExpr* filter_expr = filter_scan->filter_expr();
  auto scan_algebrizer_cb =
      [this, input_scan](std::vector<FilterConjunctInfo*>* active_conjuncts) {
        return AlgebrizeScan(input_scan, active_conjuncts);
      };
  return AlgebrizeFilterScanInternal(filter_expr, scan_algebrizer_cb,
                                     active_conjuncts);
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::ApplyAlgebrizedFilterConjuncts(
    std::unique_ptr<RelationalOp> input,
    std::vector<std::unique_ptr<ValueExpr>> algebrized_conjuncts) {
  std::unique_ptr<RelationalOp> rel_op;
  if (algebrized_conjuncts.empty()) {
    // No FilterOp needed, just use the input directly.
    rel_op = std::move(input);
  } else {
    std::unique_ptr<ValueExpr> filter;
    if (algebrized_conjuncts.size() == 1) {
      filter = std::move(algebrized_conjuncts[0]);
    } else {
      GOOGLESQL_ASSIGN_OR_RETURN(
          filter,
          BuiltinScalarFunction::CreateCall(
              FunctionKind::kAnd, language_options_, types::BoolType(),
              ConvertValueExprsToAlgebraArgs(std::move(algebrized_conjuncts)),
              ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));
    }

    GOOGLESQL_ASSIGN_OR_RETURN(rel_op,
                     FilterOp::Create(std::move(filter), std::move(input)));
  }
  return rel_op;
}

absl::StatusOr<std::unique_ptr<RelationalOp>> Algebrizer::AlgebrizeSampleScan(
    const ResolvedSampleScan* sample_scan,
    std::vector<FilterConjunctInfo*>* active_conjuncts) {
  // Algebrize the input scan.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> input,
                   AlgebrizeScan(sample_scan->input_scan()));

  // Algebrize the method and unit into a sample scan method.
  auto algebrize_method = [&]() -> absl::StatusOr<SampleScanOp::Method> {
    const std::string& method = sample_scan->method();
    googlesql::ResolvedSampleScanEnums::SampleUnit unit = sample_scan->unit();

    // Handle BERNOULLI/PERCENT.
    // Error on BERNOULLI/ROWS.
    if (googlesql_base::CaseEqual(method, "BERNOULLI")) {
      if (unit == googlesql::ResolvedSampleScanEnums::PERCENT) {
        return SampleScanOp::Method::kBernoulliPercent;
      }
      return googlesql_base::InvalidArgumentErrorBuilder()
             << "BERNOULLI/ROWS is not supported";
    }

    // Handle RESERVOIR/ROWS.
    // Error on RESERVOIR/PERCENT.
    if (googlesql_base::CaseEqual(method, "RESERVOIR")) {
      if (unit == googlesql::ResolvedSampleScanEnums::ROWS) {
        return SampleScanOp::Method::kReservoirRows;
      }
      return googlesql_base::InvalidArgumentErrorBuilder()
             << "RESERVOIR/PERCENT is not supported";
    }

    // Handle SYSTEM/PERCENT as BERNOULLI/PERCENT.
    // Handle SYSTEM/ROWS as RESERVOIR/ROWS.
    if (googlesql_base::CaseEqual(method, "SYSTEM")) {
      if (unit == googlesql::ResolvedSampleScanEnums::PERCENT) {
        return SampleScanOp::Method::kBernoulliPercent;
      }
      if (unit == googlesql::ResolvedSampleScanEnums::ROWS) {
        return SampleScanOp::Method::kReservoirRows;
      }
    }

    return googlesql_base::InvalidArgumentErrorBuilder()
           << "Unknown scan method " << method;
  };
  GOOGLESQL_ASSIGN_OR_RETURN(SampleScanOp::Method method, algebrize_method());

  // Algebrize the size, which represents the % likelihood to include the row if
  // using BERNOULLI or the # of rows if using RESERVOIR.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> size,
                   AlgebrizeExpression(sample_scan->size()));

  // Per spec:
  // - if the sampling method is BERNOULLI, the size must be a DOUBLE.
  // - if the sampling method is RESERVOIR, the size must be a INT64.
  if (method == SampleScanOp::Method::kBernoulliPercent &&
      !size->output_type()->IsNumerical()) {
    return googlesql_base::InvalidArgumentErrorBuilder()
           << "Expected size to be a DOUBLE";
  }
  if (method == SampleScanOp::Method::kReservoirRows &&
      !size->output_type()->IsInt64()) {
    return googlesql_base::InvalidArgumentErrorBuilder() << "Expected size to be a INT64";
  }

  std::unique_ptr<ValueExpr> repeatable = nullptr;
  if (sample_scan->repeatable_argument()) {
    GOOGLESQL_ASSIGN_OR_RETURN(repeatable,
                     AlgebrizeExpression(sample_scan->repeatable_argument()));
  }

  VariableId sample_weight;
  if (sample_scan->weight_column() != nullptr) {
    sample_weight = column_to_variable_->AssignNewVariableToColumn(
        sample_scan->weight_column()->column());
  }

  std::vector<std::unique_ptr<ValueExpr>> partition_key;
  for (const auto& key_part : sample_scan->partition_by_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(auto key_part_expr, AlgebrizeExpression(key_part.get()));
    partition_key.emplace_back(std::move(key_part_expr));
  }

  return SampleScanOp::Create(method, std::move(size), std::move(repeatable),
                              std::move(input), std::move(partition_key),
                              sample_weight);
}

absl::StatusOr<std::unique_ptr<AggregateOp>> Algebrizer::AlgebrizeAggregateScan(
    const ResolvedAggregateScan* aggregate_scan) {
  return AlgebrizeAggregateScanBase(aggregate_scan,
                                    /*anonymization_options=*/std::nullopt);
}

namespace {

absl::StatusOr<AnonymizationOptions> GetAnonymizationOptions(
    const ResolvedAnonymizedAggregateScan* aggregate_scan) {
  AnonymizationOptions anonymization_options;
  for (const auto& option : aggregate_scan->anonymization_option_list()) {
    if (absl::AsciiStrToLower(option->name()) == "k_threshold") {
      GOOGLESQL_RET_CHECK(!anonymization_options.delta.has_value())
          << "Only one of anonymization options DELTA or K_THRESHOLD can "
          << "be set";
      GOOGLESQL_RET_CHECK(!anonymization_options.group_selection_threshold.has_value())
          << "Anonymization option K_THRESHOLD can only be set once";
      GOOGLESQL_RET_CHECK_EQ(option->value()->node_kind(), RESOLVED_LITERAL);
      anonymization_options.group_selection_threshold =
          option->value()->GetAs<ResolvedLiteral>()->value();
    } else if (absl::AsciiStrToLower(option->name()) == "epsilon") {
      GOOGLESQL_RET_CHECK(!anonymization_options.epsilon.has_value())
          << "Differential privacy option EPSILON can only be set once";
      GOOGLESQL_RET_CHECK_EQ(option->value()->node_kind(), RESOLVED_LITERAL);
      anonymization_options.epsilon =
          option->value()->GetAs<ResolvedLiteral>()->value();
    } else if (absl::AsciiStrToLower(option->name()) == "delta") {
      GOOGLESQL_RET_CHECK(!anonymization_options.group_selection_threshold.has_value())
          << "Only one of anonymization options DELTA or K_THRESHOLD can "
          << "be set";
      GOOGLESQL_RET_CHECK(!anonymization_options.delta.has_value())
          << "Anonymization option DELTA can only be set once";
      GOOGLESQL_RET_CHECK_EQ(option->value()->node_kind(), RESOLVED_LITERAL);
      anonymization_options.delta =
          option->value()->GetAs<ResolvedLiteral>()->value();
    } else if (absl::AsciiStrToLower(option->name()) == "kappa" ||
               absl::AsciiStrToLower(option->name()) ==
                   "max_groups_contributed") {
      GOOGLESQL_RET_CHECK(!anonymization_options.max_groups_contributed.has_value())
          << "Anonymization option MAX_GROUPS_CONTRIBUTED (aka KAPPA) can "
             "only be set once";
      GOOGLESQL_RET_CHECK_EQ(option->value()->node_kind(), RESOLVED_LITERAL);
      anonymization_options.max_groups_contributed =
          option->value()->GetAs<ResolvedLiteral>()->value();
    } else if (absl::AsciiStrToLower(option->name()) ==
               "max_rows_contributed") {
      GOOGLESQL_RET_CHECK(!anonymization_options.max_rows_contributed.has_value())
          << "Anonymization option MAX_ROWS_CONTRIBUTED can only be set once";
      GOOGLESQL_RET_CHECK_EQ(option->value()->node_kind(), RESOLVED_LITERAL);
      anonymization_options.max_rows_contributed =
          option->value()->GetAs<ResolvedLiteral>()->value();
    } else if (absl::AsciiStrToLower(option->name()) ==
               "group_selection_strategy") {
      GOOGLESQL_RET_CHECK(!anonymization_options.group_selection_strategy.has_value())
          << "Anonymization option group_selection_strategy can only be set "
             "once";
      GOOGLESQL_RET_CHECK_EQ(option->value()->node_kind(), RESOLVED_LITERAL);
      anonymization_options.group_selection_strategy =
          option->value()->GetAs<ResolvedLiteral>()->value();
    } else if (absl::AsciiStrToLower(option->name()) ==
               "min_privacy_units_per_group") {
      GOOGLESQL_RET_CHECK(!anonymization_options.min_privacy_units_per_group.has_value())
          << "Anonymization option MIN_PRIVACY_UNITS_PER_GROUP can only be set "
             "once";
      GOOGLESQL_RET_CHECK_EQ(option->value()->node_kind(), RESOLVED_LITERAL);
      anonymization_options.min_privacy_units_per_group =
          option->value()->GetAs<ResolvedLiteral>()->value();
    } else {
      return googlesql_base::InvalidArgumentErrorBuilder()
             << "Unknown or invalid anonymization option found: "
             << option->name();
    }
  }

  // Epsilon must always be explicitly set and non-NULL.
  if (!anonymization_options.epsilon.has_value() ||
      anonymization_options.epsilon->is_null()) {
    return googlesql_base::InvalidArgumentErrorBuilder()
           << "Anonymization option EPSILON must be set and non-NULL";
  }

  // Either k_threshold or delta must be set.
  if (!anonymization_options.delta.has_value() &&
      !anonymization_options.group_selection_threshold.has_value()) {
    return googlesql_base::InvalidArgumentErrorBuilder()
           << "Anonymization option DELTA or K_THRESHOLD must be set";
  }

  // If `max_privacy_units_per_group` is set, then delta must be set instead of
  // the k_threshold. This is so, because setting both would imply the two
  // thresholds to be `max_privacy_units_per_group` and
  // k_threshold + (max_privacy_units_per_group - 1). This is potentially
  // confusing behaviour. So it is forbidden. The check should be performed by
  // GoogleSQL, not necessarily by the engine. Thus we check here.
  GOOGLESQL_RET_CHECK(!(anonymization_options.group_selection_threshold.has_value() &&
              anonymization_options.min_privacy_units_per_group.has_value()))
      << "Option MIN_PRIVACY_UNITS_PER_GROUP can only be "
         "set if DELTA is set, but not if K_THRESHOLD is set";

  // Split epsilon across each aggregate function.
  //
  // If the `min_privacy_units_per_group` option is set, then the rewriter
  // added a column to the aggregate list counting the exact number of
  // distinct users per group/partition. Thus, in that case, the
  // `aggregate_list` contains #anon_aggregate_functions + 1 items. We have
  // to subtract 1 to get the true number of anon_aggregate_functions. This
  // is important, because the number is used in splitting the privacy
  // budget.
  int64_t number_anonymization_aggregate_functions =
      aggregate_scan->aggregate_list_size();
  if (anonymization_options.min_privacy_units_per_group.has_value() &&
      !anonymization_options.min_privacy_units_per_group->is_null()) {
    GOOGLESQL_RET_CHECK(anonymization_options.min_privacy_units_per_group->is_valid());
    number_anonymization_aggregate_functions =
        aggregate_scan->aggregate_list_size() - 1;
  }
  anonymization_options.epsilon =
      Value::Double(anonymization_options.epsilon->double_value() /
                    number_anonymization_aggregate_functions);

  // Compute group_selection_threshold from
  // delta/epsilon/max_groups_contributed, if needed.
  if (anonymization_options.delta.has_value()) {
    const Value max_groups_contributed =
        (anonymization_options.max_groups_contributed.has_value() &&
         !anonymization_options.max_groups_contributed.value().is_null())
            ? anonymization_options.max_groups_contributed.value()
            : Value::Invalid();
    GOOGLESQL_ASSIGN_OR_RETURN(
        anonymization_options.group_selection_threshold,
        googlesql::anonymization::ComputeLaplaceThresholdFromDelta(
            anonymization_options.epsilon.value(),
            anonymization_options.delta.value(), max_groups_contributed));
  }

  return anonymization_options;
}

absl::StatusOr<AnonymizationOptions> GetDifferentialPrivacyOptions(
    const ResolvedDifferentialPrivacyAggregateScan* aggregate_scan,
    anonymization::FunctionEpsilonAssigner* function_epsilon_assigner) {
  AnonymizationOptions anonymization_options;
  for (const auto& option : aggregate_scan->option_list()) {
    if (absl::AsciiStrToLower(option->name()) == "epsilon") {
      GOOGLESQL_RET_CHECK(!anonymization_options.epsilon.has_value())
          << "Differential privacy option DELTA can only be set once";
      anonymization_options.epsilon =
          Value::Double(function_epsilon_assigner->GetTotalEpsilon());
    } else if (absl::AsciiStrToLower(option->name()) == "delta") {
      GOOGLESQL_RET_CHECK(!anonymization_options.delta.has_value())
          << "Differential privacy option EPSILON can only be set once";
      GOOGLESQL_RET_CHECK_EQ(option->value()->node_kind(), RESOLVED_LITERAL);
      anonymization_options.delta =
          option->value()->GetAs<ResolvedLiteral>()->value();
    } else if (absl::AsciiStrToLower(option->name()) ==
               "max_groups_contributed") {
      GOOGLESQL_RET_CHECK(!anonymization_options.max_groups_contributed.has_value())
          << "Differential privacy option MAX_GROUPS_CONTRIBUTED can only "
             "be set once";
      GOOGLESQL_RET_CHECK_EQ(option->value()->node_kind(), RESOLVED_LITERAL);
      anonymization_options.max_groups_contributed =
          option->value()->GetAs<ResolvedLiteral>()->value();
    } else if (absl::AsciiStrToLower(option->name()) ==
               "max_rows_contributed") {
      GOOGLESQL_RET_CHECK(!anonymization_options.max_rows_contributed.has_value())
          << "Differential privacy option MAX_ROWS_CONTRIBUTED can only be set "
             "once";
      GOOGLESQL_RET_CHECK_EQ(option->value()->node_kind(), RESOLVED_LITERAL);
    } else if (absl::AsciiStrToLower(option->name()) ==
               "group_selection_strategy") {
      GOOGLESQL_RET_CHECK(!anonymization_options.group_selection_strategy.has_value())
          << "Differential privacy option group_selection_strategy can only be "
             "set once";
      GOOGLESQL_RET_CHECK_EQ(option->value()->node_kind(), RESOLVED_LITERAL);
      anonymization_options.group_selection_strategy =
          option->value()->GetAs<ResolvedLiteral>()->value();
    } else if (absl::AsciiStrToLower(option->name()) ==
               "min_privacy_units_per_group") {
      GOOGLESQL_RET_CHECK(!anonymization_options.min_privacy_units_per_group.has_value())
          << "Differential privacy option MIN_PRIVACY_UNITS_PER_GROUP can only "
             "be set once";
      GOOGLESQL_RET_CHECK_EQ(option->value()->node_kind(), RESOLVED_LITERAL);
      anonymization_options.min_privacy_units_per_group =
          option->value()->GetAs<ResolvedLiteral>()->value();
    } else if (absl::AsciiStrToLower(option->name()) ==
               "group_selection_epsilon") {
      // Ignore this option, as this option is used to rewrite the group
      // selection aggregation in the rewriter.
    } else {
      return googlesql_base::InvalidArgumentErrorBuilder()
             << "Unknown or invalid differential privacy option found: "
             << option->name();
    }
  }

  anonymization_options.epsilon =
      Value::Double(function_epsilon_assigner->GetTotalEpsilon());

  // Delta must be set.
  if (!anonymization_options.delta.has_value()) {
    return googlesql_base::InvalidArgumentErrorBuilder()
           << "Differential privacy option DELTA must be set";
  }

  if (anonymization_options.delta->double_value() < 0 ||
      !std::isfinite(anonymization_options.delta->double_value()) ||
      anonymization_options.delta->double_value() > 1) {
    return googlesql_base::InvalidArgumentErrorBuilder()
           << "Delta must be in the inclusive interval [0,1], but is "
           << anonymization_options.delta->double_value();
  }

  // Split epsilon across each aggregate function.
  //
  // If the `min_privacy_units_per_group` option is set, then the rewriter
  // added a column to the aggregate list counting the exact number of
  // distinct users per group/partition. Thus, in that case, the
  // `aggregate_list` contains #anon_aggregate_functions + 1 items. We have
  // to subtract 1 to get the true number of anon_aggregate_functions. This
  // is important, because the number is used in splitting the privacy
  // budget.

  int64_t number_anonymization_aggregate_functions =
      aggregate_scan->aggregate_list_size();
  if (anonymization_options.min_privacy_units_per_group.has_value() &&
      !anonymization_options.min_privacy_units_per_group->is_null()) {
    GOOGLESQL_RET_CHECK(anonymization_options.min_privacy_units_per_group->is_valid());
    number_anonymization_aggregate_functions =
        aggregate_scan->aggregate_list_size() - 1;
  }
  anonymization_options.epsilon =
      Value::Double(anonymization_options.epsilon->double_value() /
                    number_anonymization_aggregate_functions);

  const Value max_groups_contributed =
      (anonymization_options.max_groups_contributed.has_value() &&
       !anonymization_options.max_groups_contributed.value().is_null())
          ? anonymization_options.max_groups_contributed.value()
          : Value::Invalid();
  GOOGLESQL_ASSIGN_OR_RETURN(
      anonymization_options.group_selection_threshold,
      googlesql::anonymization::ComputeLaplaceThresholdFromDelta(
          anonymization_options.epsilon.value(),
          anonymization_options.delta.value(), max_groups_contributed));

  return anonymization_options;
}

}  // namespace

absl::StatusOr<std::vector<std::unique_ptr<ValueExpr>>>
Algebrizer::AlgebrizeGroupSelectionThresholdExpression(
    const ResolvedExpr* group_selection_threshold_expr,
    const AnonymizationOptions& anonymization_options) {
  // A ResolvedAnonymizedAggregateScan is logically two operations, a noisy
  // aggregation followed by a k-thresholding filter.
  //
  // Build the group selection thresholding filter op comparing
  // group_selection_threshold_expr to the group_selection_threshold
  // AnonymizationOption, and apply it to the aggregate op.
  std::unique_ptr<ValueExpr> val_op;
  std::vector<std::unique_ptr<ValueExpr>> arguments;

  GOOGLESQL_RET_CHECK(anonymization_options.group_selection_threshold.has_value());
  GOOGLESQL_ASSIGN_OR_RETURN(
      val_op, ConstExpr::Create(
                  anonymization_options.group_selection_threshold.value()));
  arguments.push_back(std::move(val_op));

  GOOGLESQL_RET_CHECK(group_selection_threshold_expr)
      << "ResolvedAnonymizedAggregateScan encountered that has not been "
         "rewritten";
  GOOGLESQL_RET_CHECK(group_selection_threshold_expr->Is<ResolvedColumnRef>() ||
            group_selection_threshold_expr->Is<ResolvedGetProtoField>() ||
            group_selection_threshold_expr->Is<ResolvedFunctionCall>());

  if (group_selection_threshold_expr->Is<ResolvedColumnRef>()) {
    const ResolvedColumn& column =
        group_selection_threshold_expr->GetAs<ResolvedColumnRef>()->column();
    GOOGLESQL_ASSIGN_OR_RETURN(const VariableId variable_id,
                     column_to_variable_->LookupVariableNameForColumn(column));
    GOOGLESQL_ASSIGN_OR_RETURN(
        val_op,
        DerefExpr::Create(variable_id, group_selection_threshold_expr->type()));

  } else if (group_selection_threshold_expr->Is<ResolvedGetProtoField>()) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        val_op,
        AlgebrizeGetProtoField(
            group_selection_threshold_expr->GetAs<ResolvedGetProtoField>()));
  } else if (group_selection_threshold_expr->Is<ResolvedFunctionCall>()) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        val_op,
        AlgebrizeFunctionCall(
            group_selection_threshold_expr->GetAs<ResolvedFunctionCall>()));
  }
  arguments.push_back(std::move(val_op));
  GOOGLESQL_ASSIGN_OR_RETURN(val_op,
                   BuiltinScalarFunction::CreateCall(
                       FunctionKind::kLessOrEqual, language_options_,
                       type_factory_->get_bool(),
                       ConvertValueExprsToAlgebraArgs(std::move(arguments)),
                       ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));
  std::vector<std::unique_ptr<ValueExpr>> algebrized_conjuncts;
  algebrized_conjuncts.push_back(std::move(val_op));
  return algebrized_conjuncts;
}

absl::StatusOr<std::unique_ptr<AggregateArg>> Algebrizer::AlgebrizeGroupingCall(
    const ResolvedGroupingCall* grouping_call,
    std::optional<AnonymizationOptions> anonymization_options,
    absl::flat_hash_set<ResolvedColumn>& output_columns,
    absl::flat_hash_map<std::string, int>& key_to_index_map) {
  const ResolvedColumnRef* grouping_call_expr =
      grouping_call->group_by_column();
  const ResolvedColumn& column = grouping_call->output_column();
  if (!anonymization_options.has_value()) {
    // Sanity check that all aggregate functions appear in the output column
    // list, so that we don't accidentally return an aggregate function that
    // the analyzer pruned from the scan. (If it did that, it should have
    // pruned the aggregate function as well.)
    GOOGLESQL_RET_CHECK(output_columns.contains(column)) << column.DebugString();
  }

  // Add the GROUPING function to the output.
  const VariableId agg_variable_name =
      column_to_variable_->AssignNewVariableToColumn(column);

  const std::string arg_name = grouping_call_expr->column().name();
  const int* key_index = googlesql_base::FindOrNull(key_to_index_map, arg_name);
  GOOGLESQL_RET_CHECK_NE(key_index, nullptr) << "GROUPING function argument " << arg_name
                                   << " not found in key_to_index_map";

  // GROUPING function is different from other aggregate functions, the output
  // is calculated based on whether its argument is in the current grouping
  // set. It doesn't aggregate the actual value of the argument. Here we
  // simplify its argument to the key index, so at the runtime we can quickly
  // check whether its argument is included in the current grouping set by bit
  // operation at the grouping set id.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> argument_expr,
                   ConstExpr::Create(Value::Int64(*key_index)));
  std::vector<std::unique_ptr<ValueExpr>> args;
  args.push_back(std::move(argument_expr));

  std::unique_ptr<AggregateFunctionBody> function_body =
      std::make_unique<BuiltinAggregateFunction>(
          FunctionKind::kGrouping, type_factory_->get_int64(),
          /*num_input_fields=*/1, type_factory_->get_int64());
  return AggregateArg::Create(agg_variable_name, std::move(function_body),
                              std::move(args));
}

absl::StatusOr<std::unique_ptr<AggregateOp>>
Algebrizer::AlgebrizeAggregateScanBase(
    const ResolvedAggregateScanBase* aggregate_scan,
    std::optional<AnonymizationOptions> anonymization_options) {
  // Algebrize the relational input of the aggregate.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> input,
                   AlgebrizeScan(aggregate_scan->input_scan()));
  // Build the list of grouping keys.
  std::vector<std::unique_ptr<KeyArg>> keys;
  // The map from the key name to key index. The key index is used to generate
  // grouping set ids.
  absl::flat_hash_map<std::string, int> key_to_index_map;
  GOOGLESQL_RET_CHECK(aggregate_scan->collation_list().empty() ||
            aggregate_scan->collation_list().size() ==
                aggregate_scan->group_by_list_size())
      << "collation_list must be empty or has the same size as group_by_list";
  for (int i = 0; i < aggregate_scan->group_by_list_size(); ++i) {
    const ResolvedComputedColumn* key_expr = aggregate_scan->group_by_list(i);
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> key,
                     AlgebrizeExpression(key_expr->expr()));
    const VariableId key_variable_name =
        column_to_variable_->AssignNewVariableToColumn(key_expr->column());
    key_to_index_map[key_expr->column().name()] = i;
    keys.push_back(std::make_unique<KeyArg>(key_variable_name, std::move(key)));
    if (!aggregate_scan->collation_list().empty() &&
        !aggregate_scan->collation_list(i).Empty()) {
      std::unique_ptr<ValueExpr> group_by_collation;
      GOOGLESQL_ASSIGN_OR_RETURN(group_by_collation,
                       AlgebrizeResolvedCollation(
                           aggregate_scan->collation_list(i), type_factory_));
      keys.back()->set_collation(std::move(group_by_collation));
    }
  }

  // Build the set of output columns.
  absl::flat_hash_set<ResolvedColumn> output_columns;
  output_columns.reserve(aggregate_scan->column_list_size());
  for (const ResolvedColumn& column : aggregate_scan->column_list()) {
    GOOGLESQL_RET_CHECK(output_columns.insert(column).second) << column.DebugString();
  }

  // Build the list of aggregate functions.
  std::vector<std::unique_ptr<AggregateArg>> aggregators;
  for (const std::unique_ptr<const ResolvedComputedColumnBase>& agg_expr :
       aggregate_scan->aggregate_list()) {
    const ResolvedColumn& column =
        agg_expr->GetAs<ResolvedComputedColumnImpl>()->column();
    if (!anonymization_options.has_value()) {
      // Sanity check that all aggregate functions appear in the output column
      // list, so that we don't accidentally return an aggregate function that
      // the analyzer pruned from the scan. (If it did that, it should have
      // pruned the aggregate function as well.)
      GOOGLESQL_RET_CHECK(output_columns.contains(column)) << column.DebugString();
    }

    // Add the aggregate function to the output.
    const VariableId agg_variable_name =
        column_to_variable_->AssignNewVariableToColumn(column);

    VariableId side_effects_variable;
    if (agg_expr->Is<ResolvedDeferredComputedColumn>()) {
      auto deferred = agg_expr->GetAs<ResolvedDeferredComputedColumn>();
      side_effects_variable = column_to_variable_->AssignNewVariableToColumn(
          deferred->side_effect_column());
    }

    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<AggregateArg> agg,
                     AlgebrizeAggregateFn(
                         agg_variable_name, anonymization_options,
                         /*filter=*/nullptr,
                         agg_expr->GetAs<ResolvedComputedColumnImpl>()->expr(),
                         side_effects_variable));
    aggregators.push_back(std::move(agg));
  }

  // Algebrize the list of GROUPING function calls.
  for (const std::unique_ptr<const ResolvedGroupingCall>& grouping_call :
       aggregate_scan->grouping_call_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<AggregateArg> agg,
        AlgebrizeGroupingCall(grouping_call.get(), anonymization_options,
                              output_columns, key_to_index_map));
    aggregators.push_back(std::move(agg));
  }

  std::vector<int64_t> grouping_sets;
  if (!aggregate_scan->grouping_set_list().empty()) {
    // Before GROUPING SETS is supported, rollup columns are stored in
    // rollup_column_list, and they are expanded to grouping_set_list by
    // default. So this is just an sanity check to verify the grouping sets size
    // matches the number of rollup columns, we still extract grouping sets from
    // the grouping_set_list.
    if (!aggregate_scan->rollup_column_list().empty()) {
      GOOGLESQL_RET_CHECK_EQ(aggregate_scan->grouping_set_list_size(),
                   aggregate_scan->rollup_column_list_size() + 1);
      // Mark columns accessed in rollup_column_list.
      absl::c_for_each(
          aggregate_scan->rollup_column_list(),
          [](const std::unique_ptr<const ResolvedColumnRef>& column_ref) {
            column_ref->MarkFieldsAccessed();
          });
    }

    // Make sure we can use an int64 to represent the grouping set id.
    if (keys.size() > AggregateOp::kMaxColumnsInGroupingSet) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Too many columns in grouping sets, at most %d columns are allowed",
          AggregateOp::kMaxColumnsInGroupingSet));
    }
    for (const std::unique_ptr<const ResolvedGroupingSetBase>&
             grouping_set_base_node : aggregate_scan->grouping_set_list()) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::vector<int64_t> grouping_set_ids,
                       ConvertGroupingSetToGroupingSetIds(
                           grouping_set_base_node.get(), key_to_index_map,
                           AggregateOp::kMaxGroupingSets));
      // Sanity check the grouping set size in grouping sets.
      if (grouping_sets.size() + grouping_set_ids.size() >
          AggregateOp::kMaxGroupingSets) {
        return absl::InvalidArgumentError(absl::StrFormat(
            "Too many grouping sets, at most %d grouping sets are allowed",
            AggregateOp::kMaxGroupingSets));
      }
      absl::c_move(std::move(grouping_set_ids),
                   std::back_inserter(grouping_sets));
    }
  }
  absl::c_sort(grouping_sets);

  return AggregateOp::Create(std::move(keys), std::move(aggregators),
                             std::move(input), grouping_sets);
}

namespace {

absl::StatusOr<functions::DifferentialPrivacyEnums::GroupSelectionStrategy>
GetGroupSelectionStrategy(const AnonymizationOptions& anonymization_options) {
  if (!anonymization_options.group_selection_strategy.has_value()) {
    // The current default is LAPLACE_THRESHOLD.
    return functions::DifferentialPrivacyEnums::LAPLACE_THRESHOLD;
  }
  GOOGLESQL_RET_CHECK(anonymization_options.group_selection_strategy->type()->IsEnum());
  const int enum_value =
      anonymization_options.group_selection_strategy->enum_value();
  GOOGLESQL_RET_CHECK(functions::DifferentialPrivacyEnums::GroupSelectionStrategy_IsValid(
      enum_value));
  return static_cast<
      functions::DifferentialPrivacyEnums::GroupSelectionStrategy>(enum_value);
}

}  // namespace

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeAnonymizedAggregateScan(
    const ResolvedAnonymizedAggregateScan* aggregate_scan) {
  GOOGLESQL_ASSIGN_OR_RETURN(AnonymizationOptions anonymization_options,
                   GetAnonymizationOptions(aggregate_scan));
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<AggregateOp> relation_op,
      AlgebrizeAggregateScanBase(aggregate_scan, anonymization_options));

  std::vector<std::unique_ptr<ValueExpr>> algebrized_conjuncts;
  GOOGLESQL_ASSIGN_OR_RETURN(
      const functions::DifferentialPrivacyEnums::GroupSelectionStrategy
          group_selection_strategy,
      GetGroupSelectionStrategy(anonymization_options));
  switch (group_selection_strategy) {
    case functions::DifferentialPrivacyEnums::LAPLACE_THRESHOLD: {
      GOOGLESQL_ASSIGN_OR_RETURN(
          algebrized_conjuncts,
          AlgebrizeGroupSelectionThresholdExpression(
              aggregate_scan->k_threshold_expr(), anonymization_options));
      break;
    }
    case functions::DifferentialPrivacyEnums::PUBLIC_GROUPS: {
      // The following should have been already checked in the GoogleSQL
      // analyzer. Thus we GOOGLESQL_RET_CHECK here.
      GOOGLESQL_RET_CHECK(!anonymization_options.min_privacy_units_per_group.has_value())
          << "The MIN_PRIVACY_UNITS_PER_GROUP option must not be specified if "
             "GROUP_SELECTION_STRATEGY=PUBLIC_GROUPS";
      break;
    }
    default:
      GOOGLESQL_RET_CHECK_FAIL()
          << "Group selection strategy "
          << functions::DifferentialPrivacyEnums::GroupSelectionStrategy_Name(
                 group_selection_strategy)
          << " is not implemented";
  }
  return ApplyAlgebrizedFilterConjuncts(std::move(relation_op),
                                        std::move(algebrized_conjuncts));
}

namespace {

// Evaluator that can be used to evaluate expressions in differential privacy
// options and parameters of DP aggregation functions.  We need to be careful to
// not run into a recursive loop.
class DpOptionAndParameterEvalautor final : public ConstantEvaluator {
 public:
  DpOptionAndParameterEvalautor(const LanguageOptions& language_options,
                                const AlgebrizerOptions& algebrizer_options,
                                TypeFactory* type_factory)
      : language_options_(language_options),
        algebrizer_options_(algebrizer_options),
        type_factory_(type_factory) {}

  ~DpOptionAndParameterEvalautor() override = default;

  absl::StatusOr<Value> Evaluate(
      const ResolvedExpr& constant_expression) override {
    std::unique_ptr<ValueExpr> algebrized_expression;
    Parameters parameters;
    ParameterMap column_map;
    SystemVariablesAlgebrizerMap system_variables_map;
    GOOGLESQL_RETURN_IF_ERROR(Algebrizer::AlgebrizeExpression(
        language_options_, algebrizer_options_, type_factory_,
        &constant_expression, &algebrized_expression, &parameters, &column_map,
        &system_variables_map));

    EvaluationContext context{EvaluationOptions()};
    TupleSlot result;
    {
      absl::Status status;
      algebrized_expression->EvalSimple({}, &context, &result, &status);
      GOOGLESQL_RETURN_IF_ERROR(status);
    }
    return result.value();
  }

 private:
  const LanguageOptions& language_options_;
  const AlgebrizerOptions& algebrizer_options_;
  TypeFactory* type_factory_;
};

}  // namespace

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeDifferentialPrivacyAggregateScan(
    const ResolvedDifferentialPrivacyAggregateScan* aggregate_scan) {
  auto evaluator = std::make_unique<DpOptionAndParameterEvalautor>(
      language_options_, algebrizer_options_, type_factory_);
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<anonymization::FunctionEpsilonAssigner> epsilon_assigner,
      anonymization::FunctionEpsilonAssigner::CreateFromScan(
          aggregate_scan, std::move(evaluator)));
  GOOGLESQL_ASSIGN_OR_RETURN(
      AnonymizationOptions anonymization_options,
      GetDifferentialPrivacyOptions(aggregate_scan, epsilon_assigner.get()));
  anonymization_options.epsilon_assigner = epsilon_assigner.get();
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<AggregateOp> relation_op,
      AlgebrizeAggregateScanBase(aggregate_scan, anonymization_options));

  std::vector<std::unique_ptr<ValueExpr>> algebrized_conjuncts;
  GOOGLESQL_ASSIGN_OR_RETURN(
      const functions::DifferentialPrivacyEnums::GroupSelectionStrategy
          group_selection_strategy,
      GetGroupSelectionStrategy(anonymization_options));
  switch (group_selection_strategy) {
    case functions::DifferentialPrivacyEnums::LAPLACE_THRESHOLD: {
      GOOGLESQL_ASSIGN_OR_RETURN(algebrized_conjuncts,
                       AlgebrizeGroupSelectionThresholdExpression(
                           aggregate_scan->group_selection_threshold_expr(),
                           anonymization_options));
      break;
    }
    case functions::DifferentialPrivacyEnums::PUBLIC_GROUPS: {
      // The following should have been already checked in the GoogleSQL
      // analyzer. Thus we GOOGLESQL_RET_CHECK here.
      GOOGLESQL_RET_CHECK(!anonymization_options.min_privacy_units_per_group.has_value())
          << "The MIN_PRIVACY_UNITS_PER_GROUP option must not be specified if "
             "GROUP_SELECTION_STRATEGY=PUBLIC_GROUPS";
      break;
    }
    default:
      GOOGLESQL_RET_CHECK_FAIL()
          << "Group selection strategy "
          << functions::DifferentialPrivacyEnums::GroupSelectionStrategy_Name(
                 group_selection_strategy)
          << " is not implemented";
  }
  return ApplyAlgebrizedFilterConjuncts(std::move(relation_op),
                                        std::move(algebrized_conjuncts));
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeAggregationThresholdAggregateScan(
    const ResolvedAggregationThresholdAggregateScan* aggregate_scan) {
  return AlgebrizeAggregateScanBase(aggregate_scan,
                                    /*anonymization_options=*/std::nullopt);
}

absl::StatusOr<std::unique_ptr<RelationalOp>> Algebrizer::AlgebrizeAnalyticScan(
    const ResolvedAnalyticScan* analytic_scan) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  return AlgebrizeAnalyticFunctionGroupList(
      analytic_scan->input_scan(), analytic_scan->function_group_list(),
      /*reverse_order=*/false);
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeAnalyticFunctionGroupList(
    const ResolvedScan* input_scan,
    absl::Span<const std::unique_ptr<const ResolvedAnalyticFunctionGroup>>
        analytic_function_groups,
    bool reverse_order) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  // Algebrize the input scan.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> relation_op,
                   AlgebrizeScan(input_scan));

  // Algebrize each ResolvedAnalyticFunctionGroup sequentially.
  std::set<ResolvedColumn> input_columns(input_scan->column_list().begin(),
                                         input_scan->column_list().end());
  bool first = true;
  for (int i = 0; i < analytic_function_groups.size(); ++i) {
    size_t idx = reverse_order ? analytic_function_groups.size() - i - 1 : i;
    const std::unique_ptr<const ResolvedAnalyticFunctionGroup>& group =
        analytic_function_groups[idx];
    GOOGLESQL_ASSIGN_OR_RETURN(relation_op,
                     AlgebrizeAnalyticFunctionGroup(
                         input_columns, group.get(), std::move(relation_op),
                         /*input_is_from_same_analytic_scan=*/!first));
    first = false;
    for (const auto& analytic_column : group->analytic_function_list()) {
      GOOGLESQL_RET_CHECK(analytic_column->Is<ResolvedComputedColumn>());
      GOOGLESQL_RET_CHECK(
          googlesql_base::InsertIfNotPresent(&input_columns, analytic_column->column()));
    }
  }

  return relation_op;
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeAnalyticFunctionGroup(
    const std::set<ResolvedColumn>& input_resolved_columns,
    const ResolvedAnalyticFunctionGroup* analytic_group,
    std::unique_ptr<RelationalOp> input_relation_op,
    bool input_is_from_same_analytic_scan) {
  const ResolvedWindowPartitioning* partition_by =
      analytic_group->partition_by();
  const ResolvedWindowOrdering* order_by = analytic_group->order_by();

  // Create a SortOp if there are non-correlated partitioning or ordering
  // expressions.
  if (partition_by != nullptr || order_by != nullptr) {
    // We use a stable sort in case there are two windows (in different analytic
    // function groups) ordering by the same thing. They must see the tuples in
    // the same order. Ideally the resolver would not give us two analytic
    // function groups with the same window, and would instead consolidate into
    // one analytic function group: b/123518026.
    GOOGLESQL_ASSIGN_OR_RETURN(
        input_relation_op,
        MaybeCreateSortForAnalyticOperator(
            input_resolved_columns, analytic_group->partition_by(),
            analytic_group->order_by(), std::move(input_relation_op),
            input_is_from_same_analytic_scan));
  }

  std::vector<std::unique_ptr<KeyArg>> partition_keys;
  std::vector<std::unique_ptr<KeyArg>> order_keys;
  absl::flat_hash_map<int, VariableId> column_to_id_map;

  if (partition_by != nullptr) {
    // Create KeyArgs for partitioning expressions.
    GOOGLESQL_RETURN_IF_ERROR(AlgebrizePartitionExpressions(
        partition_by, &column_to_id_map, &partition_keys));
  }

  if (order_by != nullptr) {
    // Create KeyArgs for ordering expressions.
    // Do not create new VariableIds, because each ordering expression
    // references a distinct column produced by the SortOp we have just created.
    // Do not drop the correlated columns, because we may need them to compute
    // range-based window boundaries.
    GOOGLESQL_RETURN_IF_ERROR(AlgebrizeOrderByItems(
        false /* drop_correlated_columns */, false /* create_new_ids */,
        order_by->order_by_item_list(), &column_to_id_map, &order_keys));
  }

  std::vector<std::unique_ptr<AnalyticArg>> analytic_args;
  for (const auto& analytic_column : analytic_group->analytic_function_list()) {
    GOOGLESQL_RET_CHECK(analytic_column->Is<ResolvedComputedColumn>());
    GOOGLESQL_RET_CHECK_EQ(RESOLVED_ANALYTIC_FUNCTION_CALL,
                 analytic_column->expr()->node_kind());
    const ResolvedAnalyticFunctionCall* analytic_function_call =
        static_cast<const ResolvedAnalyticFunctionCall*>(
            analytic_column->expr());
    // TODO: Support analytic functions with collations.
    if (!analytic_function_call->collation_list().empty() &&
        !IsAnalyticFunctionCollationSupported(
            analytic_function_call->signature())) {
      GOOGLESQL_RET_CHECK_EQ(analytic_function_call->collation_list().size(), 1);
      return absl::UnimplementedError(absl::Substitute(
          "Analytic function '$0' with collation '$1' is not supported",
          analytic_function_call->function()->Name(),
          analytic_function_call->collation_list()[0].DebugString()));
    }

    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<AnalyticArg> analytic_arg,
                     AlgebrizeAnalyticFunctionCall(
                         column_to_variable_->AssignNewVariableToColumn(
                             analytic_column->column()),
                         analytic_function_call));
    analytic_args.push_back(std::move(analytic_arg));
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<RelationalOp> analytic_op,
      AnalyticOp::Create(
          std::move(partition_keys), std::move(order_keys),
          std::move(analytic_args), std::move(input_relation_op),
          // Don't allow scrambling between AnalyticOps representing
          // AnalyticFunctionGroups corresponding to the same AnalyticScan
          // because that might cause ties between partitioning and ordering
          // keys to be resolved differently for windows that are conceptually
          // the same. As above, ideally the resolver would not give us two
          // analytic function groups with the same window, and would instead
          // consolidate into one analytic function group: b/123518026.
          /*preserves_order=*/input_is_from_same_analytic_scan));
  return analytic_op;
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::MaybeCreateSortForAnalyticOperator(
    const std::set<ResolvedColumn>& input_resolved_columns,
    const ResolvedWindowPartitioning* partition_by,
    const ResolvedWindowOrdering* order_by,
    std::unique_ptr<RelationalOp> input_relation_op, bool require_stable_sort) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  std::vector<std::unique_ptr<KeyArg>> sort_keys;
  // Map from each referenced column to its VariableId from the input.
  absl::flat_hash_map<int, VariableId> column_to_id_map;

  if (partition_by != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(AlgebrizePartitionExpressions(
        partition_by, &column_to_id_map, &sort_keys));
  }

  if (order_by != nullptr) {
    // Create a new VariableId for each ordering expression, because an
    // ordering expression might appear in the order by list multiple times.
    // The SortOp produces a column for each occurrence, and
    // each output column cannot have the same VariableId. We do not eliminate
    // duplicate ordering columns because they can have different collation
    // names. If the collation names are parameters, we do not know the value
    // until we evaluate them.
    GOOGLESQL_RETURN_IF_ERROR(AlgebrizeOrderByItems(
        true /* drop_correlated_columns */, true /* create_new_ids */,
        order_by->order_by_item_list(), &column_to_id_map, &sort_keys));
  }

  if (sort_keys.empty()) {
    return input_relation_op;
  }

  GOOGLESQL_RET_CHECK(!column_to_id_map.empty());

  // Create ExprArgs for other expressions that are not partitioning or
  // ordering expressions.
  std::vector<std::unique_ptr<ExprArg>> non_sort_expressions;
  for (const ResolvedColumn& input_column : input_resolved_columns) {
    if (!googlesql_base::InsertIfNotPresent(
            &column_to_id_map, input_column.column_id(),
            column_to_variable_->GetVariableNameFromColumn(input_column))) {
      continue;
    }

    // 'column_to_id_map' stores the VariableIds produced by the input.
    // Do not use GetVariableNameFromColumn so that we won't reference
    // a column output by the SortOp itself.
    const VariableId var =
        googlesql_base::FindOrDie(column_to_id_map, input_column.column_id());
    const VariableId new_var =
        column_to_variable_->AssignNewVariableToColumn(input_column);
    GOOGLESQL_ASSIGN_OR_RETURN(auto deref_expr,
                     DerefExpr::Create(var, input_column.type()));
    non_sort_expressions.push_back(
        std::make_unique<ExprArg>(new_var, std::move(deref_expr)));
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<RelationalOp> sort_op,
      SortOp::Create(std::move(sort_keys), std::move(non_sort_expressions),
                     /*limit=*/nullptr, /*offset=*/nullptr,
                     std::move(input_relation_op),
                     /*is_order_preserving=*/true, require_stable_sort));
  return sort_op;
}

absl::Status Algebrizer::AlgebrizeOrderByItems(
    bool drop_correlated_columns, bool create_new_ids,
    absl::Span<const std::unique_ptr<const ResolvedOrderByItem>> order_by_items,
    absl::flat_hash_map<int, VariableId>* column_to_id_map,
    std::vector<std::unique_ptr<KeyArg>>* order_by_keys) {
  for (const std::unique_ptr<const ResolvedOrderByItem>& order_by_item :
       order_by_items) {
    if (drop_correlated_columns &&
        order_by_item->column_ref()->is_correlated()) {
      // Access the column field to prevent GoogleSQL from complaining that
      // the field is not accessed.
      GOOGLESQL_RET_CHECK(order_by_item->column_ref()->column().type() != nullptr);
      continue;
    }

    const ResolvedColumn& order_column = order_by_item->column_ref()->column();

    // Both column and collation defines the sort key. As we do not know
    // collation name until we evaluate it, we cannot ignore duplicate columns
    // because they can be associated with different collations.
    googlesql_base::InsertIfNotPresent(
        column_to_id_map, order_column.column_id(),
        column_to_variable_->GetVariableNameFromColumn(order_column));

    // Do not use GetVariableNameFromColumn, because the VariableId is no longer
    // the input VariableId if the column appears multiple times and we assigned
    // a new one to it.
    const VariableId key_in =
        googlesql_base::FindOrDie(*column_to_id_map, order_column.column_id());
    const VariableId key_out =
        (create_new_ids
             ? column_to_variable_->AssignNewVariableToColumn(order_column)
             : key_in);

    // <sort_collation> is created by using algebrized result from either
    // <collation> field or <collation_name> field of <order_by_item> with the
    // following rule: If <collation> is set then use it, otherwise use
    // <collation_name>. Note that if <sort_collation> is based on <collation>
    // field, its <output_type> will be ResolvedCollation Proto type, otherwise
    // its <output_type> is String type.
    std::unique_ptr<ValueExpr> sort_collation;

    if (order_by_item->collation_name() != nullptr) {
      // Always algebrize <collation_name> to mark access.
      GOOGLESQL_ASSIGN_OR_RETURN(sort_collation,
                       AlgebrizeExpression(order_by_item->collation_name()));
    }

    if (!order_by_item->collation().Empty()) {
      GOOGLESQL_ASSIGN_OR_RETURN(sort_collation,
                       AlgebrizeResolvedCollation(order_by_item->collation(),
                                                  type_factory_));
    }

    const KeyArg::SortOrder sort_order =
        (order_by_item->is_descending() ? KeyArg::kDescending
                                        : KeyArg::kAscending);
    KeyArg::NullOrder null_order = KeyArg::kDefaultNullOrder;
    switch (order_by_item->null_order()) {
      case ResolvedOrderByItemEnums::NULLS_FIRST:
        null_order = KeyArg::kNullsFirst;
        break;
      case ResolvedOrderByItemEnums::NULLS_LAST:
        null_order = KeyArg::kNullsLast;
        break;
      case ResolvedOrderByItemEnums::ORDER_UNSPECIFIED:
        break;
      default:
        GOOGLESQL_RET_CHECK_FAIL() << "Unexpected null order: "
                         << ResolvedOrderByItemEnums::NullOrderMode_Name(
                                order_by_item->null_order());
    }
    GOOGLESQL_ASSIGN_OR_RETURN(auto deref_key,
                     DerefExpr::Create(key_in, order_column.type()));
    order_by_keys->push_back(std::make_unique<KeyArg>(
        key_out, std::move(deref_key), sort_order, null_order));
    order_by_keys->back()->set_collation(std::move(sort_collation));
  }

  return absl::OkStatus();
}

absl::Status Algebrizer::AlgebrizePartitionExpressions(
    const ResolvedWindowPartitioning* partition_by,
    absl::flat_hash_map<int, VariableId>* column_to_id_map,
    std::vector<std::unique_ptr<KeyArg>>* partition_by_keys) {
  GOOGLESQL_RET_CHECK(partition_by->collation_list().empty() ||
            partition_by->collation_list().size() ==
                partition_by->partition_by_list().size());
  for (int i = 0; i < partition_by->partition_by_list().size(); ++i) {
    const ResolvedColumnRef* partition_column_ref =
        partition_by->partition_by_list(i);
    if (partition_column_ref->is_correlated()) {
      // Access the column field to prevent GoogleSQL from complaining that
      // the field is not accessed.
      GOOGLESQL_RET_CHECK(partition_column_ref->column().type() != nullptr);
      continue;
    }

    const ResolvedColumn& partition_column = partition_column_ref->column();
    if (!googlesql_base::InsertIfNotPresent(
            column_to_id_map, partition_column.column_id(),
            column_to_variable_->GetVariableNameFromColumn(partition_column))) {
      // Skip duplicate partitioning keys.
      continue;
    }

    const VariableId key =
        googlesql_base::FindOrDie(*column_to_id_map, partition_column.column_id());
    GOOGLESQL_ASSIGN_OR_RETURN(auto deref_key,
                     DerefExpr::Create(key, partition_column.type()));
    partition_by_keys->push_back(std::make_unique<KeyArg>(
        key, std::move(deref_key), KeyArg::kAscending));

    if (!partition_by->collation_list().empty() &&
        !partition_by->collation_list(i).Empty()) {
      std::unique_ptr<ValueExpr> partition_by_collation;
      GOOGLESQL_ASSIGN_OR_RETURN(partition_by_collation,
                       AlgebrizeResolvedCollation(
                           partition_by->collation_list(i), type_factory_));
      partition_by_keys->back()->set_collation(
          std::move(partition_by_collation));
    }
  }

  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<AnalyticArg>>
Algebrizer::AlgebrizeAnalyticFunctionCall(
    const VariableId& variable,
    const ResolvedAnalyticFunctionCall* analytic_function_call) {
  const std::vector<ResolvedCollation>& collation_list =
      analytic_function_call->collation_list();
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const GoogleSqlCollator> collator,
                   GetCollatorFromResolvedCollationList(collation_list));

  std::unique_ptr<WindowFrameArg> window_frame;
  if (analytic_function_call->window_frame() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(window_frame, AlgebrizeWindowFrame(
                                       analytic_function_call->window_frame()));
  }

  if (!analytic_function_call->function()->IsGoogleSQLBuiltin()) {
    return ::googlesql_base::InvalidArgumentErrorBuilder()
           << "Non-GoogleSQL built-in functions are unsupported: "
           << analytic_function_call->function()->Name();
  }

  if (!analytic_function_call->function()->SupportsOverClause()) {
    return ::googlesql_base::InvalidArgumentErrorBuilder()
           << "Function " << analytic_function_call->function()->Name()
           << " is not an analytic function";
  }

  if (analytic_function_call->function()->mode() == Function::AGGREGATE &&
      analytic_function_call->function()->SupportsWindowFraming()) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<AggregateArg> aggregate_arg,
        AlgebrizeAggregateFn(variable, std::optional<AnonymizationOptions>(),
                             /*filter=*/nullptr, analytic_function_call,
                             /*side_effects_variable=*/VariableId()));
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<AnalyticArg> analytic_arg,
                     AggregateAnalyticArg::Create(
                         std::move(window_frame), std::move(aggregate_arg),
                         analytic_function_call->error_mode()));
    return analytic_arg;
  }

  // A general AnalyticFunctionCall cannot have a where_expr, at least for now.
  GOOGLESQL_RET_CHECK(analytic_function_call->where_expr() == nullptr);

  std::vector<std::unique_ptr<ValueExpr>> arguments;
  for (const std::unique_ptr<const ResolvedExpr>& resolved_argument :
       analytic_function_call->argument_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> value_expr,
                     AlgebrizeExpression(resolved_argument.get()));
    arguments.push_back(std::move(value_expr));
  }

  std::unique_ptr<AnalyticFunctionBody> function;
  std::vector<std::unique_ptr<ValueExpr>> non_const_arguments;
  std::vector<std::unique_ptr<ValueExpr>> const_arguments;

  const FunctionSignatureId function_id = static_cast<FunctionSignatureId>(
      analytic_function_call->signature().context_id());
  switch (function_id) {
    case FN_DENSE_RANK:
      function = std::make_unique<DenseRankFunction>();
      break;
    case FN_RANK:
      function = std::make_unique<RankFunction>();
      break;
    case FN_ROW_NUMBER:
      function = std::make_unique<RowNumberFunction>();
      break;
    case FN_PERCENT_RANK:
      function = std::make_unique<PercentRankFunction>();
      break;
    case FN_CUME_DIST:
      function = std::make_unique<CumeDistFunction>();
      break;
    case FN_NTILE:
      function = std::make_unique<NtileFunction>();
      GOOGLESQL_RET_CHECK_EQ(1, arguments.size());
      const_arguments.push_back(std::move(arguments[0]));
      break;
    case FN_IS_FIRST:
      function = std::make_unique<IsFirstFunction>();
      GOOGLESQL_RET_CHECK_EQ(1, arguments.size());
      non_const_arguments.push_back(std::move(arguments[0]));
      break;
    case FN_IS_LAST:
      function = std::make_unique<IsLastFunction>();
      GOOGLESQL_RET_CHECK_EQ(1, arguments.size());
      non_const_arguments.push_back(std::move(arguments[0]));
      break;
    case FN_LEAD:
      function = std::make_unique<LeadFunction>(analytic_function_call->type());
      GOOGLESQL_RET_CHECK(!arguments.empty());
      non_const_arguments.push_back(std::move(arguments[0]));
      // Fill in the default arguments if not provided.
      if (arguments.size() > 1) {
        const_arguments.push_back(std::move(arguments[1]));
      } else {
        GOOGLESQL_ASSIGN_OR_RETURN(auto const_expr, ConstExpr::Create(Value::Int64(1)));
        const_arguments.push_back(std::move(const_expr));
      }
      if (arguments.size() > 2) {
        const ResolvedExpr* default_expression =
            analytic_function_call->argument_list(2);
        GOOGLESQL_ASSIGN_OR_RETURN(bool default_expression_is_constant_expr,
                         IsConstantExpression(default_expression));
        if (!default_expression_is_constant_expr) {
          return ::googlesql_base::InvalidArgumentErrorBuilder()
                 << "The third argument (the default expression) to LEAD "
                 << "must be constant";
        }
        const_arguments.push_back(std::move(arguments[2]));
      } else {
        GOOGLESQL_ASSIGN_OR_RETURN(
            auto const_expr,
            ConstExpr::Create(Value::Null(analytic_function_call->type())));
        const_arguments.push_back(std::move(const_expr));
      }
      break;
    case FN_LAG:
      function = std::make_unique<LagFunction>(analytic_function_call->type());
      GOOGLESQL_RET_CHECK(!arguments.empty());
      non_const_arguments.push_back(std::move(arguments[0]));
      // Fill in the default arguments if not provided.
      if (arguments.size() > 1) {
        const_arguments.push_back(std::move(arguments[1]));
      } else {
        GOOGLESQL_ASSIGN_OR_RETURN(auto const_expr, ConstExpr::Create(Value::Int64(1)));
        const_arguments.push_back(std::move(const_expr));
      }
      if (arguments.size() > 2) {
        const ResolvedExpr* default_expression =
            analytic_function_call->argument_list(2);
        GOOGLESQL_ASSIGN_OR_RETURN(bool default_expression_is_constant_expr,
                         IsConstantExpression(default_expression));
        if (!default_expression_is_constant_expr) {
          return ::googlesql_base::InvalidArgumentErrorBuilder()
                 << "The third argument (the default expression) to LAG "
                 << "must be constant";
        }
        const_arguments.push_back(std::move(arguments[2]));
      } else {
        GOOGLESQL_ASSIGN_OR_RETURN(
            auto const_expr,
            ConstExpr::Create(Value::Null(analytic_function_call->type())));
        const_arguments.push_back(std::move(const_expr));
      }
      break;
    case FN_FIRST_VALUE:
      function = std::make_unique<FirstValueFunction>(
          analytic_function_call->type(),
          analytic_function_call->null_handling_modifier());
      GOOGLESQL_RET_CHECK_EQ(1, arguments.size());
      non_const_arguments.push_back(std::move(arguments[0]));
      break;
    case FN_LAST_VALUE:
      function = std::make_unique<LastValueFunction>(
          analytic_function_call->type(),
          analytic_function_call->null_handling_modifier());
      GOOGLESQL_RET_CHECK_EQ(1, arguments.size());
      non_const_arguments.push_back(std::move(arguments[0]));
      break;
    case FN_NTH_VALUE:
      function = std::make_unique<NthValueFunction>(
          analytic_function_call->type(),
          analytic_function_call->null_handling_modifier());
      GOOGLESQL_RET_CHECK_EQ(2, arguments.size());
      non_const_arguments.push_back(std::move(arguments[0]));
      const_arguments.push_back(std::move(arguments[1]));
      break;
    case FN_PERCENTILE_CONT:
    case FN_PERCENTILE_CONT_NUMERIC:
    case FN_PERCENTILE_CONT_BIGNUMERIC:
      function = std::make_unique<PercentileContFunction>(
          analytic_function_call->type(),
          analytic_function_call->null_handling_modifier());
      GOOGLESQL_RET_CHECK_EQ(2, arguments.size());
      non_const_arguments.push_back(std::move(arguments[0]));
      const_arguments.push_back(std::move(arguments[1]));
      break;
    case FN_PERCENTILE_DISC:
    case FN_PERCENTILE_DISC_NUMERIC:
    case FN_PERCENTILE_DISC_BIGNUMERIC:
      function = std::make_unique<PercentileDiscFunction>(
          analytic_function_call->type(),
          analytic_function_call->null_handling_modifier(),
          std::move(collator));
      GOOGLESQL_RET_CHECK_EQ(2, arguments.size());
      non_const_arguments.push_back(std::move(arguments[0]));
      const_arguments.push_back(std::move(arguments[1]));
      break;
    default:
      return ::googlesql_base::UnimplementedErrorBuilder()
             << "Function " << analytic_function_call->function()->Name()
             << " not yet implemented";
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<AnalyticArg> analytic_arg,
      NonAggregateAnalyticArg::Create(
          variable, std::move(window_frame), std::move(function),
          std::move(non_const_arguments), std::move(const_arguments),
          analytic_function_call->error_mode()));
  return analytic_arg;
}

absl::StatusOr<std::unique_ptr<WindowFrameArg>>
Algebrizer::AlgebrizeWindowFrame(const ResolvedWindowFrame* window_frame) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<WindowFrameBoundaryArg> start_boundary,
                   AlgebrizeWindowFrameExpr(window_frame->start_expr()));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<WindowFrameBoundaryArg> end_boundary,
                   AlgebrizeWindowFrameExpr(window_frame->end_expr()));

  switch (window_frame->frame_unit()) {
    case ResolvedWindowFrame::ROWS:
      return WindowFrameArg::Create(WindowFrameArg::kRows,
                                    std::move(start_boundary),
                                    std::move(end_boundary));
    case ResolvedWindowFrame::RANGE:
      return WindowFrameArg::Create(WindowFrameArg::kRange,
                                    std::move(start_boundary),
                                    std::move(end_boundary));
  }
}

absl::StatusOr<std::unique_ptr<WindowFrameBoundaryArg>>
Algebrizer::AlgebrizeWindowFrameExpr(
    const ResolvedWindowFrameExpr* window_frame_expr) {
  std::unique_ptr<ValueExpr> boundary_expr;
  if (window_frame_expr->expression() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(boundary_expr,
                     AlgebrizeExpression(window_frame_expr->expression()));
  }

  WindowFrameBoundaryArg::BoundaryType boundary_type;
  switch (window_frame_expr->boundary_type()) {
    case ResolvedWindowFrameExpr::UNBOUNDED_PRECEDING:
      boundary_type = WindowFrameBoundaryArg::kUnboundedPreceding;
      break;
    case ResolvedWindowFrameExpr::OFFSET_PRECEDING:
      boundary_type = WindowFrameBoundaryArg::kOffsetPreceding;
      break;
    case ResolvedWindowFrameExpr::CURRENT_ROW:
      boundary_type = WindowFrameBoundaryArg::kCurrentRow;
      break;
    case ResolvedWindowFrameExpr::OFFSET_FOLLOWING:
      boundary_type = WindowFrameBoundaryArg::kOffsetFollowing;
      break;
    case ResolvedWindowFrameExpr::UNBOUNDED_FOLLOWING:
      boundary_type = WindowFrameBoundaryArg::kUnboundedFollowing;
      break;
  }

  return WindowFrameBoundaryArg::Create(boundary_type,
                                        std::move(boundary_expr));
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeSetOperationScan(
    const ResolvedSetOperationScan* set_scan) {
  if (set_scan->op_type() == ResolvedSetOperationScan::UNION_ALL ||
      set_scan->op_type() == ResolvedSetOperationScan::UNION_DISTINCT) {
    return AlgebrizeUnionScan(set_scan);
  } else {
    return AlgebrizeExceptIntersectScan(set_scan);
  }
}

absl::StatusOr<std::unique_ptr<RelationalOp>> Algebrizer::AlgebrizeUnionScan(
    const ResolvedSetOperationScan* set_scan) {
  const ResolvedColumnList& output_columns = set_scan->column_list();
  int num_columns = output_columns.size();
  int num_input_relations = set_scan->input_item_list_size();
  // Algebrize all children first to ensure that no errors arise later.
  std::vector<std::unique_ptr<RelationalOp>> children;
  for (int i = 0; i < num_input_relations; ++i) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> child,
                     AlgebrizeScan(set_scan->input_item_list(i)->scan()));
    children.push_back(std::move(child));
  }
  // There is one set of column mappings per input relation of the union.
  std::vector<UnionAllOp::Input> column_mappings(num_input_relations);
  for (int i = 0; i < num_input_relations; ++i) {
    column_mappings[i].first = std::move(children[i]);
    // Connect the output columns with the input columns.
    for (int j = 0; j < num_columns; ++j) {
      ResolvedColumn column =
          set_scan->input_item_list(i)->output_column_list(j);
      VariableId variable =
          column_to_variable_->GetVariableNameFromColumn(column);
      GOOGLESQL_ASSIGN_OR_RETURN(auto deref,
                       DerefExpr::Create(variable, output_columns[j].type()));
      column_mappings[i].second.push_back(std::make_unique<ExprArg>(
          column_to_variable_->GetVariableNameFromColumn(output_columns[j]),
          std::move(deref)));
    }
  }
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> union_op,
                   UnionAllOp::Create(std::move(column_mappings)));
  if (set_scan->op_type() == ResolvedSetOperationScan::UNION_ALL) {
    return union_op;
  }
  // UNION DISTINCT needs an extra GROUP BY
  std::vector<std::unique_ptr<KeyArg>> keys;
  for (int j = 0; j < num_columns; j++) {
    // output_columns are unique, no need to eliminate duplicates.
    VariableId old_variable =
        column_to_variable_->GetVariableNameFromColumn(output_columns[j]);
    VariableId new_variable =
        column_to_variable_->AssignNewVariableToColumn(output_columns[j]);
    GOOGLESQL_ASSIGN_OR_RETURN(auto deref,
                     DerefExpr::Create(old_variable, output_columns[j].type()));
    keys.push_back(std::make_unique<KeyArg>(new_variable, std::move(deref)));
    if (output_columns[j].type_annotation_map() != nullptr &&
        output_columns[j].type_annotation_map()->Has<CollationAnnotation>()) {
      GOOGLESQL_ASSIGN_OR_RETURN(ResolvedCollation collation,
                       ResolvedCollation::MakeResolvedCollation(
                           *output_columns[j].type_annotation_map()));
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> collation_expr,
                       AlgebrizeResolvedCollation(collation, type_factory_));
      keys.back()->set_collation(std::move(collation_expr));
    }
  }
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<RelationalOp> aggr_op,
      AggregateOp::Create(std::move(keys), /*aggregators=*/{},
                          std::move(union_op), /*grouping_sets=*/{}));
  return aggr_op;
}

// Algebrization of INTERSECT / EXCEPT is done as follows.
// Let Ri(X) be a query returning columns Xi = xi1,...,xiM
//
// R0 INTERSECT DISTINCT ... RN:
//   WITH T AS
//     (SELECT X0, SUM(bit0) cnt0, ..., SUM(bitN) cntN,
//      FROM (SELECT X0, 1 bit0, 0 bit1, ..., 0 bitN FROM R0 UNION ALL
//            SELECT X1, 0 bit0, 1 bit1, ..., 0 bitN FROM R1 UNION ALL ...
//            SELECT XN, 0 bit0, 0 bit1, ..., 1 bitN FROM RN) AS U
//      GROUP BY X0)
//   SELECT X0 FROM T WHERE cnt0 > 0 AND ... cntN > 0
//
// R0 EXCEPT DISTINCT ... RN:
//   WITH T AS ...
//   SELECT X0 FROM T WHERE cnt0 > 0 AND cnt1 = 0 AND ... cntN = 0
//
// R0 INTERSECT ALL ... RN:
//   WITH T AS ...
//   SELECT X0 FROM T, Enumerate(Least(cnt0, ..., cntN))
//
// R0 EXCEPT ALL RN:
//   WITH T AS ...
//   SELECT X0 FROM T, Enumerate(cnt0 - cnt1 - ... - cntN)
//
// enumerate(min, max) is a function that returns an array of integers between
// min (incl.) and max (excl.).
absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeExceptIntersectScan(
    const ResolvedSetOperationScan* set_scan) {
  const ResolvedColumnList& output_columns = set_scan->column_list();
  int num_columns = output_columns.size();
  int num_input_relations = set_scan->input_item_list_size();
  // Algebrize all children first to ensure that no errors arise later.
  std::vector<std::unique_ptr<RelationalOp>> children;
  for (int i = 0; i < num_input_relations; ++i) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> child,
                     AlgebrizeScan(set_scan->input_item_list(i)->scan()));
    children.push_back(std::move(child));
  }
  // U = (SELECT X0, 1 bit0, 0 bit1, ..., 0 bitN FROM R0 UNION ALL
  //      SELECT X1, 0 bit0, 1 bit1, ..., 0 bitN FROM R1 UNION ALL ...
  //      SELECT XN, 0 bit0, 0 bit1, ..., 1 bitN FROM RN)

  // There is one set of column mappings per input relation of the union.
  std::vector<UnionAllOp::Input> union_inputs(num_input_relations);
  // Create bit variables.
  std::vector<VariableId> bit_variables;
  bit_variables.reserve(num_input_relations);
  for (int i = 0; i < num_input_relations; i++) {
    bit_variables.push_back(
        variable_gen_->GetNewVariableName(absl::StrCat("bit", i)));
  }
  for (int rel_idx = 0; rel_idx < num_input_relations; rel_idx++) {
    union_inputs[rel_idx].first = std::move(children[rel_idx]);
    // ProjectScan all existing columns of the input.
    for (int j = 0; j < num_columns; j++) {
      const ResolvedColumn column =
          set_scan->input_item_list(rel_idx)->output_column_list(j);
      const VariableId variable =
          column_to_variable_->GetVariableNameFromColumn(column);
      GOOGLESQL_ASSIGN_OR_RETURN(auto deref,
                       DerefExpr::Create(variable, output_columns[j].type()));
      union_inputs[rel_idx].second.push_back(std::make_unique<ExprArg>(
          column_to_variable_->GetVariableNameFromColumn(output_columns[j]),
          std::move(deref)));
    }
    // Add bit columns.
    for (int bit_idx = 0; bit_idx < num_input_relations; bit_idx++) {
      // bit variable i is set to 1 iff its index matches the relation index.
      GOOGLESQL_ASSIGN_OR_RETURN(
          auto const_expr,
          ConstExpr::Create(Value::Int64(bit_idx == rel_idx ? 1 : 0)));
      union_inputs[rel_idx].second.push_back(std::make_unique<ExprArg>(
          bit_variables[bit_idx], std::move(const_expr)));
    }
  }

  // Construct the actual union.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> query_u,
                   UnionAllOp::Create(std::move(union_inputs)));

  // Aggregate the result of the union:
  // T = SELECT X0, SUM(bit0) cnt0, ..., SUM(bitN) cntN FROM U GROUP BY X0
  // Keys: X0
  std::vector<std::unique_ptr<KeyArg>> keys;
  for (int j = 0; j < num_columns; j++) {
    VariableId old_variable =
        column_to_variable_->GetVariableNameFromColumn(output_columns[j]);
    VariableId new_variable =
        column_to_variable_->AssignNewVariableToColumn(output_columns[j]);
    GOOGLESQL_ASSIGN_OR_RETURN(auto deref,
                     DerefExpr::Create(old_variable, output_columns[j].type()));
    keys.push_back(std::make_unique<KeyArg>(new_variable, std::move(deref)));
    if (output_columns[j].type_annotation_map() != nullptr &&
        output_columns[j].type_annotation_map()->Has<CollationAnnotation>()) {
      GOOGLESQL_ASSIGN_OR_RETURN(ResolvedCollation collation,
                       ResolvedCollation::MakeResolvedCollation(
                           *output_columns[j].type_annotation_map()));
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> collation_expr,
                       AlgebrizeResolvedCollation(collation, type_factory_));
      keys.back()->set_collation(std::move(collation_expr));
    }
  }
  // Aggregators: SUM(bit_i) cnt_i
  std::vector<VariableId> cnt_vars;
  std::vector<std::unique_ptr<AggregateArg>> aggregators;
  for (int i = 0; i < num_input_relations; i++) {
    const VariableId cnt_var =
        variable_gen_->GetNewVariableName(absl::StrCat("cnt", i));
    cnt_vars.push_back(cnt_var);

    GOOGLESQL_ASSIGN_OR_RETURN(auto deref_bit,
                     DerefExpr::Create(bit_variables[i], types::Int64Type()));

    std::vector<std::unique_ptr<ValueExpr>> agg_func_args;
    agg_func_args.push_back(std::move(deref_bit));

    GOOGLESQL_ASSIGN_OR_RETURN(
        auto agg_arg,
        AggregateArg::Create(cnt_var,
                             std::make_unique<BuiltinAggregateFunction>(
                                 FunctionKind::kSum, types::Int64Type(),
                                 /*num_input_fields=*/1, types::Int64Type()),
                             std::move(agg_func_args)));
    aggregators.push_back(std::move(agg_arg));
  }
  GOOGLESQL_ASSIGN_OR_RETURN(auto query_t, AggregateOp::Create(
                                     std::move(keys), std::move(aggregators),
                                     std::move(query_u), /*grouping_sets=*/{}));

  // Add filter or cross-apply depending on the kind of set operation.
  switch (set_scan->op_type()) {
    case ResolvedSetOperationScan::EXCEPT_DISTINCT:
      // SELECT X0 FROM T WHERE 0 < cnt0 AND 0 = cnt1 AND ... 0 = cntN
    case ResolvedSetOperationScan::INTERSECT_DISTINCT: {
      // SELECT X0 FROM T WHERE 0 < cnt0 AND 0 < cnt1 AND ... 0 < cntN
      std::vector<std::unique_ptr<ValueExpr>> predicates;
      for (int i = 0; i < num_input_relations; i++) {
        auto fct_kind = (i > 0 && set_scan->op_type() ==
                                      ResolvedSetOperationScan::EXCEPT_DISTINCT)
                            ? FunctionKind::kEqual
                            : FunctionKind::kLess;

        GOOGLESQL_ASSIGN_OR_RETURN(auto const_zero, ConstExpr::Create(Value::Int64(0)));
        GOOGLESQL_ASSIGN_OR_RETURN(auto deref_cnt,
                         DerefExpr::Create(cnt_vars[i], types::Int64Type()));

        std::vector<std::unique_ptr<ValueExpr>> predicate_args;
        predicate_args.push_back(std::move(const_zero));
        predicate_args.push_back(std::move(deref_cnt));

        GOOGLESQL_ASSIGN_OR_RETURN(
            auto predicate,
            BuiltinScalarFunction::CreateCall(
                fct_kind, language_options_, types::BoolType(),
                ConvertValueExprsToAlgebraArgs(std::move(predicate_args)),
                ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));
        predicates.push_back(std::move(predicate));
      }
      GOOGLESQL_ASSIGN_OR_RETURN(
          auto condition,
          BuiltinScalarFunction::CreateCall(
              FunctionKind::kAnd, language_options_, types::BoolType(),
              ConvertValueExprsToAlgebraArgs(std::move(predicates)),
              ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::unique_ptr<RelationalOp> filter_op,
          FilterOp::Create(std::move(condition), std::move(query_t)));
      return filter_op;
    }

    case ResolvedSetOperationScan::EXCEPT_ALL: {
      // rest = cnt0 - cnt1 - ... - cntN
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> rest,
                       DerefExpr::Create(cnt_vars[0], types::Int64Type()));
      for (int i = 1; i < num_input_relations; i++) {
        GOOGLESQL_ASSIGN_OR_RETURN(auto deref_cnt,
                         DerefExpr::Create(cnt_vars[i], types::Int64Type()));

        std::vector<std::unique_ptr<ValueExpr>> args;
        args.push_back(std::move(rest));
        args.push_back(std::move(deref_cnt));

        GOOGLESQL_ASSIGN_OR_RETURN(
            rest,
            BuiltinScalarFunction::CreateCall(
                FunctionKind::kSubtract, language_options_, types::Int64Type(),
                ConvertValueExprsToAlgebraArgs(std::move(args)),
                ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));
      }
      // SELECT X0 FROM T, Enumerate(rest)
      GOOGLESQL_ASSIGN_OR_RETURN(auto enum_op, EnumerateOp::Create(std::move(rest)));
      GOOGLESQL_ASSIGN_OR_RETURN(auto join_expr, ConstExpr::Create(Value::Bool(true)));
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::unique_ptr<RelationalOp> join_op,
          JoinOp::Create(JoinOp::kCrossApply, /*equality_exprs=*/{},
                         std::move(join_expr), std::move(query_t),
                         std::move(enum_op), {}, {}));  // no variable remapping
      return join_op;
    }

    case ResolvedSetOperationScan::INTERSECT_ALL: {
      // SELECT X0 FROM T, Enumerate(Least(cnt0, ..., cntN))
      std::vector<std::unique_ptr<ValueExpr>> cnt_args;
      cnt_args.reserve(num_input_relations);
      for (int i = 0; i < num_input_relations; i++) {
        GOOGLESQL_ASSIGN_OR_RETURN(auto deref,
                         DerefExpr::Create(cnt_vars[i], types::Int64Type()));
        cnt_args.push_back(std::move(deref));
      }
      GOOGLESQL_ASSIGN_OR_RETURN(
          auto least,
          BuiltinScalarFunction::CreateCall(
              FunctionKind::kLeast, language_options_, types::Int64Type(),
              ConvertValueExprsToAlgebraArgs(std::move(cnt_args)),
              ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));
      GOOGLESQL_ASSIGN_OR_RETURN(auto enum_op, EnumerateOp::Create(std::move(least)));
      GOOGLESQL_ASSIGN_OR_RETURN(auto join_expr, ConstExpr::Create(Value::Bool(true)));
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::unique_ptr<RelationalOp> join_op,
          JoinOp::Create(JoinOp::kCrossApply, /*equality_exprs=*/{},
                         std::move(join_expr), std::move(query_t),
                         std::move(enum_op), {}, {}));  // no variable remapping
      return join_op;
    }
    default:
      return ::googlesql_base::UnimplementedErrorBuilder()
             << "Unimplemented set operation: " << set_scan->op_type();
  }
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeProjectScanInternal(
    const ResolvedColumnList& column_list,
    absl::Span<const std::unique_ptr<const ResolvedComputedColumn>> expr_list,
    const ResolvedScan* input_scan, bool is_ordered,
    std::vector<FilterConjunctInfo*>* active_conjuncts) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  // Determine the new columns and their definitions.
  absl::flat_hash_set<ResolvedColumn> defined_columns;
  GOOGLESQL_RET_CHECK(!column_list.empty());
  std::vector<std::pair<ResolvedColumn, const ResolvedExpr*>>
      defined_columns_and_exprs;
  for (const ResolvedColumn& column : column_list) {
    const ResolvedExpr* local_definition;
    if (FindColumnDefinition(expr_list, column.column_id(),
                             &local_definition)) {
      // The column is defined by this SELECT.
      GOOGLESQL_RET_CHECK(defined_columns.insert(column).second);
      defined_columns_and_exprs.emplace_back(column, local_definition);
    }
  }

  // Determine the active conjuncts for the input scan and then algebrize
  // it. Note that volatile conjuncts can be pushed through projections because
  // they will only be evaluated once in both places.
  std::vector<FilterConjunctInfo*> input_active_conjuncts;
  for (FilterConjunctInfo* info : *active_conjuncts) {
    GOOGLESQL_RET_CHECK(!info->redundant);
    if (!Intersects(info->referenced_columns, defined_columns)) {
      input_active_conjuncts.push_back(info);
    }
  }
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> input,
                   AlgebrizeScan(input_scan, &input_active_conjuncts));

  // Assign variables to the new columns and algebrize their definitions.
  std::vector<std::unique_ptr<ExprArg>> arguments;
  arguments.reserve(defined_columns_and_exprs.size());
  for (const auto& entry : defined_columns_and_exprs) {
    const ResolvedColumn& column = entry.first;
    const ResolvedExpr* expr = entry.second;
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> argument,
                     AlgebrizeExpression(expr));
    const VariableId variable =
        column_to_variable_->AssignNewVariableToColumn(column);
    arguments.push_back(
        std::make_unique<ExprArg>(variable, std::move(argument)));
    // If the ProjectScan computes a measure column from some other measure
    // column, we need to track the measure column rename. This tracking
    // happens after the algebrization for the input scan and the computed
    // expression are done.
    GOOGLESQL_RETURN_IF_ERROR(measure_column_to_expr_.TrackMeasureColumnsRenamedByExpr(
        column, *expr));
  }

  // If no columns were defined by this project then just drop it.
  if (!arguments.empty()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> compute_op,
                     ComputeOp::Create(std::move(arguments), std::move(input)));
    GOOGLESQL_RETURN_IF_ERROR(compute_op->set_is_order_preserving(is_ordered));
    return compute_op;
  } else {
    // Since 'arguments' is empty we drop the project, but that project might
    // destroy order so in that case we must update the ordered property of the
    // relation.
    GOOGLESQL_RETURN_IF_ERROR(input->set_is_order_preserving(is_ordered));
    return input;
  }
}

absl::StatusOr<std::unique_ptr<RelationalOp>> Algebrizer::AlgebrizeProjectScan(
    const ResolvedProjectScan* resolved_project,
    std::vector<FilterConjunctInfo*>* active_conjuncts) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  return AlgebrizeProjectScanInternal(
      resolved_project->column_list(), resolved_project->expr_list(),
      resolved_project->input_scan(), resolved_project->is_ordered(),
      active_conjuncts);
}

absl::StatusOr<std::unique_ptr<SortOp>> Algebrizer::AlgebrizeOrderByScan(
    const ResolvedOrderByScan* scan, std::unique_ptr<ValueExpr> limit,
    std::unique_ptr<ValueExpr> offset) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  GOOGLESQL_RET_CHECK_EQ(limit == nullptr, offset == nullptr);

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> input,
                   AlgebrizeScan(scan->input_scan()));
  // 'order_by_item_list' form the key.
  std::vector<std::unique_ptr<KeyArg>> keys;
  absl::flat_hash_map<int, VariableId> column_to_id_map;

  GOOGLESQL_RETURN_IF_ERROR(AlgebrizeOrderByItems(
      true /* drop_correlated_columns */, true /* create_new_ids */,
      scan->order_by_item_list(), &column_to_id_map, &keys));

  // Output columns that are not present in 'order_by_item_list' are non-key
  // sort values.
  std::vector<std::unique_ptr<ExprArg>> values;
  for (int i = 0; i < scan->column_list().size(); i++) {
    const ResolvedColumn& column = scan->column_list()[i];
    if (!googlesql_base::InsertIfNotPresent(
            &column_to_id_map, column.column_id(),
            column_to_variable_->GetVariableNameFromColumn(column))) {
      continue;  // Skip columns that we have already seen.
    }
    const VariableId value_in =
        googlesql_base::FindOrDie(column_to_id_map, column.column_id());
    const VariableId value_out =
        column_to_variable_->AssignNewVariableToColumn(column);
    GOOGLESQL_ASSIGN_OR_RETURN(auto deref, DerefExpr::Create(value_in, column.type()));
    values.push_back(std::make_unique<ExprArg>(value_out, std::move(deref)));
  }
  for (const auto& arg : keys) {
    std::string why_not;
    GOOGLESQL_RET_CHECK(arg->type()->SupportsOrdering(language_options_, &why_not))
        << why_not;
  }
  return SortOp::Create(std::move(keys), std::move(values), std::move(limit),
                        std::move(offset), std::move(input), scan->is_ordered(),
                        /*is_stable_sort=*/false);
}

absl::StatusOr<std::unique_ptr<AggregateOp>> Algebrizer::AlgebrizePivotScan(
    const ResolvedPivotScan* pivot_scan) {
  // Algebrize the input scan and add a computed column representing the value
  // of the FOR-expr, so that this expression can be evaluated once per row,
  // regardless of the number of pivot values.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> algebrized_input,
                   AlgebrizeScan(pivot_scan->input_scan()));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> algebrized_for_expr,
                   AlgebrizeExpression(pivot_scan->for_expr()));

  // VariableId representing the result of the FOR expr
  VariableId for_expr_var = variable_gen_->GetNewVariableName("pivot");

  // VariableId's representing each aggregate argument in a pivot expression.
  // pivot_expr_args_vars[i][j] denotes the variable referring to argument 'j'
  // of the aggregate function call corresponding to pivot expression 'i'.
  //
  // If pivot_expr_args_vars[i][j] is absl::nullopt, this means that the
  // argument expression is not projected and should be cloned in the function
  // call. This is used for constant expressions to allow for cases like the
  // delimiter of STRING_AGG(), for which we do not support reading from a
  // projected column.
  std::vector<std::vector<std::optional<VariableId>>> pivot_expr_arg_vars;
  std::vector<std::vector<const Type*>> pivot_expr_arg_types;

  std::vector<std::unique_ptr<ExprArg>> wrapped_input_exprs;
  wrapped_input_exprs.push_back(
      std::make_unique<ExprArg>(for_expr_var, std::move(algebrized_for_expr)));

  for (int i = 0; i < pivot_scan->pivot_expr_list_size(); ++i) {
    GOOGLESQL_RET_CHECK(
        pivot_scan->pivot_expr_list(i)->Is<ResolvedAggregateFunctionCall>());
    const ResolvedAggregateFunctionCall* pivot_expr_call =
        pivot_scan->pivot_expr_list(i)->GetAs<ResolvedAggregateFunctionCall>();
    // TODO: Support multi-level aggregation in PIVOT ref impl.
    GOOGLESQL_RET_CHECK(pivot_expr_call->group_by_list().empty() &&
              pivot_expr_call->group_by_aggregate_list().empty());
    pivot_expr_arg_vars.emplace_back();
    pivot_expr_arg_types.emplace_back();
    for (const auto& arg : pivot_expr_call->argument_list()) {
      GOOGLESQL_ASSIGN_OR_RETURN(bool arg_is_constant_expr,
                       IsConstantExpression(arg.get()));
      if (arg_is_constant_expr) {
        // Constant expressions are ok to clone, rather than project. In most
        // cases, this doesn't matter, but some functions take constant
        // arguments that can't be read from a projected column (the delimiter
        // argument of STRING_AGG() is one such example). For simplicity, we
        // take the clone approach for all constant expressions, whether needed
        // or not.
        pivot_expr_arg_vars.back().push_back(std::nullopt);
      } else {
        VariableId arg_var = variable_gen_->GetNewVariableName("pivot");
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> algebrized_arg,
                         AlgebrizeExpression(arg.get()));
        wrapped_input_exprs.push_back(
            std::make_unique<ExprArg>(arg_var, std::move(algebrized_arg)));
        pivot_expr_arg_vars.back().push_back(arg_var);
      }
      pivot_expr_arg_types.back().push_back(arg->type());
    }
  }

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> wrapped_input,
                   ComputeOp::Create(std::move(wrapped_input_exprs),
                                     std::move(algebrized_input)));

  // Generate a KeyArg for each group-by element.
  std::vector<std::unique_ptr<KeyArg>> keys;
  for (const auto& group_by_elem : pivot_scan->group_by_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> algebrized_groupby_elem,
                     AlgebrizeExpression(group_by_elem->expr()));
    keys.push_back(std::make_unique<KeyArg>(
        column_to_variable_->GetVariableNameFromColumn(group_by_elem->column()),
        std::move(algebrized_groupby_elem)));
  }

  // Generate an aggregator for each pivot-expr/pivot-value combination.
  std::vector<std::unique_ptr<AggregateArg>> aggregators;
  for (const auto& pivot_column : pivot_scan->pivot_column_list()) {
    const ResolvedExpr* pivot_expr =
        pivot_scan->pivot_expr_list(pivot_column->pivot_expr_index());
    const ResolvedExpr* pivot_value =
        pivot_scan->pivot_value_list(pivot_column->pivot_value_index());
    VariableId agg_result_var =
        column_to_variable_->GetVariableNameFromColumn(pivot_column->column());

    // Generate an expression which compares the FOR expr result to the pivot
    // value, which will be used as the filter to the aggregate arg.
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> algebrized_pivot_value,
                     AlgebrizeExpression(pivot_value));

    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<ValueExpr> algebrized_for_expr_result_ref,
        DerefExpr::Create(for_expr_var, pivot_scan->for_expr()->type()));

    std::vector<std::unique_ptr<ValueExpr>> compare_fn_args;
    compare_fn_args.push_back(std::move(algebrized_for_expr_result_ref));
    compare_fn_args.push_back(std::move(algebrized_pivot_value));

    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<ScalarFunctionCallExpr> algebrized_compare,
        BuiltinScalarFunction::CreateCall(
            FunctionKind::kIsNotDistinct, language_options_,
            type_factory_->get_bool(),
            ConvertValueExprsToAlgebraArgs(std::move(compare_fn_args)),
            ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));

    std::vector<std::unique_ptr<ValueExpr>> algebrized_arguments;
    int pivot_expr_idx = pivot_column->pivot_expr_index();
    for (int i = 0; i < pivot_expr_arg_vars[pivot_expr_idx].size(); ++i) {
      const std::optional<VariableId>& arg_var =
          pivot_expr_arg_vars[pivot_expr_idx][i];
      if (arg_var.has_value()) {
        GOOGLESQL_ASSIGN_OR_RETURN(
            std::unique_ptr<ValueExpr> deref,
            DerefExpr::Create(arg_var.value(),
                              pivot_expr_arg_types[pivot_expr_idx][i]));
        algebrized_arguments.push_back(std::move(deref));
      } else {
        GOOGLESQL_ASSIGN_OR_RETURN(
            std::unique_ptr<ValueExpr> algebrized_arg,
            AlgebrizeExpression(pivot_scan->pivot_expr_list(pivot_expr_idx)
                                    ->GetAs<ResolvedAggregateFunctionCall>()
                                    ->argument_list()[i]
                                    .get()));
        algebrized_arguments.push_back(std::move(algebrized_arg));
      }
    }

    // TODO: Support multi-level aggregation in PIVOT ref impl.
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<AggregateArg> aggregator,
                     AlgebrizeAggregateFnWithAlgebrizedArguments(
                         agg_result_var,
                         /*anonymization_options=*/std::nullopt,
                         /*filter=*/std::move(algebrized_compare), pivot_expr,
                         std::move(algebrized_arguments),
                         /*inner_grouping_keys=*/{}, /*inner_aggregators=*/{}));
    aggregators.push_back(std::move(aggregator));
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto agg_op,
      AggregateOp::Create(std::move(keys), std::move(aggregators),
                          std::move(wrapped_input), /*grouping_sets=*/{}));
  return agg_op;
}

absl::StatusOr<Algebrizer::MatchRecognizeQueryParams>
Algebrizer::GetMatchRecognizeQueryParams(
    const ResolvedMatchRecognizeScan& scan) {
  MatchRecognizeQueryParams result;
  if (parameters_->is_named()) {
    result.query_parameter_keys = std::vector<std::string>();
  } else {
    result.query_parameter_keys = std::vector<int>();
  }

  std::vector<const ResolvedNode*> resolved_parameter_refs;
  scan.pattern()->GetDescendantsWithKinds({RESOLVED_PARAMETER},
                                          &resolved_parameter_refs);

  for (const std::unique_ptr<const ResolvedOption>& option :
       scan.option_list()) {
    std::vector<const ResolvedNode*> resolved_parameter_refs_options;
    option->value()->GetDescendantsWithKinds({RESOLVED_PARAMETER},
                                             &resolved_parameter_refs_options);
    for (const ResolvedNode* node : resolved_parameter_refs_options) {
      resolved_parameter_refs.push_back(node);
    }
  }

  for (const ResolvedNode* node : resolved_parameter_refs) {
    const ResolvedParameter* resolved_param = node->GetAs<ResolvedParameter>();
    if (resolved_param->name().empty()) {
      // Positional parameters
      GOOGLESQL_RET_CHECK(!parameters_->is_named());
      GOOGLESQL_RET_CHECK(std::holds_alternative<std::vector<int>>(
          result.query_parameter_keys));
      auto& key_vector =
          std::get<std::vector<int>>(result.query_parameter_keys);
      key_vector.push_back(resolved_param->position());
    } else {
      // Named parameters
      GOOGLESQL_RET_CHECK(std::holds_alternative<std::vector<std::string>>(
          result.query_parameter_keys));
      auto& key_vector =
          std::get<std::vector<std::string>>(result.query_parameter_keys);
      key_vector.push_back(resolved_param->name());
    }
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> algebrized_param_ref,
                     AlgebrizeExpression(resolved_param));
    result.query_parameter_values.push_back(std::move(algebrized_param_ref));
  }

  return result;
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeMatchRecognizeScan(
    const ResolvedMatchRecognizeScan* match_recognize_scan) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  // Algebrize in reverse order, so that the first group ends up on top and
  // define the order for the downstream PatternMatchingOp.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> algebrized_input,
                   AlgebrizeAnalyticFunctionGroupList(
                       match_recognize_scan->input_scan(),
                       match_recognize_scan->analytic_function_group_list(),
                       /*reverse_order=*/true));

  // Mark as order preserving to ensure the ordering carries over to the
  // PatternMatchingOp.
  GOOGLESQL_RETURN_IF_ERROR(algebrized_input->set_is_order_preserving(true));

  std::vector<std::string> pattern_variable_names;
  std::vector<std::unique_ptr<ValueExpr>> predicates;
  for (const auto& def :
       match_recognize_scan->pattern_variable_definition_list()) {
    pattern_variable_names.push_back(def->name());
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> predicate,
                     AlgebrizeExpression(def->predicate()));
    predicates.push_back(std::move(predicate));
  }

  GOOGLESQL_ASSIGN_OR_RETURN(auto pattern,
                   functions::match_recognize::CompiledPattern::Create(
                       *match_recognize_scan, {}));

  // Partition keys are needed to know when the partition ends in the input.
  std::vector<std::unique_ptr<KeyArg>> partition_keys;
  absl::flat_hash_map<int, VariableId> column_to_id_map;

  const ResolvedWindowPartitioning* partition_by =
      match_recognize_scan->partition_by();
  const ResolvedWindowOrdering* order_by = match_recognize_scan->order_by();

  if (partition_by != nullptr) {
    // Create KeyArgs for partitioning expressions.
    GOOGLESQL_RETURN_IF_ERROR(AlgebrizePartitionExpressions(
        partition_by, &column_to_id_map, &partition_keys));
  }

  // Order keys are still needed to detect partial ordering which may lead to
  // non-determinism.
  std::vector<std::unique_ptr<KeyArg>> order_keys;
  if (order_by != nullptr) {
    // Create KeyArgs for ordering expressions.
    // Do not create new VariableIds, because each ordering expression
    // references a distinct column produced by the SortOp we have just created.
    GOOGLESQL_RETURN_IF_ERROR(AlgebrizeOrderByItems(
        /*drop_correlated_columns=*/true, /*create_new_ids=*/false,
        order_by->order_by_item_list(), &column_to_id_map, &order_keys));
  }

  // VariableIds representing the 4 new columns (See the declaration of
  // PatternMatchingOp for details):
  VariableId match_number_var = column_to_variable_->AssignNewVariableToColumn(
      match_recognize_scan->match_number_column());
  VariableId row_number_var = column_to_variable_->AssignNewVariableToColumn(
      match_recognize_scan->match_row_number_column());
  VariableId assigned_label_var =
      column_to_variable_->AssignNewVariableToColumn(
          match_recognize_scan->classifier_column());
  VariableId is_sentinel_var =
      variable_gen_->GetNewVariableName("$is_sentinel");

  std::vector<VariableId> match_result_vars{
      match_number_var, row_number_var, assigned_label_var, is_sentinel_var};

  GOOGLESQL_ASSIGN_OR_RETURN(MatchRecognizeQueryParams query_params,
                   GetMatchRecognizeQueryParams(*match_recognize_scan));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto pattern_matching_op,
      PatternMatchingOp::Create(
          std::move(partition_keys), std::move(order_keys), match_result_vars,
          std::move(pattern_variable_names), std::move(predicates),
          std::move(pattern), std::move(query_params.query_parameter_keys),
          std::move(query_params.query_parameter_values),
          std::move(algebrized_input)));

  // AggregateOp to compute the measures. Each match is a group.
  std::vector<std::unique_ptr<AggregateArg>> measures;
  for (const auto& measure_group : match_recognize_scan->measure_group_list()) {
    GOOGLESQL_RET_CHECK(!measure_group->aggregate_list().empty());
    for (const auto& measure : measure_group->aggregate_list()) {
      VariableId agg_var =
          column_to_variable_->GetVariableNameFromColumn(measure->column());
      VariableId side_effects_variable;
      if (measure->Is<ResolvedDeferredComputedColumn>()) {
        auto deferred = measure->GetAs<ResolvedDeferredComputedColumn>();
        side_effects_variable = column_to_variable_->AssignNewVariableToColumn(
            deferred->side_effect_column());
      }

      // When aggregating each match to compute MEASURES, make sure to filter
      // out the sentinel row:  NOT(is_sentinel)
      GOOGLESQL_ASSIGN_OR_RETURN(
          auto is_sentinel_deref,
          DerefExpr::Create(is_sentinel_var, type_factory_->get_bool()));
      std::vector<std::unique_ptr<ValueExpr>> not_args;
      not_args.push_back(std::move(is_sentinel_deref));

      // filter_expr := NOT(is_sentinel)
      GOOGLESQL_ASSIGN_OR_RETURN(
          auto filter_expr,
          BuiltinScalarFunction::CreateCall(
              FunctionKind::kNot, language_options_, type_factory_->get_bool(),
              ConvertValueExprsToAlgebraArgs(std::move(not_args)),
              ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));

      if (measure_group->pattern_variable_ref() != nullptr) {
        GOOGLESQL_ASSIGN_OR_RETURN(
            auto assigned_label_deref,
            DerefExpr::Create(assigned_label_var, type_factory_->get_string()));
        GOOGLESQL_ASSIGN_OR_RETURN(auto var_name_expr,
                         ConstExpr::Create(Value::String(
                             measure_group->pattern_variable_ref()->name())));
        std::vector<std::unique_ptr<ValueExpr>> eq_args;
        eq_args.push_back(std::move(assigned_label_deref));
        eq_args.push_back(std::move(var_name_expr));

        GOOGLESQL_ASSIGN_OR_RETURN(auto classifier_eq,
                         BuiltinScalarFunction::CreateCall(
                             FunctionKind::kEqual, language_options_,
                             type_factory_->get_bool(),
                             ConvertValueExprsToAlgebraArgs(std::move(eq_args)),
                             ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));
        // Add a conjunction to the filter, i.e.:
        // filter := filter AND classifier_eq
        GOOGLESQL_ASSIGN_OR_RETURN(
            filter_expr,
            CreateConjunction(std::move(filter_expr), std::move(classifier_eq),
                              language_options_));
      }

      // The AggOp will group rows by (partition_keys + match_id), but each
      // aggregate will need to filter out the sentinel row.
      // Sentinel rows thus help us generate a row for each empty match, but do
      // not interfere with results.
      GOOGLESQL_ASSIGN_OR_RETURN(auto algebrized_measure,
                       AlgebrizeAggregateFn(
                           agg_var, /*anonymization_options=*/std::nullopt,
                           std::move(filter_expr),
                           measure->GetAs<ResolvedComputedColumnImpl>()->expr(),
                           side_effects_variable));
      measures.push_back(std::move(algebrized_measure));
    }
  }

  // Aggregations happen only across each match, so the keys are the partition
  // keys and the match id.
  std::vector<std::unique_ptr<KeyArg>> keys;
  if (partition_by != nullptr) {
    absl::flat_hash_map<int, VariableId> column_to_id_map;
    GOOGLESQL_RETURN_IF_ERROR(
        AlgebrizePartitionExpressions(partition_by, &column_to_id_map, &keys));
  }
  GOOGLESQL_ASSIGN_OR_RETURN(auto match_number_deref,
                   DerefExpr::Create(match_number_var, types::Int64Type()));
  // No need for a new variable, we're assigning to the same slot, we just
  // needed to indicate that `$match_number` is a key.
  keys.push_back(std::make_unique<KeyArg>(match_number_var,
                                          std::move(match_number_deref)));

  return AggregateOp::Create(std::move(keys), std::move(measures),
                             std::move(pattern_matching_op),
                             /*grouping_sets=*/{});
}

absl::StatusOr<std::unique_ptr<RelationalOp>> Algebrizer::AlgebrizePipeIfScan(
    const ResolvedPipeIfScan* scan) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> op,
                   AlgebrizeScan(scan->input_scan()));

  if (scan->selected_case() != -1) {
    const ResolvedPipeIfCase* if_case =
        scan->if_case_list(scan->selected_case());

    GOOGLESQL_ASSIGN_OR_RETURN(
        op, AlgebrizeSubpipeline(if_case->subpipeline(), std::move(op)));
  }

  return op;
}

absl::StatusOr<std::unique_ptr<RelationalOp>> Algebrizer::AlgebrizeSubpipeline(
    const ResolvedSubpipeline* subpipeline,
    std::unique_ptr<RelationalOp> input_op) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  // Put `input_op` on the stack, and expect there to be a
  // ResolvedSubpipelineInputScan so AlgebrizeSubpipelineInputScan consumes it.
  size_t old_stack_size = subpipeline_input_scan_stack_.size();
  subpipeline_input_scan_stack_.push(std::move(input_op));

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> output_op,
                   AlgebrizeScan(subpipeline->scan()));

  GOOGLESQL_RET_CHECK_EQ(subpipeline_input_scan_stack_.size(), old_stack_size);

  return output_op;
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeSubpipelineInputScan(
    const ResolvedSubpipelineInputScan* scan) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  GOOGLESQL_RET_CHECK(!subpipeline_input_scan_stack_.empty());
  std::unique_ptr<RelationalOp> op =
      std::move(subpipeline_input_scan_stack_.top());
  subpipeline_input_scan_stack_.pop();
  return std::move(op);
}

#define ALGEBRIZE_SCAN(class_name)                                       \
  {Resolved##class_name::TYPE, [](Algebrizer* a, const ResolvedScan* s,  \
                                  std::vector<FilterConjunctInfo*>* c) { \
     return a->Algebrize##class_name(s->GetAs<Resolved##class_name>());  \
   }}
#define ALGEBRIZE_SCAN_WITH_CTX(class_name)                                \
  {Resolved##class_name::TYPE, [](Algebrizer* a, const ResolvedScan* s,    \
                                  std::vector<FilterConjunctInfo*>* c) {   \
     return a->Algebrize##class_name(s->GetAs<Resolved##class_name>(), c); \
   }}

ABSL_ATTRIBUTE_NOINLINE
const absl::flat_hash_map<ResolvedNodeKind, Algebrizer::AlgebrizeScanFunc>*
Algebrizer::InitializeScanMap() {
  static const absl::NoDestructor<
      absl::flat_hash_map<ResolvedNodeKind, Algebrizer::AlgebrizeScanFunc>>
      kAlgebrizeScanMap({
          // (broken link) start
          ALGEBRIZE_SCAN(AggregateScan),
          ALGEBRIZE_SCAN(AggregationThresholdAggregateScan),
          ALGEBRIZE_SCAN(AnalyticScan),
          ALGEBRIZE_SCAN(AnonymizedAggregateScan),
          ALGEBRIZE_SCAN(AssertScan),
          ALGEBRIZE_SCAN(BarrierScan),
          ALGEBRIZE_SCAN(DescribeScan),
          ALGEBRIZE_SCAN(DifferentialPrivacyAggregateScan),
          ALGEBRIZE_SCAN(GraphRefScan),
          ALGEBRIZE_SCAN(GroupRowsScan),
          ALGEBRIZE_SCAN(LimitOffsetScan),
          ALGEBRIZE_SCAN(MatchRecognizeScan),
          ALGEBRIZE_SCAN(OrderByScan),
          ALGEBRIZE_SCAN(PipeIfScan),
          ALGEBRIZE_SCAN(PivotScan),
          ALGEBRIZE_SCAN(RecursiveRefScan),
          ALGEBRIZE_SCAN(RecursiveScan),
          ALGEBRIZE_SCAN(RelationArgumentScan),
          ALGEBRIZE_SCAN(SetOperationScan),
          ALGEBRIZE_SCAN(SingleRowScan),
          ALGEBRIZE_SCAN(StaticDescribeScan),
          ALGEBRIZE_SCAN(SubpipelineInputScan),
          ALGEBRIZE_SCAN(TVFScan),
          ALGEBRIZE_SCAN(UnpivotScan),
          ALGEBRIZE_SCAN(WithRefScan),
          ALGEBRIZE_SCAN(WithScan),
          ALGEBRIZE_SCAN_WITH_CTX(ArrayScan),
          ALGEBRIZE_SCAN_WITH_CTX(FilterScan),
          ALGEBRIZE_SCAN_WITH_CTX(GraphCallScan),
          ALGEBRIZE_SCAN_WITH_CTX(GraphEdgeScan),
          ALGEBRIZE_SCAN_WITH_CTX(GraphLinearScan),
          ALGEBRIZE_SCAN_WITH_CTX(GraphNodeScan),
          ALGEBRIZE_SCAN_WITH_CTX(GraphPathScan),
          ALGEBRIZE_SCAN_WITH_CTX(GraphScan),
          ALGEBRIZE_SCAN_WITH_CTX(GraphTableScan),
          ALGEBRIZE_SCAN_WITH_CTX(JoinScan),
          ALGEBRIZE_SCAN_WITH_CTX(ProjectScan),
          ALGEBRIZE_SCAN_WITH_CTX(SampleScan),
          ALGEBRIZE_SCAN_WITH_CTX(TableScan)
          // (broken link) end
      });
  return kAlgebrizeScanMap.get();
}
#undef ALGEBRIZE_SCAN_WITH_CTX
#undef ALGEBRIZE_SCAN

// Algebrize a resolved scan operator.
absl::StatusOr<std::unique_ptr<RelationalOp>> Algebrizer::AlgebrizeScan(
    const ResolvedScan* scan,
    std::vector<FilterConjunctInfo*>* active_conjuncts) {
  GOOGLESQL_RETURN_IF_ERROR(CheckHints(scan->hint_list()));
  const size_t original_active_conjuncts_size = active_conjuncts->size();
  // Keeping a local pointer to kAlgebrizeScanMap here avoid calling into this
  // function in recursive calls. Control flow keeps the InitializeScanMap
  // stack frame, which could be large, off the stack when the stack is deep
  // while ABSL_ATTRIBUTE_NOINLINE keeps the large stack frame of
  // InitializeScanMap from being inlined here. This means we don't need
  // ABSL_ATTRIBUTE_NOINLINE to propagate all over this file and reduces the
  // likelihood of regression.
  static const absl::flat_hash_map<ResolvedNodeKind, AlgebrizeScanFunc>*
      kAlgebrizeScanMap = InitializeScanMap();
  if (!kAlgebrizeScanMap->contains(scan->node_kind())) {
    return ::googlesql_base::UnimplementedErrorBuilder()
           << "Unhandled node type algebrizing a scan: " << scan->DebugString();
  }
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<RelationalOp> rel_op,
      kAlgebrizeScanMap->at(scan->node_kind())(this, scan, active_conjuncts));

  GOOGLESQL_RET_CHECK_EQ(active_conjuncts->size(), original_active_conjuncts_size);

  // Crete a FilterOp for any conjuncts that cannot be pushed down further.
  return MaybeApplyFilterConjuncts(std::move(rel_op), active_conjuncts);
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::MaybeApplyFilterConjuncts(
    std::unique_ptr<RelationalOp> input,
    std::vector<FilterConjunctInfo*>* active_conjuncts) {
  std::vector<std::unique_ptr<ValueExpr>> algebrized_conjuncts;
  if (algebrizer_options_.push_down_filters) {
    // Iterate over 'active_conjuncts' in reverse order because it's a stack.
    for (auto i = active_conjuncts->rbegin(); i != active_conjuncts->rend();
         ++i) {
      FilterConjunctInfo* conjunct_info = *i;
      if (!conjunct_info->redundant) {
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> algebrized_conjunct,
                         AlgebrizeExpression(conjunct_info->conjunct));
        algebrized_conjuncts.push_back(std::move(algebrized_conjunct));
        conjunct_info->redundant = true;
      }
    }
  }

  return ApplyAlgebrizedFilterConjuncts(std::move(input),
                                        std::move(algebrized_conjuncts));
}

absl::StatusOr<std::unique_ptr<RelationalOp>> Algebrizer::AlgebrizeScan(
    const ResolvedScan* scan) {
  std::vector<FilterConjunctInfo*> active_conjuncts;
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> relational_op,
                   AlgebrizeScan(scan, &active_conjuncts));
  return relational_op;
}

bool Algebrizer::FindColumnDefinition(
    absl::Span<const std::unique_ptr<const ResolvedComputedColumn>> expr_list,
    int column_id, const ResolvedExpr** definition) {
  (*definition) = nullptr;
  for (int i = 0; i < expr_list.size(); ++i) {
    if (column_id == expr_list[i]->column().column_id()) {
      // The column is defined by this expression.
      (*definition) = expr_list[i]->expr();
      return true;
    }
  }
  return false;
}

absl::StatusOr<std::unique_ptr<ArrayNestExpr>> Algebrizer::NestRelationInStruct(
    const ResolvedColumnList& output_columns,
    std::unique_ptr<RelationalOp> relation, bool is_with_table) {
  // Pack the output columns in the ArrayNestExpr.
  const bool kNonValueType = false;
  GOOGLESQL_ASSIGN_OR_RETURN(
      const ArrayType* array_type,
      CreateTableArrayType(output_columns, kNonValueType, type_factory_));
  const StructType* table_struct = array_type->element_type()->AsStruct();
  std::vector<std::unique_ptr<ExprArg>> arguments;
  for (int i = 0; i < output_columns.size(); ++i) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        auto deref,
        DerefExpr::Create(
            column_to_variable_->GetVariableNameFromColumn(output_columns[i]),
            output_columns[i].type()));
    arguments.push_back(std::make_unique<ExprArg>(std::move(deref)));
  }
  GOOGLESQL_ASSIGN_OR_RETURN(auto struct_op,
                   NewStructExpr::Create(table_struct, std::move(arguments)));
  return ArrayNestExpr::Create(array_type, std::move(struct_op),
                               std::move(relation), is_with_table);
}

absl::StatusOr<std::unique_ptr<ArrayNestExpr>>
Algebrizer::NestSingleColumnRelation(const ResolvedColumnList& output_columns,
                                     std::unique_ptr<RelationalOp> relation,
                                     bool is_with_table) {
  // A single column which may be a struct.
  GOOGLESQL_RET_CHECK_EQ(output_columns.size(), 1);
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto deref_expr,
      DerefExpr::Create(
          column_to_variable_->GetVariableNameFromColumn(output_columns[0]),
          output_columns[0].type()));
  const Type* column_type = output_columns[0].type();
  const ArrayType* array_type;
  GOOGLESQL_RETURN_IF_ERROR(type_factory_->MakeArrayType(column_type, &array_type));
  return ArrayNestExpr::Create(array_type, std::move(deref_expr),
                               std::move(relation), is_with_table);
}

absl::StatusOr<std::unique_ptr<ValueExpr>>
Algebrizer::AlgebrizeRootScanAsValueExpr(
    const ResolvedColumnList& output_columns, bool is_value_table,
    const ResolvedScan* scan) {
  const size_t original_with_map_size = with_map_.size();
  GOOGLESQL_RETURN_IF_ERROR(CheckHints(scan->hint_list()));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> relation, AlgebrizeScan(scan));
  // Put a ArrayNestExpr at the root so that we return a single ValueExpr.
  std::unique_ptr<ValueExpr> value;
  // If ResolvedQueryStmt::is_value_table is true, the output is a value
  // table, i.e., we should get ARRAY<PROTO> rather than
  // ARRAY<STRUCT<PROTO>>.
  if (is_value_table) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        value, NestSingleColumnRelation(output_columns, std::move(relation),
                                        /*is_with_table=*/false));
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(value,
                     NestRelationInStruct(output_columns, std::move(relation),
                                          /*is_with_table=*/false));
  }
  // If we have any WITH clauses, create a WithExpr that binds the names of
  // subqueries to array expressions.  WITH subqueries cannot be correlated
  // so we can attach them all in one batch at the top of the query, and that
  // will ensure we run each of them exactly once.
  if (!with_subquery_let_assignments_.empty()) {
    GOOGLESQL_ASSIGN_OR_RETURN(value,
                     WithExpr::Create(std::move(with_subquery_let_assignments_),
                                      std::move(value)));
  }
  // Correctness check - WITH entries local to this statement should have been
  // removed.
  GOOGLESQL_RET_CHECK_EQ(with_map_.size(), original_with_map_size);

  return std::move(value);
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeQueryStatementAsRelation(
    const ResolvedQueryStmt* query, ResolvedColumnList* output_column_list,
    std::vector<std::string>* output_column_names,
    std::vector<VariableId>* output_column_variables) {
  GOOGLESQL_RETURN_IF_ERROR(CheckHints(query->hint_list()));
  const ResolvedScan* scan = query->query();
  GOOGLESQL_RETURN_IF_ERROR(CheckHints(scan->hint_list()));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> relation, AlgebrizeScan(scan));

  for (const std::unique_ptr<const ResolvedOutputColumn>& output_column :
       query->output_column_list()) {
    const ResolvedColumn& column = output_column->column();
    output_column_list->push_back(column);

    output_column_names->push_back(output_column->name());

    const VariableId& var =
        column_to_variable_->GetVariableNameFromColumn(column);
    output_column_variables->push_back(var);
  }

  // Dummy access for CheckFieldsAccessed().
  query->is_value_table();
  GOOGLESQL_RETURN_IF_ERROR(query->CheckFieldsAccessed());

  // If we have any WITH clauses, create a LetOp that binds the names of
  // subqueries to array expressions.  WITH subqueries cannot be correlated
  // so we can attach them all in one batch at the top of the query, and that
  // will ensure we run each of them exactly once.
  if (!with_subquery_let_assignments_.empty()) {
    GOOGLESQL_ASSIGN_OR_RETURN(relation,
                     LetOp::Create(std::move(with_subquery_let_assignments_),
                                   /*cpp_assign=*/{}, std::move(relation)));
  }
  // Sanity check - WITH map should be cleared as WITH clauses go out of scope.
  GOOGLESQL_RET_CHECK(with_map_.empty());

  GOOGLESQL_VLOG(2) << "Algebrized tree:\n" << relation->DebugString(true);
  return std::move(relation);
}

absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeDMLStatement(
    const ResolvedStatement* ast_root, IdStringPool* id_string_pool) {
  const ResolvedTableScan* resolved_table_scan;
  // TODO: Combine these in a single struct.
  auto resolved_scan_map = std::make_unique<ResolvedScanMap>();
  auto resolved_expr_map = std::make_unique<ResolvedExprMap>();
  auto column_expr_map = std::make_unique<ColumnExprMap>();

  GOOGLESQL_RETURN_IF_ERROR(AlgebrizeDescendantsOfDMLStatement(
      ast_root, resolved_scan_map.get(), resolved_expr_map.get(),
      column_expr_map.get(), &resolved_table_scan));

  const Table* table = resolved_table_scan->table();
  const ResolvedColumnList& column_list = resolved_table_scan->column_list();
  GOOGLESQL_ASSIGN_OR_RETURN(
      const ArrayType* table_array_type,
      CreateTableArrayType(column_list, table->IsValueTable(), type_factory_));
  const StructType* key_type = nullptr;
  if (table->PrimaryKey().has_value()) {
    GOOGLESQL_ASSIGN_OR_RETURN(key_type, CreatePrimaryKeyType(table, type_factory_));
  }
  ResolvedColumnList returning_column_list;
  auto returning_column_values =
      std::make_unique<std::vector<std::unique_ptr<ValueExpr>>>();

  GOOGLESQL_RETURN_IF_ERROR(AlgebrizeDMLReturningClause(ast_root, id_string_pool,
                                              &returning_column_list,
                                              returning_column_values.get()));
  GOOGLESQL_ASSIGN_OR_RETURN(
      const ArrayType* returning_array_type,
      returning_column_list.empty()
          ? nullptr
          : CreateTableArrayType(returning_column_list,
                                 /*is_value_table=*/false, type_factory_));
  GOOGLESQL_ASSIGN_OR_RETURN(const StructType* dml_output_type,
                   CreateDMLOutputTypeWithReturning(
                       table_array_type, returning_array_type, type_factory_));

  // Create a snapshot of `column_to_variable_` for DMLValueExprs to look up
  // the variables. We can't move `column_to_variable_` directly because this
  // statement can be a part of a multi-statement and thus `column_to_variable_`
  // should stay valid.
  auto column_to_variable = std::make_unique<ColumnToVariableMapping>(
      std::make_unique<VariableGenerator>());
  column_to_variable->set_map(column_to_variable_->map());

  std::unique_ptr<ValueExpr> value_expr;
  switch (ast_root->node_kind()) {
    // TODO: Add MERGE support.
    case RESOLVED_DELETE_STMT: {
      GOOGLESQL_ASSIGN_OR_RETURN(
          value_expr,
          DMLDeleteValueExpr::Create(
              table, table_array_type, returning_array_type, key_type,
              dml_output_type, ast_root->GetAs<ResolvedDeleteStmt>(),
              &column_list, std::move(returning_column_values),
              std::move(column_to_variable), std::move(resolved_scan_map),
              std::move(resolved_expr_map)));
      break;
    }
    case RESOLVED_UPDATE_STMT: {
      GOOGLESQL_ASSIGN_OR_RETURN(
          value_expr,
          DMLUpdateValueExpr::Create(
              table, table_array_type, returning_array_type, key_type,
              dml_output_type, ast_root->GetAs<ResolvedUpdateStmt>(),
              &column_list, std::move(returning_column_values),
              std::move(column_to_variable), std::move(resolved_scan_map),
              std::move(resolved_expr_map), std::move(column_expr_map)));
      break;
    }
    case RESOLVED_INSERT_STMT: {
      GOOGLESQL_ASSIGN_OR_RETURN(
          value_expr,
          DMLInsertValueExpr::Create(
              table, table_array_type, returning_array_type, key_type,
              dml_output_type, ast_root->GetAs<ResolvedInsertStmt>(),
              &column_list, std::move(returning_column_values),
              std::move(column_to_variable), std::move(resolved_scan_map),
              std::move(resolved_expr_map), std::move(column_expr_map)));

      break;
    }
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "AlgebrizeDMLStatement() does not support node kind "
                       << ResolvedNodeKind_Name(ast_root->node_kind());
      break;
  }
  // TODO: b/362158469 - WITH scan created under subquery in DML statement is
  // not supported yet. Remove this check once it is supported. This work is
  // related to b/244184304.
  if (!with_subquery_let_assignments_.empty()) {
    return absl::UnimplementedError(
        "WITH clauses under subquery in DML statement is not supported yet.");
  }
  return std::move(value_expr);
}

absl::Status Algebrizer::AlgebrizeDescendantsOfDMLStatement(
    const ResolvedStatement* ast_root, ResolvedScanMap* resolved_scan_map,
    ResolvedExprMap* resolved_expr_map, ColumnExprMap* column_expr_map,
    const ResolvedTableScan** resolved_table_scan) {
  // Note that we have to algebrize all scans before we algebrize any
  // expressions, because there is at least one place in AlgebrizeScan()
  // (specifically, AlgebrizeProjectScan()), where we assign a new variable to a
  // column even if there is already a known column of the same name. Perhaps
  // that could be generalized, but it seems reasonable to create all columns
  // before resolving any expressions.

  // TODO: Add MERGE support.
  const ResolvedTableScan* resolved_table_scan_or_null = nullptr;
  switch (ast_root->node_kind()) {
    case RESOLVED_DELETE_STMT: {
      const ResolvedDeleteStmt* stmt = ast_root->GetAs<ResolvedDeleteStmt>();

      resolved_table_scan_or_null = stmt->table_scan();
      if (resolved_table_scan_or_null != nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(PopulateResolvedScanMap(resolved_table_scan_or_null,
                                                resolved_scan_map));
      }

      if (stmt->array_offset_column() != nullptr) {
        // The array offset column can only be set for nested DELETEs.
        GOOGLESQL_RET_CHECK(resolved_table_scan_or_null == nullptr);
        column_to_variable_->AssignNewVariableToColumn(
            stmt->array_offset_column()->column());
      }

      GOOGLESQL_RETURN_IF_ERROR(
          PopulateResolvedExprMap(stmt->where_expr(), resolved_expr_map));

      const ResolvedAssertRowsModified* assert_rows_modified =
          stmt->assert_rows_modified();
      if (assert_rows_modified != nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(PopulateResolvedExprMap(assert_rows_modified->rows(),
                                                resolved_expr_map));
      }
      break;
    }
    case RESOLVED_UPDATE_STMT: {
      const ResolvedUpdateStmt* stmt = ast_root->GetAs<ResolvedUpdateStmt>();

      resolved_table_scan_or_null = stmt->table_scan();
      if (resolved_table_scan_or_null != nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(PopulateResolvedScanMap(resolved_table_scan_or_null,
                                                resolved_scan_map));
        GOOGLESQL_RETURN_IF_ERROR(AlgebrizeDefaultAndGeneratedExpressions(
            resolved_table_scan_or_null, column_expr_map,
            stmt->generated_column_expr_list(),
            stmt->topologically_sorted_generated_column_id_list()));
      }

      if (stmt->from_scan() != nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(
            PopulateResolvedScanMap(stmt->from_scan(), resolved_scan_map));
      }

      if (stmt->array_offset_column() != nullptr) {
        // The array offset column can only be set for nested UPDATEs.
        GOOGLESQL_RET_CHECK(resolved_table_scan_or_null == nullptr);
        column_to_variable_->AssignNewVariableToColumn(
            stmt->array_offset_column()->column());
      }

      GOOGLESQL_RETURN_IF_ERROR(
          PopulateResolvedExprMap(stmt->where_expr(), resolved_expr_map));

      const ResolvedAssertRowsModified* assert_rows_modified =
          stmt->assert_rows_modified();
      if (assert_rows_modified != nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(PopulateResolvedExprMap(assert_rows_modified->rows(),
                                                resolved_expr_map));
      }

      for (const std::unique_ptr<const ResolvedUpdateItem>& item :
           stmt->update_item_list()) {
        GOOGLESQL_RETURN_IF_ERROR(AlgebrizeDescendantsOfUpdateItem(
            item.get(), resolved_scan_map, resolved_expr_map, column_expr_map));
      }
      break;
    }
    case RESOLVED_INSERT_STMT: {
      const ResolvedInsertStmt* stmt = ast_root->GetAs<ResolvedInsertStmt>();
      resolved_table_scan_or_null = stmt->table_scan();
      if (resolved_table_scan_or_null != nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(
            PopulateResolvedScanMap(stmt->table_scan(), resolved_scan_map));
        GOOGLESQL_RETURN_IF_ERROR(AlgebrizeDefaultAndGeneratedExpressions(
            resolved_table_scan_or_null, column_expr_map,
            stmt->generated_column_expr_list(),
            stmt->topologically_sorted_generated_column_id_list()));
      }

      if (stmt->query() != nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(
            PopulateResolvedScanMap(stmt->query(), resolved_scan_map));
      }

      const ResolvedAssertRowsModified* assert_rows_modified =
          stmt->assert_rows_modified();
      if (assert_rows_modified != nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(PopulateResolvedExprMap(assert_rows_modified->rows(),
                                                resolved_expr_map));
      }

      for (const std::unique_ptr<const ResolvedInsertRow>& row :
           stmt->row_list()) {
        for (int i = 0; i < row->value_list().size(); ++i) {
          const ResolvedDMLValue* dml_value = row->value_list(i);
          if (resolved_table_scan_or_null != nullptr) {
            const ResolvedColumn& insert_column = stmt->insert_column_list(i);
            auto it = column_expr_map->find(insert_column.column_id());
            if (it != column_expr_map->end() &&
                dml_value->value()->node_kind() == RESOLVED_DMLDEFAULT) {
              // If this column has a default value, and DEFAULT is specified,
              // its default value has been algebrized and stored in
              // column_expr_map.
              continue;
            }
          }
          // If this column doesn't have a default value, use the user input:
          GOOGLESQL_RETURN_IF_ERROR(
              PopulateResolvedExprMap(dml_value->value(), resolved_expr_map));
        }
      }

      // Dummy accesses for ResolvedInsertStmt::CheckFieldsAccessed().
      //
      // The reference implementation does not enforce that only certain columns
      // are visible in certain places, so there is no need to look at the query
      // parameter list.
      for (const std::unique_ptr<const ResolvedColumnRef>& parameter :
           stmt->query_parameter_list()) {
        parameter->column();
      }
      // Similarly, the query output column list is the same as the column list
      // inside the query, so there is no need to look at it either.
      stmt->query_output_column_list();

      break;
    }
    default:
      GOOGLESQL_RET_CHECK_FAIL()
          << "AlgebrizeDescendantsOfDMLStatement() does not support node kind "
          << ResolvedNodeKind_Name(ast_root->node_kind());
  }

  // The caller must pass a non-NULL 'resolved_table_scan' if and only if this
  // is a top-level DML statement.
  GOOGLESQL_RET_CHECK_EQ(resolved_table_scan == nullptr,
               resolved_table_scan_or_null == nullptr);
  if (resolved_table_scan != nullptr) {
    *resolved_table_scan = resolved_table_scan_or_null;
  }

  return absl::OkStatus();
}

absl::Status Algebrizer::AlgebrizeDMLReturningClause(
    const ResolvedStatement* ast_root, IdStringPool* id_string_pool,
    ResolvedColumnList* returning_column_list,
    std::vector<std::unique_ptr<ValueExpr>>* returning_column_values) {
  const ResolvedReturningClause* returning_clause;
  switch (ast_root->node_kind()) {
    case RESOLVED_DELETE_STMT: {
      returning_clause = ast_root->GetAs<ResolvedDeleteStmt>()->returning();
      break;
    }
    case RESOLVED_UPDATE_STMT: {
      returning_clause = ast_root->GetAs<ResolvedUpdateStmt>()->returning();
      break;
    }
    case RESOLVED_INSERT_STMT: {
      returning_clause = ast_root->GetAs<ResolvedInsertStmt>()->returning();
      break;
    }
    case RESOLVED_MERGE_STMT: {
      return ::googlesql_base::UnimplementedErrorBuilder()
             << "Unsupported node type algebrizing a DML returning statement: "
             << ast_root->node_kind_string();
    }
    default:
      GOOGLESQL_RET_CHECK_FAIL()
          << "AlgebrizeDMLReturningClause() does not support node kind "
          << ResolvedNodeKind_Name(ast_root->node_kind());
  }
  if (returning_clause == nullptr) {
    return absl::OkStatus();
  }
  size_t num_columns = returning_clause->output_column_list().size();
  for (int i = 0; i < num_columns; ++i) {
    GOOGLESQL_RET_CHECK(i < returning_clause->output_column_list().size());
    const std::unique_ptr<const ResolvedOutputColumn>& output_column =
        returning_clause->output_column_list()[i];

    const ResolvedColumn& column = output_column->column();
    returning_column_list->emplace_back(
        column.column_id(), id_string_pool->Make(column.table_name()),
        id_string_pool->Make(output_column->name()), column.type());

    const ResolvedExpr* local_definition;
    if (FindColumnDefinition(returning_clause->expr_list(), column.column_id(),
                             &local_definition)) {
      // An expression based on the input, append the algebrized expression.
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> value_expr,
                       AlgebrizeExpression(local_definition));
      returning_column_values->push_back(std::move(value_expr));
    } else if (i != num_columns - 1 ||
               returning_clause->action_column() == nullptr) {
      // A column from the target table, append it as a DerefExpr.
      // WITH ACTION column will be ignored in 'returning_column_values'.

      VariableId column_name =
          column_to_variable_->GetVariableNameFromColumn(column);
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<DerefExpr> deref_expr,
                       DerefExpr::Create(column_name, column.type()));
      returning_column_values->push_back(std::move(deref_expr));
    }
  }

  if (returning_clause->action_column() != nullptr) {
    // Dummy access of the action column for CheckFieldsAccessed()
    // where it requires to access every field in the resolved statement.
    returning_clause->action_column()->column();
  }

  return absl::OkStatus();
}

absl::Status Algebrizer::AlgebrizeDescendantsOfUpdateItem(
    const ResolvedUpdateItem* update_item, ResolvedScanMap* resolved_scan_map,
    ResolvedExprMap* resolved_expr_map, ColumnExprMap* column_expr_map) {
  GOOGLESQL_RETURN_IF_ERROR(
      PopulateResolvedExprMap(update_item->target(), resolved_expr_map));

  if (update_item->set_value() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(PopulateResolvedExprMap(update_item->set_value()->value(),
                                            resolved_expr_map));
  }

  if (update_item->element_column() != nullptr) {
    column_to_variable_->AssignNewVariableToColumn(
        update_item->element_column()->column());
  }

  for (const std::unique_ptr<const ResolvedUpdateItemElement>&
           update_item_element : update_item->update_item_element_list()) {
    GOOGLESQL_RETURN_IF_ERROR(PopulateResolvedExprMap(update_item_element->subscript(),
                                            resolved_expr_map));
    GOOGLESQL_RETURN_IF_ERROR(AlgebrizeDescendantsOfUpdateItem(
        update_item_element->update_item(), resolved_scan_map,
        resolved_expr_map, column_expr_map));
  }

  std::vector<const ResolvedStatement*> nested_dml_stmts;
  for (const std::unique_ptr<const ResolvedDeleteStmt>& delete_stmt :
       update_item->delete_list()) {
    nested_dml_stmts.push_back(delete_stmt.get());
  }
  for (const std::unique_ptr<const ResolvedUpdateStmt>& update_stmt :
       update_item->update_list()) {
    nested_dml_stmts.push_back(update_stmt.get());
  }
  for (const std::unique_ptr<const ResolvedInsertStmt>& insert_stmt :
       update_item->insert_list()) {
    nested_dml_stmts.push_back(insert_stmt.get());
  }
  for (const ResolvedStatement* dml_stmt : nested_dml_stmts) {
    GOOGLESQL_RETURN_IF_ERROR(AlgebrizeDescendantsOfDMLStatement(
        dml_stmt, resolved_scan_map, resolved_expr_map, column_expr_map,
        /*resolved_table_scan=*/nullptr));
  }

  return absl::OkStatus();
}

absl::Status Algebrizer::PopulateResolvedScanMap(
    const ResolvedScan* resolved_scan, ResolvedScanMap* resolved_scan_map) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> relational_op,
                   AlgebrizeScan(resolved_scan));
  const auto ret =
      resolved_scan_map->emplace(resolved_scan, std::move(relational_op));
  GOOGLESQL_RET_CHECK(ret.second);
  return absl::OkStatus();
}

absl::Status Algebrizer::PopulateResolvedExprMap(
    const ResolvedExpr* resolved_expr, ResolvedExprMap* resolved_expr_map) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> value_expr,
                   AlgebrizeExpression(resolved_expr));
  const auto ret =
      resolved_expr_map->emplace(resolved_expr, std::move(value_expr));
  GOOGLESQL_RET_CHECK(ret.second);
  return absl::OkStatus();
}

absl::Status Algebrizer::AlgebrizeDefaultAndGeneratedExpressions(
    const ResolvedTableScan* table_scan, ColumnExprMap* column_expr_map,
    absl::Span<const std::unique_ptr<const ResolvedExpr>>
        generated_column_exprs,
    std::vector<int> topologically_sorted_generated_column_ids) {
  GOOGLESQL_RET_CHECK(column_expr_map != nullptr);
  const Table* table = table_scan->table();

  for (int i = 0; i < table_scan->column_index_list_size(); ++i) {
    const Column* column = table->GetColumn(table_scan->column_index_list(i));
    const ResolvedColumn& resolved_column = table_scan->column_list(i);
    if (column->HasDefaultExpression()) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> value_expr,
                       AlgebrizeExpression(
                           column->GetExpression()->GetResolvedExpression()));
      const auto& [_, is_inserted] = column_expr_map->emplace(
          resolved_column.column_id(), std::move(value_expr));
      GOOGLESQL_RET_CHECK(is_inserted);
    }
  }
  GOOGLESQL_RET_CHECK(topologically_sorted_generated_column_ids.size() ==
            generated_column_exprs.size());
  for (int i = 0; i < topologically_sorted_generated_column_ids.size(); ++i) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> value_expr,
                     AlgebrizeExpression(generated_column_exprs[i].get()));
    const auto& [_, is_inserted] = column_expr_map->emplace(
        topologically_sorted_generated_column_ids[i], std::move(value_expr));
    GOOGLESQL_RET_CHECK(is_inserted);
  }

  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<ProtoFieldRegistry>>
Algebrizer::MakeProtoFieldRegistry(
    const std::optional<SharedProtoFieldPath>& id) {
  auto registry = std::make_unique<ProtoFieldRegistry>();

  if (id.has_value()) {
    GOOGLESQL_RET_CHECK(
        proto_field_registry_map_.emplace(id.value(), registry.get()).second);
  }
  return std::move(registry);
}

absl::StatusOr<std::unique_ptr<ProtoFieldReader>>
Algebrizer::MakeProtoFieldReader(const std::optional<SharedProtoFieldPath>& id,
                                 const ProtoFieldAccessInfo& access_info,
                                 ProtoFieldRegistry* registry) {
  auto reader = std::make_unique<ProtoFieldReader>(access_info, registry);

  // The check for get_has_bit might not be necessary here because the resolver
  // may always use BOOL for that case, but it doesn't hurt to be safe.
  if (id.has_value() && !access_info.field_info.get_has_bit &&
      access_info.field_info.type->IsProto()) {
    GOOGLESQL_RET_CHECK(
        get_proto_field_reader_map_.emplace(id.value(), reader.get()).second);
  }
  return std::move(reader);
}

std::string Algebrizer::SharedProtoFieldPath::DebugString() const {
  std::string s = column_or_param.DebugString();

  std::vector<std::string> path_strs;
  path_strs.reserve(field_path.size());
  for (const ProtoOrStructField& field : field_path) {
    path_strs.push_back(field.DebugString());
  }

  return absl::StrCat(s, ".", absl::StrJoin(path_strs, "."));
}

static absl::Status VerifyParameters(const Parameters* parameters) {
  if (parameters->is_named()) {
    GOOGLESQL_RET_CHECK(parameters->named_parameters().empty());
  } else {
    GOOGLESQL_RET_CHECK(parameters->positional_parameters().empty());
  }
  return absl::OkStatus();
}

absl::Status Algebrizer::AlgebrizeStatementImpl(
    const ResolvedStatement* ast_root, std::unique_ptr<ValueExpr>* output) {
  // Weirdly, the output_column_list of the statement may contain column
  // aliases not present in the output columns of 'query', so that the
  // statement wrapper acts as another PROJECT node. Compensate for this by
  // creating a column list with column names from the statement.
  // TODO: fix this.
  IdStringPool id_string_pool;
  switch (ast_root->node_kind()) {
    case RESOLVED_CREATE_TABLE_STMT: {
      // TODO: Implement for correct result, not placeholder.
      // Current codepaths that pass here just expect success, and not a
      // correctly algebrized result. For now, this placeholder empty result
      // will suffice; more accurate implementation can be added by need basis.
      GOOGLESQL_ASSIGN_OR_RETURN(*output,
                       NewArrayExpr::Create(types::Int64ArrayType(), {}));
      break;
    }
    case RESOLVED_CREATE_TABLE_AS_SELECT_STMT: {
      const ResolvedCreateTableAsSelectStmt* stmt =
          ast_root->GetAs<ResolvedCreateTableAsSelectStmt>();
      GOOGLESQL_RETURN_IF_ERROR(CheckHints(stmt->hint_list()));
      const ResolvedScan* scan = stmt->query();
      GOOGLESQL_RETURN_IF_ERROR(CheckHints(scan->hint_list()));
      ResolvedColumnList output_column_list;
      for (const auto& it : stmt->output_column_list()) {
        // TODO IdString conversion shouldn't be needed here.
        // We should have IdStrings in ResolvedOutputColumn.
        output_column_list.emplace_back(
            it->column().column_id(),
            id_string_pool.Make(it->column().table_name()),
            id_string_pool.Make(it->name()), it->column().type());
      }
      GOOGLESQL_ASSIGN_OR_RETURN(
          *output, AlgebrizeRootScanAsValueExpr(output_column_list,
                                                stmt->is_value_table(), scan));
      // Only check access in the scan that is actually being algebrized; it
      // is expected that some fields in the top-level node, such as the table
      // name, will not be accessed.
      GOOGLESQL_RETURN_IF_ERROR(scan->CheckFieldsAccessed());
      break;
    }
    case RESOLVED_QUERY_STMT: {
      const ResolvedQueryStmt* stmt = ast_root->GetAs<ResolvedQueryStmt>();
      GOOGLESQL_RETURN_IF_ERROR(CheckHints(stmt->hint_list()));
      const ResolvedScan* scan = stmt->query();
      GOOGLESQL_RETURN_IF_ERROR(CheckHints(scan->hint_list()));
      ResolvedColumnList output_column_list;
      for (const auto& it :
           ast_root->GetAs<ResolvedQueryStmt>()->output_column_list()) {
        // TODO IdString conversion shouldn't be needed here.
        // We should have IdStrings in ResolvedOutputColumn.
        output_column_list.emplace_back(
            it->column().column_id(),
            id_string_pool.Make(it->column().table_name()),
            id_string_pool.Make(it->name()), it->column().type());
      }
      GOOGLESQL_ASSIGN_OR_RETURN(
          *output, AlgebrizeRootScanAsValueExpr(output_column_list,
                                                stmt->is_value_table(), scan));
      GOOGLESQL_RETURN_IF_ERROR(ast_root->CheckFieldsAccessed());
      break;
    }
    // TODO: Add MERGE support.
    //
    // For DML statements, the algebrizer tree is just a hack wrapper around
    // 'ast_root' that does not actually traverse the resolved AST. Thus, we
    // can't check that all the resolved AST nodes are accessed yet. We check
    // that all the fields in the resolved AST are accessed after evaluation.
    case RESOLVED_DELETE_STMT:
    case RESOLVED_UPDATE_STMT:
    case RESOLVED_INSERT_STMT: {
      GOOGLESQL_ASSIGN_OR_RETURN(*output,
                       AlgebrizeDMLStatement(ast_root, &id_string_pool));
      break;
    }
    case RESOLVED_MULTI_STMT: {
      GOOGLESQL_ASSIGN_OR_RETURN(
          *output, AlgebrizeMultiStmt(ast_root->GetAs<ResolvedMultiStmt>()));
      GOOGLESQL_RETURN_IF_ERROR(ast_root->CheckFieldsAccessed());
      break;
    }
    default:
      GOOGLESQL_RET_CHECK_FAIL()
          << "AlgebrizeStatement() does not support ResolvedNodeKind "
          << ResolvedNodeKind_Name(ast_root->node_kind());
      break;
  }

  GOOGLESQL_VLOG(2) << "Algebrized tree:\n" << output->get()->DebugString(true);
  return absl::OkStatus();
}

absl::Status Algebrizer::AlgebrizeStatement(
    const LanguageOptions& language_options,
    const AlgebrizerOptions& algebrizer_options, TypeFactory* type_factory,
    const ResolvedStatement* ast_root, std::unique_ptr<ValueExpr>* output,
    Parameters* parameters, ParameterMap* column_map,
    SystemVariablesAlgebrizerMap* system_variables_map) {
  GOOGLESQL_RETURN_IF_ERROR(VerifyParameters(parameters));

  Algebrizer single_use_algebrizer(language_options, algebrizer_options,
                                   type_factory, parameters, column_map,
                                   system_variables_map);
  return single_use_algebrizer.AlgebrizeStatementImpl(ast_root, output);
}

absl::Status Algebrizer::AlgebrizeQueryStatementAsRelation(
    const LanguageOptions& language_options,
    const AlgebrizerOptions& algebrizer_options, TypeFactory* type_factory,
    const ResolvedQueryStmt* ast_root, ResolvedColumnList* output_column_list,
    std::unique_ptr<RelationalOp>* output,
    std::vector<std::string>* output_column_names,
    std::vector<VariableId>* output_column_variables, Parameters* parameters,
    ParameterMap* column_map,
    SystemVariablesAlgebrizerMap* system_variables_map) {
  GOOGLESQL_RETURN_IF_ERROR(VerifyParameters(parameters));
  Algebrizer single_use_algebrizer(language_options, algebrizer_options,
                                   type_factory, parameters, column_map,
                                   system_variables_map);
  GOOGLESQL_ASSIGN_OR_RETURN(*output,
                   single_use_algebrizer.AlgebrizeQueryStatementAsRelation(
                       ast_root, output_column_list, output_column_names,
                       output_column_variables));
  return absl::OkStatus();
}

absl::Status Algebrizer::AlgebrizeExpression(
    const LanguageOptions& language_options,
    const AlgebrizerOptions& algebrizer_options, TypeFactory* type_factory,
    const ResolvedExpr* ast_root, std::unique_ptr<ValueExpr>* output,
    Parameters* parameters, ParameterMap* column_map,
    SystemVariablesAlgebrizerMap* system_variables_map) {
  GOOGLESQL_RETURN_IF_ERROR(VerifyParameters(parameters));

  Algebrizer single_use_algebrizer(language_options, algebrizer_options,
                                   type_factory, parameters, column_map,
                                   system_variables_map);
  GOOGLESQL_ASSIGN_OR_RETURN(
      *output, single_use_algebrizer.AlgebrizeStandaloneExpression(ast_root));

  GOOGLESQL_VLOG(2) << "Algebrized tree:\n" << output->get()->DebugString();
  return ast_root->CheckFieldsAccessed();
}

absl::StatusOr<std::unique_ptr<RelationalOp>> Algebrizer::FilterDuplicates(
    std::unique_ptr<RelationalOp> input, const ResolvedColumnList& column_list,
    VariableId row_set_id) {
  std::vector<std::unique_ptr<KeyArg>> key_args;

  for (const ResolvedColumn& col : column_list) {
    VariableId column_var = column_to_variable_->GetVariableNameFromColumn(col);
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> key_deref,
                     DerefExpr::Create(column_var, col.type()));
    key_args.push_back(
        std::make_unique<KeyArg>(column_var, std::move(key_deref)));
  }

  return DistinctOp::Create(std::move(input), std::move(key_args), row_set_id);
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeRecursiveScan(
    const ResolvedRecursiveScan* recursive_scan) {
  ResolvedColumnList recursive_scan_col_list = recursive_scan->column_list();
  std::unique_ptr<ValueExpr> lower_bound, upper_bound;
  std::unique_ptr<ValueExpr> depth_init_expr, depth_assign_expr;
  VariableId depth_column_var;
  ResolvedColumn depth_column;
  if (recursive_scan->recursion_depth_modifier() != nullptr) {
    const auto* modifier = recursive_scan->recursion_depth_modifier();
    if (modifier->lower_bound() != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(lower_bound,
                       AlgebrizeExpression(modifier->lower_bound()));
    }
    if (modifier->upper_bound() != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(upper_bound,
                       AlgebrizeExpression(modifier->upper_bound()));
    }
    if (modifier->recursion_depth_column() != nullptr) {
      depth_column = modifier->recursion_depth_column()->column();

      // Depth column is not in the recursive inputs so we specially handle it.
      // Remove the column from the column list so the recursive input are
      // handled the same way.
      recursive_scan_col_list.erase(
          std::remove(recursive_scan_col_list.begin(),
                      recursive_scan_col_list.end(), depth_column),
          recursive_scan_col_list.end());
      depth_column_var =
          column_to_variable_->GetVariableNameFromColumn(depth_column);

      // Initialization: depth := 0.
      GOOGLESQL_ASSIGN_OR_RETURN(depth_init_expr, ConstExpr::Create(Value::Int64(0)));

      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<DerefExpr> deref,
                       DerefExpr::Create(depth_column_var, types::Int64Type()));
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ConstExpr> one,
                       ConstExpr::Create(Value::Int64(1)));
      std::vector<std::unique_ptr<ValueExpr>> arguments(2);
      arguments[0] = std::move(deref);
      arguments[1] = std::move(one);

      // Loop: depth := depth + 1.
      GOOGLESQL_ASSIGN_OR_RETURN(
          depth_assign_expr,
          BuiltinScalarFunction::CreateCall(
              FunctionKind::kAdd, language_options_, types::Int64Type(),
              ConvertValueExprsToAlgebraArgs(std::move(arguments)),
              ResolvedFunctionCall::DEFAULT_ERROR_MODE));
    }
  }

  // Algebrize non-recursive term first.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> non_recursive_term,
                   AlgebrizeScan(recursive_scan->non_recursive_term()->scan()));
  GOOGLESQL_ASSIGN_OR_RETURN(
      non_recursive_term,
      MapColumns(std::move(non_recursive_term),
                 recursive_scan->non_recursive_term()->output_column_list(),
                 recursive_scan_col_list));

  // Create a variable to hold the result from the previous iteration.
  // ResolvedRecursiveRefScan nodes will derefrence this.
  VariableId recursive_var =
      variable_gen_->GetNewVariableName("$recursive_var");
  GOOGLESQL_ASSIGN_OR_RETURN(
      const ArrayType* recursive_table_type,
      CreateTableArrayType(recursive_scan_col_list, false, type_factory_));

  // Now, proceed to algebrize the recursive term.
  recursive_var_id_stack_.push(
      std::make_unique<ExprArg>(recursive_var, recursive_table_type));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> recursive_term,
                   AlgebrizeScan(recursive_scan->recursive_term()->scan()));
  recursive_var_id_stack_.pop();

  GOOGLESQL_ASSIGN_OR_RETURN(
      recursive_term,
      MapColumns(std::move(recursive_term),
                 recursive_scan->recursive_term()->output_column_list(),
                 recursive_scan_col_list));

  // Under UNION DISTINCT, wrap the recursive and non-recursive terms with a
  // DistinctOp to make the rows they emit unique, not only among themselves,
  // but also between each other.
  VariableId distinct_id;
  if (recursive_scan->op_type() == ResolvedRecursiveScanEnums::UNION_DISTINCT) {
    distinct_id = variable_gen_->GetNewVariableName("distinct_row_set");
    GOOGLESQL_ASSIGN_OR_RETURN(non_recursive_term,
                     FilterDuplicates(std::move(non_recursive_term),
                                      recursive_scan_col_list, distinct_id));
    GOOGLESQL_ASSIGN_OR_RETURN(recursive_term,
                     FilterDuplicates(std::move(recursive_term),
                                      recursive_scan_col_list, distinct_id));
  }

  std::vector<std::unique_ptr<ExprArg>> initial_assign;
  std::unique_ptr<RelationalOp> body;
  std::vector<std::unique_ptr<ExprArg>> loop_assign;

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ArrayNestExpr> initial_assign_expr,
                   NestRelationInStruct(recursive_scan_col_list,
                                        std::move(non_recursive_term),
                                        /*is_with_table=*/false));
  initial_assign.emplace_back(
      std::make_unique<ExprArg>(recursive_var, std::move(initial_assign_expr)));

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<DerefExpr> deref,
                   DerefExpr::Create(recursive_var, recursive_table_type));
  GOOGLESQL_ASSIGN_OR_RETURN(body, CreateScanOfColumnsAsArray(recursive_scan_col_list,
                                                    /*is_value_table=*/false,
                                                    std::move(deref)));
  if (depth_column_var.is_valid()) {
    // For each loop, we not only scan the recursion body but also project
    // the recursion depth for it.
    std::vector<std::unique_ptr<ExprArg>> map;
    GOOGLESQL_ASSIGN_OR_RETURN(auto deref,
                     DerefExpr::Create(depth_column_var, depth_column.type()));
    map.push_back(
        std::make_unique<ExprArg>(depth_column_var, std::move(deref)));
    GOOGLESQL_ASSIGN_OR_RETURN(body, ComputeOp::Create(std::move(map), std::move(body)));
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ArrayNestExpr> loop_assign_expr,
      NestRelationInStruct(recursive_scan_col_list, std::move(recursive_term),
                           /*is_with_table=*/false));
  loop_assign.emplace_back(
      std::make_unique<ExprArg>(recursive_var, std::move(loop_assign_expr)));

  // Adds depth init and assign expr when specified.
  if (depth_init_expr != nullptr) {
    initial_assign.push_back(std::make_unique<ExprArg>(
        depth_column_var, std::move(depth_init_expr)));
  }
  if (depth_assign_expr != nullptr) {
    loop_assign.push_back(std::make_unique<ExprArg>(
        depth_column_var, std::move(depth_assign_expr)));
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<LoopOp> loop_op,
      LoopOp::Create(std::move(initial_assign), std::move(body),
                     std::move(loop_assign), std::move(lower_bound),
                     std::move(upper_bound)));

  if (recursive_scan->op_type() == ResolvedRecursiveScanEnums::UNION_DISTINCT) {
    // Wrap the LoopOp with a LetOp to associate <distinct_id> with a C++
    // DistinctRowSet object, used to track duplicates. This prevents the
    // recursive term from emitting rows from a prior iteration or from the
    // non-recursive term.
    std::vector<std::unique_ptr<CppValueArg>> cpp_assign;
    cpp_assign.push_back(DistinctOp::MakeCppValueArgForRowSet(distinct_id));
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<LetOp> let_op,
                     LetOp::Create(/*assign=*/{}, std::move(cpp_assign),
                                   std::move(loop_op)));
    return let_op;
  } else {
    return loop_op;
  }
}

absl::StatusOr<std::unique_ptr<RelationalOp>> Algebrizer::MapColumns(
    std::unique_ptr<RelationalOp> input,
    const ResolvedColumnList& input_columns,
    const ResolvedColumnList& output_columns) {
  GOOGLESQL_RET_CHECK_EQ(input_columns.size(), output_columns.size());
  std::vector<std::unique_ptr<ExprArg>> map;
  for (int i = 0; i < output_columns.size(); ++i) {
    const ResolvedColumn& input_column = input_columns.at(i);
    const ResolvedColumn& output_column = output_columns.at(i);
    GOOGLESQL_RET_CHECK(input_column.type()->Equals(output_column.type()));
    GOOGLESQL_ASSIGN_OR_RETURN(
        auto deref,
        DerefExpr::Create(
            column_to_variable_->GetVariableNameFromColumn(input_column),
            output_column.type()));
    map.push_back(std::make_unique<ExprArg>(
        column_to_variable_->GetVariableNameFromColumn(output_column),
        std::move(deref)));
  }
  return ComputeOp::Create(std::move(map), std::move(input));
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeRecursiveRefScan(
    const ResolvedRecursiveRefScan* recursive_ref_scan) {
  GOOGLESQL_RET_CHECK(!recursive_var_id_stack_.empty());
  VariableId recursive_var_id = recursive_var_id_stack_.top()->variable();
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> deref_expr,
                   DerefExpr::Create(recursive_var_id,
                                     recursive_var_id_stack_.top()->type()));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ArrayScanOp> scan_op,
                   CreateScanOfTableAsArray(recursive_ref_scan,
                                            /*is_value_table=*/false,
                                            std::move(deref_expr)));
  return scan_op;
}

// Algebrize an UnpivotArg into a UnionAllOp::Input which produces the rows
// associated with this UnpivotArg. Each row will contain all non-pivoted
// columns from <input>, plus all value columns, plus the label column.
//
// <input> is presented as an ExprArg, rather than a ResolvedScan, since it is
// assumed that the input scan has already been saved to a variable, so we
// simply need to deref it. This avoids repeat evaluation of the input when
// multiple UnpivotArg's are present.
absl::StatusOr<UnionAllOp::Input> Algebrizer::AlgebrizeUnpivotArg(
    const ResolvedUnpivotScan* unpivot_scan, const ExprArg& input,
    int arg_index) {
  const auto& unpivot_arg = unpivot_scan->unpivot_arg_list(arg_index);
  // Construct a relation that reads back the UNPIVOT input from the variable.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> deref_expr,
                   DerefExpr::Create(input.variable(), input.type()));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ArrayScanOp> input_scan_op,
                   CreateScanOfTableAsArray(unpivot_scan->input_scan(),
                                            /*is_value_table=*/false,
                                            std::move(deref_expr)));

  // Wrap the UNPIVOT input in a ComputeOp to define the corresponding label
  // column.
  std::vector<std::unique_ptr<ExprArg>> map;
  VariableId label_column_var = column_to_variable_->GetVariableNameFromColumn(
      unpivot_scan->label_column());
  VariableId internal_label_column_var =
      this->variable_gen_->GetNewVariableName("unpivot_label");
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ConstExpr> label_value_expr,
      ConstExpr::Create(unpivot_scan->label_list(arg_index)->value()));
  map.push_back(std::make_unique<ExprArg>(internal_label_column_var,
                                          std::move(label_value_expr)));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ComputeOp> compute_op,
                   ComputeOp::Create(std::move(map), std::move(input_scan_op)));

  // Now, create a UnionAllOp::Input using the ComputeOp as input, which maps
  // all of the other columns.
  UnionAllOp::Input unionall_input;
  unionall_input.first = std::move(compute_op);

  // Copy input columns, which are not being unpivoted.
  for (const auto& column : unpivot_scan->projected_input_column_list()) {
    VariableId output_column_var =
        column_to_variable_->GetVariableNameFromColumn(column->column());
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> input_column,
                     AlgebrizeExpression(column->expr()));
    unionall_input.second.push_back(
        std::make_unique<ExprArg>(output_column_var, std::move(input_column)));
  }

  // Copy value columns
  for (int j = 0; j < unpivot_arg->column_list_size(); ++j) {
    GOOGLESQL_RET_CHECK_EQ(unpivot_arg->column_list_size(),
                 unpivot_scan->value_column_list_size());
    const ResolvedColumnRef* column_ref = unpivot_arg->column_list(j);
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> deref_column_expr,
                     AlgebrizeExpression(column_ref));
    VariableId destination_var = column_to_variable_->GetVariableNameFromColumn(
        unpivot_scan->value_column_list(j));
    unionall_input.second.push_back(std::make_unique<ExprArg>(
        destination_var, std::move(deref_column_expr)));
  }

  // Add the label column to the UnionallOp column map.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<DerefExpr> deref_label_column,
                   DerefExpr::Create(internal_label_column_var,
                                     unpivot_scan->label_column().type()));
  unionall_input.second.push_back(std::make_unique<ExprArg>(
      label_column_var, std::move(deref_label_column)));
  return unionall_input;
}

// Invoked for UnpivotScan's which do not include nulls. Wraps <input>, which
// is the algebrized version of the rest of the UnpivotScan, in a FilterOp,
// which applies the condition:
//   <value-column-1 IS NOT NULL> OR <value-column-2 IS NOT NULL>...
absl::StatusOr<std::unique_ptr<FilterOp>>
Algebrizer::AlgebrizeNullFilterForUnpivotScan(
    const ResolvedUnpivotScan* unpivot_scan,
    std::unique_ptr<RelationalOp> input) {
  GOOGLESQL_RET_CHECK(!unpivot_scan->include_nulls());
  GOOGLESQL_RET_CHECK_LE(1, unpivot_scan->value_column_list_size());
  std::unique_ptr<ValueExpr> predicate;
  for (const ResolvedColumn& col : unpivot_scan->value_column_list()) {
    VariableId col_var = column_to_variable_->GetVariableNameFromColumn(col);
    std::unique_ptr<ScalarFunctionCallExpr> is_null_expr;

    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> col_deref,
                     DerefExpr::Create(col_var, col.type()));
    std::vector<std::unique_ptr<ValueExpr>> is_null_arguments;
    is_null_arguments.push_back(std::move(col_deref));

    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<ValueExpr> is_null_fn_call,
        BuiltinScalarFunction::CreateCall(
            FunctionKind::kIsNull, language_options_, BoolType(),
            ConvertValueExprsToAlgebraArgs(std::move(is_null_arguments)),
            ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));

    std::vector<std::unique_ptr<ValueExpr>> not_arguments;
    not_arguments.push_back(std::move(is_null_fn_call));
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<ValueExpr> not_fn_call,
        BuiltinScalarFunction::CreateCall(
            FunctionKind::kNot, language_options_, BoolType(),
            ConvertValueExprsToAlgebraArgs(std::move(not_arguments)),
            ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));

    if (predicate == nullptr) {
      predicate = std::move(not_fn_call);
    } else {
      // Need to combine the new predicate with what we have so far
      std::vector<std::unique_ptr<ValueExpr>> or_arguments;
      or_arguments.push_back(std::move(predicate));
      or_arguments.push_back(std::move(not_fn_call));
      GOOGLESQL_ASSIGN_OR_RETURN(
          predicate,
          BuiltinScalarFunction::CreateCall(
              FunctionKind::kOr, language_options_, BoolType(),
              ConvertValueExprsToAlgebraArgs(std::move(or_arguments)),
              ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));
    }
  }
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<FilterOp> filter_op,
                   FilterOp::Create(std::move(predicate), std::move(input)));
  return filter_op;
}

// We algebrize UNPIVOT scans using UNION ALL, with one UNION element per
// UnpivotArg. The UnionallOp is, in turn, wrapped in a LetOp, to ensure that
// the input scan is evaluated only once, even when multiple UnpivotArg's are
// present. Finally, if INCLUDE NULLS is not present, the entire LetOp is
// wrapped in a FilterOp to include only rows for which at least one value
// column is not NULL.
absl::StatusOr<std::unique_ptr<RelationalOp>> Algebrizer::AlgebrizeUnpivotScan(
    const ResolvedUnpivotScan* unpivot_scan) {
  // Algebrize the input, and save it as an array, so that when it's used
  // multiple times by different UnpivotArg's, the input is evaluated only once.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> algebrized_input,
                   AlgebrizeScan(unpivot_scan->input_scan()));

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ArrayNestExpr> algebrized_input_as_value,
                   NestRelationInStruct(
                       unpivot_scan->input_scan()->column_list(),
                       std::move(algebrized_input), /*is_with_table=*/false));
  VariableId input_array_var =
      variable_gen_->GetNewVariableName("$unpivot_input");
  std::vector<std::unique_ptr<ExprArg>> input_assign;
  input_assign.push_back(std::make_unique<ExprArg>(
      input_array_var, std::move(algebrized_input_as_value)));

  // Build up a UnionAllOp, with each input specifying the rows corresponding to
  // one UnpivotArg.
  std::vector<UnionAllOp::Input> unionall_inputs;

  GOOGLESQL_RET_CHECK_EQ(unpivot_scan->unpivot_arg_list_size(),
               unpivot_scan->label_list_size());
  for (int i = 0; i < unpivot_scan->unpivot_arg_list_size(); ++i) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        unionall_inputs.emplace_back(),
        AlgebrizeUnpivotArg(unpivot_scan, *input_assign.front(), i));
  }
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<UnionAllOp> unionall_op,
                   UnionAllOp::Create(std::move(unionall_inputs)));

  // Now, put everything together in a LetOp that defines the input variable
  // for use in the UNION scan.
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<LetOp> let_op,
      LetOp::Create(std::move(input_assign), {}, std::move(unionall_op)));

  if (unpivot_scan->include_nulls()) {
    return let_op;
  }

  // Wrap everything in a FilterOp to include rows only when at least one value
  // column is non-NULL.
  return AlgebrizeNullFilterForUnpivotScan(unpivot_scan, std::move(let_op));
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeGroupRowsScan(
    const ResolvedGroupRowsScan* group_rows_scan) {
  const std::vector<std::unique_ptr<const ResolvedComputedColumn>>& expr_list =
      group_rows_scan->input_column_list();
  const ResolvedColumnList& column_list = group_rows_scan->column_list();
  GOOGLESQL_RET_CHECK(!column_list.empty());

  // Assign variables to the new columns and algebrize their definitions.
  std::vector<std::unique_ptr<ExprArg>> arguments;
  arguments.reserve(column_list.size());

  for (const ResolvedColumn& column : column_list) {
    const ResolvedExpr* expr;
    GOOGLESQL_RET_CHECK(FindColumnDefinition(expr_list, column.column_id(), &expr));
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> argument,
                     AlgebrizeExpression(expr));
    const VariableId variable =
        column_to_variable_->AssignNewVariableToColumn(column);
    arguments.push_back(
        std::make_unique<ExprArg>(variable, std::move(argument)));
  }

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> group_rows_op,
                   GroupRowsOp::Create(std::move(arguments)));
  return std::move(group_rows_op);  // necessary to work around bugs in gcc.
}

absl::Status Algebrizer::AlgebrizeTvfCall(
    const ResolvedScan& resolved_body, bool is_value_table,
    const LanguageOptions& language_options,
    const AlgebrizerOptions& algebrizer_options, TypeFactory* type_factory,
    std::vector<TvfArgumentInfo> arg_infos,
    std::vector<TableValuedFunction::TvfEvaluatorArg> arg_evaluators,
    EvaluationContext* eval_context,
    std::unique_ptr<RelationalOp>& out_algebrized_body,
    std::vector<int>& out_column_indices) {
  Parameters parameters;
  ParameterMap column_map;
  SystemVariablesAlgebrizerMap system_variables_map;
  Algebrizer tvf_algebrizer(language_options, algebrizer_options, type_factory,
                            &parameters, &column_map, &system_variables_map);

  for (int i = 0; i < arg_infos.size(); ++i) {
    TvfArgumentInfo& arg = arg_infos[i];
    TableValuedFunction::TvfEvaluatorArg& eval_arg = arg_evaluators[i];
    switch (arg.kind) {
      case TvfArgKind::kScalar:
        GOOGLESQL_RET_CHECK(eval_arg.value.has_value());
        GOOGLESQL_RETURN_IF_ERROR(
            eval_context->AddFunctionArgumentRef(arg.name, *eval_arg.value));
        break;
      case TvfArgKind::kRelation: {
        GOOGLESQL_RET_CHECK(eval_arg.relation != nullptr);

        // Materialize the relation as an array of structs.
        GOOGLESQL_ASSIGN_OR_RETURN(
            Value relation_as_array,
            MaterializeRelationAsArray(eval_arg.relation.get(), type_factory));
        // We are not using AddTableAsArray() for fidelity, since the arg should
        // shadow any table in the catalog with the same name.
        GOOGLESQL_RETURN_IF_ERROR(eval_context->AddFunctionArgumentRef(
            arg.name, std::move(relation_as_array)));
        break;
      }
      case TvfArgKind::kGraph: {
        GOOGLESQL_RET_CHECK(eval_arg.graph != nullptr);
        return ::googlesql_base::UnimplementedErrorBuilder()
               << "Graph argument is not yet supported";
      }
      case TvfArgKind::kModel:
        GOOGLESQL_RET_CHECK(eval_arg.model != nullptr);
        return ::googlesql_base::UnimplementedErrorBuilder()
               << "Model argument is not yet supported";
      case TvfArgKind::kUndefined:
        return ::googlesql_base::InternalErrorBuilder()
               << "Undefined table valued function argument kind";
      default:
        return ::googlesql_base::UnimplementedErrorBuilder()
               << "Unimplemented table valued function argument: "
               << static_cast<int>(arg.kind);
    }
  }

  // Algebrize the body as an array of structs. This is needed to handle WITH
  // CTEs from the TVF body.
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ValueExpr> body_as_array,
      tvf_algebrizer.AlgebrizeRootScanAsValueExpr(
          resolved_body.column_list(), is_value_table, &resolved_body));

  // Restore as a relation.
  GOOGLESQL_ASSIGN_OR_RETURN(out_algebrized_body,
                   tvf_algebrizer.CreateScanOfColumnsAsArray(
                       resolved_body.column_list(), is_value_table,
                       std::move(body_as_array)));

  std::unique_ptr<TupleSchema> output_schema =
      out_algebrized_body->CreateOutputSchema();

  out_column_indices.clear();
  out_column_indices.reserve(resolved_body.column_list_size());
  for (const ResolvedColumn& column : resolved_body.column_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(VariableId var, tvf_algebrizer.column_to_variable_
                                         ->LookupVariableNameForColumn(column));
    std::optional<int> index = output_schema->FindIndexForVariable(var);
    GOOGLESQL_RET_CHECK(index.has_value());
    out_column_indices.push_back(*index);
  }
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeRelationArgumentScan(
    const ResolvedRelationArgumentScan* arg_scan) {
  // The same argument relation may be referenced multiple times in the TVF
  // body, so we need once-semantics. Algebrize as a WithRefScan.
  GOOGLESQL_ASSIGN_OR_RETURN(
      const ArrayType* array_type,
      CreateTableArrayType(arg_scan->column_list(), arg_scan->is_value_table(),
                           type_factory_));

  GOOGLESQL_ASSIGN_OR_RETURN(auto arg_ref_expr,
                   CreateFunctionArgumentRefExpr(arg_scan->name(), array_type));

  return CreateScanOfColumnsAsArray(arg_scan->column_list(),
                                    arg_scan->is_value_table(),
                                    std::move(arg_ref_expr));
}

absl::StatusOr<std::unique_ptr<RelationalOp>> Algebrizer::AlgebrizeTVFScan(
    const ResolvedTVFScan* tvf_scan) {
  // Track measure columns emitted by the TVF scan so that they can be
  // correctly aggregated later.
  GOOGLESQL_RETURN_IF_ERROR(
      measure_column_to_expr_.TrackMeasureColumnsEmittedByTVFScan(*tvf_scan));
  // Algebrize input arguments.
  std::vector<TvfAlgebraArgument> arguments;
  std::vector<TvfArgumentInfo> arg_infos;
  for (int i = 0; i < tvf_scan->argument_list().size(); ++i) {
    const ResolvedFunctionArgument* argument = tvf_scan->argument_list(i);
    if (argument->expr() != nullptr) {
      GOOGLESQL_RET_CHECK(tvf_scan->signature()->argument(i).is_scalar());
      GOOGLESQL_ASSIGN_OR_RETURN(auto expr_argument,
                       AlgebrizeExpression(argument->expr()));
      arguments.push_back({.value = std::move(expr_argument)});
      arg_infos.push_back({.kind = TvfArgKind::kScalar});
      continue;
    }

    if (argument->scan() != nullptr) {
      GOOGLESQL_RET_CHECK(tvf_scan->signature()->argument(i).is_relation());
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> relation,
                       AlgebrizeScan(argument->scan()));

      // TVF implementation needs to access input table by column names.
      // Preserve variable to column name mapping.
      const TVFRelation& relation_signature =
          tvf_scan->signature()->argument(i).relation();
      GOOGLESQL_RET_CHECK_EQ(argument->argument_column_list_size(),
                   relation_signature.num_columns());
      std::vector<TvfInputRelation::TvfInputRelationColumn> columns;
      for (int j = 0; j < argument->argument_column_list_size(); ++j) {
        const ResolvedColumn& argument_column =
            argument->argument_column_list(j);
        const TVFSchemaColumn& relation_signature_column =
            relation_signature.column(j);

        GOOGLESQL_RET_CHECK_EQ(argument_column.type(), relation_signature_column.type);
        GOOGLESQL_ASSIGN_OR_RETURN(
            VariableId input_variable,
            column_to_variable_->LookupVariableNameForColumn(argument_column));
        columns.push_back({relation_signature_column.name,
                           argument_column.type(), input_variable});
      }
      arguments.push_back({.relation = TvfInputRelation{std::move(relation),
                                                        std::move(columns)}});
      arg_infos.push_back({.kind = TvfArgKind::kRelation});
      continue;
    }

    if (argument->model() != nullptr) {
      GOOGLESQL_RET_CHECK(tvf_scan->signature()->argument(i).is_model());
      arguments.push_back({.model = argument->model()->model()});
      arg_infos.push_back({.kind = TvfArgKind::kModel});
      continue;
    }
    if (argument->graph() != nullptr) {
      GOOGLESQL_RET_CHECK(tvf_scan->signature()->argument(i).is_graph());
      arguments.push_back({.graph = argument->graph()});
      arg_infos.push_back({.kind = TvfArgKind::kGraph});
      continue;
    }

    return ::googlesql_base::UnimplementedErrorBuilder()
           << "Unimplemented table valued function argument: "
           << argument->node_kind_string() << ". "
           << "Only expressions, relations and models are currently supported";
  }

  // Algebrize output column names and variables.
  GOOGLESQL_RET_CHECK_EQ(tvf_scan->column_list_size(),
               tvf_scan->column_index_list_size());
  std::vector<TVFSchemaColumn> output_columns;
  output_columns.reserve(tvf_scan->column_list().size());
  std::vector<VariableId> variables;
  variables.reserve(tvf_scan->column_list().size());
  for (int i = 0; i < tvf_scan->column_list_size(); ++i) {
    const ResolvedColumn& column = tvf_scan->column_list(i);
    const TVFSchemaColumn& signature_column =
        tvf_scan->signature()->result_schema().column(
            tvf_scan->column_index_list(i));
    GOOGLESQL_RET_CHECK(column.type()->Equals(signature_column.type))
        << column.type()->DebugString()
        << " != " << signature_column.type->DebugString();
    output_columns.push_back(signature_column);
    variables.push_back(column_to_variable_->GetVariableNameFromColumn(column));
  }

  const TableValuedFunction* tvf = tvf_scan->tvf();
  if (tvf->IsGoogleSQLBuiltin()) {
    std::string name = tvf->FullName(/*include_group=*/false);
    GOOGLESQL_ASSIGN_OR_RETURN(FunctionKind kind,
                     BuiltinFunctionCatalog::GetKindByName(name));
    return BuiltinTableValuedFunction::CreateCall(
        kind, std::move(arguments), std::move(output_columns),
        std::move(variables), tvf_scan->function_call_signature());
  }

  TVFOp::SqlTvfEvaluator algebrize_body_callback = nullptr;
  if (tvf->Is<SQLTableValuedFunction>() || tvf->Is<TemplatedSQLTVF>()) {
    absl::Span<const std::string> arg_names;

    const ResolvedScan* resolved_body = nullptr;
    bool is_value_table;
    if (tvf->Is<SQLTableValuedFunction>()) {
      const auto* sql_tvf = tvf->GetAs<SQLTableValuedFunction>();
      arg_names = sql_tvf->GetArgumentNames();
      resolved_body = sql_tvf->ResolvedStatement()->query();
      is_value_table = sql_tvf->ResolvedStatement()->is_value_table();
    } else {
      const auto* signature =
          tvf_scan->signature()->GetAs<TemplatedSQLTVFSignature>();
      arg_names = signature->GetArgumentNames();
      resolved_body = signature->resolved_templated_query()->query();
      is_value_table = signature->resolved_templated_query()->is_value_table();
    }

    GOOGLESQL_RET_CHECK_EQ(arg_names.size(), arg_infos.size());
    for (int i = 0; i < arg_names.size(); ++i) {
      arg_infos[i].name = arg_names[i];
    }

    const LanguageOptions& language_options = this->language_options_;
    const AlgebrizerOptions& algebrizer_options = this->algebrizer_options_;
    TypeFactory* type_factory = this->type_factory_;

    algebrize_body_callback =
        [resolved_body, is_value_table, language_options, algebrizer_options,
         type_factory, output_columns, arg_infos = std::move(arg_infos)](
            std::vector<TableValuedFunction::TvfEvaluatorArg> args,
            int num_extra_slots,
            std::unique_ptr<EvaluationContext> eval_context)
        -> absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>> {
      std::unique_ptr<RelationalOp> algebrized_body;
      std::vector<int> output_column_indices;
      GOOGLESQL_RETURN_IF_ERROR(AlgebrizeTvfCall(
          *resolved_body, is_value_table, language_options, algebrizer_options,
          type_factory, std::move(arg_infos), std::move(args),
          eval_context.get(), algebrized_body, output_column_indices));

      return CreateIterator(std::move(algebrized_body),
                            std::move(output_columns),
                            std::move(output_column_indices), num_extra_slots,
                            std::move(eval_context));
    };
  }

  return TVFOp::Create(tvf, std::move(arguments), std::move(output_columns),
                       std::move(variables),
                       tvf_scan->function_call_signature(),
                       std::move(algebrize_body_callback));
}

void Algebrizer::PushConjuncts(
    absl::Span<const std::unique_ptr<FilterConjunctInfo>> conjunct_infos,
    std::vector<FilterConjunctInfo*>* active_conjuncts) {
  // Push the new conjuncts onto 'active_conjuncts' in reverse order (because
  // it's a stack).
  for (auto i = conjunct_infos.rbegin(); i != conjunct_infos.rend(); ++i) {
    active_conjuncts->push_back(i->get());
  }
}

absl::Status Algebrizer::PopConjuncts(
    absl::Span<const std::unique_ptr<FilterConjunctInfo>> conjunct_infos,
    std::vector<FilterConjunctInfo*>* active_conjuncts) {
  // Restore 'active_conjuncts'.
  for (const std::unique_ptr<FilterConjunctInfo>& info : conjunct_infos) {
    GOOGLESQL_RET_CHECK(info.get() == active_conjuncts->back());
    active_conjuncts->pop_back();
  }
  return absl::OkStatus();
}

absl::StatusOr<std::vector<std::unique_ptr<ValueExpr>>>
Algebrizer::AlgebrizeNonRedundantConjuncts(
    absl::Span<const std::unique_ptr<FilterConjunctInfo>> conjunct_infos) {
  // Drop any FilterConjunctInfos that are now redundant.
  std::vector<std::unique_ptr<ValueExpr>> algebrized_conjuncts;
  algebrized_conjuncts.reserve(conjunct_infos.size());
  for (const auto& info : conjunct_infos) {
    if (!info->redundant) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> algebrized_conjunct,
                       AlgebrizeExpression(info->conjunct));
      algebrized_conjuncts.push_back(std::move(algebrized_conjunct));
    }
  }
  return algebrized_conjuncts;
}

absl::StatusOr<std::unique_ptr<RelationalOp>> Algebrizer::AlgebrizeAssertScan(
    const ResolvedAssertScan* resolved_assert) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> input,
                   AlgebrizeScan(resolved_assert->input_scan()));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> condition,
                   AlgebrizeExpression(resolved_assert->condition()));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> message,
                   AlgebrizeExpression(resolved_assert->message()));
  return AssertOp::Create(std::move(input), std::move(condition),
                          std::move(message));
}

absl::StatusOr<std::unique_ptr<RelationalOp>> Algebrizer::AlgebrizeDescribeScan(
    const ResolvedDescribeScan* resolved_describe) {
  // Algebrize the input to mark it visited, then discard it.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> input,
                   AlgebrizeScan(resolved_describe->input_scan()));

  // Start with a single row.
  GOOGLESQL_ASSIGN_OR_RETURN(auto relational_op, AlgebrizeSingleRowScan(nullptr));

  // Then project a computed column for the describe expression.
  // Assign a variable to the new column and algebrize its definition.
  const ResolvedColumn& column = resolved_describe->describe_expr()->column();
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ValueExpr> argument,
      AlgebrizeExpression(resolved_describe->describe_expr()->expr()));
  const VariableId variable =
      column_to_variable_->AssignNewVariableToColumn(column);

  std::vector<std::unique_ptr<ExprArg>> arguments;
  arguments.push_back(std::make_unique<ExprArg>(variable, std::move(argument)));

  GOOGLESQL_ASSIGN_OR_RETURN(relational_op, ComputeOp::Create(std::move(arguments),
                                                    std::move(relational_op)));
  return relational_op;
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeStaticDescribeScan(
    const ResolvedStaticDescribeScan* resolved_static_describe) {
  // TODO The output from STATIC_DESCRIBE should be sent to
  // some static logging channel.  We don't currently have that so we just
  // algebrize this as a no-op.  This is still useful, particularly for
  // execute_query, so queries with STATIC_DESCRIBE can still be executed.

  // Algebrize the input scan.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> input_op,
                   AlgebrizeScan(resolved_static_describe->input_scan()));

  // Mark the `describe_text` as visited.  We currently don't have a way to
  // actually do the static logging so we just skip it.
  resolved_static_describe->describe_text();

  return std::move(input_op);
}

absl::StatusOr<std::unique_ptr<RelationalOp>> Algebrizer::AlgebrizeBarrierScan(
    const ResolvedBarrierScan* resolved_barrier_scan) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> input,
                   AlgebrizeScan(resolved_barrier_scan->input_scan()));
  return BarrierScanOp::Create(std::move(input));
}

absl::StatusOr<std::unique_ptr<ExprArg>>
Algebrizer::AlgebrizeCreateWithEntryStmt(
    const ResolvedCreateWithEntryStmt* create_with_stmt) {
  measure_column_to_expr_.TrackWithQueryScan(*create_with_stmt->with_entry());
  const ResolvedScan* subquery_node =
      create_with_stmt->with_entry()->with_subquery();
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ValueExpr> subquery_value_expr,
      AlgebrizeRootScanAsValueExpr(
          subquery_node->column_list(),
          // `is_value_table` does not matter for CreateWithEntryStmt because it
          // does not correspond to an output.
          /*is_value_table=*/false, subquery_node));

  const std::string& with_query_name =
      create_with_stmt->with_entry()->with_query_name();
  VariableId new_variable = variable_gen_->GetNewVariableName(with_query_name);
  auto expr_arg =
      std::make_unique<ExprArg>(new_variable, std::move(subquery_value_expr));

  // Make the new WITH entry visible to subsequent sub-statements in the current
  // MultiStmt.
  with_map_[with_query_name] = expr_arg.get();
  return expr_arg;
}

absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeMultiStmt(
    const ResolvedMultiStmt* multi_stmt) {
  std::vector<std::unique_ptr<ExprArg>> algebrized_sub_statements;

  // ResolvedMultiStmt must be a top-level statement, so no WITH entries have
  // been collected.
  GOOGLESQL_RET_CHECK(with_map_.empty());
  GOOGLESQL_RET_CHECK(with_subquery_let_assignments_.empty());
  GOOGLESQL_RET_CHECK(inlined_with_entries_.empty());

  for (const auto& sub_stmt_node : multi_stmt->statement_list()) {
    const size_t with_map_size = with_map_.size();
    std::unique_ptr<ExprArg> current_expr_arg;
    if (sub_stmt_node->node_kind() == RESOLVED_CREATE_WITH_ENTRY_STMT) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          current_expr_arg,
          AlgebrizeCreateWithEntryStmt(
              sub_stmt_node->GetAs<ResolvedCreateWithEntryStmt>()));
      // One new WITH entry should have been added to the map.
      GOOGLESQL_RET_CHECK_EQ(with_map_.size(), with_map_size + 1);
    } else {
      std::unique_ptr<ValueExpr> sub_value_expr;
      GOOGLESQL_RETURN_IF_ERROR(
          AlgebrizeStatementImpl(sub_stmt_node.get(), &sub_value_expr));
      current_expr_arg = std::make_unique<ExprArg>(std::move(sub_value_expr));
    }
    algebrized_sub_statements.push_back(std::move(current_expr_arg));

    // With entries local to each sub-statement must have been added to the
    // corresponding sub-statements, so they must remain empty after algebrizing
    // each sub-statement.
    GOOGLESQL_RET_CHECK(with_subquery_let_assignments_.empty());
    GOOGLESQL_RET_CHECK(inlined_with_entries_.empty());
  }

  // Finished algebrizing the MultiStmt, clear the WITH maps.
  with_map_.clear();
  return MultiStmtExpr::Create(std::move(algebrized_sub_statements));
}

}  // namespace googlesql
