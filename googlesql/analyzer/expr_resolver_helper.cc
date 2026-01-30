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

#include "googlesql/analyzer/expr_resolver_helper.h"

#include <memory>
#include <optional>
#include <string>

#include "googlesql/base/logging.h"
#include "googlesql/analyzer/name_scope.h"
#include "googlesql/analyzer/query_resolver_helper.h"
#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/with_modifier_mode.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "absl/memory/memory.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

absl::StatusOr<bool> IsConstantExpression(const ResolvedExpr* expr) {

  switch (expr->node_kind()) {
    case RESOLVED_CONSTANT:
    case RESOLVED_LITERAL:
    case RESOLVED_PARAMETER:
    case RESOLVED_SYSTEM_VARIABLE:
    case RESOLVED_ARGUMENT_REF:
    case RESOLVED_EXPRESSION_COLUMN:
    case RESOLVED_CATALOG_COLUMN_REF:
    case RESOLVED_DMLDEFAULT:
      // These can't contain ColumnRefs and are constant for this query.
      return true;

      // These are always treated as non-constant expressions.
    case RESOLVED_COLUMN_REF:
    case RESOLVED_AGGREGATE_FUNCTION_CALL:
    case RESOLVED_ANALYTIC_FUNCTION_CALL:
    case RESOLVED_GET_ROW_FIELD:

      // Subqueries are considered non-constant because they may involve
      // iteration over tables. We could make subqueries constant if they have
      // no table scans, but it's not implemented.
    case RESOLVED_SUBQUERY_EXPR:

      // ResolvedWithExpr/WITH() is treated as non-const.
      //
      // Some parts of GoogleSQL, notably certain rewriters (e.g. PIVOT) have
      // come to view constness as implying that new column ids are not
      // introduced in an expression. They assume that subtrees that are const
      // can be cloned without causing column id collisions, which is not the
      // case for a let expression. Without fixing this, we cannot be smarter
      // about interpreting the constness of let expressions.
      //
      // For subqueries and let expressions, there may be other reasons why
      // these are treated as non-const, but we don't know what they are if so.
    case RESOLVED_WITH_EXPR:
      return false;

    case RESOLVED_FUNCTION_CALL: {
      // Scalar function calls are treated as constant expressions based on
      // FunctionOptions::volatility.
      const ResolvedFunctionCall* function_call =
          expr->GetAs<ResolvedFunctionCall>();
      if (function_call->function()->function_options().volatility ==
          FunctionEnums::VOLATILE) {
        return false;
      }
      for (const std::unique_ptr<const ResolvedExpr>& arg :
           function_call->argument_list()) {
        GOOGLESQL_ASSIGN_OR_RETURN(bool arg_is_constant_expr,
                         IsConstantExpression(arg.get()));
        if (!arg_is_constant_expr) {
          return false;
        }
      }
      for (const std::unique_ptr<const ResolvedFunctionArgument>& arg :
           function_call->generic_argument_list()) {
        if (arg->expr() != nullptr) {
          GOOGLESQL_ASSIGN_OR_RETURN(bool arg_is_constant_expr,
                           IsConstantExpression(arg->expr()));
          if (!arg_is_constant_expr) {
            return false;
          }
        } else if (arg->inline_lambda() != nullptr) {
          return false;
        } else {
          ABSL_DCHECK(false) << "Unexpected function argument: "
                        << arg->DebugString() << "\n of function call: "
                        << function_call->DebugString();
          return false;
        }
      }
      return true;
    }

    case RESOLVED_CAST:
      return IsConstantExpression(expr->GetAs<ResolvedCast>()->expr());

    case RESOLVED_GET_STRUCT_FIELD:
      return IsConstantExpression(
          expr->GetAs<ResolvedGetStructField>()->expr());

    case RESOLVED_GET_PROTO_FIELD:
      return IsConstantExpression(expr->GetAs<ResolvedGetProtoField>()->expr());

    case RESOLVED_GET_JSON_FIELD:
      return IsConstantExpression(expr->GetAs<ResolvedGetJsonField>()->expr());

    case RESOLVED_FLATTEN:
      for (const auto& arg : expr->GetAs<ResolvedFlatten>()->get_field_list()) {
        GOOGLESQL_ASSIGN_OR_RETURN(bool arg_is_constant_expr,
                         IsConstantExpression(arg.get()));
        if (!arg_is_constant_expr) {
          return false;
        }
      }
      return IsConstantExpression(expr->GetAs<ResolvedFlatten>()->expr());

    case RESOLVED_FLATTENED_ARG:
      // These represent the result of a previous resolved flatten expression or
      // get. They may or may not be constant, but if they are not then they
      // would fail IsConstantExpression for an earlier step in RESOLVED_FLATTEN
      // above. If we said they were not constant then RESOLVED_FLATTEN would
      // never be able to be constant even if expr and all get_field_list were.
      return true;

    case RESOLVED_REPLACE_FIELD: {
      const ResolvedReplaceField* replace_field =
          expr->GetAs<ResolvedReplaceField>();
      GOOGLESQL_ASSIGN_OR_RETURN(bool replace_field_is_constant_expr,
                       IsConstantExpression(replace_field->expr()));
      if (!replace_field_is_constant_expr) {
        return false;
      }
      for (const std::unique_ptr<const ResolvedReplaceFieldItem>&
               replace_field_item : replace_field->replace_field_item_list()) {
        GOOGLESQL_ASSIGN_OR_RETURN(bool replace_field_item_is_constant_expr,
                         IsConstantExpression(replace_field_item->expr()));
        if (!replace_field_item_is_constant_expr) {
          return false;
        }
      }
      return true;
    }

    case RESOLVED_GET_PROTO_ONEOF: {
      return IsConstantExpression(expr->GetAs<ResolvedGetProtoOneof>()->expr());
    }

    case RESOLVED_MAKE_STRUCT: {
      // No code coverage on this because we don't currently have syntax to
      // generate it as a top-level expression.
      const ResolvedMakeStruct* make_struct = expr->GetAs<ResolvedMakeStruct>();
      for (const std::unique_ptr<const ResolvedExpr>& child_expr :
           make_struct->field_list()) {
        GOOGLESQL_ASSIGN_OR_RETURN(bool child_expr_is_constant_expr,
                         IsConstantExpression(child_expr.get()));
        if (!child_expr_is_constant_expr) {
          return false;
        }
      }
      return true;
    }

    case RESOLVED_FILTER_FIELD: {
      return IsConstantExpression(expr->GetAs<ResolvedFilterField>()->expr());
    }

    case RESOLVED_MAKE_PROTO: {
      // No code coverage on this because we don't currently have syntax to
      // generate it as a top-level expression.
      const ResolvedMakeProto* make_proto = expr->GetAs<ResolvedMakeProto>();
      for (const std::unique_ptr<const ResolvedMakeProtoField>& field :
           make_proto->field_list()) {
        GOOGLESQL_ASSIGN_OR_RETURN(bool field_is_constant_expr,
                         IsConstantExpression(field->expr()));
        if (!field_is_constant_expr) {
          return false;
        }
      }
      return true;
    }

    case RESOLVED_GRAPH_IS_LABELED_PREDICATE:
      // Only the operand needs to be evaluated for constness. The label-expr
      // on the RHS is effectively constant since a given label-expr
      // will have the same semantic meaning throughout the query and
      // across different query statements.
      return IsConstantExpression(
          expr->GetAs<ResolvedGraphIsLabeledPredicate>()->expr());
    case RESOLVED_GRAPH_GET_ELEMENT_PROPERTY:
    case RESOLVED_GRAPH_MAKE_ELEMENT:
    // See above reasoning for subquery + aggregation
    case RESOLVED_ARRAY_AGGREGATE:
      return false;

    case RESOLVED_UPDATE_CONSTRUCTOR: {
      const ResolvedUpdateConstructor* update_constructor =
          expr->GetAs<ResolvedUpdateConstructor>();
      GOOGLESQL_ASSIGN_OR_RETURN(bool update_constructor_expr_is_constant,
                       IsConstantExpression(update_constructor->expr()));
      if (!update_constructor_expr_is_constant) {
        return false;
      }

      for (const std::unique_ptr<const ResolvedUpdateFieldItem>&
               update_field_item :
           update_constructor->update_field_item_list()) {
        GOOGLESQL_ASSIGN_OR_RETURN(bool update_field_item_expr_is_constant,
                         IsConstantExpression(update_field_item->expr()));
        if (!update_field_item_expr_is_constant) {
          return false;
        }
      }

      return true;
    }

    default:
      // Update the static_assert above if adding or removing cases in
      // this switch.
      GOOGLESQL_RET_CHECK_FAIL() << "Unhandled expression type "
                       << expr->node_kind_string()
                       << " in IsConstantExpression";
      return false;
  }
}

// The requirements for IsConstantFunctionArg or IsNonAggregateFunctionArg
// ignore any number of wrapping cast operations. This helper removes the casts
// and returns the first interesting expression.
static const ResolvedExpr* RemoveWrappingCasts(const ResolvedExpr* expr) {
  // TODO: b/323409001 - Either do not remove casts with format, or only remove
  //     in case the format expression satisfies the definition of constant that
  //     the caller is interested in understanding.
  while (expr->Is<ResolvedCast>()) {
    expr = expr->GetAs<ResolvedCast>()->expr();
  }
  return expr;
}

absl::StatusOr<bool> IsConstantFunctionArg(const ResolvedExpr* expr) {
  switch (RemoveWrappingCasts(expr)->node_kind()) {
    case RESOLVED_PARAMETER:
    case RESOLVED_LITERAL:
    case RESOLVED_CONSTANT:
      return true;
    default:
      return false;
  }
}

absl::StatusOr<bool> IsNonAggregateFunctionArg(const ResolvedExpr* expr) {
  // LINT.IfChange(non_aggregate_args_def)
  const ResolvedExpr* uncast_expr = RemoveWrappingCasts(expr);
  switch (uncast_expr->node_kind()) {
    case RESOLVED_PARAMETER:
    case RESOLVED_LITERAL:
    case RESOLVED_CONSTANT:
      return true;
    case RESOLVED_ARGUMENT_REF:
      return uncast_expr->GetAs<ResolvedArgumentRef>()->argument_kind() ==
             ResolvedArgumentRef::NOT_AGGREGATE;
    default:
      return false;
  }
  // LINT.ThenChange(./rewriters/sql_function_inliner.cc:non_aggregate_args_def)
}

ExprResolutionInfo::ExprResolutionInfo(
    QueryResolutionInfo* query_resolution_info_in,
    const NameScope* name_scope_in, ExprResolutionInfoOptions options)
    : name_scope(name_scope_in),
      aggregate_name_scope(name_scope_in),
      analytic_name_scope(name_scope_in),
      allows_aggregation(options.allows_aggregation.value_or(false)),
      allows_analytic(options.allows_analytic.value_or(false)),
      is_graph_measure_expression(options.is_graph_measure_expression),
      clause_name(options.clause_name == nullptr ? "" : options.clause_name),
      query_resolution_info(query_resolution_info_in),
      use_post_grouping_columns(
          options.use_post_grouping_columns.value_or(false)),
      top_level_ast_expr(options.top_level_ast_expr),
      column_alias(options.column_alias),
      allows_horizontal_aggregation(
          options.allows_horizontal_aggregation.value_or(false)),
      horizontal_aggregation_info(),
      in_horizontal_aggregation(false),
      in_match_recognize_define(
          options.in_match_recognize_define.value_or(false)),
      nearest_enclosing_physical_nav_op(
          options.nearest_enclosing_physical_nav_op) {
  ABSL_DCHECK(options.name_scope == nullptr)
      << "Pass name_scope in the required argument, not in options";
  Subscribe(name_scope);
}

ExprResolutionInfo::ExprResolutionInfo(
    QueryResolutionInfo* query_resolution_info, const NameScope* name_scope_in,
    const NameScope* aggregate_name_scope_in,
    const NameScope* analytic_name_scope_in, ExprResolutionInfoOptions options)
    : ExprResolutionInfo(query_resolution_info, name_scope_in, options) {
  // Hack because I can't use initializer syntax and a delegated constructor.
  const_cast<const NameScope*&>(this->aggregate_name_scope) =
      aggregate_name_scope_in;
  const_cast<const NameScope*&>(this->analytic_name_scope) =
      analytic_name_scope_in;
}

ExprResolutionInfo::ExprResolutionInfo(const NameScope* name_scope_in,
                                       ExprResolutionInfoOptions options)
    : ExprResolutionInfo(/*query_resolution_info=*/nullptr, name_scope_in,
                         options) {}

// Macro for args that copy from the parent, overriding with the value
// from options if present.
#define ARG_UPDATE(x) x(options.x ? options.x : parent->x)
// Use this version for options that need to unwrap an std::optional.
#define ARG_UPDATE_OPT(x) x(options.x.value_or(parent->x))

ExprResolutionInfo::ExprResolutionInfo(ExprResolutionInfo* parent,
                                       ExprResolutionInfoOptions options)
    : parent(parent),
      ARG_UPDATE(name_scope),
      aggregate_name_scope(options.aggregate_name_scope
                               ? options.aggregate_name_scope
                               : parent->aggregate_name_scope),
      analytic_name_scope(options.analytic_name_scope
                              ? options.analytic_name_scope
                              : parent->analytic_name_scope),
      ARG_UPDATE_OPT(allows_aggregation),
      ARG_UPDATE_OPT(allows_analytic),
      ARG_UPDATE(is_graph_measure_expression),
      ARG_UPDATE(clause_name),
      query_resolution_info(parent->query_resolution_info),
      ARG_UPDATE_OPT(use_post_grouping_columns),
      ARG_UPDATE(top_level_ast_expr),
      column_alias(!options.column_alias.empty() ? options.column_alias
                                                 : parent->column_alias),
      ARG_UPDATE_OPT(allows_horizontal_aggregation),
      horizontal_aggregation_info(parent->horizontal_aggregation_info),
      in_horizontal_aggregation(parent->in_horizontal_aggregation),
      ARG_UPDATE_OPT(in_match_recognize_define),
      ARG_UPDATE(nearest_enclosing_physical_nav_op) {
  // This constructor can only be used to switch the name scope to the parent's
  // aggregate or analytic scope, not to introduce a new scope,
  // unless allow_new_scopes is set.
  ABSL_DCHECK(options.allow_new_scopes ||
         name_scope == parent->aggregate_name_scope ||
         name_scope == parent->analytic_name_scope ||
         name_scope == parent->name_scope)
      << "Setting new NameScape in child ExprResolutionInfo not allowed "
         "by default";
  Subscribe(name_scope);
}

ExprResolutionInfo::ExprResolutionInfo(
    ExprResolutionInfo* parent, QueryResolutionInfo* new_query_resolution_info,
    ExprResolutionInfoOptions options)
    : parent(parent),
      ARG_UPDATE(name_scope),
      aggregate_name_scope(options.aggregate_name_scope
                               ? options.aggregate_name_scope
                               : parent->aggregate_name_scope),
      analytic_name_scope(options.analytic_name_scope
                              ? options.analytic_name_scope
                              : parent->analytic_name_scope),
      ARG_UPDATE_OPT(allows_aggregation),
      ARG_UPDATE_OPT(allows_analytic),
      ARG_UPDATE(is_graph_measure_expression),
      ARG_UPDATE(clause_name),
      query_resolution_info(new_query_resolution_info),
      ARG_UPDATE_OPT(use_post_grouping_columns),
      ARG_UPDATE(top_level_ast_expr),
      column_alias(!options.column_alias.empty() ? options.column_alias
                                                 : parent->column_alias),
      ARG_UPDATE_OPT(allows_horizontal_aggregation),
      horizontal_aggregation_info(parent->horizontal_aggregation_info),
      in_horizontal_aggregation(parent->in_horizontal_aggregation),
      ARG_UPDATE_OPT(in_match_recognize_define),
      ARG_UPDATE(nearest_enclosing_physical_nav_op) {
  // This constructor can only be used to switch the name scope to the parent's
  // aggregate or analytic scope, not to introduce a new scope,
  // unless allow_new_scopes is set.
  ABSL_DCHECK(options.allow_new_scopes ||
         name_scope == parent->aggregate_name_scope ||
         name_scope == parent->analytic_name_scope ||
         name_scope == parent->name_scope)
      << "Setting new NameScape in child ExprResolutionInfo not allowed "
         "by default";
  ABSL_DCHECK(parent->query_resolution_info != nullptr);
  ABSL_DCHECK(parent->query_resolution_info->scoped_aggregation_state() ==
         query_resolution_info->scoped_aggregation_state());
  Subscribe(name_scope);
}

std::unique_ptr<ExprResolutionInfo>
ExprResolutionInfo::MakeChildForMultiLevelAggregation(
    ExprResolutionInfo* parent, QueryResolutionInfo* new_query_resolution_info,
    const NameScope* post_grouping_name_scope) {
  return absl::WrapUnique(new ExprResolutionInfo(
      parent, new_query_resolution_info,
      ExprResolutionInfoOptions{.name_scope = post_grouping_name_scope,
                                .allow_new_scopes = true,
                                .allows_aggregation = true,
                                .allows_analytic = false,
                                .use_post_grouping_columns = false,
                                .clause_name = "multi-level aggregate"}));
}

#undef ARG_UPDATE
#undef ARG_AND

// Keep this version that avoids going through Options for efficiency,
// since this is used frequently.
ExprResolutionInfo::ExprResolutionInfo(ExprResolutionInfo* parent)
    : parent(parent),
      name_scope(parent->name_scope),
      aggregate_name_scope(parent->aggregate_name_scope),
      analytic_name_scope(parent->analytic_name_scope),
      allows_aggregation(parent->allows_aggregation),
      allows_analytic(parent->allows_analytic),
      is_graph_measure_expression(parent->is_graph_measure_expression),
      clause_name(parent->clause_name),
      query_resolution_info(parent->query_resolution_info),
      use_post_grouping_columns(parent->use_post_grouping_columns),
      grouping_context(parent->grouping_context),
      top_level_ast_expr(parent->top_level_ast_expr),
      column_alias(parent->column_alias),
      allows_horizontal_aggregation(parent->allows_horizontal_aggregation),
      horizontal_aggregation_info(parent->horizontal_aggregation_info),
      in_horizontal_aggregation(parent->in_horizontal_aggregation),
      in_match_recognize_define(parent->in_match_recognize_define),
      nearest_enclosing_physical_nav_op(
          parent->nearest_enclosing_physical_nav_op) {
  Subscribe(name_scope);
}

ExprResolutionInfo::ExprResolutionInfo(
    const NameScope* name_scope_in, const NameScope* aggregate_name_scope_in,
    const NameScope* analytic_name_scope_in, bool allows_aggregation_in,
    bool allows_analytic_in, bool use_post_grouping_columns_in,
    const char* clause_name_in, QueryResolutionInfo* query_resolution_info_in,
    const ASTExpression* top_level_ast_expr_in, IdString column_alias_in)
    : ExprResolutionInfo(
          query_resolution_info_in, name_scope_in, aggregate_name_scope_in,
          analytic_name_scope_in,
          {.allows_aggregation = allows_aggregation_in,
           .allows_analytic = allows_analytic_in,
           .use_post_grouping_columns = use_post_grouping_columns_in,
           .clause_name = clause_name_in,
           .top_level_ast_expr = top_level_ast_expr_in,
           .column_alias = column_alias_in}) {
  ABSL_DCHECK(clause_name != nullptr);
}

ExprResolutionInfo::ExprResolutionInfo(const NameScope* name_scope_in,
                                       const char* clause_name_in)
    : name_scope(name_scope_in),
      aggregate_name_scope(name_scope_in),
      analytic_name_scope(name_scope_in),
      clause_name(clause_name_in) {
  ABSL_DCHECK(clause_name != nullptr);
  Subscribe(name_scope);
}

ExprResolutionInfo::~ExprResolutionInfo() {
  // Propagate has_aggregation and has_analytic up the tree to the caller.
  // We assume all child ExprResolutionInfo objects will go out of scope
  // before the caller's has_ fields are examined.
  if (parent != nullptr) {
    if (findings.has_aggregation) {
      parent->findings.has_aggregation = true;
    }
    if (findings.has_analytic) {
      parent->findings.has_analytic = true;
    }
    if (findings.has_volatile) {
      parent->findings.has_volatile = true;
    }
    if (allows_horizontal_aggregation) {
      parent->horizontal_aggregation_info = horizontal_aggregation_info;
    }
    for (SelectColumnState* column : columns_referenced_laterally) {
      parent->columns_referenced_laterally.push_back(column);
    }
  }
  Unsubscribe(name_scope);
}

bool ExprResolutionInfo::is_post_distinct() const {
  if (query_resolution_info != nullptr) {
    return query_resolution_info->is_post_distinct();
  }
  return false;
}

WithModifierMode ExprResolutionInfo::GetWithModifierMode() const {
  return query_resolution_info == nullptr
             ? WithModifierMode::NONE
             : query_resolution_info->with_modifier_mode();
}

std::string ExprResolutionInfo::DebugString() const {
  std::string debugstring;
  absl::StrAppend(&debugstring, "\nname_scope: ",
                  (name_scope != nullptr ? name_scope->DebugString() : "NULL"));
  absl::StrAppend(
      &debugstring, "\naggregate_name_scope: ",
      (aggregate_name_scope != nullptr ? aggregate_name_scope->DebugString()
                                       : "NULL"));
  absl::StrAppend(&debugstring, "\nallows_aggregation: ", allows_aggregation);
  absl::StrAppend(&debugstring,
                  "\nhas_aggregation: ", findings.has_aggregation);
  absl::StrAppend(&debugstring, "\nallows_analytic: ", allows_analytic);
  absl::StrAppend(&debugstring, "\nhas_analytic: ", findings.has_analytic);
  absl::StrAppend(&debugstring, "\nhas_volatile: ", findings.has_volatile);
  absl::StrAppend(&debugstring, "\nclause_name: ", clause_name);
  absl::StrAppend(&debugstring,
                  "\nuse_post_grouping_columns: ", use_post_grouping_columns);
  absl::StrAppend(&debugstring, "\ngrouping_context: ", grouping_context);
  absl::StrAppend(&debugstring, "\nallows_horizontal_aggregation: ",
                  allows_horizontal_aggregation);
  absl::StrAppend(&debugstring,
                  "\nin_horizontal_aggregation: ", in_horizontal_aggregation);
  absl::StrAppend(&debugstring,
                  "\nin_match_recognize_define: ", in_match_recognize_define);
  absl::StrAppend(&debugstring, "\nnearest_enclosing_physical_nav_op: ",
                  nearest_enclosing_physical_nav_op
                      ? nearest_enclosing_physical_nav_op->DebugString()
                      : "NULL");
  if (horizontal_aggregation_info.has_value()) {
    absl::StrAppend(
        &debugstring,
        "\nhorizontal_aggregation_array_and_element_column: array: ",
        horizontal_aggregation_info->array.DebugString(),
        ", element: ", horizontal_aggregation_info->element.DebugString());
  }
  absl::StrAppend(
      &debugstring, "\nQueryResolutionInfo:\n",
      (query_resolution_info != nullptr ? query_resolution_info->DebugString()
                                        : "NULL"));
  return debugstring;
}

IdString GetAliasForExpression(const ASTNode* node) {
  if (node->node_kind() == AST_IDENTIFIER) {
    return node->GetAsOrDie<ASTIdentifier>()->GetAsIdString();
  } else if (node->node_kind() == AST_PATH_EXPRESSION) {
    return node->GetAsOrDie<ASTPathExpression>()->last_name()->GetAsIdString();
  } else if (node->node_kind() == AST_DOT_IDENTIFIER) {
    return node->GetAsOrDie<ASTDotIdentifier>()->name()->GetAsIdString();
  } else {
    return IdString();
  }
}

bool IsNamedLambda(const ASTNode* node) {
  if (node == nullptr) {
    return false;
  }
  if (!node->Is<ASTNamedArgument>()) {
    return false;
  }
  return node->GetAsOrDie<ASTNamedArgument>()->expr()->Is<ASTLambda>();
}

}  // namespace googlesql
