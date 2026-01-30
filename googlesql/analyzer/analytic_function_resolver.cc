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

#include "googlesql/analyzer/analytic_function_resolver.h"

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/analyzer/expr_resolver_helper.h"
#include "googlesql/analyzer/query_resolver_helper.h"
#include "googlesql/analyzer/resolver.h"
#include "googlesql/common/thread_stack.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parse_tree_errors.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/annotation/collation.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "googlesql/resolved_ast/resolved_collation.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

STATIC_IDSTRING(kAnalyticId, "$analytic");
STATIC_IDSTRING(kPartitionById, "$partitionby");
STATIC_IDSTRING(kOrderById, "$orderby");
STATIC_IDSTRING(kMatchRecognizeId, "$match_recognize");

#define RETURN_ERROR_IF_OUT_OF_STACK_SPACE()                      \
  GOOGLESQL_RETURN_IF_NOT_ENOUGH_STACK(                           \
      "Out of stack space due to deeply nested query expression " \
      "during query resolution")

struct AnalyticFunctionResolver::AnalyticFunctionInfo {
  AnalyticFunctionInfo(
      const ASTAnalyticFunctionCall* ast_analytic_function_call_in,
      const ASTPartitionBy* ast_partition_by_in,
      const ASTOrderBy* ast_order_by_in,
      const ResolvedAnalyticFunctionCall* resolved_analytic_function_call_in,
      const WindowExprInfoList* partition_by_info_in,
      const WindowExprInfoList* order_by_info_in)
      : ast_analytic_function_call(ast_analytic_function_call_in),
        ast_partition_by(ast_partition_by_in),
        ast_order_by(ast_order_by_in),
        resolved_analytic_function_call(resolved_analytic_function_call_in),
        partition_by_info(partition_by_info_in),
        order_by_info(order_by_info_in) {}

  // All pointers are not owned.

  const ASTAnalyticFunctionCall* const ast_analytic_function_call;
  const ASTPartitionBy* const ast_partition_by;
  const ASTOrderBy* const ast_order_by;

  const ResolvedAnalyticFunctionCall* resolved_analytic_function_call;
  const WindowExprInfoList* partition_by_info;
  const WindowExprInfoList* order_by_info;

  // Copyable.
};

struct AnalyticFunctionResolver::WindowExprInfo {
 public:
  // Construct a WindowExprInfo for an alias reference to the 0-based
  // <select_list_index_in>-th SELECT-list column of type <select_column_type>.
  WindowExprInfo(const ASTNode* ast_location_in, int select_list_index_in,
                 const Type* select_column_type)
      : ast_location(ast_location_in),
        select_list_index(select_list_index_in),
        type(select_column_type) {}

  // Construct a WindowExprInfo for an expression that is not an ordinal or
  // alias reference.
  WindowExprInfo(const ASTNode* ast_location_in,
                 const ResolvedExpr* resolved_expr_in)
      : ast_location(ast_location_in), select_list_index(-1),
        resolved_expr(resolved_expr_in), type(resolved_expr->type()) {}

  WindowExprInfo(const WindowExprInfo&) = delete;
  WindowExprInfo& operator=(const WindowExprInfo&) = delete;

  const ASTNode* ast_location;

  // 0-based index into select list. -1 if the expression is not an alias or
  // ordinal reference to a SELECT-list column.
  const int select_list_index;

  // NULL if the expression is an alias or ordinal reference to a SELECT-list
  // column.
  std::unique_ptr<const ResolvedExpr> resolved_expr;

  // Type of this expression.
  const Type* type;

  std::unique_ptr<const ResolvedColumnRef> resolved_column_ref;
};

AnalyticFunctionResolver::AnalyticFunctionResolver(
    Resolver* resolver, NamedWindowInfoMap* named_window_info_map)
    : resolver_(resolver) {
  if (named_window_info_map != nullptr) {
    named_window_info_map_.reset(named_window_info_map);
  } else {
    named_window_info_map_ = std::make_unique<NamedWindowInfoMap>();
  }
}

AnalyticFunctionResolver::~AnalyticFunctionResolver() {
  if (is_create_analytic_scan_successful_) {
    ABSL_DCHECK(window_columns_to_compute_.empty())
        << "Output columns for window expressions have not been attached to "
           "the tree";
  }
}

absl::Status AnalyticFunctionResolver::SetWindowClause(
    const ASTWindowClause& window_clause) {
  GOOGLESQL_RET_CHECK(named_window_info_map_->empty());
  const absl::Span<const ASTWindowDefinition* const>& named_windows =
      window_clause.windows();

  for (const ASTWindowDefinition* named_window : named_windows) {
    const std::string named_window_name =
        absl::AsciiStrToLower(named_window->name()->GetAsString());
    if (named_window_info_map_->contains(named_window_name)) {
      return MakeSqlErrorAt(named_window)
             << "Duplicate window alias "
             << named_window->name()->GetAsString();
    }

    // A named window can only reference a preceding window which has been
    // flattened, so we can determine all the clauses right here.
    auto flattened_window_info =
        std::make_unique<FlattenedWindowInfo>(named_window->window_spec());
    GOOGLESQL_RETURN_IF_ERROR(ExtractWindowInfoFromReferencedWindow(
        flattened_window_info.get()));
    (*named_window_info_map_)[named_window_name] =
        std::move(flattened_window_info);
  }
  return absl::OkStatus();
}

AnalyticFunctionResolver::NamedWindowInfoMap*
    AnalyticFunctionResolver::ReleaseNamedWindowInfoMap() {
  return named_window_info_map_.release();
}

void AnalyticFunctionResolver::DisableNamedWindowRefs(
    const char* clause_name) {
  ABSL_CHECK_NE(clause_name[0], '\0');
  named_window_not_allowed_here_name_ = clause_name;
}

absl::Status AnalyticFunctionResolver::SetMatchRecognizeWindowContext(
    const ASTPartitionBy* ast_partition_by, const ASTOrderBy* ast_order_by,
    ExprResolutionInfo* expr_resolution_info,
    std::vector<ResolvedColumn>& out_partitioning_columns) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  GOOGLESQL_RET_CHECK(analytic_function_groups_.empty());
  GOOGLESQL_RET_CHECK(!in_match_recognize_window_context_);
  in_match_recognize_window_context_ = true;

  WindowExprInfoList* partition_by_info = nullptr;  // Not owned.
  if (ast_partition_by != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveWindowPartitionByPreAggregation(
        ast_partition_by, expr_resolution_info, /*allow_ordinals=*/false,
        "MATCH_RECOGNIZE PARTITION BY clause", &partition_by_info));

    for (std::unique_ptr<WindowExprInfo>& partitioning_expr_info :
         *partition_by_info) {
      // Since a window PARTITION BY may be shared by multiple analytic
      // functions, do not create a new column if we have created one for this
      // partitioning expressions.
      GOOGLESQL_RET_CHECK(partitioning_expr_info->resolved_column_ref == nullptr);
      GOOGLESQL_RETURN_IF_ERROR(AddColumnForWindowExpression(
          kPartitionById,
          resolver_->MakeIdString(
              absl::StrCat("$partitionbycol", ++num_partitioning_exprs_)),
          /*force_rename_columns=*/true,
          expr_resolution_info->query_resolution_info,
          partitioning_expr_info.get()));

      GOOGLESQL_RET_CHECK(partitioning_expr_info->resolved_column_ref != nullptr);
      out_partitioning_columns.push_back(
          partitioning_expr_info->resolved_column_ref->column());
    }
  }

  // ORDER BY
  WindowExprInfoList* order_by_info = nullptr;  // Not owned.
  GOOGLESQL_RETURN_IF_ERROR(ResolveWindowOrderByPreAggregation(
      ast_order_by, /*is_in_range_window=*/false, expr_resolution_info,
      /*allow_ordinals=*/false, "MATCH_RECOGNIZE ORDER BY clause",
      &order_by_info));

  auto group_info = std::make_unique<AnalyticFunctionGroupInfo>(
      ast_partition_by, ast_order_by);

  // Do not place this group in the map, since it's only grouped with function
  // calls called in its context (e.g. in the DEFINE clause) and share its
  // window.
  analytic_function_groups_.emplace_back(std::move(group_info));

  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
AnalyticFunctionResolver::AddToMatchRecognizeMainAnalyticGroup(
    const ASTFunctionCall* ast_function_call,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedFunctionCall> resolved_call,
    std::unique_ptr<const ResolvedWindowFrame> resolved_window_frame) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  GOOGLESQL_RET_CHECK(in_match_recognize_window_context_);

  GOOGLESQL_RET_CHECK(!analytic_function_groups_.empty());
  AnalyticFunctionGroupInfo* group_info =
      analytic_function_groups_.front().get();

  if (resolved_call->function()->SupportsWindowFraming() &&
      resolved_window_frame == nullptr) {
    GOOGLESQL_RET_CHECK(group_info->ast_order_by != nullptr);
    GOOGLESQL_ASSIGN_OR_RETURN(
        resolved_window_frame,
        ResolvedWindowFrameBuilder()
            .set_frame_unit(ResolvedWindowFrame::RANGE)
            .set_start_expr(
                ResolvedWindowFrameExprBuilder()
                    .set_boundary_type(
                        ResolvedWindowFrameExpr::UNBOUNDED_PRECEDING)
                    .set_expression(nullptr))
            .set_end_expr(
                ResolvedWindowFrameExprBuilder()
                    .set_boundary_type(ResolvedWindowFrameExpr::CURRENT_ROW)
                    .set_expression(nullptr))
            .Build());
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedAnalyticFunctionCall>
          resolved_analytic_function_call,
      FinalizeResolvedAnalyticFunctionCall(
          ast_function_call, ast_function_call, expr_resolution_info,
          std::move(resolved_call), /*resolved_where_expr=*/nullptr,
          std::move(resolved_window_frame)));

  const ResolvedColumn resolved_column(
      resolver_->AllocateColumnId(), kAnalyticId,
      resolver_->MakeIdString(
          absl::StrCat("$analytic", num_analytic_functions_)),
      resolved_analytic_function_call->annotated_type());

  group_info->resolved_computed_columns.emplace_back(MakeResolvedComputedColumn(
      resolved_column, std::move(resolved_analytic_function_call)));

  // Set the output resolved expression to be a column reference.
  return resolver_->MakeColumnRef(resolved_column);
}

absl::Status AnalyticFunctionResolver::ResolveOverClauseAndCreateAnalyticColumn(
    const ASTAnalyticFunctionCall* ast_analytic_function_call,
    std::unique_ptr<const ResolvedFunctionCall> resolved_function_call,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  GOOGLESQL_RET_CHECK(resolved_function_call->function() != nullptr);

  // WHERE clause resolution
  std::unique_ptr<const ResolvedExpr> resolved_where_expr;
  GOOGLESQL_RETURN_IF_ERROR(resolver_->ResolveWhereModifier(
      ast_analytic_function_call->function(), resolved_function_call.get(),
      expr_resolution_info->analytic_name_scope, &resolved_where_expr));

  const ASTWindowSpecification* over_clause_window_spec =
      ast_analytic_function_call->window_spec();
  auto flattened_window_info =
      std::make_unique<FlattenedWindowInfo>(over_clause_window_spec);

  if (over_clause_window_spec->base_window_name() != nullptr &&
      named_window_not_allowed_here_name_ != nullptr) {
    return MakeSqlErrorAt(over_clause_window_spec->base_window_name())
           << "Cannot reference a named window in "
           << named_window_not_allowed_here_name_;
  }

  GOOGLESQL_RETURN_IF_ERROR(ExtractWindowInfoFromReferencedWindow(
      flattened_window_info.get()));

  const ASTPartitionBy* ast_partition_by =
      flattened_window_info->ast_partition_by;
  const ASTOrderBy* ast_order_by =
      flattened_window_info->ast_order_by;
  const ASTWindowFrame* ast_window_frame =
      flattened_window_info->ast_window_frame;
  const ASTWindowSpecification* ast_grouping_window_spec =
      flattened_window_info->ast_grouping_window_spec;

  // Validate the analytic and DISTINCT support.
  GOOGLESQL_RETURN_IF_ERROR(CheckWindowSupport(resolved_function_call.get(),
                                     ast_analytic_function_call, ast_order_by,
                                     ast_window_frame));

  // Resolve PARTITION BY and ORDER BY.
  auto over_expr_resolution_info = std::make_unique<ExprResolutionInfo>(
      expr_resolution_info,
      ExprResolutionInfoOptions{.allows_analytic = false});
  WindowExprInfoList* partition_by_info = nullptr;  // Not owned.
  if (ast_partition_by != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveWindowPartitionByPreAggregation(
        ast_partition_by, over_expr_resolution_info.get(),
        /*allow_ordinals=*/true, "PARTITION BY", &partition_by_info));
  }

  WindowExprInfoList* order_by_info = nullptr;  // Not owned.
  const bool supports_framing =
      resolved_function_call->function()->SupportsWindowFraming();
  if (ast_order_by != nullptr) {
    bool is_in_range_window;
    if ((ast_window_frame != nullptr &&
         ast_window_frame->frame_unit() == ASTWindowFrame::RANGE) ||
        (ast_window_frame == nullptr && supports_framing)) {
      // A RANGE window clause is specified or implied.
      is_in_range_window = true;
    } else {
      is_in_range_window = false;
    }

    GOOGLESQL_RETURN_IF_ERROR(ResolveWindowOrderByPreAggregation(
        ast_order_by, is_in_range_window, over_expr_resolution_info.get(),
        /*allow_ordinals=*/true, "Window ORDER BY", &order_by_info));
  }

  // Resolve window frame.
  std::unique_ptr<const ResolvedWindowFrame> resolved_window_frame;
  if (ast_window_frame != nullptr) {
    const Type* ordering_expr_type = nullptr;
    if (ast_window_frame->frame_unit() == ASTWindowFrame::RANGE) {
      GOOGLESQL_RETURN_IF_ERROR(ValidateOrderByInRangeBasedWindow(
          ast_order_by, ast_window_frame, order_by_info));
      if (ast_order_by != nullptr) {
        ordering_expr_type = order_by_info->back()->type;
      }
    }
    GOOGLESQL_RETURN_IF_ERROR(ResolveWindowFrame(ast_window_frame, ordering_expr_type,
                                       over_expr_resolution_info.get(),
                                       &resolved_window_frame));
  }

  // Generate an implicit window frame if it is not given but is allowed.
  if (resolved_window_frame == nullptr && supports_framing) {
    if (ast_order_by != nullptr) {
      resolved_window_frame = MakeResolvedWindowFrame(
          ResolvedWindowFrame::RANGE,
          MakeResolvedWindowFrameExpr(
              ResolvedWindowFrameExpr::UNBOUNDED_PRECEDING, nullptr),
          MakeResolvedWindowFrameExpr(
              ResolvedWindowFrameExpr::CURRENT_ROW, nullptr));
    } else {
      resolved_window_frame = MakeResolvedWindowFrame(
          ResolvedWindowFrame::ROWS,
          MakeResolvedWindowFrameExpr(
              ResolvedWindowFrameExpr::UNBOUNDED_PRECEDING, nullptr),
          MakeResolvedWindowFrameExpr(
              ResolvedWindowFrameExpr::UNBOUNDED_FOLLOWING, nullptr));
    }
  }

  if (in_match_recognize_window_context_) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        *resolved_expr_out,
        AddToMatchRecognizeMainAnalyticGroup(
            ast_analytic_function_call->function(), expr_resolution_info,
            std::move(resolved_function_call),
            std::move(resolved_window_frame)));
    return absl::OkStatus();
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedAnalyticFunctionCall>
          resolved_analytic_function_call,
      FinalizeResolvedAnalyticFunctionCall(
          ast_analytic_function_call, ast_analytic_function_call->function(),
          expr_resolution_info, std::move(resolved_function_call),
          std::move(resolved_where_expr), std::move(resolved_window_frame)));

  // Create a ResolvedColumn. If this call is a top-level SELECT column and has
  // an alias, use that alias.
  IdString alias = resolver_->GetColumnAliasForTopLevelExpression(
      expr_resolution_info, ast_analytic_function_call);
  if (alias.empty() ||
      !resolver_->analyzer_options_.preserve_column_aliases()) {
    alias = resolver_->MakeIdString(
        absl::StrCat("$analytic", num_analytic_functions_));
  }
  const ResolvedColumn resolved_column(
      resolver_->AllocateColumnId(), kAnalyticId, alias,
      resolved_analytic_function_call->annotated_type());

  GOOGLESQL_RET_CHECK(googlesql_base::InsertIfNotPresent(
      &column_to_analytic_function_map_, resolved_column,
      AnalyticFunctionInfo(ast_analytic_function_call, ast_partition_by,
                           ast_order_by, resolved_analytic_function_call.get(),
                           partition_by_info, order_by_info)));

  // Update the analytic function group.
  AnalyticFunctionGroupInfo** const found_group_info = googlesql_base::FindOrNull(
      ast_window_spec_to_function_group_map_, ast_grouping_window_spec);
  AnalyticFunctionGroupInfo* group_info;
  if (found_group_info == nullptr) {
    auto new_group_info = std::make_unique<AnalyticFunctionGroupInfo>(
        ast_partition_by, ast_order_by);
    group_info = new_group_info.get();
    googlesql_base::InsertOrDie(&ast_window_spec_to_function_group_map_,
                     ast_grouping_window_spec, new_group_info.get());
    analytic_function_groups_.emplace_back(new_group_info.release());
  } else {
    group_info = *found_group_info;
  }
  group_info->resolved_computed_columns.emplace_back(
      MakeResolvedComputedColumn(
          resolved_column, std::move(resolved_analytic_function_call)));

  // Set the output resolved expression to be a column reference.
  *resolved_expr_out = resolver_->MakeColumnRef(resolved_column);

  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<const ResolvedAnalyticFunctionCall>>
AnalyticFunctionResolver::FinalizeResolvedAnalyticFunctionCall(
    const ASTNode* ast_location, const ASTFunctionCall* ast_function_call,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedFunctionCall> resolved_function_call,
    std::unique_ptr<const ResolvedExpr> resolved_where_expr,
    std::unique_ptr<const ResolvedWindowFrame> resolved_window_frame) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  ++num_analytic_functions_;

  ResolvedNonScalarFunctionCallBase::NullHandlingModifier
      resolved_null_handling_modifier_kind =
          resolver_->ResolveNullHandlingModifier(
              ast_function_call->null_handling_modifier());
  if (resolved_null_handling_modifier_kind !=
      ResolvedNonScalarFunctionCallBase::DEFAULT_NULL_HANDLING) {
    if (!resolver_->analyzer_options_.language().LanguageFeatureEnabled(
            FEATURE_NULL_HANDLING_MODIFIER_IN_ANALYTIC)) {
      return MakeSqlErrorAt(ast_location)
             << "IGNORE NULLS and RESPECT NULLS in analytic functions are not "
                "supported";
    }
    if (!resolved_function_call->function()->SupportsNullHandlingModifier()) {
      return MakeSqlErrorAt(ast_location)
             << "IGNORE NULLS and RESPECT NULLS are not allowed for analytic "
                "function "
             << resolved_function_call->function()->SQLName();
    }
  }

  auto builder = ToBuilder(std::move(resolved_function_call));

  std::unique_ptr<ResolvedAnalyticFunctionCall> resolved_analytic_function_call;
  const bool is_distinct = ast_function_call->distinct();
  resolved_analytic_function_call = MakeResolvedAnalyticFunctionCall(
      builder.type(), builder.function(), builder.signature(),
      builder.release_argument_list(), builder.release_generic_argument_list(),
      builder.error_mode(), is_distinct, resolved_null_handling_modifier_kind,
      std::move(resolved_where_expr), std::move(resolved_window_frame));

  if (ast_location->Is<ASTAnalyticFunctionCall>()) {
    resolver_->MaybeRecordAnalyticFunctionCallParseLocation(
        ast_location->GetAsOrDie<ASTAnalyticFunctionCall>(),
        resolved_analytic_function_call.get());
  } else {
    resolver_->MaybeRecordParseLocation(ast_location,
                                        resolved_analytic_function_call.get());
  }
  GOOGLESQL_RETURN_IF_ERROR(resolver_->MaybeResolveCollationForFunctionCallBase(
      /*error_location=*/ast_location, resolved_analytic_function_call.get()));
  GOOGLESQL_RETURN_IF_ERROR(resolver_->CheckAndPropagateAnnotations(
      /*error_node=*/ast_location, resolved_analytic_function_call.get()));

  return resolved_analytic_function_call;
}

absl::Status AnalyticFunctionResolver::CheckWindowSupport(
    const ResolvedFunctionCall* resolved_function_call,
    const ASTAnalyticFunctionCall* ast_function_call,
    const ASTOrderBy* ast_order_by,
    const ASTWindowFrame* ast_window_frame) const {
  const Function* function = resolved_function_call->function();

  // General window support validations.
  if (ast_order_by == nullptr) {
    if (function->RequiresWindowOrdering()) {
      return MakeSqlErrorAt(ast_function_call->window_spec())
             << "Window ORDER BY is required for analytic function "
             << function->Name();
    }
  } else {
    if (!function->SupportsWindowOrdering()) {
      return MakeSqlErrorAt(ast_order_by)
             << "Window ORDER BY is not allowed for analytic function "
             << function->Name();
    }
  }
  if (ast_window_frame != nullptr) {
    if (!function->SupportsWindowFraming()) {
      return MakeSqlErrorAt(ast_window_frame)
             << "Window framing clause is not allowed for analytic function "
             << function->Name();
    }
  }

  // Window support validations in presence of DISTINCT. DISTINCT analytic
  // functions cannot have ORDER BY nor window frame in the SQL standard.
  if (ast_function_call->function()->distinct()) {
    if (resolved_function_call->argument_list().empty()) {
      return MakeSqlErrorAt(ast_function_call)
             << "DISTINCT function call with no arguments not possible";
    }
    for (const std::unique_ptr<const ResolvedExpr>& argument :
             resolved_function_call->argument_list()) {
      std::string no_grouping_type;
      if (!argument->type()->SupportsGrouping(resolver_->language(),
                                              &no_grouping_type)) {
        return MakeSqlErrorAt(ast_function_call)
               << "Analytic aggregate functions with DISTINCT cannot be used "
                  "with arguments of type "
               << no_grouping_type;
      }
    }
    if (!function->IsAggregate() || !function->SupportsDistinctModifier()) {
      return MakeSqlErrorAt(ast_function_call)
             << "DISTINCT is not allowed for analytic function "
             << function->Name();
    }
    if (ast_order_by != nullptr) {
      return MakeSqlErrorAt(ast_order_by)
             << "Window ORDER BY is not allowed if DISTINCT is specified";
    }
    if (ast_window_frame != nullptr) {
      return MakeSqlErrorAt(ast_window_frame)
             << "Window framing clause is not allowed if DISTINCT is specified";
    }
  }

  return absl::OkStatus();
}

absl::Status AnalyticFunctionResolver::ResolveWindowPartitionByPreAggregation(
    const ASTPartitionBy* ast_partition_by,
    ExprResolutionInfo* expr_resolution_info, bool allow_ordinals,
    const char* clause_name, WindowExprInfoList** partition_by_info_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  const std::unique_ptr<WindowExprInfoList>* existing_partition_by_info =
      googlesql_base::FindOrNull(ast_to_resolved_info_map_, ast_partition_by);
  if (existing_partition_by_info != nullptr) {
    *partition_by_info_out = existing_partition_by_info->get();
    return absl::OkStatus();
  }

  auto partition_by_info = std::make_unique<WindowExprInfoList>();
  for (const ASTExpression* ast_partition_expr :
       ast_partition_by->partitioning_expressions()) {
    auto partitioning_resolution_info = std::make_unique<ExprResolutionInfo>(
        expr_resolution_info,
        ExprResolutionInfoOptions{.clause_name = clause_name});
    std::unique_ptr<WindowExprInfo> partitioning_expr_info;
    const Type* partitioning_expr_type;

    GOOGLESQL_RETURN_IF_ERROR(ResolveWindowExpression(
        ast_partition_expr, partitioning_resolution_info.get(), allow_ordinals,
        &partitioning_expr_info, &partitioning_expr_type));
    partition_by_info->emplace_back(partitioning_expr_info.release());

    std::string no_partitioning_type;
    if (!partitioning_expr_type->SupportsPartitioning(resolver_->language(),
                                                      &no_partitioning_type)) {
      return MakeSqlErrorAt(ast_partition_expr)
             << "Partitioning by expressions of type "
             << no_partitioning_type
             << " is not allowed";
    }
  }

  *partition_by_info_out = partition_by_info.get();
  ast_to_resolved_info_map_[ast_partition_by] = std::move(partition_by_info);

  return absl::OkStatus();
}

absl::Status AnalyticFunctionResolver::ResolveWindowOrderByPreAggregation(
    const ASTOrderBy* ast_order_by, bool is_in_range_window,
    ExprResolutionInfo* expr_resolution_info, bool allow_ordinals,
    const char* clause_name, WindowExprInfoList** order_by_info_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  const std::unique_ptr<WindowExprInfoList>* existing_order_by_info =
      googlesql_base::FindOrNull(ast_to_resolved_info_map_, ast_order_by);
  if (existing_order_by_info != nullptr) {
    *order_by_info_out = existing_order_by_info->get();
    return absl::OkStatus();
  }

  auto order_by_info = std::make_unique<WindowExprInfoList>();
  for (const ASTOrderingExpression* ast_ordering_expr :
       ast_order_by->ordering_expressions()) {
    ResolvedColumn resolved_ordering_column;
    std::unique_ptr<WindowExprInfo> ordering_expr_info;
    const Type* ordering_expr_type = nullptr;

    auto ordering_resolution_info = std::make_unique<ExprResolutionInfo>(
        expr_resolution_info,
        ExprResolutionInfoOptions{.clause_name = clause_name});
    GOOGLESQL_RETURN_IF_ERROR(ResolveWindowExpression(
        ast_ordering_expr->expression(), ordering_resolution_info.get(),
        allow_ordinals, &ordering_expr_info, &ordering_expr_type));
    GOOGLESQL_RET_CHECK(ordering_expr_type != nullptr);
    order_by_info->emplace_back(ordering_expr_info.release());

    // Do not allow floating point order keys in a range window
    // if DISALLOW_GROUP_BY_FLOAT is enabled.
    if (is_in_range_window &&
        resolver_->language().LanguageFeatureEnabled(
            FEATURE_DISALLOW_GROUP_BY_FLOAT) &&
        ordering_expr_type->IsFloatingPoint()) {
      return MakeSqlErrorAt(ast_ordering_expr)
             << "Ordering by expressions of type "
             << Type::TypeKindToString(
                    ordering_expr_type->kind(),
                    resolver_->language().product_mode())
             << " is not allowed in a RANGE-based window";
    }

    std::string type_description;
    if (!ordering_expr_type->SupportsOrdering(resolver_->language(),
                                              &type_description)) {
      return MakeSqlErrorAt(ast_ordering_expr)
          << "Ordering by expressions of type " << type_description
          << " is not allowed";
    }
  }

  *order_by_info_out = order_by_info.get();
  ast_to_resolved_info_map_[ast_order_by] = std::move(order_by_info);

  return absl::OkStatus();
}

absl::Status AnalyticFunctionResolver::ResolveWindowExpression(
    const ASTExpression* ast_expr, ExprResolutionInfo* expr_resolution_info,
    bool allow_ordinals, std::unique_ptr<WindowExprInfo>* resolved_item_out,
    const Type** expr_type_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  std::unique_ptr<const ResolvedExpr> tmp_resolved_expr;
  GOOGLESQL_RETURN_IF_ERROR(resolver_->ResolveExpr(ast_expr, expr_resolution_info,
                                         &tmp_resolved_expr));

  if (tmp_resolved_expr->Is<ResolvedLiteral>() &&
      !tmp_resolved_expr->GetAs<ResolvedLiteral>()->has_explicit_type() &&
      tmp_resolved_expr->GetAs<ResolvedLiteral>()->value().type_kind() ==
          TYPE_INT64 &&
      !tmp_resolved_expr->GetAs<ResolvedLiteral>()->value().is_null()) {
    if (!allow_ordinals) {
      return MakeSqlErrorAt(ast_expr) << "Ordinals are not allowed in "
                                      << expr_resolution_info->clause_name;
    } else {
      GOOGLESQL_RETURN_IF_ERROR(resolver_->AddDeprecationWarning(
          ast_expr, DeprecationWarning::ORDINALS_IN_OVER_CLAUSE,
          "Ordinals in the order clause are deprecated"));
    }
  }

  *expr_type_out = tmp_resolved_expr->type();
  *resolved_item_out =
      std::make_unique<WindowExprInfo>(ast_expr, tmp_resolved_expr.release());

  return absl::OkStatus();
}

absl::Status AnalyticFunctionResolver::ValidateOrderByInRangeBasedWindow(
    const ASTOrderBy* ast_order_by, const ASTWindowFrame* ast_window_frame,
    WindowExprInfoList* order_by_info) {
  ABSL_DCHECK_EQ(ast_window_frame->frame_unit(), ASTWindowFrame::RANGE);
  if (order_by_info == nullptr) {
    if (ast_window_frame->start_expr()->boundary_type() ==
            ASTWindowFrameExpr::UNBOUNDED_PRECEDING &&
        ast_window_frame->end_expr() != nullptr &&
        ast_window_frame->end_expr()->boundary_type() ==
            ASTWindowFrameExpr::UNBOUNDED_FOLLOWING) {
      // As per the SQL standard, a RANGE based window without ORDER BY treats
      // all rows as peers, so each row's window frame is the entire partition.
      // Thus, to avoid confusion we only allow RANGE BETWEEN UNBOUNDED
      // PRECEDING AND UNBOUNDED FOLLOWING.
      return absl::OkStatus();
    }
    if (ast_window_frame->end_expr() == nullptr) {
      // The window has an implicit CURRENT ROW end boundary expression, so
      // provide an error.
      return MakeSqlErrorAt(ast_window_frame)
             << "A RANGE-based window without ORDER BY includes all rows in "
                "the window frame so only RANGE BETWEEN UNBOUNDED PRECEDING "
                "AND UNBOUNDED FOLLOWING syntax is supported";
    }
    if (ast_window_frame->start_expr()->boundary_type() ==
            ASTWindowFrameExpr::OFFSET_PRECEDING ||
        ast_window_frame->start_expr()->boundary_type() ==
            ASTWindowFrameExpr::OFFSET_FOLLOWING ||
        ast_window_frame->end_expr()->boundary_type() ==
            ASTWindowFrameExpr::OFFSET_PRECEDING ||
        ast_window_frame->end_expr()->boundary_type() ==
            ASTWindowFrameExpr::OFFSET_FOLLOWING) {
      return MakeSqlErrorAt(ast_window_frame)
             << "A RANGE-based window including an OFFSET PRECEDING or "
                "OFFSET FOLLOWING window frame boundary requires an ORDER BY";
    }
    return MakeSqlErrorAt(ast_window_frame)
           << "A RANGE-based window without ORDER BY cannot include a CURRENT "
              "ROW window frame boundary since all rows are included in the "
              "window frame. Remove the RANGE clause or use RANGE BETWEEN "
              "UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING";
  }
  GOOGLESQL_RET_CHECK(ast_window_frame->start_expr() != nullptr);
  if (ast_window_frame->start_expr()->expression() != nullptr ||
      (ast_window_frame->end_expr() != nullptr &&
       ast_window_frame->end_expr()->expression() != nullptr)) {
    if (order_by_info->size() != 1) {
      return MakeSqlErrorAt(ast_order_by)
             << "A RANGE-based window with OFFSET PRECEDING or OFFSET "
                "FOLLOWING boundaries must have exactly one ORDER BY key";
    }
    const Type* ordering_expr_type = order_by_info->back()->type;
    if (!ordering_expr_type->IsNumerical()) {
      return MakeSqlErrorAt(ast_order_by)
             << "ORDER BY key must be numeric in a RANGE-based window with "
                "OFFSET PRECEDING or OFFSET FOLLOWING boundaries, but has type "
             << Type::TypeKindToString(
                    ordering_expr_type->kind(),
                    resolver_->language().product_mode());
    }
  }
  return absl::OkStatus();
}

absl::Status AnalyticFunctionResolver::ResolveWindowFrame(
    const ASTWindowFrame* ast_window_frame, const Type* target_expr_type,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedWindowFrame>* resolved_window_frame_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  ResolvedWindowFrame::FrameUnit frame_unit;
  GOOGLESQL_RETURN_IF_ERROR(ResolveWindowFrameUnit(ast_window_frame, &frame_unit));

  GOOGLESQL_RET_CHECK(ast_window_frame->start_expr() != nullptr);
  std::unique_ptr<const ResolvedWindowFrameExpr> start_window_frame_expr;
  GOOGLESQL_RETURN_IF_ERROR(ResolveWindowFrameExpr(
      ast_window_frame->start_expr(), frame_unit, target_expr_type,
      expr_resolution_info, &start_window_frame_expr));

  std::unique_ptr<const ResolvedWindowFrameExpr> end_window_frame_expr;
  if (ast_window_frame->end_expr() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveWindowFrameExpr(
        ast_window_frame->end_expr(), frame_unit, target_expr_type,
        expr_resolution_info, &end_window_frame_expr));
  } else {
    // Implicit ending frame boundary.
    end_window_frame_expr = MakeResolvedWindowFrameExpr(
        ResolvedWindowFrameExpr::CURRENT_ROW, nullptr);
  }

  std::unique_ptr<ResolvedWindowFrame> resolved_window_frame =
      MakeResolvedWindowFrame(frame_unit, std::move(start_window_frame_expr),
                              std::move(end_window_frame_expr));
  resolver_->MaybeRecordParseLocation(ast_window_frame,
                                      resolved_window_frame.get());
  *resolved_window_frame_out = std::move(resolved_window_frame);

  return ValidateWindowFrameSize(ast_window_frame,
                                 resolved_window_frame_out->get());
}

absl::Status AnalyticFunctionResolver::ResolveWindowFrameUnit(
    const ASTWindowFrame* ast_window_frame,
    ResolvedWindowFrame::FrameUnit* resolved_unit) const {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  switch (ast_window_frame->frame_unit()) {
    case ASTWindowFrame::ROWS:
      *resolved_unit = ResolvedWindowFrame::ROWS;
      break;
    case ASTWindowFrame::RANGE:
      *resolved_unit = ResolvedWindowFrame::RANGE;
      break;
  }
  return absl::OkStatus();
}

absl::Status AnalyticFunctionResolver::ResolveWindowFrameExpr(
    const ASTWindowFrameExpr* ast_frame_expr,
    const ResolvedWindowFrame::FrameUnit frame_unit,
    const Type* target_expr_type, ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedWindowFrameExpr>*
        resolved_window_frame_expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  // Only OFFSET_PRECEDING and OFFSET_FOLLOWING boundaries have a non-NULL
  // expression in the parse tree.
  std::unique_ptr<const ResolvedExpr> resolved_expr;
  if (ast_frame_expr->expression() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveWindowFrameOffsetExpr(
        ast_frame_expr, frame_unit, target_expr_type, expr_resolution_info,
        &resolved_expr));
  }

  std::unique_ptr<ResolvedWindowFrameExpr> resolved_window_frame_expr;
  switch (ast_frame_expr->boundary_type()) {
    case ASTWindowFrameExpr::UNBOUNDED_PRECEDING:
      GOOGLESQL_RET_CHECK(resolved_expr == nullptr);
      resolved_window_frame_expr = MakeResolvedWindowFrameExpr(
          ResolvedWindowFrameExpr::UNBOUNDED_PRECEDING, nullptr);
      break;
    case ASTWindowFrameExpr::OFFSET_PRECEDING:
      GOOGLESQL_RET_CHECK(resolved_expr != nullptr);
      resolved_window_frame_expr = MakeResolvedWindowFrameExpr(
          ResolvedWindowFrameExpr::OFFSET_PRECEDING, std::move(resolved_expr));
      break;
    case ASTWindowFrameExpr::CURRENT_ROW:
      GOOGLESQL_RET_CHECK(resolved_expr == nullptr);
      resolved_window_frame_expr = MakeResolvedWindowFrameExpr(
          ResolvedWindowFrameExpr::CURRENT_ROW, nullptr);
      break;
    case ASTWindowFrameExpr::OFFSET_FOLLOWING:
      GOOGLESQL_RET_CHECK(resolved_expr != nullptr);
      resolved_window_frame_expr = MakeResolvedWindowFrameExpr(
          ResolvedWindowFrameExpr::OFFSET_FOLLOWING, std::move(resolved_expr));
      break;
    case ASTWindowFrameExpr::UNBOUNDED_FOLLOWING:
      GOOGLESQL_RET_CHECK(resolved_expr == nullptr);
      resolved_window_frame_expr = MakeResolvedWindowFrameExpr(
          ResolvedWindowFrameExpr::UNBOUNDED_FOLLOWING, nullptr);
      break;
  }
  resolver_->MaybeRecordParseLocation(ast_frame_expr,
                                      resolved_window_frame_expr.get());
  *resolved_window_frame_expr_out = std::move(resolved_window_frame_expr);
  return absl::OkStatus();
}

absl::Status AnalyticFunctionResolver::ResolveWindowFrameOffsetExpr(
    const ASTWindowFrameExpr* ast_frame_expr,
    const ResolvedWindowFrame::FrameUnit frame_unit,
    const Type* ordering_expr_type,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* resolved_offset_expr) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  GOOGLESQL_RET_CHECK(ast_frame_expr->expression() != nullptr);
  static const char window_frame_clause_name[] = "window frame";
  auto frame_expr_resolution_info = std::make_unique<ExprResolutionInfo>(
      expr_resolution_info,
      ExprResolutionInfoOptions{.clause_name = window_frame_clause_name});
  GOOGLESQL_RETURN_IF_ERROR(resolver_->ResolveExpr(ast_frame_expr->expression(),
                                         frame_expr_resolution_info.get(),
                                         resolved_offset_expr));

  if ((*resolved_offset_expr)->node_kind() != RESOLVED_PARAMETER &&
      (*resolved_offset_expr)->node_kind() != RESOLVED_LITERAL) {
    return MakeSqlErrorAt(ast_frame_expr)
           << "Window framing expression must be a literal or parameter";
  }

  // Check the expression type and coerce it if necessary.
  if (frame_unit == ResolvedWindowFrame::ROWS) {
    GOOGLESQL_RETURN_IF_ERROR(resolver_->CoerceExprToType(
        ast_frame_expr->expression(), resolver_->type_factory_->get_int64(),
        Resolver::kImplicitCoercion,
        "Window framing expression for ROWS can only be of integer type, but "
        "has type $1",
        resolved_offset_expr));
  } else {
    ABSL_DCHECK_EQ(frame_unit, ResolvedWindowFrame::RANGE);
    ABSL_DCHECK(ordering_expr_type != nullptr);

    GOOGLESQL_RETURN_IF_ERROR(resolver_->CoerceExprToType(
        ast_frame_expr->expression(), ordering_expr_type,
        Resolver::kImplicitCoercion,
        "Window framing expression has type $1 that cannot coerce to the type "
        "of the ORDER BY expression, which is $0",
        resolved_offset_expr));
  }

  // Check the expression value if it is a literal.
  if ((*resolved_offset_expr)->node_kind() == RESOLVED_LITERAL) {
    const Value& value =
        (*resolved_offset_expr)->GetAs<ResolvedLiteral>()->value();
    if (value.is_null()) {
      return MakeSqlErrorAt(ast_frame_expr)
             << "Window framing expression cannot be NULL";
    }
    if (value.ToDouble() < 0) {
      return MakeSqlErrorAt(ast_frame_expr)
             << "Window framing expression cannot evaluate to a negative value";
    }
  }

  return absl::OkStatus();
}

absl::Status AnalyticFunctionResolver::ValidateWindowFrameSize(
    const ASTWindowFrame* ast_window_frame,
    const ResolvedWindowFrame* resolved_window_frame) const {
  const ASTWindowFrameExpr* ast_start_frame_expr =
      ast_window_frame->start_expr();
  const ASTWindowFrameExpr* ast_end_frame_expr =
      ast_window_frame->end_expr();

  const ResolvedWindowFrameExpr* resolved_start_frame_expr =
      resolved_window_frame->start_expr();
  const ResolvedWindowFrameExpr* resolved_end_frame_expr =
      resolved_window_frame->end_expr();
  switch (resolved_start_frame_expr->boundary_type()) {
    case ResolvedWindowFrameExpr::UNBOUNDED_FOLLOWING:
      return MakeSqlErrorAt(ast_start_frame_expr)
             << "Starting window framing expression cannot be UNBOUNDED "
                "FOLLOWING";
      break;
    case ResolvedWindowFrameExpr::CURRENT_ROW:
      if (resolved_end_frame_expr->boundary_type() ==
          ResolvedWindowFrameExpr::OFFSET_PRECEDING) {
        return MakeSqlErrorAt(ast_end_frame_expr)
               << "Starting window framing expression cannot be "
               << resolved_start_frame_expr->GetBoundaryTypeString()
               << " when the ending window framing expression is "
               << resolved_end_frame_expr->GetBoundaryTypeString();
      }
      break;
    case ResolvedWindowFrameExpr::OFFSET_FOLLOWING: {
      if (ast_end_frame_expr == nullptr) {
        return MakeSqlErrorAt(ast_start_frame_expr)
               << "Starting window framing expression cannot be "
               << resolved_start_frame_expr->GetBoundaryTypeString()
               << " because the implicit ending window framing expression is "
               << resolved_end_frame_expr->GetBoundaryTypeString();
        break;
      }
      switch (resolved_end_frame_expr->boundary_type()) {
        case ResolvedWindowFrameExpr::OFFSET_PRECEDING:
        case ResolvedWindowFrameExpr::CURRENT_ROW:
          return MakeSqlErrorAt(ast_end_frame_expr)
                 << "Ending window framing expression cannot be "
                 << resolved_end_frame_expr->GetBoundaryTypeString()
                 << " when the starting window framing expression is "
                 << resolved_start_frame_expr->GetBoundaryTypeString();
          break;
        default:
          break;
      }
      break;
    }
    default:
      break;
  }
  if (resolved_end_frame_expr->boundary_type() ==
      ResolvedWindowFrameExpr::UNBOUNDED_PRECEDING) {
    return MakeSqlErrorAt(ast_end_frame_expr)
           << "Ending window framing expression cannot be UNBOUNDED PRECEDING";
  }
  return absl::OkStatus();
}

bool AnalyticFunctionResolver::HasAnalytic() const {
  return !analytic_function_groups_.empty();
}

absl::StatusOr<std::unique_ptr<const ResolvedWindowPartitioning>>
AnalyticFunctionResolver::ResolveStandalonePartitionBy(
    const ASTPartitionBy* ast_partition_by, bool force_rename_columns,
    ExprResolutionInfo* expr_resolution_info) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  WindowExprInfoList* partition_by_info = nullptr;  // Not owned.
  GOOGLESQL_RETURN_IF_ERROR(ResolveWindowPartitionByPreAggregation(
      ast_partition_by, expr_resolution_info, /*allow_ordinals=*/false,
      expr_resolution_info->clause_name, &partition_by_info));

  std::unique_ptr<const ResolvedWindowPartitioning>
      resolved_window_partitioning;
  GOOGLESQL_RETURN_IF_ERROR(ResolveWindowPartitionByPostAggregation(
      ast_partition_by, expr_resolution_info->query_resolution_info,
      force_rename_columns, &resolved_window_partitioning));
  return resolved_window_partitioning;
}

absl::StatusOr<std::unique_ptr<const ResolvedWindowOrdering>>
AnalyticFunctionResolver::ResolveStandaloneOrderBy(
    const ASTOrderBy* ast_order_by, ExprResolutionInfo* expr_resolution_info) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  WindowExprInfoList* order_by_info = nullptr;  // Not owned.
  GOOGLESQL_RETURN_IF_ERROR(ResolveWindowOrderByPreAggregation(
      ast_order_by, /*is_in_range_window=*/false, expr_resolution_info,
      /*allow_ordinals=*/false, expr_resolution_info->clause_name,
      &order_by_info));

  std::unique_ptr<const ResolvedWindowOrdering> resolved_window_ordering;
  GOOGLESQL_RETURN_IF_ERROR(ResolveWindowOrderByPostAggregation(
      ast_order_by, expr_resolution_info->query_resolution_info,
      &resolved_window_ordering));
  return resolved_window_ordering;
}

// Add a wrapper scan if there are any computed columns for partitioning or
// ordering expressions.
absl::StatusOr<std::unique_ptr<const ResolvedScan>>
AnalyticFunctionResolver::AddMissingPartitioningAndOrderingExpressions(
    std::unique_ptr<const ResolvedScan> input_scan) {
  if (window_columns_to_compute_.empty()) {
    return input_scan;
  }

  ResolvedColumnList wrapper_column_list(input_scan->column_list());
  for (const std::unique_ptr<const ResolvedComputedColumn>& computed_column :
       window_columns_to_compute_) {
    wrapper_column_list.emplace_back(computed_column->column());
  }
  std::vector<std::unique_ptr<const ResolvedComputedColumn>> computed_columns;
  std::swap(window_columns_to_compute_, computed_columns);

  return MakeResolvedProjectScan(
      wrapper_column_list, std::move(computed_columns), std::move(input_scan));
}

absl::Status AnalyticFunctionResolver::CreateAnalyticScan(
    QueryResolutionInfo* query_resolution_info,
    std::unique_ptr<const ResolvedScan>* scan) {
  GOOGLESQL_RET_CHECK(HasAnalytic());
  is_create_analytic_scan_successful_ = false;

  // The output columns for an AnalyticScan include all output columns
  // from the input, the columns for all analytic functions and columns
  // for some partitioning or ordering expressions that are referenced
  // by SELECT-list expressions.
  ResolvedColumnList output_column_list((*scan)->column_list());
  std::unique_ptr<ResolvedAnalyticScan> analytic_scan;
  std::vector<std::unique_ptr<const ResolvedAnalyticFunctionGroup>>
      resolved_function_groups;

  for (std::unique_ptr<AnalyticFunctionGroupInfo>& function_group_info :
       analytic_function_groups_) {
    std::unique_ptr<ResolvedAnalyticFunctionGroup> resolved_analytic_group;
    GOOGLESQL_RETURN_IF_ERROR(ResolveAnalyticFunctionGroup(
        query_resolution_info, function_group_info.get(),
        &resolved_analytic_group, &output_column_list));
    resolved_function_groups.emplace_back(std::move(resolved_analytic_group));
  }

  // Add a wrapper scan if there are any computed columns for partitioning or
  // ordering expressions.
  GOOGLESQL_ASSIGN_OR_RETURN(
      *scan, AddMissingPartitioningAndOrderingExpressions(std::move(*scan)));

  if (!window_columns_to_compute_.empty()) {
    ResolvedColumnList wrapper_column_list((*scan)->column_list());
    for (const std::unique_ptr<const ResolvedComputedColumn>& computed_column :
         window_columns_to_compute_) {
      wrapper_column_list.emplace_back(computed_column->column());
    }
    *scan = MakeResolvedProjectScan(
        wrapper_column_list,
        std::move(window_columns_to_compute_),
        std::move(*scan));
    window_columns_to_compute_.clear();
  }

  analytic_scan = MakeResolvedAnalyticScan(
      output_column_list, std::move(*scan),
      std::move(resolved_function_groups));

  *scan = std::move(analytic_scan);

  is_create_analytic_scan_successful_ = true;
  return absl::OkStatus();
}

absl::Status AnalyticFunctionResolver::ResolveAnalyticFunctionGroup(
    QueryResolutionInfo* query_resolution_info,
    AnalyticFunctionGroupInfo* function_group_info,
    std::unique_ptr<ResolvedAnalyticFunctionGroup>*
        resolved_analytic_function_group,
    ResolvedColumnList* analytic_column_list) {
  // Finish resolving PARTITION BY.
  std::unique_ptr<const ResolvedWindowPartitioning>
      resolved_window_partitioning;
  if (function_group_info->ast_partition_by != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveWindowPartitionByPostAggregation(
        function_group_info->ast_partition_by, query_resolution_info,
        /*force_rename_columns=*/false, &resolved_window_partitioning));
  }

  // Finish resolving ORDER BY.
  std::unique_ptr<const ResolvedWindowOrdering> resolved_window_ordering;
  if (function_group_info->ast_order_by != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(ResolveWindowOrderByPostAggregation(
        function_group_info->ast_order_by, query_resolution_info,
        &resolved_window_ordering));
  }

  // Populate the analytic function list.
  std::vector<std::unique_ptr<const ResolvedComputedColumn>>
      analytic_function_groups;
  for (std::unique_ptr<ResolvedComputedColumn>& resolved_computed_column :
       function_group_info->resolved_computed_columns) {
    analytic_column_list->push_back(resolved_computed_column->column());
    analytic_function_groups.push_back(std::move(resolved_computed_column));
  }

  *resolved_analytic_function_group =
      MakeResolvedAnalyticFunctionGroup(
          std::move(resolved_window_partitioning),
          std::move(resolved_window_ordering),
          std::move(analytic_function_groups));

  return absl::OkStatus();
}

absl::Status AnalyticFunctionResolver::ResolveWindowPartitionByPostAggregation(
    const ASTPartitionBy* ast_partition_by,
    QueryResolutionInfo* query_resolution_info, bool force_rename_columns,
    std::unique_ptr<const ResolvedWindowPartitioning>*
        resolved_window_partitioning_out) {
  std::unique_ptr<WindowExprInfoList>* partition_by_info =
      googlesql_base::FindOrNull(ast_to_resolved_info_map_, ast_partition_by);
  GOOGLESQL_RET_CHECK(partition_by_info != nullptr && *partition_by_info != nullptr);

  std::unique_ptr<ResolvedWindowPartitioning> resolved_window_partitioning;
  std::vector<std::unique_ptr<const ResolvedColumnRef>>
      resolved_partition_by_exprs;
  std::vector<std::unique_ptr<const ResolvedOption>>
      resolved_partition_by_hints;

  for (std::unique_ptr<WindowExprInfo>& partitioning_expr_info :
       **partition_by_info) {
    // Since a window PARTITION BY may be shared by multiple analytic functions,
    // do not create a new column if we have created one for this partitioning
    // expressions.
    if (partitioning_expr_info->resolved_column_ref == nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(AddColumnForWindowExpression(
          kPartitionById,
          resolver_->MakeIdString(
              absl::StrCat("$partitionbycol", ++num_partitioning_exprs_)),
          force_rename_columns, query_resolution_info,
          partitioning_expr_info.get()));
    }
    resolved_partition_by_exprs.emplace_back(Resolver::CopyColumnRef(
        partitioning_expr_info->resolved_column_ref.get()));
  }

  resolved_window_partitioning = MakeResolvedWindowPartitioning(
      std::move(resolved_partition_by_exprs));
  // Avoid deletion after ownership transfer.
  resolved_partition_by_exprs.clear();

  // Populate the <collation_list> based on <resolved_partition_by_exprs> if
  // the feature is enabled.
  if (resolver_->language().LanguageFeatureEnabled(FEATURE_COLLATION_SUPPORT)) {
    std::vector<ResolvedCollation> collation_list;
    bool empty = true;
    for (const auto& partition_by_expr :
         resolved_window_partitioning->partition_by_list()) {
      ResolvedCollation resolved_collation;
      if (partition_by_expr->type_annotation_map() != nullptr) {
        GOOGLESQL_ASSIGN_OR_RETURN(resolved_collation,
                         ResolvedCollation::MakeResolvedCollation(
                             *partition_by_expr->type_annotation_map()));
        empty &= resolved_collation.Empty();
      }
      collation_list.push_back(std::move(resolved_collation));
    }
    if (!empty) {
      resolved_window_partitioning->set_collation_list(
          std::move(collation_list));
    }
  }

  if (ast_partition_by->hint() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(resolver_->ResolveHintAndAppend(
        ast_partition_by->hint(), &resolved_partition_by_hints));
    resolved_window_partitioning->set_hint_list(
        std::move(resolved_partition_by_hints));
  }
  resolver_->MaybeRecordParseLocation(ast_partition_by,
                                      resolved_window_partitioning.get());

  *resolved_window_partitioning_out = std::move(resolved_window_partitioning);
  return absl::OkStatus();
}

absl::Status AnalyticFunctionResolver::ResolveWindowOrderByPostAggregation(
    const ASTOrderBy* ast_order_by,
    QueryResolutionInfo* query_resolution_info,
    std::unique_ptr<const ResolvedWindowOrdering>*
        resolved_window_ordering_out) {
  std::unique_ptr<WindowExprInfoList>* found_order_by_info =
      googlesql_base::FindOrNull(ast_to_resolved_info_map_, ast_order_by);
  GOOGLESQL_RET_CHECK(found_order_by_info != nullptr && *found_order_by_info != nullptr);
  WindowExprInfoList* order_by_info = found_order_by_info->get();

  std::vector<std::unique_ptr<const ResolvedOrderByItem>> order_by_items;
  std::vector<std::unique_ptr<const ResolvedOption>> order_by_hints;

  const absl::Span<const ASTOrderingExpression* const>& ast_ordering_exprs =
      ast_order_by->ordering_expressions();
  GOOGLESQL_RET_CHECK_EQ(ast_ordering_exprs.size(), order_by_info->size());
  for (int i = 0; i < order_by_info->size(); ++i) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        ResolvedOrderByItemEnums::NullOrderMode null_order,
        resolver_->ResolveNullOrderMode(ast_ordering_exprs[i]->null_order()));

    // Since a window ORDER BY may be shared by multiple analytic functions,
    // do not create a new column if we have created one for this ordering
    // expression.
    if ((*order_by_info)[i]->resolved_column_ref == nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(AddColumnForWindowExpression(
          kOrderById,
          resolver_->MakeIdString(
              absl::StrCat("$orderbycol", ++num_ordering_items_)),
          /*force_rename_columns=*/false, query_resolution_info,
          (*order_by_info)[i].get()));
    }

    std::unique_ptr<const ResolvedColumnRef> resolved_column_ref =
        resolver_->CopyColumnRef(
            (*order_by_info)[i]->resolved_column_ref.get());

    std::unique_ptr<const ResolvedExpr> resolved_collation_name;
    const ASTCollate* ast_collate =
        ast_order_by->ordering_expressions().at(i)->collate();
    if (ast_collate != nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(resolver_->ValidateAndResolveOrderByCollate(
          ast_collate,
          ast_order_by->ordering_expressions().at(i),
          resolved_column_ref->column().type(),
          &resolved_collation_name));
    }

    auto resolved_order_by_item = MakeResolvedOrderByItem(
        std::move(resolved_column_ref), std::move(resolved_collation_name),
        ast_ordering_exprs[i]->descending(), null_order);
    resolver_->MaybeRecordParseLocation(ast_ordering_exprs[i],
                                        resolved_order_by_item.get());

    if (resolver_->language().LanguageFeatureEnabled(
            FEATURE_COLLATION_SUPPORT)) {
      GOOGLESQL_RETURN_IF_ERROR(
          CollationAnnotation::ResolveCollationForResolvedOrderByItem(
              resolved_order_by_item.get()));
    }

    order_by_items.emplace_back(std::move(resolved_order_by_item));
  }

  std::unique_ptr<ResolvedWindowOrdering> resolved_window_ordering =
      MakeResolvedWindowOrdering(std::move(order_by_items));

  if (ast_order_by->hint() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(
        resolver_->ResolveHintAndAppend(ast_order_by->hint(), &order_by_hints));
    resolved_window_ordering->set_hint_list(std::move(order_by_hints));
  }
  resolver_->MaybeRecordParseLocation(ast_order_by,
                                      resolved_window_ordering.get());

  *resolved_window_ordering_out = std::move(resolved_window_ordering);
  return absl::OkStatus();
}

absl::Status AnalyticFunctionResolver::AddColumnForWindowExpression(
    IdString query_alias, IdString column_alias, bool force_rename_columns,
    QueryResolutionInfo* query_resolution_info,
    WindowExprInfo* window_expr_info) {
  GOOGLESQL_RET_CHECK(window_expr_info->resolved_column_ref == nullptr);
  GOOGLESQL_RET_CHECK(window_expr_info->select_list_index >= 0 ||
            window_expr_info->resolved_expr != nullptr);

  const SelectColumnStateList* select_column_state_list =
      query_resolution_info->select_column_state_list();
  std::unique_ptr<const ResolvedColumnRef> resolved_column_ref;
  if (window_expr_info->select_list_index >= 0) {
    // The <select_list_index> is non-negative if the original expression
    // referenced a SELECT list alias.  This is only valid if the analytic
    // function appears in the ORDER BY, in which case all SELECT list
    // ResolvedColumns are assigned and initialized.
    const SelectColumnState* select_column_state =
        select_column_state_list->GetSelectColumnState(
            window_expr_info->select_list_index);
    GOOGLESQL_RET_CHECK(select_column_state->resolved_select_column.IsInitialized());
    resolved_column_ref =
        resolver_->MakeColumnRef(select_column_state->resolved_select_column);
  } else if (window_expr_info->resolved_expr->node_kind() ==
                 RESOLVED_COLUMN_REF &&
             !force_rename_columns) {
    resolved_column_ref = resolver_->CopyColumnRef(
        window_expr_info->resolved_expr->GetAs<ResolvedColumnRef>());
  } else {
    IdString alias = GetAliasForExpression(window_expr_info->ast_location);
    if (alias.empty()) {
      alias = column_alias;
    }
    ResolvedColumn resolved_column(
        resolver_->AllocateColumnId(), query_alias, alias,
        window_expr_info->resolved_expr->annotated_type());
    window_columns_to_compute_.emplace_back(
        MakeResolvedComputedColumn(
            resolved_column, std::move(window_expr_info->resolved_expr)));
    resolved_column_ref = resolver_->MakeColumnRef(resolved_column);
  }

  window_expr_info->resolved_column_ref = std::move(resolved_column_ref);
  return absl::OkStatus();
}

absl::Status AnalyticFunctionResolver::ExtractWindowInfoFromReferencedWindow(
    FlattenedWindowInfo* flattened_window_info) const {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  const ASTWindowSpecification* window_spec =
      flattened_window_info->ast_window_spec;
  if (window_spec->base_window_name() != nullptr) {
    const std::string referenced_window_name =
        window_spec->base_window_name()->GetAsString();
    const std::unique_ptr<const FlattenedWindowInfo>*
        flattened_referenced_window_info =
            googlesql_base::FindOrNull(*named_window_info_map_,
                            absl::AsciiStrToLower(referenced_window_name));
    if (flattened_referenced_window_info == nullptr) {
      return MakeSqlErrorAt(window_spec->base_window_name())
             << "Unrecognized window alias " << referenced_window_name;
    }

    GOOGLESQL_RETURN_IF_ERROR(CheckForConflictsWithReferencedWindow(
        window_spec, flattened_referenced_window_info->get()));

    if (flattened_window_info->ast_partition_by == nullptr &&
        flattened_window_info->ast_order_by == nullptr) {
      flattened_window_info->ast_grouping_window_spec =
          (*flattened_referenced_window_info)->ast_grouping_window_spec;
    }
    if (flattened_window_info->ast_partition_by == nullptr) {
      flattened_window_info->ast_partition_by =
          (*flattened_referenced_window_info)->ast_partition_by;
    }
    if (flattened_window_info->ast_order_by == nullptr) {
      flattened_window_info->ast_order_by =
          (*flattened_referenced_window_info)->ast_order_by;
    }
    if (flattened_window_info->ast_window_frame == nullptr) {
      flattened_window_info->ast_window_frame =
          (*flattened_referenced_window_info)->ast_window_frame;
    }
  }

  return absl::OkStatus();
}

absl::Status AnalyticFunctionResolver::CheckForConflictsWithReferencedWindow(
    const ASTWindowSpecification* window_spec,
    const FlattenedWindowInfo* flattened_referenced_window_info) const {
  // The following three rules are in the SQL standard. They guarantee that:
  // 1) If two analytic functions use a common window with ORDER BY, then they
  //    must also share PARTTION BY so that they are guaranteed to be in
  //    the same function group and should observe the same ordering of
  //    peer rows.
  // 2) If two analytic functions without ORDER BY use a window with
  //    PARTITION BY, then they are guaranteed to be in the same function group
  //    and should observe the same arbitrary ordering within each partition.
  // 3) If two analytic functions share a common window with a framing clause,
  //    then they must also share both PARTITION BY and ORDER BY and hence
  //    are guaranteed to be in the same function group.
  if (window_spec->base_window_name() != nullptr &&
      window_spec->partition_by() != nullptr) {
    return MakeSqlErrorAt(window_spec->base_window_name())
           << "If a window has a PARTITION BY, it cannot reference a named "
              "window";
  }
  if ((window_spec->order_by() != nullptr ||
       window_spec->window_frame() != nullptr) &&
      flattened_referenced_window_info->ast_window_frame != nullptr) {
    return MakeSqlErrorAt(window_spec->base_window_name())
           << "If a window has an inline ORDER BY or window frame clause, it "
              "cannot reference another window that contains or inherits a "
              "window frame clause";
  }
  if (window_spec->order_by() != nullptr &&
      flattened_referenced_window_info->ast_order_by != nullptr) {
    return MakeSqlErrorAt(window_spec->order_by())
           << "If a window inherits an ORDER BY from its referenced window, it "
              "cannot have an inline ORDER BY";
  }
  return absl::OkStatus();
}

const std::vector<
    std::unique_ptr<AnalyticFunctionResolver::AnalyticFunctionGroupInfo>>&
AnalyticFunctionResolver::analytic_function_groups() const {
  return analytic_function_groups_;
}

}  // namespace googlesql
