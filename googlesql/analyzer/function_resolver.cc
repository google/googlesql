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

#include "googlesql/analyzer/function_resolver.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/analyzer/expr_resolver_helper.h"
#include "googlesql/analyzer/function_signature_matcher.h"
#include "googlesql/analyzer/input_argument_type_resolver_helper.h"
#include "googlesql/analyzer/lambda_util.h"
#include "googlesql/analyzer/name_scope.h"
#include "googlesql/analyzer/named_argument_info.h"
#include "googlesql/analyzer/query_resolver_helper.h"
#include "googlesql/analyzer/resolver.h"
#include "googlesql/common/constant_utils.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/internal_analyzer_options.h"
#include "googlesql/common/status_payload_utils.h"
#include "googlesql/common/thread_stack.h"
#include "googlesql/common/type_and_argument_kind_visitor.h"
#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parse_tree_errors.h"
#include "googlesql/parser/parser.h"
#include "googlesql/proto/internal_error_location.pb.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/annotation/collation.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/cast.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/coercer.h"
#include "googlesql/public/constness_level.pb.h"
#include "googlesql/public/cycle_detector.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/error_location.pb.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/public/pico_time.h"
#include "googlesql/public/signature_match_result.h"
#include "googlesql/public/sql_function.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/templated_sql_function.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/collation.h"
#include "googlesql/public/types/simple_value.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type_modifiers.h"
#include "googlesql/public/types/type_parameters.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "googlesql/resolved_ast/resolved_ast_helper.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/base/case.h"
#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "googlesql/base/check.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {
// Enable preserve_in_literal_remover for a literal. The caller is responsible
// for ensuring that `expr` is a literal.
absl::Status EnablePreserveInLiteralRemover(const ResolvedExpr* expr) {
  GOOGLESQL_RET_CHECK(expr->node_kind() == RESOLVED_LITERAL);
  const auto* literal = expr->GetAs<ResolvedLiteral>();
  const_cast<ResolvedLiteral*>(literal)->set_preserve_in_literal_remover(true);
  return absl::OkStatus();
}
}  // namespace

FunctionResolver::FunctionResolver(Catalog* catalog, TypeFactory* type_factory,
                                   Resolver* resolver)
    : catalog_(catalog), type_factory_(type_factory), resolver_(resolver) {}

constexpr absl::string_view kBitwiseNotFnName = "$bitwise_not";
constexpr absl::string_view kInvalidUnaryOperatorFnName =
    "$invalid_unary_operator";
constexpr absl::string_view kNotFnName = "$not";
constexpr absl::string_view kUnaryMinusFnName = "$unary_minus";
constexpr absl::string_view kUnaryPlusFnName = "$unary_plus";
constexpr absl::string_view kIsNullFnName = "$is_null";

absl::string_view FunctionResolver::UnaryOperatorToFunctionName(
    ASTUnaryExpression::Op op) {
  switch (op) {
    case ASTUnaryExpression::NOT:
      return kNotFnName;
    case ASTUnaryExpression::MINUS:
      return kUnaryMinusFnName;
    case ASTUnaryExpression::PLUS:
      // Note that this function definition does not actually exist.  The
      // resolver treats this as a no-op and effectively removes it from the
      // resolved tree.
      return kUnaryPlusFnName;
    case ASTUnaryExpression::BITWISE_NOT:
      return kBitwiseNotFnName;
    case ASTUnaryExpression::NOT_SET:
      return kInvalidUnaryOperatorFnName;
    case ASTUnaryExpression::IS_UNKNOWN:
    case ASTUnaryExpression::IS_NOT_UNKNOWN:
      // As a side note, IS [NOT] UNKNOWN reuses the existing "is_null" function
      // name since they share the exact same engine implementation. "is_null"
      // is currently implemented as a binary operator which can recognize if a
      // preceding keyword NOT is presented. But unary operator does not support
      // that so two unary operators are created to be mapped to the same name.
      return kIsNullFnName;
  }
}

constexpr absl::string_view kAddFnName = "$add";
constexpr absl::string_view kBitwiseAndFnName = "$bitwise_and";
constexpr absl::string_view kBitwiseOrFnName = "$bitwise_or";
constexpr absl::string_view kBitwiseXorFnName = "$bitwise_xor";
constexpr absl::string_view kConcatOpFnName = "$concat_op";
constexpr absl::string_view kDivideFnName = "$divide";
constexpr absl::string_view kEqualFnName = "$equal";
constexpr absl::string_view kGreaterFnName = "$greater";
constexpr absl::string_view kGreaterOrEqualFnName = "$greater_or_equal";
constexpr absl::string_view kLessFnName = "$less";
constexpr absl::string_view kLessOrEqualFnName = "$less_or_equal";
constexpr absl::string_view kLikeFnName = "$like";
constexpr absl::string_view kMultiplyFnName = "$multiply";
constexpr absl::string_view kNotEqualFnName = "$not_equal";
constexpr absl::string_view kSubtractFnName = "$subtract";
constexpr absl::string_view kDistinctOpFnName = "$is_distinct_from";
constexpr absl::string_view kNotDistinctOpFnName = "$is_not_distinct_from";
constexpr absl::string_view kIsSourceNodeOpFnName = "$is_source_node";
constexpr absl::string_view kIsDestNodeOpFnName = "$is_dest_node";
constexpr absl::string_view kInvalidBinaryOperatorStr =
    "$invalid_binary_operator";

absl::string_view FunctionResolver::BinaryOperatorToFunctionName(
    ASTBinaryExpression::Op op, bool is_not, bool* not_handled) {
  if (not_handled != nullptr) {
    *not_handled = false;
  }
  switch (op) {
    case ASTBinaryExpression::DIVIDE:
      return kDivideFnName;
    case ASTBinaryExpression::EQ:
      return kEqualFnName;
    case ASTBinaryExpression::NE:
    case ASTBinaryExpression::NE2:
      return kNotEqualFnName;
    case ASTBinaryExpression::GT:
      return kGreaterFnName;
    case ASTBinaryExpression::GE:
      return kGreaterOrEqualFnName;
    case ASTBinaryExpression::LT:
      return kLessFnName;
    case ASTBinaryExpression::LE:
      return kLessOrEqualFnName;
    case ASTBinaryExpression::MINUS:
      return kSubtractFnName;
    case ASTBinaryExpression::MULTIPLY:
      return kMultiplyFnName;
    case ASTBinaryExpression::PLUS:
      return kAddFnName;
    case ASTBinaryExpression::LIKE:
      return kLikeFnName;
    case ASTBinaryExpression::BITWISE_OR:
      return kBitwiseOrFnName;
    case ASTBinaryExpression::BITWISE_XOR:
      return kBitwiseXorFnName;
    case ASTBinaryExpression::BITWISE_AND:
      return kBitwiseAndFnName;
    case ASTBinaryExpression::IS:
    case ASTBinaryExpression::NOT_SET:
      return kInvalidBinaryOperatorStr;
    case ASTBinaryExpression::CONCAT_OP:
      return kConcatOpFnName;
    case ASTBinaryExpression::DISTINCT:
      if (is_not) {
        ABSL_CHECK(not_handled != nullptr);
        *not_handled = true;
        return kNotDistinctOpFnName;
      } else {
        return kDistinctOpFnName;
      }
    case ASTBinaryExpression::IS_SOURCE_NODE:
      return kIsSourceNodeOpFnName;
    case ASTBinaryExpression::IS_DEST_NODE:
      return kIsDestNodeOpFnName;
  }
}

absl::StatusOr<bool> FunctionResolver::SignatureMatches(
    const std::vector<const ASTNode*>& arg_ast_nodes,
    absl::Span<const InputArgumentType> input_arguments,
    const FunctionSignature& signature, bool allow_argument_coercion,
    const NameScope* name_scope,
    std::unique_ptr<FunctionSignature>* result_signature,
    SignatureMatchResult* signature_match_result,
    std::vector<ArgIndexEntry>* arg_index_mapping,
    std::vector<FunctionArgumentOverride>* arg_overrides,
    absl::flat_hash_map<const ResolvedInlineLambda*, const ASTLambda*>*
        lambda_ast_nodes) const {
  GOOGLESQL_RETURN_IF_NOT_ENOUGH_STACK(
      "Out of stack space due to deeply nested query expression "
      "during signature matching");

  ResolveLambdaCallback lambda_resolve_callback =
      [resolver = this->resolver_, name_scope, lambda_ast_nodes](
          const ASTLambda* ast_lambda, absl::Span<const IdString> arg_names,
          absl::Span<const Type* const> arg_types, const Type* body_result_type,
          bool allow_argument_coercion,
          std::unique_ptr<const ResolvedInlineLambda>* resolved_expr_out)
      -> absl::Status {
    ABSL_DCHECK(name_scope != nullptr);
    GOOGLESQL_RETURN_IF_ERROR(resolver->ResolveLambda(
        ast_lambda, arg_names, arg_types, body_result_type,
        allow_argument_coercion, name_scope, resolved_expr_out));
    GOOGLESQL_RET_CHECK(lambda_ast_nodes != nullptr);
    GOOGLESQL_RET_CHECK(lambda_ast_nodes->insert({resolved_expr_out->get(), ast_lambda})
                  .second);
    return absl::OkStatus();
  };
  return FunctionSignatureMatchesWithStatus(
      resolver_->language(), coercer(), arg_ast_nodes, input_arguments,
      signature, allow_argument_coercion, type_factory_,
      &lambda_resolve_callback, result_signature, signature_match_result,
      arg_index_mapping, arg_overrides);
}

// Get the parse location from a ResolvedNode, if it has one stored in it.
// Otherwise, fall back to the location on an ASTNode.
// Can be used as
//   return MakeSqlErrorAtPoint(GetLocationFromResolvedNode(node, ast_node))
static ParseLocationPoint GetLocationFromResolvedNode(const ResolvedNode* node,
                                                      const ASTNode* fallback) {
  ABSL_DCHECK(fallback != nullptr);
  const ParseLocationRange* range = node->GetParseLocationOrNULL();
  if (range != nullptr) {
    return range->start();
  } else {
    return GetErrorLocationPoint(fallback, /*include_leftmost_child=*/true);
  }
}

// static
absl::Status FunctionResolver::CheckCreateAggregateFunctionProperties(
    const ResolvedExpr& resolved_expr,
    const ASTNode* sql_function_body_location,
    const ExprResolutionInfo* expr_info, QueryResolutionInfo* query_info,
    const LanguageOptions& language_options) {
  auto sql_error = [sql_function_body_location](std::string message) {
    if (sql_function_body_location != nullptr) {
      return MakeSqlErrorAt(sql_function_body_location) << message;
    } else {
      return MakeSqlError() << message;
    }
  };
  // In CREATE AGGREGATE FUNCTION, we are only ever ranging over the full input.
  GOOGLESQL_RETURN_IF_ERROR(query_info->PinToRowRange(std::nullopt));
  if (expr_info->findings.has_aggregation) {
    GOOGLESQL_RET_CHECK(query_info->group_by_column_state_list().empty());
    GOOGLESQL_RET_CHECK(!query_info->aggregate_columns_to_compute().empty());
    for (const std::unique_ptr<const ResolvedComputedColumnBase>&
             computed_column : query_info->aggregate_columns_to_compute()) {
      GOOGLESQL_RET_CHECK(computed_column->expr()->Is<ResolvedAggregateFunctionCall>());
      const ResolvedAggregateFunctionCall* aggregate_function_call =
          computed_column->expr()->GetAs<ResolvedAggregateFunctionCall>();

      if (!aggregate_function_call->group_by_list().empty() &&
          !language_options.LanguageFeatureEnabled(
              FEATURE_MULTILEVEL_AGGREGATION)) {
        return sql_error(
            "Function body with aggregate functions with GROUP BY modifiers "
            "are not currently supported");
      }
    }

    // TODO: If we have an aggregate with ORDER BY inside, we normally
    // make a Project first to create columns, so the ResolvedAggregateScan can
    // reference them with just a ColumnRef (not a full Expr).  We don't have a
    // way to represent that Project in the ResolvedCreateFunction node, so for
    // now, we detect that case here and give an error.
    if (!query_info->select_list_columns_to_compute_before_aggregation()
             ->empty()) {
      return sql_error(
          "Function body with aggregate functions with ORDER BY not currently "
          "supported");
    }
  }

  // Give an error if an aggregate argument is referenced in a non-aggregated
  // way, i.e. outside of an AggregateFunctionCall. This implementation is a bit
  // of a hack.  We traverse the ResolvedExpr after extracting and clearing the
  // aggregate function calls and see if we have any ResolvedArgumentRefs
  // remaining.  This was easier than tracking state during expression
  // resolution so we can give an error immediately when we see the reference,
  // but means that we don't have the AST node to use for error location.
  // Instead, we save the parse location into the ResolvedArgumentRef at
  // construction time.
  std::vector<const ResolvedNode*> found_nodes;
  resolved_expr.GetDescendantsWithKinds({RESOLVED_ARGUMENT_REF}, &found_nodes);
  for (const ResolvedNode* found_node : found_nodes) {
    const ResolvedArgumentRef* arg = found_node->GetAs<ResolvedArgumentRef>();
    if (arg->argument_kind() != ResolvedArgumentRef::NOT_AGGREGATE) {
      // Use the location we stored in the ResolvedArgumentRef when
      // we constructed it.
      const std::string message =
          absl::StrCat("Function argument ", ToIdentifierLiteral(arg->name()),
                       " cannot be referenced outside aggregate function calls"
                       " unless marked as NOT AGGREGATE");
      if (sql_function_body_location != nullptr) {
        return MakeSqlErrorAtPoint(
                   GetLocationFromResolvedNode(arg, sql_function_body_location))
               << message;
      } else {
        return MakeSqlError() << message;
      }
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string>
FunctionResolver::GetFunctionArgumentIndexMappingPerSignature(
    absl::string_view function_name, const FunctionSignature& signature,
    const ASTNode* ast_location,
    const std::vector<const ASTNode*>& arg_locations,
    absl::Span<const NamedArgumentInfo> named_arguments,
    int num_repeated_args_repetitions,
    bool always_include_omitted_named_arguments_in_index_mapping,
    std::vector<ArgIndexEntry>* index_mapping) const {
  // Make the reservation for the largest number of arguments that are possibly
  // to be handled and put into the list.
  index_mapping->reserve(
      signature.NumRequiredArguments() + signature.NumOptionalArguments() +
      signature.NumRepeatedArguments() * num_repeated_args_repetitions);

  // Build a map from each argument name to the index in which the named
  // argument appears in <arguments> and <arg_locations>.
  std::map<std::string, int, googlesql_base::CaseLess>
      argument_names_to_indexes;
  int first_named_arg_index_in_call = std::numeric_limits<int>::max();
  int last_named_arg_index_in_call = -1;
  // Sanity check that the named argument map initialization in the signature is
  // ok.
  GOOGLESQL_RETURN_IF_ERROR(signature.init_status());
  for (const auto& named_argument : named_arguments) {
    // Map the argument name to the index in which it appears in the function
    // call. If the name already exists in the map, this is a duplicate named
    // argument which is not allowed.
    const std::string provided_arg_name = named_argument.name().ToString();
    if (!googlesql_base::InsertIfNotPresent(&argument_names_to_indexes, provided_arg_name,
                                 named_argument.index())) {
      return named_argument.MakeSQLError()
             << "Duplicate named argument " << provided_arg_name
             << " found in call to function " << function_name;
    }
    // Make sure the provided argument name exists in the function signature.
    if (!signature.HasNamedArgument(provided_arg_name)) {
      return absl::StrCat("Named argument ",
                          ToAlwaysQuotedIdentifierLiteral(provided_arg_name),
                          " does not exist in signature");
    }
    // Make sure the argument name is allowed in function calls.
    const FunctionArgumentTypeOptions* argument_options =
        signature.FindNamedArgumentOptions(provided_arg_name);
    GOOGLESQL_RET_CHECK(argument_options != nullptr);
    if (argument_options->named_argument_kind() == kPositionalOnly) {
      return absl::StrCat("Argument ",
                          ToAlwaysQuotedIdentifierLiteral(provided_arg_name),
                          " must by supplied by position, not by name");
    }
    // Keep track of the first and last named argument index.
    first_named_arg_index_in_call =
        std::min(first_named_arg_index_in_call, named_argument.index());
    last_named_arg_index_in_call =
        std::max(last_named_arg_index_in_call, named_argument.index());
  }

  // Check that named arguments are not followed by positional arguments.
  GOOGLESQL_RET_CHECK_LE(arg_locations.size(), std::numeric_limits<int32_t>::max());
  int num_provided_args = static_cast<int>(arg_locations.size());
  if (!named_arguments.empty() &&
      (last_named_arg_index_in_call - first_named_arg_index_in_call >=
           named_arguments.size() ||
       last_named_arg_index_in_call + 1 < num_provided_args)) {
    return named_arguments.back().MakeSQLError()
           << "Call to function " << function_name << " must not specify "
           << "positional arguments after named arguments; named arguments "
           << "must be specified last in the argument list";
  }

  // Iterate through the function signature and rearrange the provided arguments
  // using the 'argument_names_to_indexes' map.
  int call_arg_index = 0;
  int first_repeated = signature.FirstRepeatedArgumentIndex();
  int last_repeated = signature.LastRepeatedArgumentIndex();

  for (int sig_index = 0; sig_index < signature.arguments().size();
       ++sig_index) {
    const FunctionArgumentType& arg_type = signature.arguments()[sig_index];
    const std::string& signature_arg_name =
        arg_type.options().has_argument_name()
            ? arg_type.options().argument_name()
            : "";
    const int* named_argument_call_index =
        signature_arg_name.empty()
            ? nullptr
            : googlesql_base::FindOrNull(argument_names_to_indexes, signature_arg_name);
    // For positional arguments that appear before any named arguments appear,
    // simply retain their locations and argument types.
    if ((named_arguments.empty() ||
         call_arg_index < first_named_arg_index_in_call) &&
        (call_arg_index < arg_locations.size() || signature_arg_name.empty())) {
      // Make sure that the function signature does not specify an optional name
      // for this positional argument that also appears later as a named
      // argument in the function call.
      if (!signature_arg_name.empty() && named_argument_call_index != nullptr) {
        return absl::StrCat(
            "Named argument ",
            ToAlwaysQuotedIdentifierLiteral(signature_arg_name),
            " duplicates positional argument ", call_arg_index + 1,
            ", which also provides ",
            ToAlwaysQuotedIdentifierLiteral(signature_arg_name));
      }
      // Make sure that the function signature does not specify an argument
      // name positionally when the options require that it must be named.
      if (!signature_arg_name.empty() &&
          arg_type.options().named_argument_kind() == kNamedOnly) {
        return absl::StrCat("Positional argument at ", call_arg_index + 1,
                            " is invalid because argument ",
                            ToAlwaysQuotedIdentifierLiteral(signature_arg_name),
                            " can only be referred to by name");
      }

      // Skip the repeated part if we run into it but the repetition is zero.
      if (num_repeated_args_repetitions == 0 && sig_index >= first_repeated &&
          sig_index <= last_repeated) {
        sig_index = last_repeated;
        continue;
      }

      if (call_arg_index < num_provided_args) {
        index_mapping->push_back({.signature_arg_index = sig_index,
                                  .call_arg_index = call_arg_index++});
      } else if (sig_index <= signature.last_arg_index_with_default() ||
                 (always_include_omitted_named_arguments_in_index_mapping &&
                  sig_index <= signature.last_named_arg_index())) {
        // If the current argument was omitted but it or an argument after it
        // has a default value in function signature, then add an entry to the
        // index_mapping.
        index_mapping->push_back(
            {.signature_arg_index = sig_index, .call_arg_index = -1});
      }

      if (sig_index == last_repeated) {
        --num_repeated_args_repetitions;
        if (num_repeated_args_repetitions > 0) {
          sig_index = first_repeated - 1;
        }
      }

      continue;
    }
    // Lookup the required argument name from the map of provided named
    // arguments. If not found, return an error reporting the missing required
    // argument name.
    if (named_argument_call_index == nullptr) {
      if (num_repeated_args_repetitions != 0 && arg_type.repeated()) {
        return MakeSqlErrorAt(ast_location)
               << "Call to function " << function_name
               << " is missing repeated arguments.";
      }
      if (arg_type.required()) {
        return !signature_arg_name.empty()
                   ? absl::StrCat(
                         "Required named argument ",
                         ToAlwaysQuotedIdentifierLiteral(signature_arg_name),
                         " is not provided")
                   : absl::StrCat("Required positional argument number ",
                                  (sig_index + 1), " is not provided");
      }

      if (arg_type.optional() &&
          (always_include_omitted_named_arguments_in_index_mapping ||
           !named_arguments.empty() ||
           sig_index <= signature.last_arg_index_with_default())) {
        index_mapping->push_back(
            {.signature_arg_index = sig_index, .call_arg_index = -1});
      }
      continue;
    }

    // Repeated argument types may never have required argument names.
    GOOGLESQL_RET_CHECK(!arg_type.repeated())
        << "Call to function " << function_name << " includes named "
        << "argument " << signature_arg_name << " referring to a repeated "
        << "argument type, which is not supported";

    GOOGLESQL_RET_CHECK_LT(*named_argument_call_index, num_provided_args);

    index_mapping->push_back({.signature_arg_index = sig_index,
                              .call_arg_index = *named_argument_call_index});
  }
  return "";
}

// static
absl::Status FunctionResolver::
    ReorderInputArgumentTypesPerIndexMappingAndInjectDefaultValues(
        const FunctionSignature& signature,
        absl::Span<const ArgIndexEntry> index_mapping,
        std::vector<InputArgumentType>* input_argument_types,
        std::vector<const ASTNode*>* arg_locations) {
  GOOGLESQL_RET_CHECK_NE(input_argument_types, nullptr);
  if (arg_locations != nullptr) {
    GOOGLESQL_RET_CHECK_EQ(input_argument_types->size(), arg_locations->size());
  }

  std::vector<InputArgumentType> orig_input_argument_types =
      std::move(*input_argument_types);
  input_argument_types->clear();

  for (const ArgIndexEntry& p : index_mapping) {
    if (p.call_arg_index >= 0) {
      input_argument_types->emplace_back(
          std::move(orig_input_argument_types[p.call_arg_index]));
    } else {
      GOOGLESQL_RET_CHECK_LE(0, p.signature_arg_index);
      // The argument was omitted from the function call so we add an entry into
      // <input_argument_types>, using the default value if present, or NULL if
      // not.
      const FunctionArgumentType& arg_type =
          signature.arguments()[p.signature_arg_index];
      GOOGLESQL_RET_CHECK(arg_type.optional());
      const std::optional<Value>& opt_default = arg_type.GetDefault();

      if (opt_default.has_value()) {
        input_argument_types->emplace_back(opt_default.value(),
                                           /*is_default_argument_value=*/true);
      } else if (arg_type.type() != nullptr) {
        input_argument_types->emplace_back(Value::Null(arg_type.type()));
      } else {
        input_argument_types->emplace_back(InputArgumentType::UntypedNull());
      }
    }
  }
  if (arg_locations == nullptr) {
    return absl::OkStatus();
  }
  // If `arg_locations` is provided, also reorder it to match the signature.
  std::vector<const ASTNode*> original_arg_locations =
      std::move(*arg_locations);
  arg_locations->clear();
  for (const ArgIndexEntry& p : index_mapping) {
    const ASTNode* call_arg_location =
        p.call_arg_index >= 0 ? original_arg_locations[p.call_arg_index]
                              : nullptr;
    arg_locations->push_back(call_arg_location);
  }
  // Check the invariant that `input_argument_types` still match positionally
  // with `arg_locations` after the reorder.
  GOOGLESQL_RET_CHECK_EQ(input_argument_types->size(), arg_locations->size());
  return absl::OkStatus();
}

namespace {
// Helper function to generate a ResolvedExpr node for an injected function
// argument whose value is either its default value or NULL.
absl::StatusOr<std::unique_ptr<ResolvedExpr>>
MakeResolvedLiteralForInjectedArgument(const InputArgumentType& input_arg_type,
                                       const Type* value_type) {
  // There should be a default value specified in <input_arg_type>,
  // which is either a default value provided in function definition,
  // or an untyped NULL acting as a default default value.
  GOOGLESQL_RET_CHECK(input_arg_type.is_literal() || input_arg_type.is_untyped_null());
  if (input_arg_type.is_literal()) {
    return MakeResolvedLiteral(*input_arg_type.literal_value());
  }
  if (value_type != nullptr) {
    return MakeResolvedLiteral(value_type, Value::Null(value_type));
  }
  return MakeResolvedLiteral(Value::Null(types::Int64Type()));
}
}  // namespace

// static
absl::Status FunctionResolver::ReorderArgumentExpressionsPerIndexMapping(
    absl::string_view function_name, const FunctionSignature& signature,
    absl::Span<const ArgIndexEntry> index_mapping, const ASTNode* ast_location,
    absl::Span<const InputArgumentType> input_argument_types,
    std::vector<const ASTNode*>* arg_locations,
    std::vector<std::unique_ptr<const ResolvedExpr>>* resolved_args,
    std::vector<ResolvedTVFArg>* resolved_tvf_args) {
  GOOGLESQL_RET_CHECK_EQ(index_mapping.size(), input_argument_types.size());

  std::vector<const ASTNode*> orig_arg_locations;
  std::vector<std::unique_ptr<const ResolvedExpr>> orig_resolved_args;
  std::vector<ResolvedTVFArg> orig_resolved_tvf_args;
  if (arg_locations != nullptr) {
    orig_arg_locations = std::move(*arg_locations);
    arg_locations->clear();
  }
  if (resolved_args != nullptr) {
    orig_resolved_args = std::move(*resolved_args);
    resolved_args->clear();
  }
  if (resolved_tvf_args != nullptr) {
    orig_resolved_tvf_args = std::move(*resolved_tvf_args);
    resolved_tvf_args->clear();
  }

  for (int i = 0; i < index_mapping.size(); ++i) {
    const ArgIndexEntry& aip = index_mapping[i];
    if (aip.call_arg_index >= 0) {
      if (arg_locations != nullptr) {
        arg_locations->emplace_back(orig_arg_locations[aip.call_arg_index]);
      }
      if (resolved_args != nullptr) {
        resolved_args->emplace_back(
            std::move(orig_resolved_args[aip.call_arg_index]));
      }
      if (resolved_tvf_args != nullptr) {
        resolved_tvf_args->emplace_back(
            std::move(orig_resolved_tvf_args[aip.call_arg_index]));
      }
    } else {
      GOOGLESQL_RET_CHECK_LE(0, aip.concrete_signature_arg_index);
      const FunctionArgumentType& arg_type =
          signature.arguments()[aip.concrete_signature_arg_index];
      const InputArgumentType& input_arg_type = input_argument_types[i];

      GOOGLESQL_RET_CHECK(arg_type.optional());

      if (arg_locations != nullptr) {
        arg_locations->emplace_back(ast_location);
      }

      if (resolved_args != nullptr) {
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> resolved_arg,
                         MakeResolvedLiteralForInjectedArgument(
                             input_arg_type, arg_type.type()));
        resolved_args->emplace_back(std::move(resolved_arg));
      }
      if (resolved_tvf_args != nullptr) {
        if (arg_type.IsRelation()) {
          ResolvedTVFArg arg;
          arg.SetScan(MakeResolvedUnsetArgumentScan(),
                      std::make_shared<NameList>(),
                      /*is_pipe_input_table=*/false);
          resolved_tvf_args->emplace_back(std::move(arg));
          continue;
        }
        if (!arg_type.IsScalar()) {
          return MakeSqlErrorAt(ast_location)
                 << "Call to table valued function " << function_name
                 << " does not specify a value for the non-scalar argument "
                 << (arg_type.options().has_argument_name()
                         ? arg_type.options().argument_name()
                         : "");
        }
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> resolved_arg,
                         MakeResolvedLiteralForInjectedArgument(
                             input_arg_type, arg_type.type()));
        ResolvedTVFArg arg;
        arg.SetExpr(std::move(resolved_arg));
        resolved_tvf_args->emplace_back(std::move(arg));
      }
    }
  }
  return absl::OkStatus();
}

// Appends signature mismatch reason to the error `message`, which contains the
// corresponding signature on the last line.
static absl::Status AppendMismatchReasonWithIndent(std::string* message,
                                                   absl::string_view reason) {
  // Could happen when a new signature mismatch case is introduced without
  // setting the reason.
  GOOGLESQL_RET_CHECK(!reason.empty()) << "No reason is specified while trying to "
                                "compose mismatch error message: "
                             << *message;

  for (absl::string_view line : absl::StrSplit(reason, '\n')) {
    absl::StrAppend(message, "\n    ");
    absl::StrAppend(message, line);
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> FunctionResolver::GetSupportedSignaturesWithMessage(
    const Function* function, absl::Span<const std::string> mismatch_errors,
    FunctionArgumentType::NamePrintingStyle print_style,
    int* num_signatures) const {
  GOOGLESQL_RET_CHECK_EQ(mismatch_errors.size(), function->signatures().size());

  // We don't show detailed error message when HideSupportedSignatures is true.
  // ABSL_DCHECK for sanity.
  ABSL_DCHECK(!function->HideSupportedSignatures());
  if (function->HideSupportedSignatures()) {
    return "";
  }

  // Use the customized signatures callback if set.
  const LanguageOptions& language_options = resolver_->language();
  if (function->GetSupportedSignaturesCallback() != nullptr &&
      // In case we have per signature callback, we have opportunity for per
      // signature mismatch error.
      !function->HasSignatureTextCallback()) {
    *num_signatures = function->NumSignatures();
    return function->GetSupportedSignaturesCallback()(language_options,
                                                      *function);
  }

  std::string result;
  for (int sig_idx = 0; sig_idx < function->signatures().size(); sig_idx++) {
    const FunctionSignature& signature = *function->GetSignature(sig_idx);
    // Ignore deprecated signatures, and signatures that include unsupported
    // data types etc.
    if (signature.HideInSupportedSignatureList(language_options)) {
      continue;
    }
    (*num_signatures)++;
    if (!result.empty()) {
      absl::StrAppend(&result, "\n");
    }
    absl::StrAppend(&result, "  Signature: ");
    if (function->HasSignatureTextCallback()) {
      absl::StrAppend(&result, function->GetSignatureTextCallback()(
                                   language_options, *function, signature));
    } else {
      std::vector<std::string> argument_texts =
          signature.GetArgumentsUserFacingTextWithCardinality(
              language_options, print_style, /*print_template_details=*/true);
      absl::StrAppend(&result, function->GetSQL(argument_texts));
    }
    GOOGLESQL_RETURN_IF_ERROR(
        AppendMismatchReasonWithIndent(&result, mismatch_errors[sig_idx]));
  }
  return result;
}

absl::StatusOr<std::string>
FunctionResolver::GenerateErrorMessageWithSupportedSignatures(
    const Function* function, absl::string_view prefix_message,
    FunctionArgumentType::NamePrintingStyle print_style,
    const std::vector<std::string>* mismatch_errors) const {
  int num_signatures = 0;
  std::string supported_signatures;
  GOOGLESQL_RET_CHECK(mismatch_errors != nullptr);
  if (!function->HideSupportedSignatures()) {
    GOOGLESQL_ASSIGN_OR_RETURN(supported_signatures, GetSupportedSignaturesWithMessage(
                                               function, *mismatch_errors,
                                               print_style, &num_signatures));
  }

  if (!supported_signatures.empty()) {
    // Example `prefix_message`:
    //   No matching signature for function ARRAY_INCLUDES_ANY.
    //     Argument types: ARRAY<INT64>, ARRAY<STRING>
    return absl::StrCat(prefix_message, "\n", supported_signatures);
  } else {
    if (function->GetSupportedSignaturesCallback() == nullptr &&
        !function->HideSupportedSignatures()) {
      // If we do not have any supported signatures and there is
      // no custom callback for producing the signature messages,
      // then we provide an error message as if the function did
      // not exist at all (similar to the error message produced in
      // Resolver::LookupFunctionFromCatalog()). Note that it does
      // not make sense to try to suggest a different function name
      // in this context (like we do in LookupFunctionFromCatalog()).
      return absl::StrCat("Function not found: ", function->SQLName());
    } else {
      // In some cases, like for 'IN', we do not produce a suggested
      // signature.  But we still want to get a 'no matching signature'
      // error message since it indicates the invalid arguments (rather
      // than a 'function not found' message, which would be odd in
      // this case since IN does exist).
      return std::string(prefix_message);
    }
  }
}

// TODO: Eventually we want to keep track of the closest
// signature even if there is no match, so that we can provide a good
// error message.  Currently, this code takes an early exit if a signature
// does not match and does not accurately determine how close the signature
// was, nor does it keep track of the best non-matching signature.
absl::StatusOr<const FunctionSignature*>
FunctionResolver::FindMatchingSignature(
    const Function* function, const ASTNode* ast_location,
    const std::vector<const ASTNode*>& arg_locations_in,
    bool match_internal_signatures,
    absl::Span<const NamedArgumentInfo> named_arguments,
    const NameScope* name_scope,
    std::vector<InputArgumentType>* input_arguments,
    std::vector<FunctionArgumentOverride>* arg_overrides,
    std::vector<ArgIndexEntry>* arg_index_mapping_out,
    // Needed for re-resolution of lambdas when args have annotations.
    absl::flat_hash_map<const ResolvedInlineLambda*, const ASTLambda*>*
        lambda_ast_nodes_out,
    std::vector<std::string>* mismatch_errors) const {
  GOOGLESQL_RETURN_IF_NOT_ENOUGH_STACK(
      "Out of stack space due to deeply nested query expression "
      "during function resolution");

  std::unique_ptr<FunctionSignature> best_result_signature;
  SignatureMatchResult best_result;
  std::vector<FunctionArgumentOverride> best_result_arg_overrides;
  bool seen_matched_signature_with_lambda = false;
  GOOGLESQL_RET_CHECK(mismatch_errors != nullptr);

  GOOGLESQL_VLOG(6) << "FindMatchingSignature for function: "
          << function->DebugString(/*verbose=*/true) << "\n  for arguments: "
          << InputArgumentType::ArgumentsToString(
                 *input_arguments, ProductMode::PRODUCT_INTERNAL);

  if (!named_arguments.empty() &&
      !resolver_->language().LanguageFeatureEnabled(FEATURE_NAMED_ARGUMENTS)) {
    return named_arguments[0].MakeSQLError()
           << "Named arguments are not supported";
  }

  GOOGLESQL_RET_CHECK_LE(arg_locations_in.size(), std::numeric_limits<int32_t>::max());
  const int num_provided_args = static_cast<int>(arg_locations_in.size());
  const int num_signatures = function->NumSignatures();
  std::vector<InputArgumentType> original_input_arguments = *input_arguments;
  mismatch_errors->reserve(num_signatures);
  for (const FunctionSignature& signature : function->signatures()) {
    int repetitions = 0;
    int optionals = 0;
    // If a user calls a function with an internal signature, we won't match it.
    // Only when an internal caller invoke this method, the internal signature
    // will be matched.
    if (signature.IsInternal() && !match_internal_signatures) {
      mismatch_errors->push_back("Internal error");
      continue;
    }

    // Prioritize named argument not found error, which is better than a general
    // "argument count does not match".
    if (!named_arguments.empty()) {
      bool error_found = false;
      for (const NamedArgumentInfo& named_argument : named_arguments) {
        absl::string_view name = named_argument.name().ToStringView();
        if (!signature.HasNamedArgument(name)) {
          mismatch_errors->push_back(absl::StrCat(
              "Named argument ", ToAlwaysQuotedIdentifierLiteral(name),
              " does not exist in signature"));
          error_found = true;
          break;
        }
      }
      if (error_found) {
        continue;
      }
    }

    SignatureMatchResult signature_match_result;
    signature_match_result.set_allow_mismatch_message(true);
    GOOGLESQL_RET_CHECK_EQ(original_input_arguments.size(), num_provided_args);
    if (!SignatureArgumentCountMatches(signature, original_input_arguments,
                                       &repetitions, &optionals,
                                       &signature_match_result)) {
      GOOGLESQL_RET_CHECK(!signature_match_result.mismatch_message().empty());
      mismatch_errors->push_back(signature_match_result.mismatch_message());
      continue;
    }

    std::vector<ArgIndexEntry> arg_index_mapping;
    absl::StatusOr<std::string> mismatch_message_or =
        GetFunctionArgumentIndexMappingPerSignature(
            function->FullName(), signature, ast_location, arg_locations_in,
            named_arguments, repetitions,
            /*always_include_omitted_named_arguments_in_index_mapping=*/true,
            &arg_index_mapping);
    // NOTE: Some errors means the call will match no signature providing the
    // same named argument twice) while others means this signature mismatches
    // (for example providing unknown argument name for a signature). This is
    // better handled below when show_function_signature_mismatch_details is
    // enabled.

    // Error means the call can never match any signature.
    if (!mismatch_message_or.ok()) {
      return mismatch_message_or.status();
    }
    // Record the mismatch message when it's set.
    if (!mismatch_message_or.value().empty()) {
      mismatch_errors->push_back(mismatch_message_or.value());
      continue;
    }

    std::vector<InputArgumentType> input_arguments_copy =
        original_input_arguments;
    std::vector<const ASTNode*> reordered_arg_locations = arg_locations_in;
    if (!arg_index_mapping.empty()) {
      GOOGLESQL_RETURN_IF_ERROR(
          ReorderInputArgumentTypesPerIndexMappingAndInjectDefaultValues(
              signature, arg_index_mapping, &input_arguments_copy,
              &reordered_arg_locations));
    }

    std::unique_ptr<FunctionSignature> result_concrete_signature;
    std::vector<FunctionArgumentOverride> sig_arg_overrides;
    absl::flat_hash_map<const ResolvedInlineLambda*, const ASTLambda*>
        lambda_ast_nodes;
    GOOGLESQL_ASSIGN_OR_RETURN(
        const bool is_match,
        SignatureMatches(reordered_arg_locations, input_arguments_copy,
                         signature, function->ArgumentsAreCoercible(),
                         name_scope, &result_concrete_signature,
                         &signature_match_result, &arg_index_mapping,
                         &sig_arg_overrides, &lambda_ast_nodes));
    if (!is_match) {
      mismatch_errors->push_back(signature_match_result.mismatch_message());
      continue;
    }

    GOOGLESQL_RET_CHECK(result_concrete_signature != nullptr);
    GOOGLESQL_ASSIGN_OR_RETURN(const std::string arg_constraints_violation_reason,
                     result_concrete_signature->CheckArgumentConstraints(
                         input_arguments_copy));
    // If this signature has argument constraints and they are not
    // satisfied then check the next signature.
    if (!arg_constraints_violation_reason.empty()) {
      mismatch_errors->push_back(arg_constraints_violation_reason);
      continue;
    }

    GOOGLESQL_VLOG(6) << "Found signature for input arguments: "
            << InputArgumentType::ArgumentsToString(
                   input_arguments_copy, ProductMode::PRODUCT_INTERNAL)
            << "\nfunction signature: "
            << signature.DebugString(/*function_name=*/"",
                                     /*verbose=*/true)
            << "\nresult signature: "
            << result_concrete_signature->DebugString(/*function_name=*/"",
                                                      /*verbose=*/true)
            << "\n  cost: " << signature_match_result.DebugString();

    if (best_result_signature != nullptr) {
      // When the other arguments are not enough to distinguish which
      // signature to use, we're left only with the lambdas, which can't
      // distinguish between two overloads. If this ABSL_CHECK fails, an engine has
      // set up its catalog with a function signature that GoogleSQL doesn't
      // mean to support for the time being. This shouldn't happen as
      // Function::CheckMultipleSignatureMatchingSameFunctionCall() validation
      // should have screened that.
      // Another interesting example function shape is the following:
      //   Func(T1, LAMBDA(T1, T1) -> INT64)
      //   Func(T1, LAMBDA(T1, T1) -> STRING)
      // These two signatures cannot match any actual function call at the
      // same time as one expression could only be coerced to either string or
      // int64. But our restriction is still restricting this for simplicity
      // and lack of use case.
      GOOGLESQL_RET_CHECK(!seen_matched_signature_with_lambda ||
                sig_arg_overrides.empty())
          << "Multiple matched signature with lambda is not supported";
    }

    if ((best_result_signature == nullptr) ||
        (signature_match_result.IsCloserMatchThan(best_result))) {
      best_result_signature = std::move(result_concrete_signature);
      best_result = std::move(signature_match_result);
      if (!sig_arg_overrides.empty()) {
        GOOGLESQL_RET_CHECK(arg_overrides != nullptr)
            << "Function call has lambdas but nowhere to put them";
      }
      seen_matched_signature_with_lambda =
          seen_matched_signature_with_lambda || !sig_arg_overrides.empty();
      best_result_arg_overrides = std::move(sig_arg_overrides);
      *input_arguments = std::move(input_arguments_copy);
      *arg_index_mapping_out = std::move(arg_index_mapping);
      *lambda_ast_nodes_out = std::move(lambda_ast_nodes);
    } else {
      GOOGLESQL_VLOG(4) << "Found duplicate signature matches for function: "
              << function->DebugString() << "\nGiven input arguments: "
              << InputArgumentType::ArgumentsToString(
                     input_arguments_copy, ProductMode::PRODUCT_INTERNAL)
              << "\nBest result signature: "
              << best_result_signature->DebugString()
              << "\n  cost: " << best_result.DebugString()
              << "\nDuplicate signature: "
              << result_concrete_signature->DebugString()
              << "\n  cost: " << signature_match_result.DebugString();
    }
  }

  if (arg_overrides != nullptr) {
    *arg_overrides = std::move(best_result_arg_overrides);
  }
  return best_result_signature.release();
}

static void ConvertMakeStructToLiteralIfAllExplicitLiteralFields(
    std::unique_ptr<const ResolvedExpr>* argument) {
  if (!(*argument)->type()->IsStruct() ||
      argument->get()->node_kind() != RESOLVED_MAKE_STRUCT) {
    return;
  }

  ResolvedMakeStruct* struct_expr = const_cast<ResolvedMakeStruct*>(
      argument->get()->GetAs<ResolvedMakeStruct>());
  std::vector<Value> field_values;
  for (const auto& field_expr : struct_expr->field_list()) {
    if (field_expr->node_kind() != RESOLVED_LITERAL ||
        !field_expr->GetAs<ResolvedLiteral>()->has_explicit_type()) {
      // This field is not a literal or is not explicitly typed so do not
      // collapse this into a literal.  Return with <argument> unchanged.
      return;
    }
    field_values.push_back(field_expr->GetAs<ResolvedLiteral>()->value());
  }
  // All fields were explicitly typed literals, so construct a literal for
  // this MakeStruct, and mark this new Struct literal as explicitly typed.
  *argument = MakeResolvedLiteral(
      (*argument)->type(),
      Value::Struct((*argument)->type()->AsStruct(), field_values),
      /*has_explicit_type=*/true);
}

absl::Status ExtractStructFieldLocations(
    const StructType* to_struct_type, const ASTNode* ast_location,
    std::vector<const ASTNode*>* field_arg_locations) {
  // Skip through gratuitous casts in the AST so that we can get the field
  // argument locations.
  const ASTNode* cast_free_ast_location = ast_location;
  while (cast_free_ast_location != nullptr) {
    if (cast_free_ast_location->node_kind() == AST_CAST_EXPRESSION) {
      const ASTCastExpression* ast_cast =
          cast_free_ast_location->GetAs<ASTCastExpression>();
      cast_free_ast_location = ast_cast->expr();
    } else if (cast_free_ast_location->node_kind() == AST_NAMED_ARGUMENT) {
      // The grammar guarantees the named argument does not have a lambda as
      // its value.
      const ASTNamedArgument* ast_arg =
          cast_free_ast_location->GetAs<ASTNamedArgument>();
      cast_free_ast_location = ast_arg->expr();
    } else {
      break;
    }
  }
  GOOGLESQL_RET_CHECK_NE(nullptr, cast_free_ast_location) << ast_location->DebugString();

  switch (cast_free_ast_location->node_kind()) {
    case AST_STRUCT_CONSTRUCTOR_WITH_PARENS: {
      const ASTStructConstructorWithParens* ast_struct =
          cast_free_ast_location->GetAs<ASTStructConstructorWithParens>();
      GOOGLESQL_RET_CHECK_EQ(ast_struct->field_expressions().size(),
                   to_struct_type->num_fields());
      *field_arg_locations = ToASTNodes(ast_struct->field_expressions());
      break;
    }
    case AST_STRUCT_CONSTRUCTOR_WITH_KEYWORD: {
      const ASTStructConstructorWithKeyword* ast_struct =
          cast_free_ast_location->GetAs<ASTStructConstructorWithKeyword>();
      GOOGLESQL_RET_CHECK_EQ(ast_struct->fields().size(), to_struct_type->num_fields());
      // Strip "AS <alias>" clauses from field arg locations.
      for (const ASTStructConstructorArg* arg : ast_struct->fields()) {
        field_arg_locations->push_back(arg->expression());
      }
      break;
    }
    case AST_BRACED_CONSTRUCTOR: {
      const ASTBracedConstructor* ast_braced =
          cast_free_ast_location->GetAsOrDie<ASTBracedConstructor>();
      GOOGLESQL_RET_CHECK_EQ(ast_braced->fields().size(), to_struct_type->num_fields());
      for (const ASTBracedConstructorField* field : ast_braced->fields()) {
        field_arg_locations->push_back(field->value());
      }
      break;
    }
    case AST_STRUCT_BRACED_CONSTRUCTOR: {
      const ASTStructBracedConstructor* ast_braced_struct =
          cast_free_ast_location->GetAsOrDie<ASTStructBracedConstructor>();
      const ASTBracedConstructor* ast_braced =
          ast_braced_struct->braced_constructor();
      GOOGLESQL_RET_CHECK_EQ(ast_braced->fields().size(), to_struct_type->num_fields());
      for (const ASTBracedConstructorField* field : ast_braced->fields()) {
        field_arg_locations->push_back(field->value());
      }
      break;
    }
    default: {
      GOOGLESQL_RET_CHECK_FAIL() << "Cannot obtain the AST expressions for field "
                       << "arguments of struct constructor:\n"
                       << ast_location->DebugString();
    }
  }
  GOOGLESQL_RET_CHECK_EQ(field_arg_locations->size(), to_struct_type->num_fields());
  return absl::OkStatus();
}

absl::Status FunctionResolver::AddCastOrConvertLiteral(
    const ASTNode* ast_location, AnnotatedType annotated_target_type,
    std::unique_ptr<const ResolvedExpr> format,
    std::unique_ptr<const ResolvedExpr> time_zone, TypeModifiers type_modifiers,
    const ResolvedScan* scan, bool set_has_explicit_type,
    bool return_null_on_error,
    std::unique_ptr<const ResolvedExpr>* argument) const {
  GOOGLESQL_RET_CHECK_NE(ast_location, nullptr);

  const Type* target_type = annotated_target_type.type;
  const AnnotationMap* target_type_annotation_map =
      annotated_target_type.annotation_map;
  const TypeParameters& type_parameters = type_modifiers.type_parameters();
  const Collation& collation = type_modifiers.collation();

  GOOGLESQL_ASSIGN_OR_RETURN(
      bool equals_collation_annotation,
      collation.EqualsCollationAnnotation(target_type_annotation_map));
  GOOGLESQL_RET_CHECK(equals_collation_annotation);

  // If this conversion is a no-op we can return early.
  if (target_type->Equals(argument->get()->type()) && !set_has_explicit_type &&
      AnnotationMap::Equals(argument->get()->type_annotation_map(),
                            target_type_annotation_map)) {
    // These fields should only be used for explicit casts when
    // set_has_explicit_type is true.
    GOOGLESQL_RET_CHECK(type_parameters.IsEmpty());
    GOOGLESQL_RET_CHECK_EQ(format.get(), nullptr);
    GOOGLESQL_RET_CHECK_EQ(time_zone.get(), nullptr);
    return absl::OkStatus();
  }

  // We add the casts field-by-field for struct expressions. We will collapse
  // a ResolvedMakeStruct to a struct literal if all the fields are literals
  // and <set_has_explicit_type> is true. If the MakeStruct is the result of a
  // path expression rather than an explicit struct constructor with fields, use
  // a struct cast.
  if (target_type->IsStruct() &&
      argument->get()->node_kind() == RESOLVED_MAKE_STRUCT &&
      ast_location->node_kind() != AST_PATH_EXPRESSION) {
    GOOGLESQL_RET_CHECK(target_type_annotation_map == nullptr ||
              target_type_annotation_map->HasCompatibleStructure(target_type));
    // Remove constness so that we can add casts on the field expressions inside
    // the struct.
    ResolvedMakeStruct* struct_expr = const_cast<ResolvedMakeStruct*>(
        argument->get()->GetAs<ResolvedMakeStruct>());
    const StructType* to_struct_type = target_type->AsStruct();
    GOOGLESQL_RET_CHECK_EQ(struct_expr->field_list_size(), to_struct_type->num_fields());
    GOOGLESQL_RET_CHECK(type_parameters.MatchType(to_struct_type));
    GOOGLESQL_RET_CHECK(collation.HasCompatibleStructure(to_struct_type));

    std::vector<const ASTNode*> field_arg_locations;
    // If we can't obtain the locations of field arguments and replace literals
    // inside that expression, their parse locations will be wrong.
    GOOGLESQL_RETURN_IF_ERROR(ExtractStructFieldLocations(to_struct_type, ast_location,
                                                &field_arg_locations));

    std::vector<std::unique_ptr<const ResolvedExpr>> field_exprs =
        struct_expr->release_field_list();
    for (int i = 0; i < to_struct_type->num_fields(); ++i) {
      const AnnotationMap* field_type_annotation_map =
          target_type_annotation_map == nullptr
              ? nullptr
              : target_type_annotation_map->AsStructMap()->field(i);
      if (to_struct_type->field(i).type->Equals(field_exprs[i]->type()) &&
          AnnotationMap::Equals(field_exprs[i]->type_annotation_map(),
                                field_type_annotation_map)) {
        if (field_exprs[i]->node_kind() == RESOLVED_LITERAL &&
            set_has_explicit_type &&
            !field_exprs[i]->GetAs<ResolvedLiteral>()->has_explicit_type()) {
          // This field has the same Type, but is a literal that needs to
          // have it set as has_explicit_type so we must replace the
          // expression.
          field_exprs[i] = resolver_->MakeResolvedLiteral(
              field_arg_locations[i], field_exprs[i]->annotated_type(),
              field_exprs[i]->GetAs<ResolvedLiteral>()->value(),
              /*has_explicit_type=*/true);
        }
      }

      TypeModifiers field_type_modifers = TypeModifiers::MakeTypeModifiers(
          type_modifiers.type_parameters().IsEmpty() ? TypeParameters()
                                                     : type_parameters.child(i),
          collation.Empty() ? Collation() : collation.child(i));

      // We pass nullptr for 'format' and 'time_zone' here because there is
      // currently no way to define format strings for individual struct fields.
      const absl::Status cast_status = AddCastOrConvertLiteral(
          field_arg_locations[i],
          AnnotatedType(to_struct_type->field(i).type,
                        field_type_annotation_map),
          /*format=*/nullptr, /*time_zone=*/nullptr,
          std::move(field_type_modifers), scan, set_has_explicit_type,
          return_null_on_error, &field_exprs[i]);
      if (!cast_status.ok()) {
        // Propagate "Out of stack space" errors.
        // TODO: Propagate internal, unimplemented, etc
        if (cast_status.code() == absl::StatusCode::kResourceExhausted) {
          return cast_status;
        }

        // This cast failed. However, we need all the information we gathered
        // so far (e.g. deciding an untyped NULL's type by attempting to coerce
        // it), so successful parts are left folded. At the same time, when the
        // failure is deferred to runtime, we need to be able to fully resolve
        // the cast. Make sure we maintain proper internal state (e.g. STRUCT
        // field_types must match its field_exprs).
        const StructType* new_type;
        std::vector<StructField> struct_fields;
        const googlesql::StructType* old_type = struct_expr->type()->AsStruct();
        struct_fields.reserve(field_exprs.size());
        for (int j = 0; j < field_exprs.size(); j++) {
          struct_fields.push_back(
              {old_type->field(j).name, field_exprs[j]->type()});
        }

        GOOGLESQL_RET_CHECK_OK(type_factory_->MakeStructType(struct_fields, &new_type));
        struct_expr->set_type(new_type);
        struct_expr->set_field_list(std::move(field_exprs));
        return MakeSqlErrorAt(field_arg_locations[i]) << cast_status.message();
      }
    }
    std::unique_ptr<ResolvedMakeStruct> make_struct =
        MakeResolvedMakeStruct(target_type, std::move(field_exprs));
    GOOGLESQL_RETURN_IF_ERROR(resolver_->CheckAndPropagateAnnotations(
        /*error_node=*/nullptr, make_struct.get()));
    *argument = std::move(make_struct);

    // If all the fields are now explicitly casted literals, then we can
    // convert this MakeStruct into a Literal instead.
    ConvertMakeStructToLiteralIfAllExplicitLiteralFields(argument);

    return absl::OkStatus();
  } else if ((*argument)->node_kind() == RESOLVED_FUNCTION_CALL &&
             (*argument)->GetAs<ResolvedFunctionCall>()->function()->FullName(
                 /*include_group=*/true) == "GoogleSQL:error") {
    // This is an ERROR(message) function call.  We special case this to
    // make the output argument coercible to anything so expressions like
    //   IF(<condition>, <value>, ERROR("message"))
    // work for any value type.
    const ResolvedFunctionCall* old_call =
        (*argument)->GetAs<ResolvedFunctionCall>();
    FunctionSignature new_signature = old_call->signature();
    new_signature.SetConcreteResultType(
        target_type, old_call->signature().result_type().original_kind());
    *argument = MakeResolvedFunctionCall(
        target_type, old_call->function(), new_signature,
        const_cast<ResolvedFunctionCall*>(old_call)->release_argument_list(),
        old_call->error_mode());
    // Add a no-op cast to represent that the return type of ERROR(), which is
    // usually coercible to everything, is now explicitly specified. For
    // example, COALESCE('abc', CAST(ERROR('def') AS BYTES)) fails because BYTES
    // does not implicitly coerce to STRING.
    //
    // Also note that the value of 'return_null_on_error' does not matter here
    // because this no-op cast cannot fail.
    *argument = MakeResolvedCast(target_type, std::move(*argument),
                                 return_null_on_error);
    return absl::OkStatus();
  }

  const ResolvedLiteral* argument_literal = nullptr;
  if (argument->get()->node_kind() == RESOLVED_LITERAL) {
    argument_literal = argument->get()->GetAs<ResolvedLiteral>();
  } else if (argument->get()->node_kind() == RESOLVED_COLUMN_REF &&
             scan != nullptr && scan->node_kind() == RESOLVED_PROJECT_SCAN) {
    // TODO This FindProjectExpr uses a linear scan, so converting
    // N expressions one by one is potentially N^2.  Maybe build a map somehow.
    const ResolvedExpr* found_expr =
        FindProjectExpr(scan->GetAs<ResolvedProjectScan>(),
                        argument->get()->GetAs<ResolvedColumnRef>()->column());
    if (found_expr != nullptr && found_expr->node_kind() == RESOLVED_LITERAL) {
      argument_literal = found_expr->GetAs<ResolvedLiteral>();
    }
  }

  // Implicitly convert literals if possible.  When casting to a type with
  // modifiers, we first cast to the base type here, and then add an explicit
  // cast to the type with modifiers after this implicit conversion.
  //
  // TODO: Should this look at time_zone and type_params too?
  if (resolver_->analyzer_options().fold_literal_cast() &&
      argument_literal != nullptr && format == nullptr) {
    std::unique_ptr<const ResolvedLiteral> converted_literal;
    GOOGLESQL_RETURN_IF_ERROR(ConvertLiteralToType(
        ast_location, argument_literal, target_type,
        type_modifiers.type_parameters(), scan, set_has_explicit_type,
        return_null_on_error, &converted_literal));
    *argument = std::move(converted_literal);
  }

  // Assign type to undeclared parameters.
  GOOGLESQL_ASSIGN_OR_RETURN(
      const bool type_assigned,
      resolver_->MaybeAssignTypeToUndeclaredParameter(argument, target_type));
  if (type_assigned && type_modifiers.IsEmpty()) {
    return absl::OkStatus();
  }

  return resolver_->ResolveCastWithResolvedArgument(
      ast_location, annotated_target_type, std::move(format),
      std::move(time_zone), std::move(type_modifiers), return_null_on_error,
      argument);
}

absl::Status FunctionResolver::AddCastOrConvertLiteral(
    const ASTNode* ast_location, const Type* target_type,
    std::unique_ptr<const ResolvedExpr> format,
    std::unique_ptr<const ResolvedExpr> time_zone,
    const TypeParameters& type_params, const ResolvedScan* scan,
    bool set_has_explicit_type, bool return_null_on_error,
    std::unique_ptr<const ResolvedExpr>* argument) const {
  return AddCastOrConvertLiteral(
      ast_location, AnnotatedType(target_type, /*annotation_map=*/nullptr),
      std::move(format), std::move(time_zone),
      TypeModifiers::MakeTypeModifiers(type_params, Collation()), scan,
      set_has_explicit_type, return_null_on_error, std::move(argument));
}

namespace {
bool GetFloatImage(
    const absl::flat_hash_map<int, std::string>& float_literal_images,
    const ASTNode* ast_location, const ResolvedLiteral* argument_literal,
    absl::string_view* float_image) {
  const std::string* float_image_ptr = googlesql_base::FindOrNull(
      float_literal_images, argument_literal->float_literal_id());
  if (float_image_ptr != nullptr) {
    *float_image = *float_image_ptr;
    return true;
  }
  switch (ast_location->node_kind()) {
    case AST_FLOAT_LITERAL:
      *float_image = ast_location->GetAs<ASTFloatLiteral>()->image();
      return true;
    case AST_INT_LITERAL:
      *float_image = ast_location->GetAs<ASTIntLiteral>()->image();
      return true;
    default:
      return false;
  }
}
}  // namespace

absl::Status FunctionResolver::ConvertLiteralToType(
    const ASTNode* ast_location, const ResolvedLiteral* argument_literal,
    const Type* target_type, const TypeParameters& target_type_params,
    const ResolvedScan* scan, bool set_has_explicit_type,
    bool return_null_on_error,
    std::unique_ptr<const ResolvedLiteral>* converted_literal) const {
  const Value* argument_value = &argument_literal->value();
  absl::StatusOr<Value> coerced_literal_value;  // Initialized to UNKNOWN
  absl::string_view float_literal_image;

  const AnnotationMap* annotation_map = nullptr;

  if (argument_value->is_null()) {
    coerced_literal_value = Value::Null(target_type);
  } else if (argument_value->is_empty_array() &&
             !argument_literal->has_explicit_type() && target_type->IsArray()) {
    // Coerces an untyped empty array to an empty array of the target_type.
    coerced_literal_value =
        Value::Array(target_type->AsArray(), {} /* values */);
  } else if (argument_value->type()->IsStruct()) {
    // TODO: Make this clearer by factoring it out to a helper function
    // that returns an absl::StatusOr<Value>, making 'success' unnecessary and
    // allowing for a more detailed error message (like for string -> proto
    // conversion below).
    bool success = true;
    if (!target_type->IsStruct() ||
        argument_value->num_fields() != target_type->AsStruct()->num_fields() ||
        // Target type parameters can be empty.
        (target_type_params.num_children() != 0 &&
         argument_value->num_fields() != target_type_params.num_children())) {
      success = false;
    }

    // We construct the coerced literal field-by-field for structs.
    std::vector<std::unique_ptr<const ResolvedLiteral>> coerced_field_literals;
    for (int i = 0; i < argument_value->num_fields() && success; ++i) {
      const Type* target_field_type = target_type->AsStruct()->field(i).type;
      const TypeParameters& target_field_type_params =
          target_type_params.num_children() == 0 ? TypeParameters()
                                                 : target_type_params.child(i);
      // Parse locations of the literals created below are irrelevant because
      // in case of success we create a new literal struct containing these
      // literals.
      auto field_literal =
          MakeResolvedLiteral(target_field_type, argument_value->field(i));
      std::unique_ptr<const ResolvedLiteral> coerced_field_literal;
      if (ConvertLiteralToType(ast_location, field_literal.get(),
                               target_field_type, target_field_type_params,
                               scan, set_has_explicit_type,
                               return_null_on_error, &coerced_field_literal)
              .ok()) {
        ABSL_DCHECK_EQ(field_literal->node_kind(), RESOLVED_LITERAL);
        coerced_field_literals.push_back(std::move(coerced_field_literal));
      } else {
        success = false;
      }
    }

    if (success) {
      auto new_annotation_map = AnnotationMap::Create(target_type);
      GOOGLESQL_RET_CHECK(new_annotation_map->IsStructMap());
      std::vector<Value> coerced_field_values;
      for (int i = 0; i < coerced_field_literals.size(); ++i) {
        GOOGLESQL_RETURN_IF_ERROR(new_annotation_map->AsStructMap()->CloneIntoField(
            i, coerced_field_literals[i]->type_annotation_map()));
        coerced_field_values.push_back(coerced_field_literals[i]->value());
      }
      coerced_literal_value =
          Value::Struct(target_type->AsStruct(), coerced_field_values);
      new_annotation_map->Normalize();
      if (!new_annotation_map->Empty()) {
        GOOGLESQL_ASSIGN_OR_RETURN(annotation_map, type_factory_->TakeOwnership(
                                             std::move(new_annotation_map)));
      }
    }
  } else if (argument_value->type()->IsFloatingPoint() &&
             (target_type->IsNumericType() ||
              target_type->IsBigNumericType()) &&
             GetFloatImage(resolver_->float_literal_images_, ast_location,
                           argument_literal, &float_literal_image)) {
    // If we are casting a floating point literal into a NUMERIC or BIGNUMERIC,
    // reparse the original literal image instead of converting the floating
    // point value to preserve precision.
    if (target_type->IsNumericType()) {
      GOOGLESQL_ASSIGN_OR_RETURN(NumericValue coerced_numeric,
                       NumericValue::FromString(float_literal_image));
      coerced_literal_value = Value::Numeric(coerced_numeric);
    } else if (target_type->IsBigNumericType()) {
      GOOGLESQL_ASSIGN_OR_RETURN(BigNumericValue coerced_bignumeric,
                       BigNumericValue::FromString(float_literal_image));
      coerced_literal_value = Value::BigNumeric(coerced_bignumeric);
    }
  } else {
    // <coerced_literal_value> gets populated only when we can successfully
    // cast <argument_value> to <target_type>.
    coerced_literal_value = CastValue(
        *argument_value, resolver_->default_time_zone(), resolver_->language(),
        target_type, catalog_, /*canonicalize_zero=*/true);
  }

  if (!coerced_literal_value.status().ok()) {
    // If return_null_on_error is set to true, in-place converts the literal
    // to a NULL value of target_type. Otherwise, returns an error.
    if (return_null_on_error) {
      *converted_literal = resolver_->MakeResolvedLiteral(
          ast_location, AnnotatedType{target_type, annotation_map},
          Value::Null(target_type), set_has_explicit_type);
      return absl::OkStatus();
    } else {
      googlesql_base::StatusBuilder builder =
          MakeSqlErrorAt(ast_location)
          << "Could not cast "
          << (argument_literal->has_explicit_type() ? "" : "literal ")
          << argument_value->DebugString() << " to type "
          << target_type->ShortTypeName(resolver_->language().product_mode());
      // Give a more detailed error message for string/bytes -> proto
      // conversions, which can have subtle issues.
      absl::string_view error_message =
          coerced_literal_value.status().message();
      const Type* argument_type = argument_value->type();
      if ((argument_type->IsString() || argument_type->IsBytes()) &&
          target_type->IsProto() && !error_message.empty()) {
        builder << " (" << error_message << ")";
      } else if (target_type->IsEnum() && argument_type->IsString() &&
                 !argument_value->is_null()) {
        std::string suggestion = catalog_->SuggestEnumValue(
            target_type->AsEnum(), argument_value->string_value());

        if (!suggestion.empty()) {
          builder << "; Did you mean '" << suggestion << "'?";
          if (googlesql_base::CaseEqual(suggestion,
                                     argument_value->string_value())) {
            // If the actual value only differs by case, add a reminder.
            builder << " (Note: ENUM values are case sensitive)";
          }
        }
      }
      return builder;
    }
  }

  const bool has_explicit_type =
      argument_literal->has_explicit_type() || set_has_explicit_type;

  // Create replacement resolved literal.
  GOOGLESQL_ASSIGN_OR_RETURN(auto replacement_literal,
                   ResolvedLiteralBuilder()
                       .set_value(*coerced_literal_value)
                       .set_type(target_type)
                       .set_type_annotation_map(annotation_map)
                       .set_has_explicit_type(has_explicit_type)
                       .Build());

  // The float literal cache entry (if there is one) is no longer valid after
  // replacement.
  resolver_->float_literal_images_.erase(argument_literal->float_literal_id());
  if (resolver_->analyzer_options_.parse_location_record_type() !=
      PARSE_LOCATION_RECORD_NONE) {
    // Copy parse location to the replacement literal.
    if (argument_literal->GetParseLocationRangeOrNULL() != nullptr) {
      const_cast<ResolvedLiteral*>(replacement_literal.get())
          ->SetParseLocationRange(
              *argument_literal->GetParseLocationRangeOrNULL());
    }
    // Remove parse location on the original literal.  We don't want to
    // do a replacement based on that one because it has the original type,
    // not the inferred type, and it would have the same string location
    // as the replacement literal.
    const_cast<ResolvedLiteral*>(argument_literal)->ClearParseLocationRange();
  }
  *converted_literal = std::move(replacement_literal);

  return absl::OkStatus();
}

absl::Status FunctionResolver::ResolveGeneralFunctionCall(
    const ASTNode* ast_location,
    const std::vector<const ASTNode*>& arg_locations,
    bool match_internal_signatures,
    absl::Span<const std::string> function_name_path, bool is_analytic,
    std::vector<std::unique_ptr<const ResolvedExpr>> arguments,
    std::vector<NamedArgumentInfo> named_arguments,
    const Type* expected_result_type,
    std::unique_ptr<ResolvedFunctionCall>* resolved_expr_out) {
  const Function* function;
  ResolvedFunctionCallBase::ErrorMode error_mode;
  GOOGLESQL_RETURN_IF_ERROR(resolver_->LookupFunctionFromCatalog(
      ast_location, function_name_path,
      Resolver::FunctionNotFoundHandleMode::kReturnError, &function,
      &error_mode));
  return ResolveGeneralFunctionCall(
      ast_location, arg_locations, match_internal_signatures, function,
      error_mode, is_analytic, std::move(arguments), std::move(named_arguments),
      expected_result_type,
      /*name_scope=*/nullptr, resolved_expr_out);
}

absl::Status FunctionResolver::ResolveGeneralFunctionCall(
    const ASTNode* ast_location,
    const std::vector<const ASTNode*>& arg_locations,
    bool match_internal_signatures, absl::string_view function_name,
    bool is_analytic,
    std::vector<std::unique_ptr<const ResolvedExpr>> arguments,
    std::vector<NamedArgumentInfo> named_arguments,
    const Type* expected_result_type,
    std::unique_ptr<ResolvedFunctionCall>* resolved_expr_out) {
  const std::vector<std::string> function_name_path = {
      std::string(function_name)};
  return ResolveGeneralFunctionCall(
      ast_location, arg_locations, match_internal_signatures,
      function_name_path, is_analytic, std::move(arguments),
      std::move(named_arguments), expected_result_type, resolved_expr_out);
}

// Shorthand to make ResolvedFunctionArgument from ResolvedExpr
static std::unique_ptr<ResolvedFunctionArgument> MakeResolvedFunctionArgument(
    std::unique_ptr<const ResolvedExpr> expr) {
  std::unique_ptr<ResolvedFunctionArgument> function_argument =
      MakeResolvedFunctionArgument();
  function_argument->set_expr(std::move(expr));
  return function_argument;
}

absl::Status FunctionResolver::ResolveCollationForCollateFunction(
    const ASTNode* error_location, ResolvedFunctionCall* function_call) {
  GOOGLESQL_RET_CHECK_EQ(function_call->argument_list_size(), 2);
  const ResolvedExpr* arg_1 = function_call->argument_list(1);
  // Pre-resolution check has made sure the second argument is a string literal.
  GOOGLESQL_RET_CHECK(arg_1->type()->IsString() &&
            arg_1->node_kind() == RESOLVED_LITERAL &&
            !arg_1->GetAs<ResolvedLiteral>()->value().is_null());
  GOOGLESQL_RETURN_IF_ERROR(EnablePreserveInLiteralRemover(arg_1));
  const std::string& collation_name =
      arg_1->GetAs<ResolvedLiteral>()->value().string_value();
  if (!collation_name.empty()) {
    std::unique_ptr<AnnotationMap> mutable_annotation_map;
    if (function_call->type_annotation_map() == nullptr) {
      mutable_annotation_map = AnnotationMap::Create(function_call->type());
    } else {
      mutable_annotation_map = function_call->type_annotation_map()->Clone();
    }
    mutable_annotation_map->SetAnnotation<CollationAnnotation>(
        SimpleValue::String(collation_name));
    GOOGLESQL_ASSIGN_OR_RETURN(
        const AnnotationMap* annotation_map,
        type_factory_->TakeOwnership(std::move(mutable_annotation_map)));
    function_call->set_type_annotation_map(annotation_map);
  }
  return absl::OkStatus();
}

static std::vector<absl::string_view> NamedArgInfoToNameVector(
    int num_args, absl::Span<const NamedArgumentInfo> named_arguments) {
  std::vector<absl::string_view> names(num_args);
  for (const auto& arg : named_arguments) {
    names[arg.index()] = arg.name().ToStringView();
  }
  return names;
}

static bool IsBuiltinArrayZipFunctionLambdaSignatures(
    const Function* function, const FunctionSignature* signature) {
  if (!function->IsGoogleSQLBuiltin()) {
    return false;
  }
  // TODO: Add signatures as we implement ARRAY_ZIP.
  switch (signature->context_id()) {
    case FN_ARRAY_ZIP_TWO_ARRAY_LAMBDA:
    case FN_ARRAY_ZIP_THREE_ARRAY_LAMBDA:
    case FN_ARRAY_ZIP_FOUR_ARRAY_LAMBDA:
      return true;
    default:
      return false;
  }
}

static absl::Status MakeArgumentAliasSqlError(
    const ASTNode* error_location, const Function* function,
    const FunctionSignature* signature,
    const ASTFunctionCall* ast_function_call) {
  if (IsBuiltinArrayZipFunctionLambdaSignatures(function, signature)) {
    // The ARRAY_ZIP signatures with lambdas do not allow argument aliases.
    // Return a special error message explaining this.
    return MakeSqlErrorAt(error_location)
           << "ARRAY_ZIP function with lambda argument does not allow "
           << "providing argument aliases";
  }
  // Default error message.
  // TODO: Update the error message to be of the form
  // "Unexpected argument alias found for the argument `arg_name` of
  // `FunctionName`".
  return MakeSqlErrorAt(error_location)
         << "Unexpected function call argument alias found at "
         << ast_function_call->function()->ToIdentifierPathString();
}

// Validates the argument aliases of given `args` against the requirements
// enforced by `result_signature`, and stores the argument aliases in
// `input_argument_types`.
//
// `result_signature` must be concrete.
static absl::Status ResolveArgumentAliases(
    const ASTNode* ast_node, const std::vector<const ASTNode*>& args,
    const Function* function, const FunctionSignature* result_signature,
    std::vector<InputArgumentType>& input_argument_types) {
  const ASTFunctionCall* ast_function_call = nullptr;
  if (ast_node->Is<ASTFunctionCall>()) {
    ast_function_call = ast_node->GetAsOrDie<ASTFunctionCall>();
  } else if (ast_node->Is<ASTAnalyticFunctionCall>()) {
    ast_function_call =
        ast_node->GetAsOrDie<ASTAnalyticFunctionCall>()->function();
  } else {
    // Currently only (regular or window) function call arguments can have
    // aliases.
    for (const ASTNode* arg : args) {
      GOOGLESQL_RET_CHECK(!arg->Is<ASTExpressionWithAlias>());
    }
    return absl::OkStatus();
  }

  GOOGLESQL_RET_CHECK(result_signature->HasConcreteArguments());
  GOOGLESQL_RET_CHECK_EQ(result_signature->NumConcreteArguments(), args.size());

  std::vector<std::optional<IdString>> argument_aliases;
  argument_aliases.reserve(result_signature->NumConcreteArguments());
  for (int i = 0; i < result_signature->NumConcreteArguments(); ++i) {
    const FunctionArgumentType& argument =
        result_signature->ConcreteArgument(i);
    std::optional<IdString> alias = std::nullopt;
    if (argument.options().argument_alias_kind() !=
            FunctionEnums::ARGUMENT_ALIASED &&
        args[i]->Is<ASTExpressionWithAlias>()) {
      // The argument does not support aliases but an alias is provided,
      // return a SQL error.
      return MakeArgumentAliasSqlError(
          args[i]->GetAsOrDie<ASTExpressionWithAlias>()->alias(), function,
          result_signature, ast_function_call);
    }
    if (argument.options().argument_alias_kind() ==
        FunctionEnums::ARGUMENT_ALIASED) {
      // This argument needs to have an alias, either provided by the user or
      // generated by the resolver.
      if (args[i]->Is<ASTExpressionWithAlias>()) {
        alias = args[i]
                    ->GetAsOrDie<ASTExpressionWithAlias>()
                    ->alias()
                    ->GetAsIdString();
      } else {
        // It is possible that an alias cannot be inferred, i.e. the returned
        // alias is an empty string. Empty strings are still used argument
        // aliases because, for example for ARRAY_ZIP, empty aliases means the
        // result STRUCT should have anonymous fields.
        alias = GetAliasForExpression(args[i]);
      }
    }
    argument_aliases.push_back(alias);
  }

  // Store the resolved argument aliases into `input_argument_types` so that
  // SQL function callbacks can access them.
  GOOGLESQL_RET_CHECK_EQ(input_argument_types.size(), argument_aliases.size());
  for (int i = 0; i < argument_aliases.size(); ++i) {
    if (argument_aliases[i].has_value()) {
      input_argument_types[i].set_argument_alias(*argument_aliases[i]);
    }
  }
  return absl::OkStatus();
}

absl::Status FunctionResolver::CustomPropagateAnnotations(
    const ComputeResultAnnotationsCallback& callback,
    const ASTNode* ast_location, ResolvedFunctionCallBase& function_call) {
  GOOGLESQL_RET_CHECK(callback != nullptr);

  absl::StatusOr<const AnnotationMap*> annotation_map =
      callback(function_call, *type_factory_);

  if (!annotation_map.ok()) {
    // If there's already an error location, do not override it.
    if (internal::HasPayloadWithType<InternalErrorLocation>(
            annotation_map.status()) ||
        internal::HasPayloadWithType<ErrorLocation>(annotation_map.status())) {
      return annotation_map.status();
    }

    // Attach the location of the current function call.
    absl::Status status = std::move(annotation_map).status();
    internal::AttachPayload(
        &status, ast_location->start_location().ToInternalErrorLocation());
    return status;
  }

  function_call.set_type_annotation_map(*annotation_map);
  return absl::OkStatus();
}

static bool IsSqlFunction(const Function* function) {
  return function->Is<SQLFunctionInterface>() ||
         function->Is<TemplatedSQLFunction>();
}

// Returns a new error message reporting a failure parsing or analyzing the
// SQL body. If 'message' is not empty, appends it to the end of the error
// string.
static absl::Status MakeFunctionExprAnalysisError(const Function& function,
                                                  absl::string_view message) {
  std::string result =
      absl::StrCat("Analysis of function ", function.FullName(), " failed");
  if (!message.empty()) {
    absl::StrAppend(&result, ":\n", message);
  }
  return MakeSqlError() << result;
}

absl::Status FunctionResolver::ResolveGeneralFunctionCall(
    const ASTNode* ast_location,
    const std::vector<const ASTNode*>& arg_locations_in,
    bool match_internal_signatures, const Function* function,
    ResolvedFunctionCallBase::ErrorMode error_mode, bool is_analytic,
    std::vector<std::unique_ptr<const ResolvedExpr>> arguments,
    std::vector<NamedArgumentInfo> named_arguments,
    const Type* expected_result_type, const NameScope* name_scope,
    std::unique_ptr<ResolvedFunctionCall>* resolved_expr_out) {
  GOOGLESQL_RETURN_IF_NOT_ENOUGH_STACK(
      "Out of stack space due to deeply nested query expression "
      "during function resolution");

  std::vector<const ASTNode*> arg_locations = arg_locations_in;
  GOOGLESQL_RET_CHECK(ast_location != nullptr);
  GOOGLESQL_RET_CHECK_EQ(arg_locations.size(), arguments.size());

  const auto* opt_ast_function_call =
      ast_location->GetAsOrNull<ASTFunctionCall>();

  // For binary operators, point the error message at the operator in the
  // middle rather that at the start of the leftmost argument.
  // This works for some operators but not others, depending on the
  // construction rules in the parser.
  //
  // TODO Point error location for 'abc=def' at the '='.
  // This appears to never have worked for binary operators.
  // We don't even have a location for the operator in ASTBinaryExpression.
  // We would always want to use the operator location if we can.
  //
  // This function is now updated to always request the inner location.
  // This works for chained function calls, pointing at the function name
  // rather than the base argument.
  const bool include_leftmost_child = false;

  if (is_analytic && !function->SupportsOverClause()) {
    return MakeSqlErrorAtLocalNode(ast_location)
           << function->QualifiedSQLName(/*capitalize_qualifier=*/true)
           << " does not support an OVER clause";
  }

  // We do not determined the actual signature and its argument types yet, so
  // leaving NULL arguments as untyped.
  GOOGLESQL_ASSIGN_OR_RETURN(std::vector<InputArgumentType> input_argument_types,
                   GetInputArgumentTypesForGenericArgumentList(
                       arg_locations, arguments,
                       /*pick_default_type_for_untyped_expr=*/false,
                       resolver_->analyzer_options()));
  GOOGLESQL_RET_CHECK_EQ(arg_locations.size(), input_argument_types.size());

  // When resolving $make_array with an expected type, convert any untyped NULL
  // arguments to typed NULLs. This prevents unexpected behavior in cases like
  // SELECT ARRAY<TIMESTAMP>[NULL], which would otherwise default to
  // ARRAY<INT64>.
  if (expected_result_type != nullptr && expected_result_type->IsArray() &&
      function->IsGoogleSQLBuiltin(FN_MAKE_ARRAY)) {
    const Type* element_type = expected_result_type->AsArray()->element_type();
    for (int i = 0; i < input_argument_types.size(); ++i) {
      if (input_argument_types[i].is_untyped_null()) {
        input_argument_types[i] = InputArgumentType(Value::Null(element_type));
      }
    }
  }

  // If this is a chained function call, mark the first InputArgumentType
  // with `is_chained_function_call_input`.  This doesn't affect any behavior
  // but is used to improve error messages.
  if (opt_ast_function_call != nullptr &&
      opt_ast_function_call->is_chained_call() &&
      !input_argument_types.empty()) {
    input_argument_types.front().set_is_chained_function_call_input();
  }

  // Check initial argument constraints, if any.
  if (function->PreResolutionConstraints() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(StatusWithInternalErrorLocation(
        function->CheckPreResolutionArgumentConstraints(input_argument_types,
                                                        resolver_->language()),
        ast_location, include_leftmost_child));
  }

  std::unique_ptr<const FunctionSignature> result_signature;
  std::vector<FunctionArgumentOverride> arg_overrides;
  std::vector<ArgIndexEntry> arg_reorder_index_mapping;
  absl::flat_hash_map<const ResolvedInlineLambda*, const ASTLambda*>
      lambda_ast_nodes;
  // When function has SupportedSignaturesCallback which returns list of
  // signatures in one string, we cannot interleave signatures and mismatch
  // errors.
  // When function has SignatureTextCallback, we have opportunity for per
  // signature mismatch error.
  // To simplify code, we always collect the mismatch errors, but may ignore
  // them if we use other callbacks.
  auto mismatch_errors = std::make_unique<std::vector<std::string>>();
  GOOGLESQL_ASSIGN_OR_RETURN(
      const FunctionSignature* signature,
      FindMatchingSignature(function, ast_location, arg_locations,
                            match_internal_signatures, named_arguments,
                            name_scope, &input_argument_types, &arg_overrides,
                            &arg_reorder_index_mapping, &lambda_ast_nodes,
                            mismatch_errors.get()));
  result_signature.reset(signature);

  if (nullptr == result_signature) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::string error_message,
        GenerateErrorMessageWithSupportedSignatures(
            function,
            function->GetNoMatchingFunctionSignatureErrorMessage(
                input_argument_types, resolver_->language().product_mode(),
                NamedArgInfoToNameVector(
                    static_cast<int>(input_argument_types.size()),
                    named_arguments),
                /*argument_types_on_new_line=*/true),
            named_arguments.empty()
                ? FunctionArgumentType::NamePrintingStyle::kIfNamedOnly
                : FunctionArgumentType::NamePrintingStyle::kIfNotPositionalOnly,
            mismatch_errors.get()));
    return MakeSqlErrorAtNode(ast_location, include_leftmost_child)
           << error_message;
  }

  GOOGLESQL_RET_CHECK(result_signature->HasConcreteArguments());

  SignatureArgumentKind original_result_kind =
      result_signature->result_type().original_kind();
  GOOGLESQL_RET_CHECK_NE(original_result_kind,
               __SignatureArgumentKind__switch_must_have_a_default__);

  // If the `function` is a `TemplatedSqlFunction` or has a callback to compute
  // the result type, it is ok to not have a concrete return type for now.
  if (!function->Is<TemplatedSQLFunction>() &&
      function->GetComputeResultTypeCallback() == nullptr) {
    if (!result_signature->IsConcrete()) {
      return ::googlesql_base::InternalErrorBuilder()
             << "Non-concrete result signature for non-templated function: "
             << function->SQLName() << " " << signature->DebugString();
    }
  }

  // If we found a matching signature, go back and resolve any null typed,
  // non-lambda arguments as additional argument overrides.
  for (int i = 0; i < input_argument_types.size(); ++i) {
    const InputArgumentType& input_arg_type = input_argument_types[i];
    if (input_arg_type.type() == nullptr && !input_arg_type.is_lambda()) {
      if (input_arg_type.is_sequence()) {
        const ASTSequenceArg* sequence_arg =
            arg_locations[i]->GetAs<ASTSequenceArg>();
        std::unique_ptr<const ResolvedSequence> resolved_sequence;
        GOOGLESQL_RETURN_IF_ERROR(resolver_->ResolveSequence(
            sequence_arg->sequence_path(), &resolved_sequence));
        std::unique_ptr<ResolvedFunctionArgument> arg =
            googlesql::MakeResolvedFunctionArgument();
        arg->set_sequence(std::move(resolved_sequence));
        arg_overrides.push_back(FunctionArgumentOverride{i, std::move(arg)});
      }
    }
  }

  GOOGLESQL_RETURN_IF_ERROR(ReorderArgumentExpressionsPerIndexMapping(
      function->SQLName(), *result_signature, arg_reorder_index_mapping,
      ast_location, input_argument_types, &arg_locations, &arguments,
      /*resolved_tvf_args=*/nullptr));

  const auto BadArgErrorPrefix = [&result_signature, &named_arguments,
                                  function](int idx) {
    if (function->GetBadArgumentErrorPrefixCallback() != nullptr) {
      return function->GetBadArgumentErrorPrefixCallback()(*result_signature,
                                                           idx);
    }
    const FunctionArgumentType& argument =
        result_signature->ConcreteArgument(idx);
    if (argument.has_argument_name()) {
      // Check whether function call was using named argument or positional
      // argument, and if it was named - use the name in the error message.
      for (const auto& named_arg : named_arguments) {
        if (googlesql_base::CaseEqual(named_arg.name().ToString(),
                                   argument.argument_name())) {
          return absl::StrCat("Argument '", argument.argument_name(), "' to ",
                              function->SQLName());
        }
      }
    }
    if (result_signature->NumConcreteArguments() == 1) {
      return absl::StrCat("The argument to ", function->SQLName());
    } else {
      return absl::StrCat("Argument ", idx + 1, " to ", function->SQLName());
    }
  };
  const ProductMode product_mode = resolver_->language().product_mode();

  for (int idx = 0; idx < arguments.size(); ++idx) {
    // The GOOGLESQL_RET_CHECK above ensures that the arguments are concrete for both
    // templated and non-templated functions.
    const FunctionArgumentType& concrete_argument =
        result_signature->ConcreteArgument(idx);
    if (concrete_argument.IsLambda()) {
      GOOGLESQL_RET_CHECK(arguments[idx] == nullptr);

      // There might be multiple lambda args in a given function signature.
      // Each FunctionArgumentOverride has a unique index in the concrete
      // signature.
      auto arg_override = std::find_if(
          arg_overrides.begin(), arg_overrides.end(),
          [&](const FunctionArgumentOverride& o) { return o.index == idx; });
      GOOGLESQL_RET_CHECK(arg_override != arg_overrides.end())
          << "No arg override found for lambda argument: "
          << arguments[idx]->DebugString();

      const ResolvedInlineLambda* lambda =
          arg_override->argument->inline_lambda();
      const FunctionArgumentType::ArgumentTypeLambda& concrete_lambda =
          concrete_argument.lambda();
      GOOGLESQL_RET_CHECK_EQ(lambda->argument_list().size(),
                   concrete_lambda.argument_types().size());
      // Check that lambda argument matches concrete argument. This shouldn't
      // trigger under known use cases. But still doing the check to be safe.
      // An example is fn_fp_T_LAMBDA_T(1, e->e>0, 1.0) for signature
      // fn_fp_T_LAMBDA_T(T1, T1->BOOL, T1). e is infered to be INT64 from
      // argument 1, but T1 is later decided to be DOUBLE.
      for (int i = 0; i < lambda->argument_list_size(); i++) {
        GOOGLESQL_RET_CHECK(lambda->argument_list(i).type()->Equals(
            concrete_lambda.argument_types()[i].type()))
            << "Failed to infer the type of lambda argument at index " << i
            << ". It is inferred from arguments preceding the lambda to "
               "be "
            << lambda->argument_list(i).type()->ShortTypeName(product_mode)
            << " but found to be "
            << concrete_lambda.argument_types()[i].type()->ShortTypeName(
                   product_mode)
            << " after considering arguments following the lambda";
      }
      // Check that lambda body type matches concrete argument.
      if (!lambda->body()->type()->Equals(concrete_lambda.body_type().type())) {
        return MakeSqlErrorAt(arg_locations[idx])
               << "Lambda body is resolved to have type "
               << lambda->body()->type()->ShortTypeName(product_mode)
               << " but the signature requires it to be "
               << concrete_lambda.body_type().type()->ShortTypeName(
                      product_mode);
      }
      continue;
    } else if (concrete_argument.IsSequence()) {
      // Same as with lambdas, sequences are resolved into arg_overrides.
      GOOGLESQL_RET_CHECK(arguments[idx] == nullptr);
      auto arg_override = std::find_if(
          arg_overrides.begin(), arg_overrides.end(),
          [&](const FunctionArgumentOverride& o) { return o.index == idx; });
      GOOGLESQL_RET_CHECK(arg_override != arg_overrides.end())
          << "No arg override found for sequence argument";
      GOOGLESQL_RET_CHECK(arg_override->argument->sequence() != nullptr);
      continue;
    }
    ABSL_DCHECK(arguments[idx] != nullptr);
    GOOGLESQL_RETURN_IF_ERROR(CheckArgumentConstraints(
        ast_location, function->SQLName(), /*is_tvf=*/false, arg_locations[idx],
        idx, concrete_argument, arguments[idx].get(), BadArgErrorPrefix));

    const Type* target_type = concrete_argument.type();
    if (!(arguments[idx])->type()->Equals(target_type)) {
      // We keep the original <type_annotation_map> when coercing function
      // arguments. These <type_annotation_map> of arguments will be processed
      // to detemine the <type_annotation_map> of function call in a later
      // stage.
      GOOGLESQL_RETURN_IF_ERROR(resolver_->CoerceExprToType(
          arg_locations[idx],
          AnnotatedType(target_type, arguments[idx]->type_annotation_map()),
          Resolver::kExplicitCoercion, &arguments[idx]));
      // Update the argument type with the casted one, so that the
      // PostResolutionArgumentConstraintsCallback and the
      // ComputeResultTypeCallback can get the exact types passed to function.
      GOOGLESQL_ASSIGN_OR_RETURN(
          input_argument_types[idx],
          GetInputArgumentTypeForExpr(
              arguments[idx].get(),
              /*pick_default_type_for_untyped_expr=*/
              function->Is<TemplatedSQLFunction>() &&
                  resolver_->language().LanguageFeatureEnabled(
                      FEATURE_TEMPLATED_SQL_FUNCTION_RESOLVE_WITH_TYPED_ARGS),
              resolver_->analyzer_options()));
    }

    // If we have a literal argument value, check it against the value
    // constraints for that argument.
    if (arguments[idx]->node_kind() == RESOLVED_LITERAL) {
      const Value& value = arguments[idx]->GetAs<ResolvedLiteral>()->value();
      GOOGLESQL_RETURN_IF_ERROR(CheckArgumentValueConstraints(arg_locations[idx], idx,
                                                    value, concrete_argument,
                                                    BadArgErrorPrefix));
    }
  }

  GOOGLESQL_RETURN_IF_ERROR(ResolveArgumentAliases(ast_location, arg_locations, function,
                                         result_signature.get(),
                                         input_argument_types));

  if (function->PostResolutionConstraints() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(StatusWithInternalErrorLocation(
        function->CheckPostResolutionArgumentConstraints(
            *result_signature, input_argument_types, resolver_->language()),
        ast_location, include_leftmost_child));
  }

  if (function->GetComputeResultTypeCallback() != nullptr) {
    // Note that the result type of SQL functions cannot be overridden, since
    // the result type is determined by the type of the resolved SQL expression.
    GOOGLESQL_RET_CHECK(!function->Is<TemplatedSQLFunction>()) << function->DebugString();
    GOOGLESQL_RET_CHECK(!function->Is<SQLFunctionInterface>()) << function->DebugString();
    CycleDetector owned_cycle_detector;
    const absl::StatusOr<const Type*> result_type =
        function->GetComputeResultTypeCallback()(
            catalog_, type_factory_, &owned_cycle_detector, *result_signature,
            input_argument_types, resolver_->analyzer_options());
    GOOGLESQL_RETURN_IF_ERROR(StatusWithInternalErrorLocation(
        result_type.status(), ast_location, include_leftmost_child));
    GOOGLESQL_RET_CHECK(result_type.value() != nullptr);

    std::unique_ptr<FunctionSignature> new_signature =
        std::make_unique<FunctionSignature>(*result_signature);
    new_signature->SetConcreteResultType(result_type.value(),
                                         original_result_kind);

    result_signature = std::move(new_signature);
    GOOGLESQL_RET_CHECK(result_signature->IsConcrete())
        << "result_signature: '" << result_signature->DebugString() << "'";
  }

  if (IsSqlFunction(function)) {
    const std::vector<std::string>& arg_names =
        function->Is<SQLFunctionInterface>()
            ? function->GetAs<SQLFunctionInterface>()->GetArgumentNames()
            : function->GetAs<TemplatedSQLFunction>()->GetArgumentNames();
    for (int i = 0; i < input_argument_types.size(); ++i) {
      if (arguments[i]->type_annotation_map() != nullptr &&
          !arguments[i]->type_annotation_map()->Empty()) {
        std::unique_ptr<AnnotationMap> annotations_to_block =
            arguments[i]->type_annotation_map()->Clone();
        // Allow collation if the flag is on. If the flag is off, block all
        // annotations.
        if (resolver_->language().LanguageFeatureEnabled(
                FEATURE_TYPE_ANNOTATIONS_ON_SQL_FUNCTION_ARGUMENTS)) {
          annotations_to_block
              ->UnsetAnnotationRecursively<CollationAnnotation>();
        }

        if (!annotations_to_block->Empty()) {
          return MakeSqlErrorAt(arg_locations[i])
                 << "Annotations on arguments of SQL functions are not "
                    "supported, but found annotations on argument "
                 << arg_names[i];
        }
      }
    }
  }

  std::shared_ptr<ResolvedFunctionCallInfo> function_call_info(
      new ResolvedFunctionCallInfo);
  if (function->Is<TemplatedSQLFunction>()) {
    const TemplatedSQLFunction* sql_function =
        function->GetAs<TemplatedSQLFunction>();
    // Call the TemplatedSQLFunction::Resolve() method to get the output type.
    // Use a new empty cycle detector, or the cycle detector from an enclosing
    // Resolver if we are analyzing one or more templated function calls.
    CycleDetector owned_cycle_detector;
    AnalyzerOptions analyzer_options = resolver_->analyzer_options_;
    if (analyzer_options.find_options().cycle_detector() == nullptr) {
      analyzer_options.mutable_find_options()->set_cycle_detector(
          &owned_cycle_detector);
    }
    const absl::Status resolve_status = ResolveTemplatedSQLFunctionCall(
        ast_location, *sql_function, analyzer_options, input_argument_types,
        arguments, *result_signature, &function_call_info);

    if (!resolve_status.ok()) {
      // The Resolve method returned an error status that reflects the
      // <analyzer_options> ErrorMessageMode.  We want to return an error
      // status here that indicates that the function is invalid along with
      // the function call location, so create a new error status with an
      // ErrorSource based on the <resolve_status>.
      return WrapNestedErrorStatus(
          ast_location, absl::StrCat("Invalid function ", sql_function->Name()),
          resolve_status, analyzer_options.error_message_mode());
    }
    std::unique_ptr<FunctionSignature> new_signature =
        std::make_unique<FunctionSignature>(*result_signature);
    new_signature->SetConcreteResultType(
        function_call_info->GetAs<TemplatedSQLFunctionCall>()->expr()->type(),
        original_result_kind);
    result_signature = std::move(new_signature);
    GOOGLESQL_RET_CHECK(result_signature->IsConcrete())
        << "result_signature: '" << result_signature->DebugString() << "'";
  }

  GOOGLESQL_RET_CHECK(!result_signature->result_type().IsVoid());

  // We transform the concatenation operator (||) function into the
  // corresponding CONCAT or ARRAY_CONCAT function call here. Engines never
  // see the concatenation operator function in the ResolvedAST, and only
  // see the canonical CONCAT/ARRAY_CONCAT function call.
  if (function->Name() == "$concat_op") {
    std::vector<std::string> function_name_path;
    arg_reorder_index_mapping.clear();

    if (result_signature->result_type().type()->IsArray()) {
      function_name_path.push_back("array_concat");
    } else if (result_signature->result_type().type()->IsGraphPath()) {
      function_name_path.push_back("$path_concat");
    } else {
      function_name_path.push_back("concat");
    }
    const Function* concat_op_function;
    ResolvedFunctionCallBase::ErrorMode concat_op_error_mode;
    GOOGLESQL_RETURN_IF_ERROR(resolver_->LookupFunctionFromCatalog(
        ast_location, function_name_path,
        Resolver::FunctionNotFoundHandleMode::kReturnError, &concat_op_function,
        &concat_op_error_mode));
    absl::flat_hash_map<const ResolvedInlineLambda*, const ASTLambda*>
        lambda_ast_nodes;
    GOOGLESQL_ASSIGN_OR_RETURN(const FunctionSignature* matched_signature,
                     FindMatchingSignature(
                         concat_op_function, ast_location, arg_locations_in,
                         match_internal_signatures, named_arguments,
                         /*name_scope=*/nullptr, &input_argument_types,
                         /*arg_overrides=*/nullptr, &arg_reorder_index_mapping,
                         &lambda_ast_nodes, mismatch_errors.get()));
    GOOGLESQL_RET_CHECK_NE(matched_signature, nullptr);
    std::unique_ptr<const FunctionSignature> concat_op_result_signature(
        matched_signature);
    *resolved_expr_out = MakeResolvedFunctionCall(
        concat_op_result_signature->result_type().type(), concat_op_function,
        *concat_op_result_signature, std::move(arguments),
        /*generic_argument_list=*/{}, concat_op_error_mode, function_call_info);
  } else if (!arg_overrides.empty()) {
    // As a precautionary measure, sort in case arg overrides were inserted out
    // of order.
    absl::c_sort(arg_overrides, [](const FunctionArgumentOverride& lhs,
                                   const FunctionArgumentOverride& rhs) {
      return lhs.index < rhs.index;
    });
    // Replace the nullptr placeholders with resolved lambdas in
    // <arg_overrides>. We have lambdas so need to use <generic_argument_list>
    // instead of <argument_list>.
    std::vector<std::unique_ptr<const ResolvedFunctionArgument>>
        generic_argument_list;
    generic_argument_list.reserve(arguments.size());
    int arg_override_index = 0;
    // Merge <arguments> and <arg_overrides> into a list of
    // ResolvedFunctionArguments.
    for (int arg_index = 0; arg_index < arguments.size(); arg_index++) {
      if (arguments[arg_index] != nullptr) {
        // Wrap ResolvedExpr as ResolvedFunctionArgument
        generic_argument_list.push_back(
            MakeResolvedFunctionArgument(std::move(arguments[arg_index])));
      } else {
        GOOGLESQL_RET_CHECK(arg_overrides.size() > arg_override_index);
        GOOGLESQL_RET_CHECK_EQ(arg_overrides[arg_override_index].index, arg_index);
        generic_argument_list.push_back(
            std::move(arg_overrides[arg_override_index].argument));
        arg_override_index++;
      }
    }
    arguments.clear();
    *resolved_expr_out = MakeResolvedFunctionCall(
        result_signature->result_type().type(), function, *result_signature,
        /*argument_list=*/{}, std::move(generic_argument_list), error_mode,
        function_call_info);
  } else if (function->IsScalar() &&
             SignatureSupportsArgumentAliases(*result_signature)) {
    // Use `generic_argument_list` to store argument aliases if a scalar
    // function has argument aliases.
    //
    // Aggregate and window functions can also have argument aliases, but
    // because currently they do not support `generic_argument_list` we are not
    // able to store the argument aliases in the resolved ast.
    std::vector<std::unique_ptr<const ResolvedFunctionArgument>>
        generic_argument_list;
    generic_argument_list.reserve(arguments.size());
    GOOGLESQL_RET_CHECK_EQ(arguments.size(), input_argument_types.size());
    for (int i = 0; i < arguments.size(); i++) {
      std::unique_ptr<ResolvedFunctionArgument> generic_argument =
          MakeResolvedFunctionArgument(std::move(arguments[i]));
      const std::optional<IdString>& argument_alias =
          input_argument_types[i].argument_alias();
      generic_argument->set_argument_alias(
          argument_alias.has_value() ? argument_alias->ToString() : "");
      generic_argument_list.push_back(std::move(generic_argument));
    }
    arguments.clear();
    *resolved_expr_out = MakeResolvedFunctionCall(
        result_signature->result_type().type(), function, *result_signature,
        /*argument_list=*/{}, std::move(generic_argument_list), error_mode,
        function_call_info);
  } else {
    // If there is no lambda argument, we specify <argument_list> so that
    // non-lambda functions stay compatible with existing engine
    // implementations.
    *resolved_expr_out = MakeResolvedFunctionCall(
        result_signature->result_type().type(), function, *result_signature,
        std::move(arguments), /*generic_argument_list=*/{}, error_mode,
        function_call_info);
  }
  GOOGLESQL_RETURN_IF_ERROR(resolver_->MaybeResolveCollationForFunctionCallBase(
      /*error_location=*/ast_location, (*resolved_expr_out).get()));

  // Perform the required re-resolutions for annotations.
  if (!resolved_expr_out->get()->generic_argument_list().empty()) {
    GOOGLESQL_RETURN_IF_ERROR(ReResolveLambdasIfNecessary(result_signature.get(),
                                                name_scope, lambda_ast_nodes,
                                                resolved_expr_out->get()));
  }

  if (!IsSqlFunction(function) &&
      result_signature->options().rejects_collation()) {
    absl::Status collation_check_status =
        CollationAnnotation::RejectsCollationOnFunctionArguments(
            **resolved_expr_out);
    if (!collation_check_status.ok()) {
      internal::AttachPayload(
          &collation_check_status,
          ast_location->start_location().ToInternalErrorLocation());
      return collation_check_status;
    }
  }

  if (const ComputeResultAnnotationsCallback& annotation_callback =
          result_signature->GetComputeResultAnnotationsCallback();
      annotation_callback != nullptr) {
    // This function signature has a custom callback to compute the result
    // annotations.
    GOOGLESQL_RET_CHECK(!IsSqlFunction(function));
    GOOGLESQL_RETURN_IF_ERROR(CustomPropagateAnnotations(
        annotation_callback, ast_location, *resolved_expr_out->get()));
  } else {
    // No custom annotation callbacks for this SQL function signature, use the
    // default annotation propagation logic.
    GOOGLESQL_RETURN_IF_ERROR(resolver_->CheckAndPropagateAnnotations(
        /*error_node=*/ast_location, (*resolved_expr_out).get()));
  }

  if ((*resolved_expr_out)->function()->GetGroup() ==
          Function::kGoogleSQLFunctionGroupName &&
      (*resolved_expr_out)->signature().context_id() ==
          FunctionSignatureId::FN_COLLATE) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveCollationForCollateFunction(
        /*error_location=*/ast_location, (*resolved_expr_out).get()));
  }

  if (opt_ast_function_call != nullptr) {
    resolver_->MaybeRecordFunctionCallParseLocation(opt_ast_function_call,
                                                    resolved_expr_out->get());
  } else if (resolver_->analyzer_options().parse_location_record_type() ==
             PARSE_LOCATION_RECORD_FULL_NODE_SCOPE) {
    resolver_->MaybeRecordParseLocation(ast_location, resolved_expr_out->get());
  }

  GOOGLESQL_RET_CHECK_NE(
      resolved_expr_out->get()->signature().result_type().original_kind(),
      __SignatureArgumentKind__switch_must_have_a_default__);

  return absl::OkStatus();
}

// This is a helper method when parsing or analyzing the function's SQL
// expression.  If 'status' is OK, also returns OK. Otherwise, returns a
// new error forwarding any nested errors in 'status' obtained from the
// nested parsing or analysis.
absl::Status FunctionResolver::ForwardNestedResolutionAnalysisError(
    const TemplatedSQLFunction& function, const absl::Status& status,
    ErrorMessageOptions options) {
  if (status.ok() || ShouldPropagateError(status)) {
    return status;
  }
  ParseResumeLocation parse_resume_location = function.GetParseResumeLocation();
  absl::Status new_status;
  if (HasErrorLocation(status)) {
    new_status = MakeFunctionExprAnalysisError(function, "");
    googlesql::internal::AttachPayload(
        &new_status, SetErrorSourcesFromStatus(
                         googlesql::internal::GetPayload<ErrorLocation>(status),
                         status, options.mode, parse_resume_location.input()));
  } else {
    new_status = StatusWithInternalErrorLocation(
        MakeFunctionExprAnalysisError(function, ""),
        ParseLocationPoint::FromByteOffset(
            parse_resume_location.filename(),
            parse_resume_location.byte_position()));
    googlesql::internal::AttachPayload(
        &new_status,
        SetErrorSourcesFromStatus(
            googlesql::internal::GetPayload<InternalErrorLocation>(new_status),
            status, options.mode, parse_resume_location.input()));
  }

  // Update the <new_status> based on <mode>.
  return MaybeUpdateErrorFromPayload(
      options, parse_resume_location.input(),
      ConvertInternalErrorPayloadsToExternal(new_status,
                                             parse_resume_location.input()));
}

absl::Status FunctionResolver::ResolveTemplatedSQLFunctionCall(
    const ASTNode* ast_location, const TemplatedSQLFunction& function,
    const AnalyzerOptions& analyzer_options,
    absl::Span<const InputArgumentType> actual_arguments,
    absl::Span<const std::unique_ptr<const ResolvedExpr>> argument_list,
    const FunctionSignature& concrete_signature,
    std::shared_ptr<ResolvedFunctionCallInfo>* function_call_info_out) {
  // Check if this function calls itself. If so, return an error. Otherwise, add
  // a pointer to this class to the cycle detector in the analyzer options.
  CycleDetector::ObjectInfo object(
      function.FullName(), &function,
      analyzer_options.find_options().cycle_detector());
  // TODO: Attach proper error locations to the returned Status.
  GOOGLESQL_RETURN_IF_ERROR(object.DetectCycle("function"));

  // Build a map for the function arguments.
  IdStringHashMapCase<std::unique_ptr<ResolvedArgumentRef>> function_arguments;
  GOOGLESQL_RET_CHECK_EQ(function.GetArgumentNames().size(), actual_arguments.size());
  // Templated SQL functions only support one signature for now.
  GOOGLESQL_RET_CHECK_EQ(1, function.NumSignatures());
  GOOGLESQL_RET_CHECK_GE(function.signatures()[0].arguments().size(),
               actual_arguments.size());
  GOOGLESQL_RET_CHECK(concrete_signature.HasConcreteArguments());
  for (int i = 0; i < actual_arguments.size(); ++i) {
    GOOGLESQL_RET_CHECK_EQ(concrete_signature.argument(i).num_occurrences(), 1);
    SignatureArgumentKind argument_original_kind =
        concrete_signature.argument(i).original_kind();
    GOOGLESQL_RET_CHECK_NE(argument_original_kind,
                 __SignatureArgumentKind__switch_must_have_a_default__);
    const IdString arg_name =
        analyzer_options.id_string_pool()->Make(function.GetArgumentNames()[i]);
    const InputArgumentType& arg_type = actual_arguments[i];
    if (function_arguments.contains(arg_name)) {
      // TODO: Attach proper error locations to the returned Status.
      return MakeFunctionExprAnalysisError(
          function,
          absl::StrCat("Duplicate argument name ", arg_name.ToString()));
    }
    // Figure out the argument kind to use depending on whether this is a scalar
    // or aggregate function and whether the corresponding argument in the
    // function signature is marked "NOT AGGREGATE".
    ResolvedArgumentDefEnums::ArgumentKind arg_kind;
    if (function.IsAggregate()) {
      if (concrete_signature.argument(i).options().is_not_aggregate()) {
        arg_kind = ResolvedArgumentDefEnums::NOT_AGGREGATE;
      } else {
        arg_kind = ResolvedArgumentDefEnums::AGGREGATE;
      }
    } else {
      arg_kind = ResolvedArgumentDefEnums::SCALAR;
    }
    auto arg_ref =
        MakeResolvedArgumentRef(arg_type.type(), arg_name.ToString(), arg_kind);

    if (argument_list[i]->type_annotation_map() != nullptr &&
        !argument_list[i]->type_annotation_map()->Empty()) {
      std::unique_ptr<AnnotationMap> annotations_to_block =
          argument_list[i]->type_annotation_map()->Clone();
      // Allow collation if the flag is on. If the flag is off, block all
      // annotations.
      if (resolver_->language().LanguageFeatureEnabled(
              FEATURE_TYPE_ANNOTATIONS_ON_SQL_FUNCTION_ARGUMENTS)) {
        annotations_to_block->UnsetAnnotationRecursively<CollationAnnotation>();
      }

      if (!annotations_to_block->Empty()) {
        return MakeFunctionExprAnalysisError(
            function,
            absl::StrCat("Annotations on arguments of SQL functions are not "
                         "supported, but found annotations on argument ",
                         arg_name.ToStringView()));
      }
    }
    // Annotations are only propagated for templated arguments.
    if (argument_original_kind != ARG_TYPE_FIXED) {
      arg_ref->set_type_annotation_map(argument_list[i]->type_annotation_map());
    }
    function_arguments[arg_name] = std::move(arg_ref);
  }

  std::unique_ptr<ParserOutput> parser_output_storage;
  const ASTExpression* expression = nullptr;
  // If parsed AST is not available create a separate new parser and parse the
  // function's SQL expression from the <parse_resume_location_>. Use the same
  // ID string pool as the original parser.
  if (expression == nullptr) {
    // ParseExpression didn't used to take error_message_options as a parameter
    // and just picked some defaults. Those defaults worked well here. Copying
    // the error message options from the analyzer results in weird error
    // messages, so we're making a copy and setting it up like the old defaults
    // from ParseExpression.
    ParserOptions function_body_parser_options =
        analyzer_options.GetParserOptions();
    ErrorMessageOptions& error_opts =
        function_body_parser_options.mutable_error_message_options();
    error_opts.attach_error_location_payload = true;
    error_opts.mode = ERROR_MESSAGE_WITH_PAYLOAD;
    GOOGLESQL_RETURN_IF_ERROR(ForwardNestedResolutionAnalysisError(
        function,
        ParseExpression(function.GetParseResumeLocation(),
                        function_body_parser_options, &parser_output_storage),
        analyzer_options.error_message_options()));
    expression = parser_output_storage->expression();
  }
  Catalog* catalog = catalog_;
  if (function.resolution_catalog() != nullptr) {
    catalog = function.resolution_catalog();
  }

  // Create a separate new resolver and resolve the function's SQL expression,
  // using the specified function arguments.
  // If resolution_catalog_ is set, it is used instead of the catalog passed
  // as the function argument.
  // Otherwise, the catalog passed as the argument is used, and it may include
  // names that were not previously available when the function was initially
  // declared.
  // If the analyzer option is set, suspend lookup expression callbacks when
  // resolving template functions. Name resolution via lookup expression
  // callback takes precedence over name resolution against function arguments.
  // This can result in incorrect resolution behavior, where a name 'user'
  // resolves to an expression generated via a callback, when in fact 'user' is
  // present in the argument list of the CREATE FUNCTION statement.
  AnalyzerOptions options_for_template_analysis = analyzer_options;
  if (analyzer_options
          .GetSuspendLookupExpressionCallbackWhenResolvingTemplatedFunction()) {
    InternalAnalyzerOptions::SetLookupExpressionCallback(
        options_for_template_analysis, nullptr);
  }
  auto resolver = std::make_unique<Resolver>(catalog, type_factory_,
                                             &options_for_template_analysis);

  NameScope empty_name_scope;
  auto query_resolution_info =
      std::make_unique<QueryResolutionInfo>(resolver.get());
  auto expr_resolution_info = std::make_unique<ExprResolutionInfo>(
      query_resolution_info.get(), &empty_name_scope,
      ExprResolutionInfoOptions{
          .allows_aggregation = function.IsAggregate(),
          .allows_analytic = false,
          .clause_name = function.IsAggregate()
                             ? "templated SQL aggregate function call"
                             : "templated SQL function call"});

  std::unique_ptr<const ResolvedExpr> resolved_sql_body;
  GOOGLESQL_RETURN_IF_ERROR(ForwardNestedResolutionAnalysisError(
      function,
      resolver->ResolveExprWithFunctionArguments(
          function.GetParseResumeLocation().input(), expression,
          function_arguments, expr_resolution_info.get(), &resolved_sql_body),
      options_for_template_analysis.error_message_options()));

  if (function.IsAggregate()) {
    const absl::Status status =
        FunctionResolver::CheckCreateAggregateFunctionProperties(
            *resolved_sql_body, /*sql_function_body_location=*/nullptr,
            expr_resolution_info.get(), query_resolution_info.get(),
            resolver->language());
    if (!status.ok()) {
      if (ShouldPropagateError(status)) {
        return status;
      }
      return ForwardNestedResolutionAnalysisError(
          function, MakeFunctionExprAnalysisError(function, status.message()),
          options_for_template_analysis.error_message_options());
    }
  }

  if (resolved_sql_body->type_annotation_map() != nullptr &&
      !resolved_sql_body->type_annotation_map()->Empty()) {
    if (!resolver->language().LanguageFeatureEnabled(
            FEATURE_TYPE_ANNOTATIONS_ON_SQL_FUNCTION_ARGUMENTS)) {
      // The flag is off, so not even collation is allowed.
      GOOGLESQL_RETURN_IF_ERROR(resolver_->ThrowErrorIfExprHasCollation(
          /*error_node=*/nullptr,
          "Collation $0 in return type of user-defined function body is not "
          "allowed",
          resolved_sql_body.get()));
    }

    std::unique_ptr<AnnotationMap> annotations_to_block =
        resolved_sql_body->type_annotation_map()->Clone();
    annotations_to_block->UnsetAnnotationRecursively<CollationAnnotation>();

    // The flag is on, so we allow collation on arguments, but not other
    // annotations. Create a copy without annotations and produce an error if
    // it's not empty.
    if (!annotations_to_block->Empty()) {
      return MakeFunctionExprAnalysisError(
          function,
          "Annotations in return type of user-defined function body is not "
          "supported");
    }
  }

  // Check the result type of the resolved expression against the expected
  // concrete return type of the function signature, if any. If the types do not
  // match, add a coercion or return an error.

  // Note that GoogleSQL does not yet support overloaded templated functions.
  // So we check that there is exactly one signature and retrieve it.
  GOOGLESQL_RET_CHECK_EQ(1, function.NumSignatures());
  const FunctionArgumentType& expected_type =
      function.signatures()[0].result_type();
  if (expected_type.kind() == ARG_TYPE_FIXED) {
    if (absl::Status status = resolver_->CoerceExprToType(
            ast_location, expected_type.type(), Resolver::kImplicitCoercion,
            "Function declared to return $0 but the function body produces "
            "incompatible type $1",
            &resolved_sql_body);
        !status.ok()) {
      if (ShouldPropagateError(status)) {
        return status;
      }
      return MakeFunctionExprAnalysisError(function, status.message());
    }
  }

  auto aggregate_base_columns =
      query_resolution_info->release_aggregate_columns_to_compute();
  std::vector<std::unique_ptr<const ResolvedComputedColumn>> aggregate_columns;
  aggregate_columns.reserve(aggregate_base_columns.size());
  for (auto& column : aggregate_base_columns) {
    if (!column->Is<ResolvedComputedColumn>()) {
      // TODO: support conditional evaluation for UDAs.
      return MakeSqlErrorAt(ast_location)
             << "Conditional evaluation is not yet supported for UDAs.";
    }
    aggregate_columns.push_back(absl::WrapUnique(
        static_cast<const ResolvedComputedColumn*>(column.release())));
  }
  function_call_info_out->reset(new TemplatedSQLFunctionCall(
      std::move(resolved_sql_body), std::move(aggregate_columns)));

  return absl::OkStatus();
}

namespace {
template <typename T>
absl::Status CheckRange(
    T value, const ASTNode* arg_location, int idx,
    const FunctionArgumentTypeOptions& options,
    const std::function<std::string(int)>& BadArgErrorPrefix) {
  // Currently all ranges have integer bounds.
  if (options.has_min_value()) {
    const int64_t min_value = options.min_value();
    if (!(value >= T(min_value))) {  // handles value = NaN
      if (options.has_max_value()) {
        return MakeSqlErrorAt(arg_location)
               << BadArgErrorPrefix(idx) << " must be between " << min_value
               << " and " << options.max_value();
      } else {
        return MakeSqlErrorAt(arg_location)
               << BadArgErrorPrefix(idx) << " must be at least " << min_value;
      }
    }
  }
  if (options.has_max_value()) {
    const int64_t max_value = options.max_value();
    if (!(value <= T(max_value))) {  // handles value = NaN
      if (options.has_min_value()) {
        return MakeSqlErrorAt(arg_location)
               << BadArgErrorPrefix(idx) << " must be between "
               << options.min_value() << " and " << max_value;
      } else {
        return MakeSqlErrorAt(arg_location)
               << BadArgErrorPrefix(idx) << " must be at most " << max_value;
      }
    }
  }
  return absl::OkStatus();
}
}  // namespace

absl::Status FunctionResolver::CheckArgumentConstraints(
    const ASTNode* function_location, absl::string_view function_name,
    bool is_tvf, const ASTNode* arg_location, int idx,
    const FunctionArgumentType& concrete_argument, const ResolvedExpr* arg_expr,
    const std::function<std::string(int)>& BadArgErrorPrefix) const {
  GOOGLESQL_RET_CHECK(concrete_argument.IsConcrete());
  const FunctionArgumentTypeOptions& options = concrete_argument.options();
  const LanguageOptions& language_options = resolver_->language();
  const ProductMode product_mode = resolver_->product_mode();
  if (options.must_support_equality() &&
      !concrete_argument.type()->SupportsEquality(language_options)) {
    return MakeSqlErrorAt(arg_location)
           << BadArgErrorPrefix(idx) << " must support equality; Type "
           << concrete_argument.type()->ShortTypeName(product_mode)
           << " does not";
  }
  if (options.must_support_ordering() &&
      !concrete_argument.type()->SupportsOrdering(
          language_options, /*type_description=*/nullptr)) {
    return MakeSqlErrorAt(arg_location)
           << BadArgErrorPrefix(idx) << " must support ordering; Type "
           << concrete_argument.type()->ShortTypeName(product_mode)
           << " does not";
  }
  if (options.must_support_grouping() &&
      !concrete_argument.type()->SupportsGrouping(language_options)) {
    return MakeSqlErrorAt(arg_location)
           << BadArgErrorPrefix(idx) << " must support grouping; Type "
           << concrete_argument.type()->ShortTypeName(product_mode)
           << " does not";
  }

  auto get_function_display_name = [function_name, is_tvf]() {
    return absl::StrCat(is_tvf ? "Table-valued function " : "", function_name);
  };

  // Constraint which requires argument type to be array type and its element
  // type to support comparison (ordering, grouping, or equality).
  if (options.array_element_must_support_equality()) {
    GOOGLESQL_RET_CHECK(concrete_argument.type()->IsArray())
        << BadArgErrorPrefix(idx)
        << " must be array type with element type that supports "
           "equality. Type "
        << concrete_argument.type()->ShortTypeName(product_mode)
        << " is not an array type";
    const ArrayType* array_type = concrete_argument.type()->AsArray();
    GOOGLESQL_RET_CHECK_NE(array_type, nullptr);
    if (!array_type->element_type()->SupportsEquality(language_options)) {
      return MakeSqlErrorAt(function_location)
             << get_function_display_name()
             << " cannot be used on argument of type "
             << array_type->ShortTypeName(product_mode)
             << " because the array's element type does not support equality";
    }
  }
  if (options.array_element_must_support_ordering()) {
    GOOGLESQL_RET_CHECK(concrete_argument.type()->IsArray())
        << BadArgErrorPrefix(idx)
        << " must be array type with element type that supports "
           "ordering. Type "
        << concrete_argument.type()->ShortTypeName(product_mode)
        << " is not an array type";

    const ArrayType* array_type = concrete_argument.type()->AsArray();
    GOOGLESQL_RET_CHECK_NE(array_type, nullptr);
    if (!array_type->element_type()->SupportsOrdering(
            language_options, /*type_description=*/nullptr)) {
      return MakeSqlErrorAt(function_location)
             << get_function_display_name()
             << " cannot be used on argument of type "
             << array_type->ShortTypeName(product_mode)
             << " because the array's element type does not support ordering";
    }
  }
  if (options.array_element_must_support_grouping()) {
    GOOGLESQL_RET_CHECK(concrete_argument.type()->IsArray())
        << BadArgErrorPrefix(idx)
        << " must be array type with element type that supports "
           "grouping. Type "
        << concrete_argument.type()->ShortTypeName(product_mode)
        << " is not an array type";

    const ArrayType* array_type = concrete_argument.type()->AsArray();
    GOOGLESQL_RET_CHECK_NE(array_type, nullptr);
    if (!array_type->element_type()->SupportsGrouping(language_options)) {
      return MakeSqlErrorAt(function_location)
             << get_function_display_name()
             << " cannot be used on argument of type "
             << array_type->ShortTypeName(product_mode)
             << " because the array's element type does not support grouping";
    }
  }

  if (concrete_argument.must_be_constant_expression()) {
    GOOGLESQL_ASSIGN_OR_RETURN(bool arg_is_constant_expr, IsConstantExpression(arg_expr));
    if (!arg_is_constant_expr) {
      return MakeSqlErrorAt(arg_location)
             << BadArgErrorPrefix(idx) << " must be a constant expression";
    }
  }

  bool satisfies_non_aggregate_requirement = true;
  if (options.is_not_aggregate()) {
    GOOGLESQL_ASSIGN_OR_RETURN(satisfies_non_aggregate_requirement,
                     IsNonAggregateFunctionArg(arg_expr));
  }
  bool satisfies_constant_requirement = true;
  if (concrete_argument.must_be_constant()) {
    GOOGLESQL_ASSIGN_OR_RETURN(satisfies_constant_requirement,
                     IsConstantFunctionArg(arg_expr));
  }
  bool satisfies_analysis_constant_requirement = true;
  if (concrete_argument.must_be_analysis_constant()) {
    satisfies_analysis_constant_requirement = IsAnalysisConstant(arg_expr);
    if (arg_expr->Is<ResolvedLiteral>()) {
      // Ensure it doesn't get replaced by a parameter in tests.
      const_cast<ResolvedLiteral*>(arg_expr->GetAs<ResolvedLiteral>())
          ->set_preserve_in_literal_remover(true);
    }
  }
  bool satisfies_immutable_constant_requirement = true;
  if (concrete_argument.must_be_immutable_constant()) {
    satisfies_immutable_constant_requirement = IsImmutableConstant(arg_expr);
    if (arg_expr->Is<ResolvedLiteral>()) {
      // Ensure it doesn't get replaced by a parameter in tests.
      const_cast<ResolvedLiteral*>(arg_expr->GetAs<ResolvedLiteral>())
          ->set_preserve_in_literal_remover(true);
    }
  }

  // TODO: b/323602106 - Improve correctness of error message
  if (!satisfies_constant_requirement || !satisfies_non_aggregate_requirement) {
    return MakeSqlErrorAt(arg_location)
           << BadArgErrorPrefix(idx) << " must be a literal or query parameter";
  }
  if (!satisfies_analysis_constant_requirement) {
    return MakeSqlErrorAt(arg_location)
           << BadArgErrorPrefix(idx) << " must be an analysis time constant";
  }
  if (!satisfies_immutable_constant_requirement) {
    return MakeSqlErrorAt(arg_location)
           << BadArgErrorPrefix(idx) << " must be an immutable constant";
  }
  return absl::OkStatus();
}

absl::Status FunctionResolver::CheckArgumentValueConstraints(
    const ASTNode* arg_location, int idx, const Value& value,
    const FunctionArgumentType& concrete_argument,
    const std::function<std::string(int)>& BadArgErrorPrefix) const {
  GOOGLESQL_RET_CHECK(concrete_argument.IsConcrete());
  const FunctionArgumentTypeOptions& options = concrete_argument.options();
  if (value.is_null()) {
    if (options.must_be_non_null()) {
      return MakeSqlErrorAt(arg_location)
             << BadArgErrorPrefix(idx) << " must be non-NULL";
    }
  } else {
    switch (value.type_kind()) {
      case TYPE_INT64:
        return CheckRange<int64_t>(value.int64_value(), arg_location, idx,
                                   options, BadArgErrorPrefix);
      case TYPE_INT32:
        return CheckRange<int64_t>(value.int32_value(), arg_location, idx,
                                   options, BadArgErrorPrefix);
      case TYPE_UINT32:
        return CheckRange<int64_t>(value.uint32_value(), arg_location, idx,
                                   options, BadArgErrorPrefix);
      case TYPE_DOUBLE:
        return CheckRange<double>(value.double_value(), arg_location, idx,
                                  options, BadArgErrorPrefix);
      case TYPE_FLOAT:
        return CheckRange<double>(value.float_value(), arg_location, idx,
                                  options, BadArgErrorPrefix);
      case TYPE_NUMERIC:
        return CheckRange<NumericValue>(value.numeric_value(), arg_location,
                                        idx, options, BadArgErrorPrefix);
      case TYPE_BIGNUMERIC:
        return CheckRange<BigNumericValue>(value.bignumeric_value(),
                                           arg_location, idx, options,
                                           BadArgErrorPrefix);
      default:
        // For other types including UINT64, range check is not supported now.
        GOOGLESQL_RET_CHECK(!options.has_min_value());
        GOOGLESQL_RET_CHECK(!options.has_max_value());
    }
  }
  return absl::OkStatus();
}

const Coercer& FunctionResolver::coercer() const {
  ABSL_DCHECK(resolver_ != nullptr);
  return resolver_->coercer_;
}

namespace {

using SigArgKindToTypeMap =
    absl::flat_hash_map<SignatureArgumentKind, std::optional<AnnotatedType>>;

bool HasAnnotations(const AnnotatedType& annotated_type) {
  if (annotated_type.annotation_map == nullptr ||
      annotated_type.annotation_map->Empty()) {
    return false;
  }
  return true;
}

// Collects from the lambda arguments into the map.
// Conflicts are allowed so long as they are on argument kinds unrelated to
// the lambda arguments. This is because we have not yet started propagating
// annotations. We are annotating the lambda body as a prerequisite step for
// for the actual propagation. If we do hit a conflict, we reset the entry's
// AnnotatedType to nullptr. However, the fact that the entry exists itself
// means the lambda argument is affected by more than one non-lambda argument
// kind, which is not allowed.
// One example where conflicts are OK is:
//    MAP_REPLACE(MAP<K,V>, K, K, ..., V -> V)
// The lambda argument is tied to V, and is unaffected by K.
// The actual annotation propagation will take care of reconciling K's.
//
// No output is needed, because it simply registers the type and recursively
// its component kinds, but no output to report.
class AnnotatedTypeCollector
    : public AnnotatedTypeAndSignatureArgumentKindVisitor<void*> {
 public:
  explicit AnnotatedTypeCollector(SigArgKindToTypeMap& assigned_types)
      : assigned_types_(assigned_types) {}

  absl::Status PreVisitChildren(AnnotatedType annotated_type,
                                SignatureArgumentKind original_kind) override {
    GOOGLESQL_RET_CHECK(annotated_type.type != nullptr);
    auto [it, success] =
        assigned_types_.insert({original_kind, annotated_type});
    if (!success) {
      // If the existing entry is already marked as a conflict, or if there are
      // any annotations on either the existing or the new entry, this is a
      // conflict.
      const auto& existing = it->second;
      if (!existing.has_value() || HasAnnotations(*existing) ||
          HasAnnotations(annotated_type)) {
        it->second = std::nullopt;
      }
    }
    return absl::OkStatus();
  }

  absl::StatusOr<void*> PostVisitChildren(
      AnnotatedType annotated_type, SignatureArgumentKind original_kind,
      std::vector<void*> component_results) override {
    // Nothing to do, we already put the components in the map.
    // Ensure it is indeed there, even if it was set to std::nullopt.
    auto it = assigned_types_.find(original_kind);
    GOOGLESQL_RET_CHECK(it != assigned_types_.end());
    return nullptr;
  }

 private:
  SigArgKindToTypeMap& assigned_types_;
};

}  // namespace

absl::Status FunctionResolver::ReResolveLambdasIfNecessary(
    const FunctionSignature* result_signature, const NameScope* name_scope,
    const absl::flat_hash_map<const ResolvedInlineLambda*, const ASTLambda*>&
        lambda_ast_nodes,
    ResolvedFunctionCall* resolved_expr_out) const {
  // Re-resolve any lambdas if any of the arguments have annotations.
  // For now, we just require direct inference, so a function cannot have args
  // where one lambda feeds into the other, e.g. F(T1, T1->T2, T2->T3) ->T3
  // or with complex interactions between the args and the lambda.
  // It's likely a cycle can never happen anyway, and more shapes can be
  // supported but this is left as a future exercise.
  // For now, we require:
  // 1. The lambda outputs do not interfere or relate to any of the non-lambda
  //    argument kinds.
  // 2. We further require that each lambda argument relates to at most one
  //    non-lambda argument kind.
  // These may be relaxed in the future as needed (e.g. by reusing the logic
  // from DefaultAnnotationSpec for argument kind merging).
  auto generic_args = resolved_expr_out->release_generic_argument_list();

  // First pass to collect non-lambda argument annotations. We only consider
  // expression arguments that whose original kind is templated, because
  // nothing else might relate to the lambda args.
  // For example, in `ARRAY_ZIP(arr1, arr2, (e1, e2) -> ...)`, only `arr1`
  // and `arr2` may propagate annotations to the lambda args `e1` and `e2`.
  // If they do not, then the originally resolved body has all the needed
  // annotations, and there's no need to re-resolve it.
  SigArgKindToTypeMap assigned_types;
  GOOGLESQL_RET_CHECK_EQ(generic_args.size(), result_signature->NumConcreteArguments());
  for (int i = 0; i < generic_args.size(); ++i) {
    if (generic_args[i]->expr() == nullptr) {
      continue;
    }
    SignatureArgumentKind original_kind =
        result_signature->ConcreteArgument(i).original_kind();

    AnnotatedTypeCollector collector(assigned_types);
    GOOGLESQL_RETURN_IF_ERROR(
        collector
            .Visit(generic_args[i]->expr()->annotated_type(), original_kind)
            .status());
  }

  // Then, re-resolve lambdas whose argument kinds have any annotations.
  // For example, in ARRAY_ZIP(arr1, arr2, (e1, e2) -> ...), we now need to
  // check for annotations assigned to `e1` and `e2`, which here correspond to
  // `ANY_1` and `ANY_2`.
  for (int i = 0; i < generic_args.size(); ++i) {
    auto& generic_arg = generic_args[i];
    if (generic_arg->inline_lambda() == nullptr) {
      continue;
    }

    bool args_have_annotations = false;
    std::vector<AnnotatedType> lambda_arg_annotated_types;
    for (const auto& lambda_arg :
         result_signature->ConcreteArgument(i).lambda().argument_types()) {
      // TODO Use the general logic to build the type instead.
      // This would only be needed when we have a lambda arg whose kind is a
      // container of simpler kinds, e.g. the non-lambda arg is ANY_1 and the
      // lambda arg is ARRAY_ANY_1. We have no such cases today, so a quick
      // lookup in the map is sufficient.
      auto it = assigned_types.find(lambda_arg.original_kind());
      if (it != assigned_types.end()) {
        const auto& assigned_type = it->second;
        GOOGLESQL_RET_CHECK(assigned_type.has_value())
            << "Signature has lambda whose arguments are influenced by more "
               "than one non-lambda argument: "
            << result_signature->DebugString();
        if (assigned_type->annotation_map != nullptr &&
            !assigned_type->annotation_map->Empty()) {
          args_have_annotations = true;
        }
        lambda_arg_annotated_types.push_back(*assigned_type);

      } else {
        lambda_arg_annotated_types.emplace_back(lambda_arg.type(),
                                                /*annotation_map=*/nullptr);
      }
    }

    if (!args_have_annotations) {
      // No need to re-resolve this lambda. There are no annotations to
      // propagate from the arguments. Any annotations from the body itself
      // are already there from the initial resolution.
      continue;
    }

    auto it = lambda_ast_nodes.find(generic_arg->inline_lambda());
    GOOGLESQL_RET_CHECK(it != lambda_ast_nodes.end());
    const ASTLambda* ast_lambda = it->second;

    GOOGLESQL_ASSIGN_OR_RETURN(std::vector<IdString> arg_names,
                     ExtractLambdaArgumentNames(ast_lambda));
    std::unique_ptr<const ResolvedInlineLambda> re_resolved_lambda;
    GOOGLESQL_RETURN_IF_ERROR(resolver_->ResolveLambdaWithAnnotations(
        ast_lambda, arg_names, lambda_arg_annotated_types,
        result_signature->ConcreteArgument(i).lambda().body_type().type(),
        /*allow_argument_coercion=*/false, name_scope, &re_resolved_lambda));

    // Replace with the new argument.
    auto new_arg = MakeResolvedFunctionArgument();
    new_arg->set_inline_lambda(std::move(re_resolved_lambda));
    generic_arg = std::move(new_arg);
  }
  resolved_expr_out->set_generic_argument_list(std::move(generic_args));
  return absl::OkStatus();
}

}  // namespace googlesql
