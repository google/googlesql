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

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output_properties.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_deep_copy_visitor.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/rewrite_utils.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

// A visitor that rewrites ResolvedFlatten nodes into standard UNNESTs.
class FlattenRewriterVisitor : public ResolvedASTDeepCopyVisitor {
 public:
  explicit FlattenRewriterVisitor(const AnalyzerOptions* options,
                                  Catalog* catalog,
                                  ColumnFactory* column_factory,
                                  TypeFactory& type_factory)
      : fn_builder_(*options, *catalog, type_factory),
        column_factory_(column_factory) {}

 private:
  absl::Status VisitResolvedArrayScan(const ResolvedArrayScan* node) override;

  absl::Status VisitResolvedFlatten(const ResolvedFlatten* node) override;

  // Takes the components of a ResolvedFlatten (its expr, 'flatten_expr' and its
  // 'get_field_list' and converts it into a resulting ResolvedScan that is
  // functionally equivalent.
  //
  // When 'flatten_expr' uses ColumnRefs from a scan, 'input_scan' must be
  // provided to be that input scan.
  //
  // When 'order_results' is true, the generated scan ends with an OrderByScan
  // to retain order (using offsets from array scans).
  //
  // 'in_subquery' should be set to indicate whether the resulting scan will be
  // in a subquery or not. If it is, then column references need to be
  // correlated to access the column outside the subquery it's in.
  //
  // Note that the final OrderByScan is not needed for a case like
  // SELECT ... FROM t, UNNEST(t.a.b.c) since the UNNEST produces an unordered
  // relation. The final OrderByScan is needed for explicit FLATTEN(t.a.b.c) or
  // UNNEST with OFFSET.
  //
  // The result is the last column in the output scan's column list.
  absl::StatusOr<std::unique_ptr<ResolvedScan>> FlattenToScan(
      std::unique_ptr<ResolvedExpr> flatten_expr,
      absl::Span<const std::unique_ptr<const ResolvedExpr>> get_field_list,
      std::unique_ptr<ResolvedScan> input_scan, bool order_results,
      bool in_subquery);

  FunctionCallBuilder fn_builder_;
  ColumnFactory* column_factory_;
};

absl::Status FlattenRewriterVisitor::VisitResolvedArrayScan(
    const ResolvedArrayScan* node) {
  // Multiway UNNEST with more than one array, if containing any FLATTEN
  // expression, will be handled by `VisitResolvedFlatten` instead.
  if (node->array_expr_list_size() > 1 ||
      !node->array_expr_list(0)->Is<ResolvedFlatten>()) {
    return CopyVisitResolvedArrayScan(node);
  }
  const ResolvedFlatten* flatten =
      node->array_expr_list(0)->GetAs<ResolvedFlatten>();
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedScan> input_scan,
                   ProcessNode(node->input_scan()));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> join_expr,
                   ProcessNode(node->join_expr()));

  bool need_offset_column = node->array_offset_column() != nullptr;
  if (need_offset_column || join_expr != nullptr || node->is_outer()) {
    // If we need an offset column, we rewrite each row to a subquery to
    // generate a single array and then do an array scan over that. This allows
    // us to have a single ordered offset.
    //
    // Without doing so we end up with one offset per repeated pivot and no way
    // to combine them.
    //
    // TODO: Avoid using a subquery for joins. For now we also do this
    // for joins to handle the case where the flatten ends up with a ProjectScan
    // instead of an ArrayScan. A better solution would be to stop adding the
    // ProjectScan if the final element path is a scalar and instead to rewrite
    // column references to the output to do the Get*Field there instead. This
    // is a significantly more complex change but would avoid needing the
    // subquery.
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> flatten_expr,
                     ProcessNode(flatten->expr()));
    GOOGLESQL_ASSIGN_OR_RETURN(flatten_expr, CorrelateColumnRefs(*flatten_expr));
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<ResolvedScan> scan,
        FlattenToScan(std::move(flatten_expr), flatten->get_field_list(),
                      MakeResolvedSingleRowScan(), need_offset_column,
                      /*in_subquery=*/true));

    std::vector<std::unique_ptr<const ResolvedColumnRef>> column_refs;
    GOOGLESQL_RETURN_IF_ERROR(CollectSortUniqueColumnRefs(*flatten, column_refs));
    if (scan->column_list_size() > 1) {
      // Subquery must produce one value. Remove unneeded intermediary columns.
      // TODO: This can be removed if we avoid using subquery for joins.
      std::vector<ResolvedColumn> column_list;
      column_list.push_back(scan->column_list().back());
      scan->set_column_list(std::move(column_list));
    }
    std::unique_ptr<ResolvedSubqueryExpr> subquery = MakeResolvedSubqueryExpr(
        flatten->type(), ResolvedSubqueryExpr::ARRAY, std::move(column_refs),
        /*in_expr=*/nullptr, std::move(scan));
    subquery->set_type_annotation_map(flatten->type_annotation_map());

    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedColumnHolder> offset_column,
                     ProcessNode(node->array_offset_column()));
    std::vector<std::unique_ptr<const ResolvedExpr>> array_expr_list;
    array_expr_list.push_back(std::move(subquery));
    std::vector<ResolvedColumn> element_column_list;
    element_column_list.push_back(node->element_column());
    PushNodeToStack(MakeResolvedArrayScan(
        node->column_list(), std::move(input_scan), std::move(array_expr_list),
        std::move(element_column_list), std::move(offset_column),
        std::move(join_expr), node->is_outer(),
        /*array_zip_mode=*/nullptr));
    return absl::OkStatus();
  }

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> flatten_expr,
                   ProcessNode(flatten->expr()));
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ResolvedScan> scan,
      FlattenToScan(std::move(flatten_expr), flatten->get_field_list(),
                    std::move(input_scan), /*order_results=*/false,
                    /*in_subquery=*/false));

  // Project the flatten result back to the expected output column.
  std::vector<std::unique_ptr<const ResolvedComputedColumn>> expr_list;
  expr_list.push_back(MakeResolvedComputedColumn(
      node->element_column(), MakeResolvedColumnRef(scan->column_list().back(),
                                                    /*is_correlated=*/false)));
  PushNodeToStack(MakeResolvedProjectScan(
      node->column_list(), std::move(expr_list), std::move(scan)));
  return absl::OkStatus();
}

absl::Status FlattenRewriterVisitor::VisitResolvedFlatten(
    const ResolvedFlatten* node) {
  // Define a column to represent the result of evaluating the input. We want
  // the input value referenced both by null checking and flattening, so we use
  // a column to ensure it is only evaluated once.
  ResolvedColumn flatten_expr_column = column_factory_->MakeCol(
      "$flatten_input", "injected", node->expr()->annotated_type());

  // To avoid returning an empty array if the input is NULL, we rewrite to
  // explicitly return NULL in that case. The flatten rewrite would return an
  // empty array.
  //
  // TODO: Use AnalyzeSubstitute once it's ready.

  // Check if the input expression is NULL.
  std::unique_ptr<ResolvedExpr> input_col =
      MakeResolvedColumnRef(flatten_expr_column,
                            /*is_correlated=*/false);
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> if_condition,
                   fn_builder_.IsNull(std::move(input_col)));
  // If so, we return NULL.
  std::unique_ptr<ResolvedExpr> if_then =
      MakeResolvedLiteral(Value::Null(node->type()));
  if_then->set_type_annotation_map(node->type_annotation_map());

  // Otherwise, return the flattened result.
  std::vector<std::unique_ptr<const ResolvedColumnRef>> column_refs;
  column_refs.push_back(MakeResolvedColumnRef(flatten_expr_column,
                                              /*is_correlated=*/false));
  // RowTypes are like joins and scanning them does not preserve order.
  // TODO: If we step through single-row ROWs, and had other arrays
  // before or after, maybe we should preserve order from the arrays?
  bool order_results = !node->expr()->type()->IsRow();
  for (const auto& get_field : node->get_field_list()) {
    GOOGLESQL_RETURN_IF_ERROR(CollectColumnRefs(*get_field, &column_refs));
    if (get_field->type()->IsRow()) {
      // TODO: Add a test for this.  We can't current reach this
      // because we can't have ROW types in structs or returned from functions,
      // so there's always a ROW type on the outside, so order_results is
      // already false.
      order_results = false;
    }
  }
  // Clean up any duplicated parameters.
  SortUniqueColumnRefs(column_refs);
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ResolvedScan> rewritten_flatten,
      FlattenToScan(MakeResolvedColumnRef(flatten_expr_column,
                                          /*is_correlated=*/true),
                    node->get_field_list(), /*input_scan=*/nullptr,
                    /*order_results=*/order_results, /*in_subquery=*/true));
  std::unique_ptr<ResolvedExpr> if_else = MakeResolvedSubqueryExpr(
      node->type(), ResolvedSubqueryExpr::ARRAY, std::move(column_refs),
      /*in_expr=*/nullptr, std::move(rewritten_flatten));
  if_else->set_type_annotation_map(node->type_annotation_map());

  GOOGLESQL_ASSIGN_OR_RETURN(auto resolved_if,
                   fn_builder_.If(std::move(if_condition), std::move(if_then),
                                  std::move(if_else)));

  // Use a ResolvedWithExpr to populate the input variable.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> flatten_expr,
                   ProcessNode(node->expr()));
  std::vector<std::unique_ptr<const ResolvedComputedColumn>> let_assignments;
  let_assignments.push_back(
      MakeResolvedComputedColumn(flatten_expr_column, std::move(flatten_expr)));
  PushNodeToStack(
      MakeResolvedWithExpr(std::move(let_assignments), std::move(resolved_if)));
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<ResolvedScan>>
FlattenRewriterVisitor::FlattenToScan(
    std::unique_ptr<ResolvedExpr> flatten_expr,
    absl::Span<const std::unique_ptr<const ResolvedExpr>> get_field_list,
    std::unique_ptr<ResolvedScan> input_scan, bool order_results,
    bool in_subquery) {
  std::vector<ResolvedColumn> column_list;
  if (input_scan != nullptr) column_list = input_scan->column_list();
  GOOGLESQL_RET_CHECK(flatten_expr->type()->IsArrayLike());
  GOOGLESQL_ASSIGN_OR_RETURN(const Type* element_type,
                   flatten_expr->type()->GetElementType());
  const AnnotationMap* element_annotation_map = nullptr;
  if (flatten_expr->type_annotation_map() != nullptr) {
    element_annotation_map =
        flatten_expr->type_annotation_map()->AsStructMap()->field(0);
  }
  ResolvedColumn column = column_factory_->MakeCol(
      "$flatten", "injected", {element_type, element_annotation_map});
  column_list.push_back(column);

  std::vector<ResolvedColumn> offset_columns;
  ResolvedColumn offset_column;
  if (order_results) {
    offset_column =
        column_factory_->MakeCol("$offset", "injected", types::Int64Type());
    offset_columns.push_back(offset_column);
    column_list.push_back(offset_column);
  }

  std::vector<std::unique_ptr<const ResolvedExpr>> array_expr_list_233;
  array_expr_list_233.push_back(std::move(flatten_expr));
  std::vector<ResolvedColumn> element_column_list_233;
  element_column_list_233.push_back(column);
  std::unique_ptr<ResolvedScan> scan = MakeResolvedArrayScan(
      column_list, std::move(input_scan), std::move(array_expr_list_233),
      std::move(element_column_list_233),
      order_results ? MakeResolvedColumnHolder(offset_column) : nullptr,
      /*join_expr=*/nullptr,
      /*is_outer=*/false,
      /*array_zip_mode=*/nullptr);

  // Keep track of pending Get*Field on non-array fields.
  std::unique_ptr<const ResolvedExpr> input;

  for (const auto& const_get_field : get_field_list) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> get_field,
                     ProcessNode(const_get_field.get()));
    // Change the input from the FlattenedArg to instead be a ColumnRef or the
    // built-up non-array expression.
    if (input == nullptr) {
      input = MakeResolvedColumnRef(column, /*is_correlated=*/false);
    }
    ResolvedExpr* to_set_input = get_field.get();
    // ResolvedFunctionCalls show up here for $array_at_offset.
    if (get_field->Is<ResolvedFunctionCall>()) {
      if (in_subquery) {
        GOOGLESQL_ASSIGN_OR_RETURN(get_field, CorrelateColumnRefs(*get_field));
      }
      ResolvedFunctionCall* call = get_field->GetAs<ResolvedFunctionCall>();
      GOOGLESQL_RET_CHECK_EQ(2, call->argument_list_size());
      to_set_input = const_cast<ResolvedExpr*>(
          get_field->GetAs<ResolvedFunctionCall>()->argument_list(0));
    }
    if (to_set_input->Is<ResolvedGetProtoField>()) {
      to_set_input->GetAs<ResolvedGetProtoField>()->set_expr(std::move(input));
    } else if (to_set_input->Is<ResolvedGetStructField>()) {
      to_set_input->GetAs<ResolvedGetStructField>()->set_expr(std::move(input));
    } else if (to_set_input->Is<ResolvedGetJsonField>()) {
      to_set_input->GetAs<ResolvedGetJsonField>()->set_expr(std::move(input));
    } else if (to_set_input->Is<ResolvedGraphGetElementProperty>()) {
      to_set_input->GetAs<ResolvedGraphGetElementProperty>()->set_expr(
          std::move(input));
    } else if (to_set_input->Is<ResolvedGetRowField>()) {
      to_set_input->GetAs<ResolvedGetRowField>()->set_expr(std::move(input));
    } else {
      GOOGLESQL_RET_CHECK_FAIL() << "Unsupported node: " << to_set_input->DebugString();
    }
    input = nullptr;  // already null, but avoids ClangTidy "use after free"

    if (!get_field->type()->IsArrayLike()) {
      // Not array-like so can't turn it into an ArrayScan.
      // Collect as input for next array.
      input = std::move(get_field);
    } else {
      const AnnotationMap* element_annotation_map = nullptr;
      if (get_field->type_annotation_map() != nullptr) {
        element_annotation_map =
            get_field->type_annotation_map()->AsStructMap()->field(0);
      }
      GOOGLESQL_ASSIGN_OR_RETURN(const Type* field_element_type,
                       get_field->type()->GetElementType());
      column = column_factory_->MakeCol(
          "$flatten", "injected", {field_element_type, element_annotation_map});
      column_list.push_back(column);

      if (order_results) {
        offset_column =
            column_factory_->MakeCol("$offset", "injected", types::Int64Type());
        offset_columns.push_back(offset_column);
        column_list.push_back(offset_column);
      }
      std::vector<std::unique_ptr<const ResolvedExpr>> array_expr_list;
      array_expr_list.push_back(std::move(get_field));
      std::vector<ResolvedColumn> element_column_list;
      element_column_list.push_back(column);
      scan = MakeResolvedArrayScan(
          column_list, std::move(scan), std::move(array_expr_list),
          std::move(element_column_list),
          order_results ? MakeResolvedColumnHolder(offset_column) : nullptr,
          /*join_expr=*/nullptr,
          /*is_outer=*/false,
          /*array_zip_mode=*/nullptr);
    }
  }

  if (input != nullptr) {
    // We have leftover "gets" that resulted in non-arrays.
    // Use a ProjectScan to resolve them to the expected column.
    column = column_factory_->MakeCol("$flatten", "injected",
                                      input->annotated_type());
    column_list.push_back(column);
    std::vector<std::unique_ptr<const ResolvedComputedColumn>> expr_list;
    expr_list.push_back(MakeResolvedComputedColumn(column, std::move(input)));
    scan = MakeResolvedProjectScan(column_list, std::move(expr_list),
                                   std::move(scan));
  }

  if (order_results) {
    std::vector<std::unique_ptr<const ResolvedOrderByItem>> order_by;
    order_by.reserve(offset_columns.size());
    for (const ResolvedColumn& c : offset_columns) {
      order_by.push_back(MakeResolvedOrderByItem(
          MakeResolvedColumnRef(c, /*is_correlated=*/false),
          /*collation_name=*/nullptr, /*is_descending=*/false,
          ResolvedOrderByItemEnums::ORDER_UNSPECIFIED));
    }
    scan =
        MakeResolvedOrderByScan({column}, std::move(scan), std::move(order_by));
    scan->set_is_ordered(true);
  }

  if (in_subquery) {
    // When in an expression subquery, we need to project down to just the
    // single output column for the subquery.
    // For all Scan types we might have here, we can just prune the column_list
    // without adding a ResolvedProjectScan.
    scan->set_column_list({column});
  }
  return scan;
}

}  // namespace

class FlattenRewriter : public Rewriter {
 public:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, const ResolvedNode& input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    GOOGLESQL_RET_CHECK(options.column_id_sequence_number() != nullptr);
    ColumnFactory column_factory(0, options.id_string_pool().get(),
                                 options.column_id_sequence_number());
    FlattenRewriterVisitor rewriter(&options, &catalog, &column_factory,
                                    type_factory);
    GOOGLESQL_RETURN_IF_ERROR(input.Accept(&rewriter));
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedNode> result,
                     rewriter.ConsumeRootNode<ResolvedNode>());
    return result;
  }

  std::string Name() const override { return "FlattenRewriter"; }
};

const Rewriter* GetFlattenRewriter() {
  static const auto* const kRewriter = new FlattenRewriter;
  return kRewriter;
}

}  // namespace googlesql
