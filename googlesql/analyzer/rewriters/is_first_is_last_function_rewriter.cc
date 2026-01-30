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

#include "googlesql/analyzer/rewriters/is_first_is_last_function_rewriter.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output_properties.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_ast_rewrite_visitor.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/rewrite_utils.h"
#include "absl/status/statusor.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

// Propagates the rewrite information for a single call to IS_FIRST() or
// IS_LAST() back to its enclosing ResolvedAnalyticFunctionGroup, to record the
// new analytic computed columns that compose the replacement expression.
struct CallRewriteInfo {
  // Captures the new expression for the IS_FIRST() or IS_LAST() call,
  // referencing the new ROW_NUMBER() and COUNT(*) columns, adding the
  // condition, etc. Carries the same column ID as the original call.
  std::unique_ptr<const ResolvedComputedColumn> new_expr;

  // Captures the new ROW_NUMBER() and COUNT(*) computed columns and their new
  // column IDs.
  std::vector<std::unique_ptr<const ResolvedComputedColumnBase>> new_columns;

  // Captures the argument expression for the IS_FIRST() and IS_LAST() calls, to
  // be captured in a ProjectScan to avoid repeating it.
  std::unique_ptr<const ResolvedComputedColumn> arg_expr;
};

// Propagates the rewrite information for a single ResolvedAnalyticFunctionGroup
// back to its enclosing ResolvedAnalyticScan, to update its `column_list` and
// create a ProjectScan to replace the column with the final expression
// composing ROW_NUMBER(), k (plus COUNT(*) for IS_LAST()) as well as error &
// NULL handling.
struct GroupRewriteInfo {
  std::unique_ptr<const ResolvedAnalyticFunctionGroup> new_group;
  std::vector<std::unique_ptr<const ResolvedComputedColumn>> new_exprs;
  std::vector<std::unique_ptr<const ResolvedComputedColumn>> arg_exprs;
};

static std::unique_ptr<const ResolvedColumnRef> MakeColumnRef(
    const ResolvedColumn& column) {
  return MakeResolvedColumnRef(column.type(), column,
                               /*is_correlated=*/false);
}

// A visitor that rewrites IS_FIRST() and IS_LAST() into ROW_NUMBER() as
// follows:
//
//   # Note: `w` can be a named or unnamed window, but it cannot have a frame
//   either way
// IS_FIRST(k) OVER(w) -->
//      IF(k < 0, ERROR(‘k cannot be negative’), ROW_NUMBER() OVER(w) <= k)
//
// SAFE.IS_FIRST(k) OVER(w) -->
//      IF(k < 0, NULL, ROW_NUMBER() OVER(w) <= k)
//
// IS_LAST(k) OVER(w) -->
//     IF (k < 0,
//         ERROR(‘k cannot be negative’),
//         ROW_NUMBER() OVER (w) + k > COUNT(*) OVER (w))
//
// SAFE.IS_LAST(k) OVER(w) -->
//     IF (k < 0,
//         NULL,
//         ROW_NUMBER() OVER (w) + k > COUNT(*) OVER (w))
//
// The rewriter, thus, needs to add the appropriate ROW_NUMBER() and COUNT(*)
// calls on the ResolvedAnalyticFunctionGroup, as well as a ResolvedProjectScan
// to replace the original columns for IS_FIRST() or IS_LAST() and point them
// to the new computation instead.
//
// Note that because `k` may be an expression, we cannot simply copy it, we need
// once-semantics for all its references (the condition of IF, as well as
// comparison with ROW_NUMBER()). We have to isolate it into a project scan
// before the input, to avoid repeating it as it may contain volatile functions
// or repeated column IDs (from subqueries).
class IsFirstIsLastFunctionRewriteVisitor : public ResolvedASTRewriteVisitor {
 public:
  IsFirstIsLastFunctionRewriteVisitor(const AnalyzerOptions& analyzer_options,
                                      Catalog& catalog,
                                      TypeFactory& type_factory,
                                      ColumnFactory& column_factory)
      : type_factory_(type_factory),
        fn_builder_(analyzer_options, catalog, type_factory),
        column_factory_(column_factory) {}

 private:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedAnalyticScan(
      std::unique_ptr<const ResolvedAnalyticScan> node) override {
    // This project scan captures all the arguments to IS_FIRST() and IS_LAST()
    // to avoid repeating them in the IF() condition, and gives them new
    // column IDs.
    auto input_project_builder = ResolvedProjectScanBuilder().set_column_list(
        node->input_scan()->column_list());

    // This project scan will go on top of the modified analytic scan to
    // ensure the final columns are the same for downstream operators. This
    // is where we collect the CASE... rewrites.
    ResolvedProjectScanBuilder output_project_builder;
    output_project_builder.set_column_list(node->column_list());

    auto analytic_builder = ToBuilder(std::move(node));

    // Rebuild the column list to include the new analytic computations,
    // ROW_NUMBER(), and additionally COUNT(*) for IS_LAST().
    analytic_builder.set_column_list(
        analytic_builder.input_scan()->column_list());

    for (auto& group : analytic_builder.release_function_group_list()) {
      GOOGLESQL_ASSIGN_OR_RETURN(auto rewrite_info, RewriteGroup(std::move(group)));
      for (const auto& call :
           rewrite_info.new_group->analytic_function_list()) {
        analytic_builder.add_column_list(call->column());
      }
      analytic_builder.add_function_group_list(
          std::move(rewrite_info.new_group));
      for (auto& new_expr : rewrite_info.new_exprs) {
        output_project_builder.add_expr_list(std::move(new_expr));
      }

      for (auto& arg_expr : rewrite_info.arg_exprs) {
        analytic_builder.add_column_list(arg_expr->column());
        input_project_builder.add_column_list(arg_expr->column());
        input_project_builder.add_expr_list(std::move(arg_expr));
      }
    }

    input_project_builder.set_input_scan(analytic_builder.release_input_scan());
    analytic_builder.set_input_scan(std::move(input_project_builder));

    return std::move(output_project_builder)
        .set_input_scan(std::move(analytic_builder))
        .Build();
  }

  absl::StatusOr<GroupRewriteInfo> RewriteGroup(
      std::unique_ptr<const ResolvedAnalyticFunctionGroup> node) {
    std::vector<std::unique_ptr<const ResolvedComputedColumn>> new_exprs;
    std::vector<std::unique_ptr<const ResolvedComputedColumn>> arg_exprs;
    auto builder = ToBuilder(std::move(node));
    for (auto& call : builder.release_analytic_function_list()) {
      GOOGLESQL_RET_CHECK(call->Is<ResolvedComputedColumn>());
      const auto* computed_col = call->GetAs<ResolvedComputedColumn>();
      GOOGLESQL_RET_CHECK(computed_col->expr()->Is<ResolvedAnalyticFunctionCall>());
      const auto* analytic_call =
          computed_col->expr()->GetAs<ResolvedAnalyticFunctionCall>();

      if (IsBuiltInFunctionIdEq(analytic_call, FN_IS_FIRST) ||
          IsBuiltInFunctionIdEq(analytic_call, FN_IS_LAST)) {
        GOOGLESQL_ASSIGN_OR_RETURN(CallRewriteInfo rewrite_info,
                         RewriteCall(std::move(call)));
        new_exprs.push_back(std::move(rewrite_info.new_expr));
        arg_exprs.push_back(std::move(rewrite_info.arg_expr));
        for (auto& new_column : rewrite_info.new_columns) {
          builder.add_analytic_function_list(std::move(new_column));
        }
      } else {
        builder.add_analytic_function_list(std::move(call));
      }
    }

    GOOGLESQL_ASSIGN_OR_RETURN(auto new_node, std::move(builder).Build());
    return GroupRewriteInfo{.new_group = std::move(new_node),
                            .new_exprs = std::move(new_exprs),
                            .arg_exprs = std::move(arg_exprs)};
  }

  absl::StatusOr<CallRewriteInfo> RewriteCall(
      std::unique_ptr<const ResolvedComputedColumnBase> node) {
    GOOGLESQL_RET_CHECK(node->Is<ResolvedComputedColumn>());
    ResolvedColumn column = node->GetAs<ResolvedComputedColumn>()->column();

    auto expr = ToBuilder(std::unique_ptr<const ResolvedComputedColumn>(
                              node.release()->GetAs<ResolvedComputedColumn>()))
                    .release_expr();
    GOOGLESQL_RET_CHECK(expr->Is<ResolvedAnalyticFunctionCall>());
    auto call = std::unique_ptr<const ResolvedAnalyticFunctionCall>(
        expr.release()->GetAs<ResolvedAnalyticFunctionCall>());

    const bool is_safe =
        call->error_mode() == ResolvedFunctionCall::SAFE_ERROR_MODE;
    const bool is_first = IsBuiltInFunctionIdEq(call.get(), FN_IS_FIRST);
    auto args = ToBuilder(std::move(call)).release_argument_list();
    GOOGLESQL_RET_CHECK_EQ(args.size(), 1);

    const Type* arg_type = args[0]->type();
    auto arg_column = MakeResolvedComputedColumn(
        column_factory_.MakeCol(column.table_name(), "$analytic_arg", arg_type),
        std::move(args[0]));

    GOOGLESQL_ASSIGN_OR_RETURN(
        auto less_than_zero,
        fn_builder_.Less(
            MakeColumnRef(arg_column->column()),
            MakeResolvedLiteral(type_factory_.get_int64(), Value::Int64(0))));
    GOOGLESQL_ASSIGN_OR_RETURN(auto arg_is_null,
                     fn_builder_.IsNull(MakeColumnRef(arg_column->column())));
    std::vector<std::unique_ptr<const ResolvedExpr>> or_args;
    or_args.push_back(std::move(arg_is_null));
    or_args.push_back(std::move(less_than_zero));
    GOOGLESQL_ASSIGN_OR_RETURN(auto arg_is_null_or_less_than_zero,
                     fn_builder_.Or(std::move(or_args)));

    std::unique_ptr<const ResolvedExpr> error_or_null;
    if (is_safe) {
      error_or_null =
          MakeResolvedLiteral(type_factory_.get_bool(), Value::NullBool());
    } else {
      GOOGLESQL_ASSIGN_OR_RETURN(error_or_null,
                       fn_builder_.Error("k cannot be null or negative",
                                         type_factory_.get_bool()));
    }

    std::vector<std::unique_ptr<const ResolvedComputedColumnBase>> new_columns;
    GOOGLESQL_ASSIGN_OR_RETURN(auto row_number, fn_builder_.RowNumber());
    ResolvedColumn row_number_column = column_factory_.MakeCol(
        column.table_name(), column.name(), type_factory_.get_int64());
    new_columns.push_back(
        MakeResolvedComputedColumn(row_number_column, std::move(row_number)));

    std::unique_ptr<const ResolvedExpr> actual_rewrite;
    if (is_first) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          actual_rewrite,
          fn_builder_.LessOrEqual(MakeColumnRef(row_number_column),
                                  MakeColumnRef(arg_column->column())));
    } else {
      GOOGLESQL_ASSIGN_OR_RETURN(auto count, fn_builder_.CountStar(/*is_distinct=*/false,
                                                         /*is_analytic=*/true));
      GOOGLESQL_RET_CHECK(count->Is<ResolvedAnalyticFunctionCall>());

      ResolvedColumn count_column = column_factory_.MakeCol(
          column.table_name(), column.name(), type_factory_.get_uint64());
      new_columns.push_back(
          MakeResolvedComputedColumn(count_column, std::move(count)));

      std::vector<std::unique_ptr<const ResolvedExpr>> add_args;
      add_args.push_back(MakeColumnRef(row_number_column));
      add_args.push_back(MakeColumnRef(arg_column->column()));
      GOOGLESQL_ASSIGN_OR_RETURN(auto row_num_plus_arg,
                       fn_builder_.NestedBinaryInt64Add(std::move(add_args)));
      GOOGLESQL_ASSIGN_OR_RETURN(actual_rewrite,
                       fn_builder_.Greater(std::move(row_num_plus_arg),
                                           MakeColumnRef(count_column)));
    }

    GOOGLESQL_ASSIGN_OR_RETURN(
        auto new_expr,
        fn_builder_.If(std::move(arg_is_null_or_less_than_zero),
                       std::move(error_or_null), std::move(actual_rewrite)));

    return CallRewriteInfo{
        .new_expr = MakeResolvedComputedColumn(column, std::move(new_expr)),
        .new_columns = std::move(new_columns),
        .arg_expr = std::move(arg_column)};
  }

  TypeFactory& type_factory_;
  FunctionCallBuilder fn_builder_;
  ColumnFactory& column_factory_;
};

}  // namespace

class IsFirstIsLastFunctionRewriter : public Rewriter {
 public:
  std::string Name() const override { return "IsFirstIsLastFunctionRewriter"; }

  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, std::unique_ptr<const ResolvedNode> input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    GOOGLESQL_RET_CHECK(options.id_string_pool() != nullptr);
    GOOGLESQL_RET_CHECK(options.column_id_sequence_number() != nullptr);
    ColumnFactory column_factory(/*max_seen_col_id=*/0,
                                 *options.id_string_pool(),
                                 *options.column_id_sequence_number());
    IsFirstIsLastFunctionRewriteVisitor rewriter(options, catalog, type_factory,
                                                 column_factory);
    return rewriter.VisitAll(std::move(input));
  };
};

const Rewriter* GetIsFirstIsLastFunctionRewriter() {
  static const auto* const kRewriter = new IsFirstIsLastFunctionRewriter();
  return kRewriter;
}

}  // namespace googlesql
