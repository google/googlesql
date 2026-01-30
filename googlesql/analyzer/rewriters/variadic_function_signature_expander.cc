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

#include "googlesql/analyzer/rewriters/variadic_function_signature_expander.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output_properties.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_ast_rewrite_visitor.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {

// SQL templates for rewriting variadic MAP functions.
constexpr char kMapInsertSql[] = R"sql(
IF(
  input_map IS NULL,
  NULL,
  MAP_FROM_ARRAY(
    ARRAY(
      SELECT
        CASE
          -- If a key arg appears more than once
          WHEN SUM(entries.is_new) > 1 THEN
            ERROR(
              FORMAT(
                'Key provided more than once as argument: %T',
                 entries.key
               )
             )
          -- If a key to insert already exists in the map
          WHEN SUM(entries.is_new) = 1 AND COUNT(*) > 1 THEN
            ERROR(FORMAT('Key already exists in map: %T', entries.key))
          -- Otherwise, insert the new <key,value> pair.
          ELSE
            STRUCT(entries.key, ANY_VALUE(entries.value HAVING MAX entries.is_new))
        END
      FROM (
        -- Entries from the original map
        SELECT t.key, t.value, 0 AS is_new
        FROM UNNEST(MAP_ENTRIES_UNSORTED(input_map)) AS t
        UNION ALL
        -- Entries to insert from the arguments
        SELECT key, value, 1 AS is_new
        FROM ( $NEW_ENTRIES_SUBQUERY )
      ) AS entries
      GROUP BY entries.key
    )
  )
)
)sql";

constexpr char kMapInsertOrReplaceSql[] = R"sql(
IF(
  input_map IS NULL,
  NULL,
  MAP_FROM_ARRAY(
    ARRAY(
      SELECT
        CASE
          -- If a key arg appears more than once
          WHEN SUM(entries.is_new) > 1 THEN
            ERROR(
              FORMAT(
                'Key provided more than once as argument: %T',
                entries.key
              )
            )
          ELSE
            STRUCT(
              entries.key,
              -- Prioritise keeping new values
              ANY_VALUE(entries.value HAVING MAX entries.is_new)
            )
        END
      FROM (
        -- Entries from the original map
        SELECT t.key, t.value, 0 AS is_new
        FROM UNNEST(MAP_ENTRIES_UNSORTED(input_map)) AS t
        UNION ALL
        -- Entries to insert or replace from the arguments
        SELECT key, value, 1 AS is_new
        FROM ( $NEW_ENTRIES_SUBQUERY )
      ) AS entries
      GROUP BY entries.key
    )
  )
)
)sql";

constexpr char kMapReplaceKvPairsSql[] = R"sql(
IF(
  input_map IS NULL,
  NULL,
  MAP_FROM_ARRAY(
    ARRAY(
      SELECT
        CASE
          -- If a key arg appears more than once
          WHEN SUM(entries.is_new) > 1 THEN
            ERROR(
              FORMAT(
                'Key provided more than once as argument: %T',
                entries.key
              )
            )
          -- If a key to update is not in the map
          WHEN MIN(entries.is_new) = 1 THEN
            ERROR(FORMAT('Key does not exist in map: %T', entries.key))
          ELSE
            -- If the key's value is to be replaced, update it (i.e., MAX(is_new) = 1)
            -- Otherwise, keep the existing value (i.e., MAX(is_new) = 0)
            STRUCT(entries.key, ANY_VALUE(entries.value HAVING MAX entries.is_new))
        END
      FROM (
        -- Entries from the original map
        SELECT t.key, t.value, 0 AS is_new
        FROM UNNEST(MAP_ENTRIES_UNSORTED(input_map)) AS t
        UNION ALL
        -- Entries to replace from the arguments
        SELECT key, value, 1 AS is_new
        FROM ( $NEW_ENTRIES_SUBQUERY )
      ) AS entries
      GROUP BY entries.key
    )
  )
)
)sql";

constexpr char kMapReplaceKRepeatedVLambdaSql[] = R"sql(
IF(
  input_map IS NULL,
  NULL,
  MAP_FROM_ARRAY(
    ARRAY(
      SELECT
        CASE
          -- If a key arg appears more than once
          WHEN SUM(entries.is_new) > 1 THEN
            ERROR(
              FORMAT(
                'Key provided more than once as argument: %T',
                entries.key
              )
            )
          -- If a key to update is not in the map
          WHEN MIN(entries.is_new) = 1 THEN
            ERROR(FORMAT('Key does not exist in map: %T', entries.key))
          ELSE
            STRUCT(
              entries.key,
              IF(
                MAX(entries.is_new) = 1,
                -- If the key's value is to be replaced, update it with the lambda
                updater_lambda(ANY_VALUE(entries.value HAVING MIN entries.is_new)),
                -- Otherwise, all entries.is_new are 0, so keep the original value
                ANY_VALUE(entries.value)
              )
            )
        END
      FROM (
        -- Entries from the original map
        SELECT t.key, t.value, 0 AS is_new
        FROM UNNEST(MAP_ENTRIES_UNSORTED(input_map)) AS t
        UNION ALL
        -- Keys to update from the arguments
        SELECT key, NULL AS value, 1 AS is_new
        FROM ($KEYS_TO_UPDATE_SUBQUERY)
      ) AS entries
      GROUP BY entries.key
    )
  )
)
)sql";

constexpr char kMapDeleteSql[] = R"sql(
IF(
  input_map IS NULL,
  NULL,
  MAP_FROM_ARRAY(
    ARRAY(
      SELECT STRUCT(entries.key, ANY_VALUE(entries.value))
      FROM (
        SELECT t.key, t.value, 0 AS is_delete_key
        FROM UNNEST(MAP_ENTRIES_UNSORTED(input_map)) AS t
        UNION ALL
        SELECT key, NULL AS value, 1 AS is_delete_key
        FROM ($KEYS_TO_DELETE_SUBQUERY)
      ) AS entries
      GROUP BY entries.key
      HAVING MAX(entries.is_delete_key) = 0
    )
  )
)
)sql";

// Struct to handle arguments from both
// ResolvedFunctionCall::argument_list() or
// ResolvedFunctionCall::generic_argument_list() (in the case of lambda args) in
// a unified manner.
struct ArgumentInfo {
  std::string name;
  const ResolvedExpr* expr = nullptr;
  const ResolvedInlineLambda* inline_lambda = nullptr;

  ArgumentInfo(std::string name, const ResolvedExpr* expr)
      : name(std::move(name)), expr(expr) {}
  ArgumentInfo(std::string name, const ResolvedFunctionArgument* arg)
      : name(std::move(name)) {
    if (arg->expr() != nullptr) {
      expr = arg->expr();
    } else {
      ABSL_DCHECK(arg->inline_lambda() != nullptr);
      inline_lambda = arg->inline_lambda();
    }
  }
};

// Determines the name for an argument in a variadic function call.
// `arg_index` is the index of the argument within the entire function call
// including all leading, repeated, and trailing arguments. Determining the
// name of an argument depends on whether it is a leading, repeated, or trailing
// argument.
inline std::string DetermineArgumentName(
    const FunctionSignature& signature, int arg_index, size_t num_total_args,
    int num_leading_non_repeated_args, int num_trailing_non_repeated_args,
    absl::Span<const int> repeated_arg_indices) {
  if (arg_index < num_leading_non_repeated_args) {
    // Leading non-repeated arguments map directly to the first arguments in the
    // function signature.
    const auto& sig_arg = signature.argument(arg_index);
    if (sig_arg.has_argument_name()) {
      return sig_arg.argument_name();
    }
  } else if (arg_index >= num_total_args - num_trailing_non_repeated_args) {
    // Trailing non-repeated arguments map to the last arguments in the function
    // signature, which means we need to calculate the corresponding index in
    // the signature's argument list.
    const int sig_arg_idx = static_cast<int>(signature.arguments().size() -
                                             (num_total_args - arg_index));
    const auto& sig_arg = signature.argument(sig_arg_idx);
    if (sig_arg.has_argument_name()) {
      return sig_arg.argument_name();
    }
  } else {
    // Repeated arguments, between the leading and trailing non-repeated
    // arguments.
    const size_t num_repeated_args_in_signature = repeated_arg_indices.size();
    const int idx_in_repeated_block = arg_index - num_leading_non_repeated_args;

    // Find which repeated argument from the signature this corresponds to.
    const int repeated_sig_arg_offset =
        idx_in_repeated_block % num_repeated_args_in_signature;
    const int sig_arg_idx = repeated_arg_indices[repeated_sig_arg_offset];
    const auto& sig_arg = signature.argument(sig_arg_idx);

    if (sig_arg.has_argument_name()) {
      // For repeated arguments, we append an occurrence number, e.g., "key_1",
      // "key_2".
      const int occurrence =
          (idx_in_repeated_block / num_repeated_args_in_signature) + 1;
      return absl::StrCat(sig_arg.argument_name(), "_", occurrence);
    }
  }

  // Fallback for any case where the argument has no name.
  return absl::StrCat("unnamed_arg_", arg_index);
}

template <typename T>
void ExtractArguments(const T& args, const FunctionSignature& signature,
                      int num_leading_non_repeated_args,
                      int num_trailing_non_repeated_args,
                      absl::Span<const int> repeated_arg_indices,
                      std::vector<ArgumentInfo>* arguments) {
  for (int i = 0; i < args.size(); ++i) {
    std::string arg_name = DetermineArgumentName(
        signature, i, args.size(), num_leading_non_repeated_args,
        num_trailing_non_repeated_args, repeated_arg_indices);
    arguments->emplace_back(arg_name, args[i].get());
  }
}

std::vector<ArgumentInfo> ExtractArgumentInfos(
    const ResolvedFunctionCall& node, const FunctionSignature& signature,
    int num_leading_non_repeated_args,
    const std::vector<int>& repeated_arg_indices) {
  std::vector<ArgumentInfo> arguments;
  bool uses_generic_args = !node.generic_argument_list().empty();

  int num_trailing_non_repeated_args =
      static_cast<int>(signature.arguments().size()) -
      num_leading_non_repeated_args -
      static_cast<int>(repeated_arg_indices.size());

  if (uses_generic_args) {
    ExtractArguments(
        node.generic_argument_list(), signature, num_leading_non_repeated_args,
        num_trailing_non_repeated_args, repeated_arg_indices, &arguments);
  } else {
    ExtractArguments(
        node.argument_list(), signature, num_leading_non_repeated_args,
        num_trailing_non_repeated_args, repeated_arg_indices, &arguments);
  }
  return arguments;
}

absl::StatusOr<std::string> GenerateSqlForVariadicFunction(
    const ResolvedFunctionCall& node,
    absl::Span<const ArgumentInfo> arguments) {
  const FunctionSignature& signature = node.signature();
  std::string generated_sql;
  // Extract variadic arguments or argument pairs based on the function.
  // This could be generalised further if support for variadic functions with
  // repeated tuples of 3+ arguments is needed.
  switch (signature.context_id()) {
    case FN_MAP_INSERT:
    case FN_MAP_INSERT_OR_REPLACE:
    case FN_MAP_REPLACE_KV_PAIRS: {
      GOOGLESQL_RET_CHECK_GE(arguments.size(), 3)
          << node.function()->Name()
          << " requires at least 3 arguments: a map, and at least one "
             "key/value pair.";
      GOOGLESQL_RET_CHECK_EQ((arguments.size() - 1) % 2, 0)
          << node.function()->Name()
          << " requires an even number of key/value pair arguments.";
      std::vector<std::string> union_clauses;
      for (size_t i = 1; i < arguments.size(); i += 2) {
        const std::string& key_name = arguments[i].name;
        const std::string& val_name = arguments[i + 1].name;
        union_clauses.push_back(absl::Substitute(
            "SELECT $0 AS key, $1 AS value", key_name, val_name));
      }
      std::string new_entries_sql = absl::StrJoin(union_clauses, " UNION ALL ");

      const char* sql_template;
      switch (signature.context_id()) {
        case FN_MAP_INSERT:
          sql_template = kMapInsertSql;
          break;
        case FN_MAP_INSERT_OR_REPLACE:
          sql_template = kMapInsertOrReplaceSql;
          break;
        case FN_MAP_REPLACE_KV_PAIRS:
          sql_template = kMapReplaceKvPairsSql;
          break;
      }
      generated_sql = absl::StrReplaceAll(
          sql_template, {{"$NEW_ENTRIES_SUBQUERY", new_entries_sql}});
      break;
    }
    case FN_MAP_REPLACE_K_REPEATED_V_LAMBDA: {
      GOOGLESQL_RET_CHECK_GE(arguments.size(), 3)
          << node.function()->Name()
          << " requires at least 3 arguments: a map, one or more keys, and a "
             "lambda.";

      std::vector<std::string> union_clauses;
      for (size_t i = 1; i < arguments.size() - 1; ++i) {
        const std::string& key_name = arguments[i].name;
        union_clauses.push_back(absl::Substitute("SELECT $0 AS key", key_name));
      }
      std::string keys_to_update_sql =
          absl::StrJoin(union_clauses, " UNION ALL ");
      const std::string& lambda_name = arguments.back().name;
      generated_sql =
          absl::StrReplaceAll(kMapReplaceKRepeatedVLambdaSql,
                              {{"$KEYS_TO_UPDATE_SUBQUERY", keys_to_update_sql},
                               {"updater_lambda", lambda_name}});
      break;
    }
    case FN_MAP_DELETE: {
      GOOGLESQL_RET_CHECK_GE(arguments.size(), 2)
          << node.function()->Name()
          << " requires at least 2 arguments: a map, and one or "
             "more keys to delete.";
      std::vector<std::string> union_clauses;
      for (size_t i = 1; i < arguments.size(); ++i) {
        const std::string& key_name = arguments[i].name;
        union_clauses.push_back(absl::Substitute("SELECT $0 AS key", key_name));
      }
      std::string keys_to_delete_sql =
          absl::StrJoin(union_clauses, " UNION ALL ");
      generated_sql = absl::StrReplaceAll(
          kMapDeleteSql, {{"$KEYS_TO_DELETE_SUBQUERY", keys_to_delete_sql}});
      break;
    }
    default:
      return absl::InternalError(
          absl::StrCat("Unsupported function signature: ",
                       FunctionSignatureId_Name(signature.context_id())));
  }
  return generated_sql;
}

absl::StatusOr<std::vector<FunctionArgumentType>> BuildExpandedArgumentTypes(
    absl::Span<const ArgumentInfo> arguments,
    const ResolvedFunctionCall& node) {
  std::vector<FunctionArgumentType> new_arg_types;
  for (const auto& arg_info : arguments) {
    FunctionArgumentTypeOptions options;
    options.set_argument_name(arg_info.name, googlesql::kPositionalOrNamed);
    if (arg_info.expr != nullptr) {
      new_arg_types.push_back(FunctionArgumentType(
          arg_info.expr->type(), options, /*num_occurrences=*/1));
    } else {
      ABSL_DCHECK(arg_info.inline_lambda != nullptr)
          << "Unsupported argument type in " << node.function()->Name();

      const googlesql::ResolvedInlineLambda* lambda = arg_info.inline_lambda;

      googlesql::FunctionArgumentTypeList lambda_arg_types;
      for (const googlesql::ResolvedColumn& arg_col : lambda->argument_list()) {
        lambda_arg_types.push_back(
            googlesql::FunctionArgumentType(arg_col.type(),
                                            /*num_occurrences=*/1));
      }

      const googlesql::Type* body_type = lambda->body()->type();
      googlesql::FunctionArgumentType lambda_body_type(body_type,
                                                       /*num_occurrences=*/1);

      new_arg_types.push_back(googlesql::FunctionArgumentType::Lambda(
          std::move(lambda_arg_types), std::move(lambda_body_type), options));
    }
  }
  return new_arg_types;
}

class VariadicFunctionSignatureExpanderVisitor
    : public ResolvedASTRewriteVisitor {
 private:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedFunctionCall(
      std::unique_ptr<const ResolvedFunctionCall> node) override {
    const FunctionSignature& signature = node->signature();
    // Return quickly if the function is not supported by this rewriter.
    switch (signature.context_id()) {
      case FN_MAP_INSERT:
      case FN_MAP_INSERT_OR_REPLACE:
      case FN_MAP_REPLACE_KV_PAIRS:
      case FN_MAP_REPLACE_K_REPEATED_V_LAMBDA:
      case FN_MAP_DELETE:
        // These functions are supported by this rewriter.
        break;
      default:
        return node;
    }

    // Collect signature arguments, identifying repeated ones.
    std::vector<int> repeated_arg_indices;
    int num_leading_non_repeated_args = -1;
    for (int i = 0; i < signature.arguments().size(); ++i) {
      if (signature.argument(i).repeated()) {
        repeated_arg_indices.push_back(i);
        if (num_leading_non_repeated_args == -1) {
          num_leading_non_repeated_args = i;
        }
      }
    }

    std::vector<ArgumentInfo> arguments = ExtractArgumentInfos(
        *node, signature, num_leading_non_repeated_args, repeated_arg_indices);

    GOOGLESQL_ASSIGN_OR_RETURN(std::string generated_sql,
                     GenerateSqlForVariadicFunction(*node, arguments));

    GOOGLESQL_ASSIGN_OR_RETURN(std::vector<FunctionArgumentType> new_arg_types,
                     BuildExpandedArgumentTypes(arguments, *node));

    FunctionSignature current_sig = node->signature();
    FunctionSignatureOptions sig_options = current_sig.options();

    FunctionSignatureRewriteOptions rewrite_options;
    rewrite_options.set_sql(generated_sql);
    rewrite_options.set_rewriter(REWRITE_BUILTIN_FUNCTION_INLINER);
    rewrite_options.set_enabled(true);
    sig_options.set_rewrite_options(rewrite_options);

    FunctionSignature new_sig(current_sig.result_type(), new_arg_types,
                              current_sig.context_id(), sig_options);

    ResolvedFunctionCallBuilder builder = ToBuilder(std::move(node));
    builder.set_signature(new_sig);
    GOOGLESQL_ASSIGN_OR_RETURN(auto new_node, std::move(builder).Build());
    return new_node;
  }
};

}  // namespace

absl::StatusOr<std::unique_ptr<const ResolvedNode>>
VariadicFunctionSignatureExpander::Rewrite(
    const AnalyzerOptions& options, std::unique_ptr<const ResolvedNode> input,
    Catalog& catalog, TypeFactory& type_factory,
    AnalyzerOutputProperties& analyzer_output_properties) const {
  VariadicFunctionSignatureExpanderVisitor visitor;
  return visitor.VisitAll(std::move(input));
}

const Rewriter* GetVariadicFunctionSignatureExpander() {
  static const auto* const kRewriter = new VariadicFunctionSignatureExpander();
  return kRewriter;
}

}  // namespace googlesql
