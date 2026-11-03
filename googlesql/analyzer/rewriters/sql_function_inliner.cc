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

#include "googlesql/analyzer/rewriters/sql_function_inliner.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/common/errors.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output_properties.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/public/sql_function.h"
#include "googlesql/public/sql_tvf.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/templated_sql_function.h"
#include "googlesql/public/templated_sql_tvf.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_ast_deep_copy_visitor.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "googlesql/resolved_ast/resolved_ast_rewrite_visitor.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/resolved_ast/rewrite_utils.h"
#include "absl/algorithm/container.h"
#include "absl/base/nullability.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "googlesql/base/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {
namespace {

// Maps argument names to scalar expression arguments.
using ArgNameToExprMap =
    absl::flat_hash_map</*argument_name=*/std::string, const ResolvedExpr*>;

// Builds replacement scan subtrees for relation arguments during TVF inlining.
using ArgScanBuilder =
    std::function<absl::StatusOr<std::unique_ptr<const ResolvedScan>>(
        const ResolvedScan* arg_scan)>;

// Maps relation argument names to their scan builder closures.
using ArgNameToScanBuilderMap =
    absl::flat_hash_map</*argument_name=*/absl::string_view, ArgScanBuilder>;

// Maps function-typed parameter names to either a concrete inline lambda
// or a forwarded function parameter.
using ArgNameToFnArgMap = absl::flat_hash_map<
    /*argument_name=*/std::string,
    std::variant<const ResolvedInlineLambda*, const Function*>>;

// Adjusts correlation flags on outer column references and populates
// parameter lists for enclosing subqueries, lambdas, lateral joins, and
// graph calls in a rewritten function body.
class OuterColumnCorrelator : public ResolvedASTRewriteVisitor {
 public:
  template <typename T>
  static absl::StatusOr<std::unique_ptr<const T>> Correlate(
      std::unique_ptr<const T> node,
      const absl::flat_hash_set<ResolvedColumn>& outer_columns,
      const absl::flat_hash_set<ResolvedColumn>& pre_correlated_columns = {}) {
    OuterColumnCorrelator correlator(outer_columns, pre_correlated_columns);
    return correlator.VisitAll<T>(std::move(node));
  }

 private:
  explicit OuterColumnCorrelator(
      const absl::flat_hash_set<ResolvedColumn>& outer_columns,
      const absl::flat_hash_set<ResolvedColumn>& pre_correlated_columns)
      : outer_columns_(outer_columns),
        pre_correlated_columns_(pre_correlated_columns) {}

  // Subqueries evaluate `in_expr` in the enclosing outer scope while `subquery`
  // runs in the inner subquery scope. Re-visit `subquery` in a nested scope so
  // that column references inside the subquery are marked correlated and added
  // to the subquery's parameter list.
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedSubqueryExpr(
      std::unique_ptr<const ResolvedSubqueryExpr> node) override {
    auto builder = ToBuilder(std::move(node));
    std::unique_ptr<const ResolvedScan> subquery = builder.release_subquery();
    scope_depth_++;
    correlated_columns_stack_.push_back({});

    GOOGLESQL_ASSIGN_OR_RETURN(subquery, VisitAll<ResolvedScan>(std::move(subquery)));
    builder.set_subquery(std::move(subquery));

    builder.set_parameter_list(
        PopScopeAndAdjustParameterList(builder.release_parameter_list()));
    return std::move(builder).Build();
  }

  // Lateral joins allow `right_scan` and `join_expr` to reference output
  // columns from `left_scan`. Re-visit `right_scan` and `join_expr` in a nested
  // scope while `left_scan` remains evaluated in the outer enclosing scope.
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> PostVisitResolvedJoinScan(
      std::unique_ptr<const ResolvedJoinScan> node) override {
    if (!node->is_lateral()) {
      return node;
    }

    auto builder = ToBuilder(std::move(node));
    std::unique_ptr<const ResolvedScan> right_scan =
        builder.release_right_scan();
    std::unique_ptr<const ResolvedExpr> join_expr = builder.release_join_expr();

    scope_depth_++;
    correlated_columns_stack_.push_back({});

    GOOGLESQL_ASSIGN_OR_RETURN(right_scan, VisitAll<ResolvedScan>(std::move(right_scan)));
    if (join_expr != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(join_expr, VisitAll<ResolvedExpr>(std::move(join_expr)));
    }

    builder.set_right_scan(std::move(right_scan));
    if (join_expr != nullptr) {
      builder.set_join_expr(std::move(join_expr));
    }

    builder.set_parameter_list(
        PopScopeAndAdjustParameterList(builder.release_parameter_list()));
    return std::move(builder).Build();
  }

  // ResolvedGraphCallScan represents a GQL CALL operation, acting similarly to
  // a lateral join. Its `input_scan` is evaluated in the outer scope, while
  // `subquery` runs in the inner lateral scope.
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedGraphCallScan(
      std::unique_ptr<const ResolvedGraphCallScan> node) override {
    auto builder = ToBuilder(std::move(node));
    std::unique_ptr<const ResolvedScan> subquery = builder.release_subquery();
    if (subquery != nullptr) {
      scope_depth_++;
      correlated_columns_stack_.push_back({});

      GOOGLESQL_ASSIGN_OR_RETURN(subquery, VisitAll<ResolvedScan>(std::move(subquery)));
      builder.set_subquery(std::move(subquery));

      builder.set_parameter_list(
          PopScopeAndAdjustParameterList(builder.release_parameter_list()));
    }

    return std::move(builder).Build();
  }

  absl::Status PreVisitResolvedInlineLambda(
      const ResolvedInlineLambda& node) override {
    scope_depth_++;
    correlated_columns_stack_.push_back({});
    return absl::OkStatus();
  }

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedInlineLambda(
      std::unique_ptr<const ResolvedInlineLambda> node) override {
    auto builder = ToBuilder(std::move(node));
    builder.set_parameter_list(
        PopScopeAndAdjustParameterList(builder.release_parameter_list()));
    return std::move(builder).Build();
  }

  // Adjusts ResolvedColumnRef nodes that reference outer columns to set the
  // correlation flag when evaluated inside a nested scope.
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedColumnRef(
      std::unique_ptr<const ResolvedColumnRef> node) override {
    const ResolvedColumn& col = node->column();
    if (outer_columns_.contains(col)) {
      if (scope_depth_ > 0 && !correlated_columns_stack_.empty()) {
        correlated_columns_stack_.back().insert(col);
      }
      return ToBuilder(std::move(node))
          .set_is_correlated(scope_depth_ > 0 ||
                             pre_correlated_columns_.contains(col))
          .Build();
    }
    return node;
  }

  // Pops the active subquery, lambda, or lateral join scope and returns the
  // updated parameter list.
  std::vector<std::unique_ptr<const ResolvedColumnRef>>
  PopScopeAndAdjustParameterList(
      std::vector<std::unique_ptr<const ResolvedColumnRef>> parameters) {
    // Pop the completed scope and extract its captured columns.
    scope_depth_--;
    absl::btree_set<ResolvedColumn> captured =
        std::move(correlated_columns_stack_.back());
    correlated_columns_stack_.pop_back();

    // Update correlation flags on pre-existing outer column parameters.
    for (auto& param : parameters) {
      if (outer_columns_.contains(param->column())) {
        const_cast<ResolvedColumnRef*>(param.get())
            ->set_is_correlated(
                scope_depth_ > 0 ||
                pre_correlated_columns_.contains(param->column()));
      }
    }

    // Add newly captured outer columns and propagate them to the parent scope.
    for (const ResolvedColumn& col : captured) {
      if (!outer_columns_.contains(col)) {
        continue;
      }

      // Append the column to the parameter list if not already present.
      bool already_present = absl::c_any_of(
          parameters,
          [&col](const auto& param) { return param->column() == col; });
      if (!already_present) {
        const bool is_correlated =
            scope_depth_ > 0 || pre_correlated_columns_.contains(col);
        parameters.push_back(MakeResolvedColumnRef(col, is_correlated));
      }

      // Propagate the captured column to the enclosing scope on the stack.
      if (!correlated_columns_stack_.empty()) {
        correlated_columns_stack_.back().insert(col);
      }
    }
    // Canonicalize the parameter list order.
    SortUniqueColumnRefs(parameters);
    return parameters;
  }

  const absl::flat_hash_set<ResolvedColumn>& outer_columns_;
  const absl::flat_hash_set<ResolvedColumn>& pre_correlated_columns_;
  int scope_depth_ = 0;
  std::vector<absl::btree_set<ResolvedColumn>> correlated_columns_stack_;
};

// A visitor that updates is_correlated to false for specified outer column refs
// if they are at the top level (not inside a subquery).
class UncorrelateTopLevelColumnRefVisitor : public ResolvedASTVisitor {
 public:
  explicit UncorrelateTopLevelColumnRefVisitor(
      const absl::flat_hash_set<ResolvedColumn>& columns)
      : columns_(columns) {}

 private:
  absl::Status VisitResolvedColumnRef(const ResolvedColumnRef* node) override {
    if (subquery_depth_ == 0 && columns_.contains(node->column())) {
      const_cast<ResolvedColumnRef*>(node)->set_is_correlated(false);
    }
    return ResolvedASTVisitor::VisitResolvedColumnRef(node);
  }

  absl::Status VisitResolvedSubqueryExpr(
      const ResolvedSubqueryExpr* node) override {
    // `in_expr` and query hints belong to the enclosing outer scope.
    if (node->in_expr() != nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(node->in_expr()->Accept(this));
    }
    for (const auto& hint : node->hint_list()) {
      GOOGLESQL_RETURN_IF_ERROR(hint->Accept(this));
    }

    // `subquery` and its parameters evaluate in the nested subquery scope.
    ++subquery_depth_;
    if (node->subquery() != nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(node->subquery()->Accept(this));
    }
    for (const auto& column_ref : node->parameter_list()) {
      GOOGLESQL_RETURN_IF_ERROR(column_ref->Accept(this));
    }
    --subquery_depth_;

    if (subquery_depth_ == 0) {
      for (const auto& column_ref : node->parameter_list()) {
        if (columns_.contains(column_ref->column())) {
          const_cast<ResolvedColumnRef*>(column_ref.get())
              ->set_is_correlated(false);
        }
      }
    }
    return absl::OkStatus();
  }

  absl::Status VisitResolvedInlineLambda(
      const ResolvedInlineLambda* node) override {
    ++subquery_depth_;
    GOOGLESQL_RETURN_IF_ERROR(ResolvedASTVisitor::VisitResolvedInlineLambda(node));
    --subquery_depth_;

    if (subquery_depth_ == 0) {
      for (const auto& column_ref : node->parameter_list()) {
        if (columns_.contains(column_ref->column())) {
          const_cast<ResolvedColumnRef*>(column_ref.get())
              ->set_is_correlated(false);
        }
      }
    }
    return absl::OkStatus();
  }

  const absl::flat_hash_set<ResolvedColumn>& columns_;
  int subquery_depth_ = 0;
};

static absl::Status UncorrelateTopLevelColumnRefs(
    ResolvedNode* node, const absl::flat_hash_set<ResolvedColumn>& columns) {
  UncorrelateTopLevelColumnRefVisitor visitor(columns);
  return node->Accept(&visitor);
}

// Constructs the finalized inlined expression for a function or lambda call by
// applying SAFE error mode wrapping, transferring type annotations, and
// packaging intermediate argument bindings into a ResolvedWithExpr.
static absl::StatusOr<std::unique_ptr<const ResolvedExpr>> BuildInlinedCallExpr(
    ResolvedFunctionCall::ErrorMode error_mode, absl::string_view function_name,
    const AnnotationMap* call_annotation_map,
    std::unique_ptr<const ResolvedExpr> body,
    std::vector<std::unique_ptr<const ResolvedComputedColumn>> with_bindings,
    FunctionCallBuilder* /*absl_nullable*/ fn_builder) {
  if (error_mode == ResolvedFunctionCall::SAFE_ERROR_MODE) {
    GOOGLESQL_RET_CHECK_NE(fn_builder, nullptr);
    GOOGLESQL_RETURN_IF_ERROR(fn_builder->CheckCatalogSupportsSafeMode(function_name));
    Value null_value = Value::Null(body->type());
    GOOGLESQL_ASSIGN_OR_RETURN(
        body,
        fn_builder->IfError(std::move(body), MakeResolvedLiteral(null_value)));
  }

  const AnnotationMap* annotation_map = call_annotation_map != nullptr
                                            ? call_annotation_map
                                            : body->type_annotation_map();

  if (with_bindings.empty()) {
    if (call_annotation_map != nullptr) {
      const_cast<ResolvedExpr*>(body.get())
          ->set_type_annotation_map(annotation_map);
    }
    return body;
  }

  return ResolvedWithExprBuilder()
      .set_type(body->type())
      .set_type_annotation_map(annotation_map)
      .set_assignment_list(std::move(with_bindings))
      .set_expr(std::move(body))
      .Build();
}

static absl::StatusOr<std::unique_ptr<const ResolvedExpr>> BuildInlinedCallExpr(
    const ResolvedFunctionCall* call, std::unique_ptr<const ResolvedExpr> body,
    std::vector<std::unique_ptr<const ResolvedComputedColumn>> with_bindings,
    FunctionCallBuilder* /*absl_nullable*/ fn_builder) {
  return BuildInlinedCallExpr(call->error_mode(), call->function()->Name(),
                              call->type_annotation_map(), std::move(body),
                              std::move(with_bindings), fn_builder);
}

// Replaces argument references with references to columns or relation scans
// containing the argument values, and substitutes function-typed parameters.
class ResolvedArgumentRefReplacer : public ResolvedASTRewriteVisitor {
 public:
  // Entry point for SQL function, TVF, and UDA argument replacement.
  // Replaces argument references by copying expressions from `argument_map`,
  // building CTE scan references from `table_arg_map`, and substituting
  // function-typed parameters from `fn_arg_map`, followed by outer column
  // correlation fixup across nested scopes. If `has_unreferenced_fn_arg` is
  // non-null, sets it to true when at least one entry in `fn_arg_map` was not
  // referenced in `body`.
  template <typename T>
  static absl::StatusOr<std::unique_ptr<T>> Replace(
      std::unique_ptr<T> body, const ArgNameToExprMap& argument_map,
      const ArgNameToScanBuilderMap& table_arg_map = {},
      const ArgNameToFnArgMap& fn_arg_map = {},
      const absl::flat_hash_set<ResolvedColumn>& outer_columns = {},
      ColumnFactory* /*absl_nullable*/ column_factory = nullptr,
      FunctionCallBuilder* /*absl_nullable*/ fn_builder = nullptr,
      bool* /*absl_nullable*/ has_unreferenced_fn_arg = nullptr) {
    // Substitute argument and function-typed parameter references.
    ResolvedArgumentRefReplacer replacer(
        argument_map, table_arg_map, fn_arg_map, column_factory, fn_builder);
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const T> replaced,
                     replacer.VisitAll<T>(std::move(body)));
    if (has_unreferenced_fn_arg != nullptr) {
      *has_unreferenced_fn_arg =
          replacer.referenced_fn_args_.size() < fn_arg_map.size();
    }

    // Collect outer columns originating from argument expressions and
    // referenced lambdas.
    absl::flat_hash_set<ResolvedColumn> all_outer_columns = outer_columns;
    absl::flat_hash_set<ResolvedColumn> pre_correlated_columns;
    std::vector<std::unique_ptr<const ResolvedColumnRef>> column_refs;
    for (const auto& [_, expr] : argument_map) {
      if (expr != nullptr && CollectColumnRefs(*expr, &column_refs).ok()) {
        for (const auto& ref : column_refs) {
          all_outer_columns.insert(ref->column());
          if (ref->is_correlated()) {
            pre_correlated_columns.insert(ref->column());
          }
        }
        column_refs.clear();
      }
    }
    for (const auto& [_, fn_binding] : fn_arg_map) {
      if (std::holds_alternative<const ResolvedInlineLambda*>(fn_binding)) {
        const auto* lambda = std::get<const ResolvedInlineLambda*>(fn_binding);
        if (lambda != nullptr) {
          for (const auto& param : lambda->parameter_list()) {
            all_outer_columns.insert(param->column());
            if (param->is_correlated()) {
              pre_correlated_columns.insert(param->column());
            }
          }
        }
      }
    }

    // Correlate outer column references across nested scopes.
    if (!all_outer_columns.empty()) {
      GOOGLESQL_ASSIGN_OR_RETURN(replaced, OuterColumnCorrelator::Correlate(
                                     std::move(replaced), all_outer_columns,
                                     pre_correlated_columns));
    }

    return absl::WrapUnique(const_cast<T*>(replaced.release()));
  }

 private:
  ResolvedArgumentRefReplacer(const ArgNameToExprMap& argument_map,
                              const ArgNameToScanBuilderMap& table_arg_map,
                              const ArgNameToFnArgMap& fn_arg_map,
                              ColumnFactory* /*absl_nullable*/ column_factory,
                              FunctionCallBuilder* /*absl_nullable*/ fn_builder)
      : argument_map_(argument_map),
        table_arg_map_(table_arg_map),
        fn_arg_map_(fn_arg_map),
        column_factory_(column_factory),
        fn_builder_(fn_builder) {}

  absl::Status PreVisitResolvedWithEntry(
      const ResolvedWithEntry& node) override {
    with_entry_depth_++;
    return absl::OkStatus();
  }

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedWithEntry(
      std::unique_ptr<const ResolvedWithEntry> node) override {
    with_entry_depth_--;
    return node;
  }

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedArgumentRef(
      std::unique_ptr<const ResolvedArgumentRef> node) override {
    // Function argument references will be ResolvedArgumentRef when a
    // function's body is resolved as part of the CREATE FUNCTION statement.
    return ReferenceArgumentColumn(node->name());
  }

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedExpressionColumn(
      std::unique_ptr<const ResolvedExpressionColumn> node) override {
    // Function argument references will be ResolvedExpressionColumn when a
    // function's body is resolved using AnalyzeExpressionForAssignmentToType.
    return ReferenceArgumentColumn(node->name());
  }

  // Central dispatch for argument replacement.
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> ReferenceArgumentColumn(
      absl::string_view arg_name) {
    if (with_entry_depth_ > 0) {
      return absl::UnimplementedError(
          "SQL defined functions that contain argument references inside "
          "embedded WITH clauses are not implemented.");
    }

    auto it = argument_map_.find(arg_name);
    GOOGLESQL_RET_CHECK(it != argument_map_.end())
        << "Unresolved parameter reference without argument map: " << arg_name;

    std::unique_ptr<ResolvedExpr> copy;
    // For subsequent references to TVF scalar subqueries, remap columns to
    // avoid duplicate column definitions. Initial references and leaf
    // references (such as scalar UDF arguments) are copied directly.
    if (column_factory_ != nullptr &&
        it->second->node_kind() == RESOLVED_SUBQUERY_EXPR &&
        !copied_subquery_args_.insert(it->second).second) {
      const auto* subquery = it->second->GetAs<ResolvedSubqueryExpr>();
      GOOGLESQL_RET_CHECK_NE(subquery, nullptr);
      GOOGLESQL_RET_CHECK_NE(column_factory_, nullptr);
      GOOGLESQL_ASSIGN_OR_RETURN(copy,
                       RemapTvfSubqueryArgument(subquery, *column_factory_));
    } else {
      GOOGLESQL_ASSIGN_OR_RETURN(copy, ResolvedASTDeepCopyVisitor::Copy(it->second));
    }

    return copy;
  }

  // Allocates fresh column IDs for a TVF scalar subquery argument when it is
  // referenced multiple times across the function body, ensuring that each
  // reference defines distinct column IDs in the rewritten AST.
  static absl::StatusOr<std::unique_ptr<ResolvedExpr>> RemapTvfSubqueryArgument(
      const ResolvedSubqueryExpr* subquery, ColumnFactory& column_factory) {
    const ResolvedScan* scan = subquery->subquery();
    GOOGLESQL_RET_CHECK_NE(scan, nullptr);

    ColumnReplacementMap column_map;
    auto allocate_replacement_columns = [&column_factory, &column_map](
                                            const ResolvedColumnList& columns) {
      for (const ResolvedColumn& col : columns) {
        // TODO: Use col.annotated_type() instead of col.type() to
        // preserve type annotations when remapping TVF scalar subquery columns.
        // Use try_emplace to avoid allocating duplicate column IDs when a
        // ProjectScan passes through columns from its input scan.
        column_map.try_emplace(
            col,
            column_factory.MakeCol(col.table_name(), col.name(), col.type()));
      }
    };

    // Pre-seed column replacement IDs in topological order (input scan before
    // project scan) so new column IDs are allocated sequentially without gaps.
    if (scan->node_kind() == RESOLVED_PROJECT_SCAN) {
      const auto* project = scan->GetAs<ResolvedProjectScan>();
      if (project->input_scan() != nullptr) {
        allocate_replacement_columns(project->input_scan()->column_list());
      }
    }
    allocate_replacement_columns(scan->column_list());

    return CopyResolvedASTAndRemapColumns(*subquery, column_factory,
                                          column_map);
  }

  // Replaces ResolvedRelationArgumentScan nodes for TVF table arguments
  // with the scan produced by the ArgScanBuilder closure.
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedRelationArgumentScan(
      std::unique_ptr<const ResolvedRelationArgumentScan> node) override {
    if (table_arg_map_.empty()) {
      return node;
    }
    absl::string_view arg_name = node->name();
    const ArgScanBuilder* scan_builder =
        googlesql_base::FindOrNull(table_arg_map_, arg_name);
    GOOGLESQL_RET_CHECK_NE(scan_builder, nullptr);
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedScan> arg_scan,
                     (*scan_builder)(node.get()));
    return arg_scan;
  }

  // Replaces calls to function-typed parameters by either inlining the concrete
  // lambda body or retargeting the call to an outer function parameter.
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedFunctionCall(
      std::unique_ptr<const ResolvedFunctionCall> node) override {
    const Function* function = node->function();
    if (function == nullptr || !function->IsFunctionTypedParameter()) {
      return node;
    }
    auto it = fn_arg_map_.find(function->Name());
    if (it == fn_arg_map_.end()) {
      return node;
    }
    if (with_entry_depth_ > 0) {
      return absl::UnimplementedError(
          "SQL defined functions that contain argument references inside "
          "embedded WITH clauses are not implemented.");
    }
    referenced_fn_args_.insert(it->first);
    if (std::holds_alternative<const ResolvedInlineLambda*>(it->second)) {
      const auto* lambda = std::get<const ResolvedInlineLambda*>(it->second);
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> inlined_lambda,
                       InlineLambdaBody(std::move(node), lambda));
      return inlined_lambda;
    }
    const Function* target_fn = std::get<const Function*>(it->second);
    return ToBuilder(std::move(node)).set_function(target_fn).Build();
  }

  // Replaces function-typed parameter references passed as arguments to other
  // functions (e.g. array_transform(arr, FUNCTION f)).
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedFunctionArgument(
      std::unique_ptr<const ResolvedFunctionArgument> node) override {
    const bool is_function_typed_param =
        node->function_ref() != nullptr &&
        node->function_ref()->function() != nullptr &&
        node->function_ref()->function()->IsFunctionTypedParameter();
    if (!is_function_typed_param) {
      return node;
    }

    const Function* function = node->function_ref()->function();
    auto it = fn_arg_map_.find(function->Name());
    if (it == fn_arg_map_.end()) {
      return node;
    }
    if (with_entry_depth_ > 0) {
      return absl::UnimplementedError(
          "SQL defined functions that contain argument references inside "
          "embedded WITH clauses are not implemented.");
    }
    referenced_fn_args_.insert(it->first);

    if (std::holds_alternative<const ResolvedInlineLambda*>(it->second)) {
      const auto* lambda = std::get<const ResolvedInlineLambda*>(it->second);
      GOOGLESQL_RET_CHECK_NE(column_factory_, nullptr);
      // Remap lambda arguments and internal columns to ensure unique column IDs
      // across multiple forwardings of the same lambda.
      ColumnReplacementMap column_map;
      for (const auto& param : lambda->parameter_list()) {
        column_map[param->column()] = param->column();
      }
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedInlineLambda> lambda_copy,
                       CopyResolvedASTAndRemapColumns(*lambda, *column_factory_,
                                                      column_map));
      return ToBuilder(std::move(node))
          .set_function_ref(nullptr)
          .set_inline_lambda(std::move(lambda_copy))
          .Build();
    }

    const Function* target_fn = std::get<const Function*>(it->second);
    auto arg_builder = ToBuilder(std::move(node));
    auto fn_ref_builder = ToBuilder(arg_builder.release_function_ref());
    fn_ref_builder.set_function(target_fn);
    GOOGLESQL_ASSIGN_OR_RETURN(auto fn_ref, std::move(fn_ref_builder).Build());
    arg_builder.set_function_ref(std::move(fn_ref));
    return std::move(arg_builder).Build();
  }

  // Inlines a lambda call directly into the rewritten expression.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> InlineLambdaBody(
      std::unique_ptr<const ResolvedFunctionCall> call,
      const ResolvedInlineLambda* lambda) {
    GOOGLESQL_RET_CHECK_EQ(lambda->argument_list_size(), call->argument_list_size());
    GOOGLESQL_RET_CHECK_NE(column_factory_, nullptr);

    ColumnReplacementMap column_map;
    for (const auto& param : lambda->parameter_list()) {
      column_map[param->column()] = param->column();
    }

    const ResolvedFunctionCall::ErrorMode error_mode = call->error_mode();
    const std::string function_name = call->function()->Name();
    const AnnotationMap* type_annotation_map = call->type_annotation_map();

    auto call_builder = ToBuilder(std::move(call));
    std::vector<std::unique_ptr<const ResolvedExpr>> actual_args =
        call_builder.release_argument_list();

    std::vector<std::unique_ptr<const ResolvedComputedColumn>> with_bindings;
    with_bindings.reserve(lambda->argument_list_size());
    for (int i = 0; i < lambda->argument_list_size(); ++i) {
      std::unique_ptr<const ResolvedExpr> actual_arg =
          std::move(actual_args[i]);
      GOOGLESQL_RET_CHECK_NE(actual_arg, nullptr);
      const std::string& param_name = lambda->argument_list()[i].name();
      ResolvedColumn arg_col = column_factory_->MakeCol(
          "$inlined_lambda", param_name, actual_arg->annotated_type());
      column_map[lambda->argument_list()[i]] = arg_col;
      with_bindings.push_back(
          MakeResolvedComputedColumn(arg_col, std::move(actual_arg)));
    }

    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> body,
                     CopyResolvedASTAndRemapColumns(
                         *lambda->body(), *column_factory_, column_map));

    if (lambda->parameter_list_size() > 0) {
      absl::flat_hash_set<ResolvedColumn> captured_columns;
      for (const auto& param : lambda->parameter_list()) {
        if (!param->is_correlated()) {
          captured_columns.insert(param->column());
        }
      }
      if (!captured_columns.empty()) {
        GOOGLESQL_RETURN_IF_ERROR(
            UncorrelateTopLevelColumnRefs(body.get(), captured_columns));
      }
    }

    return BuildInlinedCallExpr(error_mode, function_name, type_annotation_map,
                                std::move(body), std::move(with_bindings),
                                fn_builder_);
  }

  // Maps scalar argument names to their replacement expressions.
  const ArgNameToExprMap& argument_map_;

  // Maps table argument names to their replacement relation scans.
  const ArgNameToScanBuilderMap& table_arg_map_;

  // Maps function-typed parameter names to concrete lambdas or forwarded
  // functions.
  const ArgNameToFnArgMap& fn_arg_map_;

  // Tracks function-typed parameter names referenced in the function body.
  absl::flat_hash_set<std::string> referenced_fn_args_;

  // Tracks pre-constructed TVF scalar subquery AST nodes that have already been
  // copied into the rewritten AST. Used inside ReferenceArgumentColumn() to
  // ensure the first reference preserves exact CTE column IDs while subsequent
  // references remap their columns to prevent duplicate column ID errors across
  // multiple argument references.
  absl::flat_hash_set<const ResolvedNode*> copied_subquery_args_;

  // Factory for allocating new column IDs.
  ColumnFactory* /*absl_nullable*/ column_factory_;

  // Builder for catalog safe mode checks and IF_ERROR expressions.
  FunctionCallBuilder* /*absl_nullable*/ fn_builder_;

  // Tracks depth inside embedded WITH entries.
  int with_entry_depth_ = 0;
};

// Helper function that checks to see if a ResolvedFunctionCall is a call to a
// function that may be inlined. If the function call is inlininable, metadata
// that is useful to the inliner is populated in 'arg_names' and
// 'fn_expression'.
//
// 'arg_names' is the name of the arguments to this function call.
// 'fn_expression' is the ResolvedAST representation of the function body. These
//     nodes may not be owned by the SQL statement being rewritten. For example,
//     they may be owned by the catalog implementation.
static absl::StatusOr<bool> IsCallInlinableAndCollectInfo(
    const ResolvedFunctionCall* call, std::vector<std::string>& arg_names,
    const ResolvedExpr*& fn_expression) {
  const Function* function = call->function();
  GOOGLESQL_RET_CHECK(function != nullptr);
  if (function->Is<SQLFunctionInterface>()) {
    auto sql_fn = call->function()->GetAs<SQLFunctionInterface>();
    arg_names = sql_fn->GetArgumentNames();
    // In case a re-resolved body is attached (for annotations) treat it as a
    // templated function.
    if (call->function_call_info() != nullptr &&
        call->function_call_info()->Is<TemplatedSQLFunctionCall>()) {
      fn_expression =
          call->function_call_info()->GetAs<TemplatedSQLFunctionCall>()->expr();
    } else {
      fn_expression = sql_fn->FunctionExpression();
    }
  } else if (function->Is<TemplatedSQLFunction>()) {
    auto sql_fn = call->function()->GetAs<TemplatedSQLFunction>();
    auto fn_call_info =
        call->function_call_info()->GetAs<TemplatedSQLFunctionCall>();
    GOOGLESQL_RET_CHECK_NE(fn_call_info, nullptr);
    arg_names = sql_fn->GetArgumentNames();
    fn_expression = fn_call_info->expr();
  } else {
    return false;
  }
  if (call->hint_list_size() > 0) {
    // Function inlining leaves no place to attach function call hints. It's not
    // clear that inlining a function call with hints is the right thing to do.
    return absl::UnimplementedError(
        absl::StrCat("Hinted calls to SQL defined function '", function->Name(),
                     "' are not supported."));
  }
  return true;
}

// A visitor that replaces calls to SQL UDFs with the resolved function body.
class SqlFunctionInlineVisitor : public ResolvedASTDeepCopyVisitor {
 public:
  SqlFunctionInlineVisitor(const AnalyzerOptions& analyzer_options,
                           Catalog& catalog, ColumnFactory* column_factory,
                           TypeFactory& type_factory)
      : column_factory_(column_factory),
        fn_builder_(analyzer_options, catalog, type_factory) {}

  // Returns true if any inlined UDF had an unreferenced function-typed
  // parameter.
  bool has_unreferenced_fn_arg() const { return has_unreferenced_fn_arg_; }

 private:
  absl::Status VisitResolvedFunctionCall(
      const ResolvedFunctionCall* node) override {
    std::vector<std::string> arg_names;
    const ResolvedExpr* fn_expression;
    GOOGLESQL_ASSIGN_OR_RETURN(bool is_inlinable, IsCallInlinableAndCollectInfo(
                                            node, arg_names, fn_expression));
    if (is_inlinable) {
      GOOGLESQL_RET_CHECK_NE(fn_expression, nullptr)
          << "No function expression supplied with resolved call to SQL "
          << "function " << node->DebugString();
      return InlineSqlFunction(node, arg_names, fn_expression);
    }
    return CopyVisitResolvedFunctionCall(node);
  }

  // Evaluates argument expressions in the caller scope, substitutes arguments
  // and function-typed parameters in the function body, and packages the
  // inlined result into a ResolvedWithExpr.
  absl::Status InlineSqlFunction(const ResolvedFunctionCall* call,
                                 absl::Span<const std::string> argument_names,
                                 const ResolvedExpr* fn_expression) {
    const bool use_generic = call->generic_argument_list_size() > 0;
    const int num_args = use_generic ? call->generic_argument_list_size()
                                     : call->argument_list_size();
    GOOGLESQL_RET_CHECK_EQ(num_args, argument_names.size());
    GOOGLESQL_RET_CHECK_NE(column_factory_, nullptr);

    // The input function body is potentially owned by a catalog or some other
    // component. Copy the body so that its column ids are compatible with the
    // invoking query and the expression is locally owned.
    ColumnReplacementMap column_map;
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> body_expr,
                     CopyResolvedASTAndRemapColumns(
                         *fn_expression, *column_factory_, column_map));

    // Disambiguate any CTEs defined locally within the callee's body to prevent
    // them from capturing outer CTE references when arguments or closures
    // (such as lambdas) are substituted into the callee.
    GOOGLESQL_RETURN_IF_ERROR(RemapCteNames(body_expr.get(), inlined_cte_counter_));

    // Nullary functions peel one layer per rewriter pass to maintain
    // convergence limits across rewriter iterations.
    if (argument_names.empty()) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::unique_ptr<const ResolvedExpr> inlined_call,
          BuildInlinedCallExpr(call, std::move(body_expr),
                               /*with_bindings=*/{}, &fn_builder_));
      PushNodeToStack(
          absl::WrapUnique(const_cast<ResolvedExpr*>(inlined_call.release())));
      return absl::OkStatus();
    }

    ArgNameToExprMap arg_map;
    ArgNameToFnArgMap fn_arg_map;
    std::vector<std::unique_ptr<const ResolvedExpr>> col_refs;
    std::vector<std::unique_ptr<const ResolvedComputedColumn>>
        with_expr_bindings;
    col_refs.reserve(argument_names.size());
    with_expr_bindings.reserve(argument_names.size());

    for (int i = 0; i < num_args; ++i) {
      const std::string& arg_name = argument_names[i];
      if (use_generic) {
        const auto* gen_arg = call->generic_argument_list(i);
        if (gen_arg->inline_lambda() != nullptr) {
          fn_arg_map[arg_name] = gen_arg->inline_lambda();
          continue;
        }
        if (gen_arg->function_ref() != nullptr) {
          const Function* fn = gen_arg->function_ref()->function();
          GOOGLESQL_RET_CHECK(fn != nullptr && fn->IsFunctionTypedParameter())
              << "Passing non-parameter function references as UDF arguments "
                 "is not supported by the inliner.";
          fn_arg_map[arg_name] = fn;
          continue;
        }
      }

      const ResolvedExpr* arg_expr =
          use_generic ? call->generic_argument_list(i)->expr()
                      : call->argument_list(i);
      GOOGLESQL_RET_CHECK_NE(arg_expr, nullptr);
      GOOGLESQL_RETURN_IF_ERROR(arg_expr->Accept(this));
      auto actual_arg = ConsumeTopOfStack<ResolvedExpr>();
      ResolvedColumn arg_column = column_factory_->MakeCol(
          absl::StrCat("$inlined_", call->function()->Name()), arg_name,
          actual_arg->annotated_type());
      col_refs.push_back(
          MakeResolvedColumnRef(arg_column, /*is_correlated=*/false));
      arg_map[arg_name] = col_refs.back().get();
      with_expr_bindings.push_back(
          MakeResolvedComputedColumn(arg_column, std::move(actual_arg)));
    }

    // Rewrite the function body so that it references the columns in
    // with_expr_bindings rather than having ResolvedArgumentRefs, and
    // substitutes function-typed parameters.
    absl::flat_hash_set<ResolvedColumn> outer_columns;
    outer_columns.reserve(with_expr_bindings.size());
    for (const auto& binding : with_expr_bindings) {
      outer_columns.insert(binding->column());
    }
    bool has_unreferenced_fn_arg = false;
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> replaced_body,
                     ResolvedArgumentRefReplacer::Replace(
                         std::move(body_expr), arg_map, /*table_arg_map=*/{},
                         fn_arg_map, outer_columns, column_factory_,
                         &fn_builder_, &has_unreferenced_fn_arg));
    if (has_unreferenced_fn_arg) {
      has_unreferenced_fn_arg_ = true;
    }

    // Inlines nested function calls in the callee body. Arguments in
    // with_expr_bindings were already visited in the caller environment.
    GOOGLESQL_RETURN_IF_ERROR(replaced_body->Accept(this));
    auto inlined_body = ConsumeTopOfStack<ResolvedExpr>();

    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> inlined_call,
        BuildInlinedCallExpr(call, std::move(inlined_body),
                             std::move(with_expr_bindings), &fn_builder_));
    PushNodeToStack(
        absl::WrapUnique(const_cast<ResolvedExpr*>(inlined_call.release())));
    return absl::OkStatus();
  }

  ColumnFactory* column_factory_;
  FunctionCallBuilder fn_builder_;
  int inlined_cte_counter_ = 0;
  bool has_unreferenced_fn_arg_ = false;
};

// Rewriter pass that inlines scalar SQL UDF calls in a ResolvedAST.
class SqlFunctionInliner : public Rewriter {
 public:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, const ResolvedNode& input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    GOOGLESQL_RET_CHECK(options.column_id_sequence_number() != nullptr);
    ColumnFactory column_factory(0, options.id_string_pool().get(),
                                 options.column_id_sequence_number());

    SqlFunctionInlineVisitor rewriter(options, catalog, &column_factory,
                                      type_factory);
    GOOGLESQL_RETURN_IF_ERROR(input.Accept(&rewriter));
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedNode> result,
                     rewriter.ConsumeRootNode<ResolvedNode>());
    if (rewriter.has_unreferenced_fn_arg()) {
      GOOGLESQL_ASSIGN_OR_RETURN(result,
                       PruneUnusedCorrelatedParameters(std::move(result)));
    }
    return result;
  }

  std::string Name() const override { return "SqlFunctionInliner"; }
};

// A visitor that replaces calls to SQL TVFs with the resolved function body.
class SqlTableFunctionInlineVistor : public ResolvedASTDeepCopyVisitor {
 public:
  explicit SqlTableFunctionInlineVistor(ColumnFactory* column_factory)
      : column_factory_(column_factory) {}

 private:
  absl::StatusOr<bool> IsCallInlinable(const ResolvedTVFScan* scan) {
    if (scan->hint_list_size() > 0) {
      // Function inlining leaves no place to hang function call hints. It's not
      // clear that inlining a function call with hints is even the right thing
      // to do.
      return false;
    }
    const TableValuedFunction* function = scan->tvf();
    GOOGLESQL_RET_CHECK_NE(function, nullptr)
        << "Expected ResolvedTableFunctionScan to have non-null function";
    return function->Is<SQLTableValuedFunction>() ||
           function->Is<TemplatedSQLTVF>();
  }

  absl::Status VisitResolvedTVFScan(const ResolvedTVFScan* tvf_scan) override {
    GOOGLESQL_ASSIGN_OR_RETURN(bool inlinable, IsCallInlinable(tvf_scan));
    if (inlinable) {
      return InlineTVF(tvf_scan);
    }
    return CopyVisitResolvedTVFScan(tvf_scan);
  }

  absl::Status ErrorIfArgumentIsCorrelated(const ResolvedNode& arg,
                                           int64_t arg_number,
                                           absl::string_view arg_name) {
    std::vector<std::unique_ptr<const ResolvedColumnRef>> column_refs;
    GOOGLESQL_RETURN_IF_ERROR(CollectColumnRefs(arg, &column_refs));
    if (!column_refs.empty()) {
      return absl::UnimplementedError(absl::StrCat(
          "TVF arguments that reference columns are not supported. ", "Arg #",
          arg_number, " ('", arg_name, "') references column '",
          column_refs[0]->column().name(), "'."));
    }
    return absl::OkStatus();
  }

  // This function replaces a ResolvedTVFScan that invokes a SQL table function
  // with a query that computes the function result directly. The
  // transformation looks a bit like this:
  //
  // SELECT ... FROM MyTvf() AS t;
  // ~~>
  // (SELECT ... FROM (tvf_query) AS t
  absl::Status InlineTVF(const ResolvedTVFScan* scan) {
    GOOGLESQL_RET_CHECK_NE(scan, nullptr);
    GOOGLESQL_RET_CHECK_NE(column_factory_, nullptr);
    const ResolvedScan* query = nullptr;
    std::vector<std::string> argument_names;
    if (scan->tvf()->Is<SQLTableValuedFunction>()) {
      const auto* sql_tvf = scan->tvf()->GetAs<SQLTableValuedFunction>();
      GOOGLESQL_RET_CHECK_NE(sql_tvf, nullptr);
      query = sql_tvf->query();
      GOOGLESQL_RET_CHECK_NE(query, nullptr);
      argument_names = sql_tvf->GetArgumentNames();
    } else if (scan->tvf()->Is<TemplatedSQLTVF>()) {
      const auto* sql_tvf = scan->tvf()->GetAs<TemplatedSQLTVF>();
      GOOGLESQL_RET_CHECK_NE(sql_tvf, nullptr);
      query = scan->signature()
                  ->GetAs<TemplatedSQLTVFSignature>()
                  ->resolved_templated_query()
                  ->query();
      argument_names = sql_tvf->GetArgumentNames();
    } else {
      return absl::InternalError(
          "Inlining only supports SQL TVFs and TemplateTVFs.");
    }

    std::unique_ptr<ResolvedScan> body_scan;
    if (scan->tvf()->sql_security() ==
        ResolvedCreateStatementEnums::SQL_SECURITY_DEFINER) {
      ColumnReplacementMap column_map;
      GOOGLESQL_ASSIGN_OR_RETURN(
          body_scan,
          ReplaceScanColumns(
              *column_factory_, *query, scan->column_index_list(),
              CreateReplacementColumns(*column_factory_, scan->column_list(),
                                       column_map)));
      GOOGLESQL_ASSIGN_OR_RETURN(body_scan,
                       CreateResolvedExecuteAsRoleScan(
                           *column_factory_, column_map, std::move(body_scan),
                           /*original_inlined_view=*/nullptr, scan->tvf()));
    } else {
      // TODO We should decide what to do in the case of
      // UNSPECIFIED, to be consistent with VIEWs and the desired behavior.
      GOOGLESQL_ASSIGN_OR_RETURN(body_scan, ReplaceScanColumns(*column_factory_, *query,
                                                     scan->column_index_list(),
                                                     scan->column_list()));
    }

    // Nullary functions get special treatment because we don't have to do any
    // special argument processing.
    if (scan->argument_list_size() == 0) {
      PushNodeToStack(std::move(body_scan));
      return absl::OkStatus();
    }

    GOOGLESQL_RET_CHECK_EQ(argument_names.size(), scan->argument_list_size());

    // The inlined TVF will become a subquery that contains one CTE query per
    // table argument and one CTE query that computes all scalar arguments with
    // as-if-once semantics.
    std::vector<std::unique_ptr<const ResolvedWithEntry>> with_entry_list;

    // Traverse TVF arguments: wrap relation arguments in CTE scans (table_args)
    // and collect scalar argument expressions to be computed together inside a
    // single shared CTE row (scalars_cte_name).
    std::vector<std::unique_ptr<const ResolvedComputedColumn>> scalar_arg_exprs;
    std::vector<ResolvedColumn> arg_columns;
    ArgNameToExprMap scalar_args;
    std::vector<std::unique_ptr<const ResolvedExpr>> owned_scalar_subqueries;
    std::vector<std::string> scalar_arg_names;
    scalar_arg_names.reserve(scan->argument_list_size());
    std::string scalars_cte_name =
        absl::StrCat("$inlined_", scan->tvf()->Name(), "_scalar_args");
    ArgNameToScanBuilderMap table_args = ArgNameToScanBuilderMap{};
    for (int i = 0; i < scan->argument_list_size(); ++i) {
      const ResolvedFunctionArgument* arg = scan->argument_list(i);
      const std::string& arg_name = argument_names[i];
      // Handle relation/table arguments by wrapping each in a WITH CTE entry.
      if (scan->argument_list(i)->scan() != nullptr) {
        GOOGLESQL_ASSIGN_OR_RETURN(auto arg_scan, ProcessNode<ResolvedScan>(arg->scan()));
        GOOGLESQL_RET_CHECK_GE(scan->argument_list_size(), 1);
        GOOGLESQL_RETURN_IF_ERROR(
            ErrorIfArgumentIsCorrelated(*arg_scan, i + 1, arg_name));
        const std::string& arg_cte_name = argument_names[i];
        with_entry_list.emplace_back(
            MakeResolvedWithEntry(arg_cte_name, std::move(arg_scan)));
        table_args[argument_names[i]] =
            [arg_cte_name](const ResolvedScan* arg_scan)
            -> absl::StatusOr<std::unique_ptr<const ResolvedScan>> {
          auto with_ref =
              ResolvedWithRefScanBuilder().set_with_query_name(arg_cte_name);
          for (ResolvedColumn col : arg_scan->column_list()) {
            with_ref.add_column_list(col);
          }
          return std::move(with_ref).Build();
        };
        continue;
      }

      // Handle scalar arguments by collecting expressions into a shared CTE
      // row.
      const ResolvedExpr* argument = scan->argument_list(i)->expr();
      if (argument == nullptr) {
        return absl::UnimplementedError(
            absl::StrCat("TVF argument #", i + 1, " ('", arg_name,
                         "') is not an argument kind supported by inlining."));
      }
      GOOGLESQL_RET_CHECK_NE(argument, nullptr);
      GOOGLESQL_RETURN_IF_ERROR(ErrorIfArgumentIsCorrelated(*argument, i + 1, arg_name));
      GOOGLESQL_RETURN_IF_ERROR(argument->Accept(this));
      auto arg_expr = ConsumeTopOfStack<ResolvedExpr>();
      scalar_arg_names.push_back(argument_names[i]);
      ResolvedColumn arg_column = column_factory_->MakeCol(
          absl::StrCat("$inlined_", scan->tvf()->Name()), arg_name,
          arg_expr->annotated_type());
      scalar_arg_exprs.push_back(
          MakeResolvedComputedColumn(arg_column, std::move(arg_expr)));
      arg_columns.push_back(arg_column);
    }

    // Build a scalar subquery (SELECT arg_idx FROM scalars_cte_name) for each
    // scalar argument and populate scalar_args so that argument references in
    // the TVF body can be replaced directly.
    for (int idx = 0; idx < arg_columns.size(); ++idx) {
      std::string scan_name = absl::StrCat("$inlined_", scan->tvf()->Name());
      auto with_ref =
          ResolvedWithRefScanBuilder().set_with_query_name(scalars_cte_name);
      ResolvedProjectScanBuilder project;
      ResolvedSubqueryExprBuilder subquery;
      for (int c = 0; c < arg_columns.size(); ++c) {
        // TODO: Use arg_columns[c].annotated_type() and propagate
        // type annotations on the scalar subquery expression.
        ResolvedColumn col = column_factory_->MakeCol(
            scan_name, arg_columns[c].name(), arg_columns[c].type());
        with_ref.add_column_list(col);
        if (c == idx) {
          project.add_column_list(col);
          subquery.set_type(col.type());
        }
      }
      GOOGLESQL_ASSIGN_OR_RETURN(auto subquery_expr,
                       std::move(subquery)
                           .set_subquery_type(ResolvedSubqueryExpr::SCALAR)
                           .set_in_expr(nullptr)
                           .set_subquery(std::move(project).set_input_scan(
                               std::move(with_ref)))
                           .Build());
      owned_scalar_subqueries.push_back(std::move(subquery_expr));
      scalar_args[scalar_arg_names[idx]] = owned_scalar_subqueries.back().get();
    }
    if (!scalar_arg_exprs.empty()) {
      with_entry_list.emplace_back(MakeResolvedWithEntry(
          scalars_cte_name,
          MakeResolvedProjectScan(arg_columns, std::move(scalar_arg_exprs),
                                  MakeResolvedSingleRowScan())));
    }

    // Rewrite the function body so that scalar argument references are replaced
    // by scalar subqueries scanning the scalars_cte_name CTE. TVFs do not use
    // top-level ResolvedWithExpr bindings.
    GOOGLESQL_ASSIGN_OR_RETURN(
        body_scan,
        ResolvedArgumentRefReplacer::Replace(
            std::move(body_scan), scalar_args, table_args,
            /*fn_arg_map=*/{}, /*outer_columns=*/{}, column_factory_));

    GOOGLESQL_RET_CHECK(!with_entry_list.empty());
    // This variable prevents use-after move ambiguity in the following stmt.
    const std::vector<ResolvedColumn>& columns = body_scan->column_list();
    PushNodeToStack(MakeResolvedWithScan(columns, std::move(with_entry_list),
                                         std::move(body_scan),
                                         /*recursive=*/false));
    return absl::OkStatus();
  }

 private:
  ColumnFactory* column_factory_;
};

class SqlTvfInliner : public Rewriter {
 public:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, const ResolvedNode& input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    GOOGLESQL_RET_CHECK(options.column_id_sequence_number() != nullptr);
    ColumnFactory column_factory(0, options.id_string_pool().get(),
                                 options.column_id_sequence_number());
    SqlTableFunctionInlineVistor rewriter(&column_factory);
    GOOGLESQL_RETURN_IF_ERROR(input.Accept(&rewriter));
    return rewriter.ConsumeRootNode<ResolvedNode>();
  }

  std::string Name() const override { return "SqlTvfInliner"; }
};

// Returns the `TemplatedSQLFunctionCall` representing the attached SQL body
// resolved for that particular SQL function call. The result is never null when
// the call if for a templated SQL function, or a concrete SQL function with
// annotated args that differ from the function declaration.
// For non-SQL functions, the result is always `nullptr`.
//
// When a concrete SQL function is invoked with annotated args which do not
// match the annotations on the argument declaration, the function acts as a
// templated function: it is re-resolved and the annotated body for that
// invocation is attached.
static const TemplatedSQLFunctionCall*
GetInfoForTemplatedOrReResolvedSqlFunctionCall(
    const ResolvedAggregateFunctionCall* call) {
  if (call->function()->Is<TemplatedSQLFunction>()) {
    return call->function_call_info()->GetAs<TemplatedSQLFunctionCall>();
  }
  if (!call->function()->Is<SQLFunctionInterface>()) {
    // Not a SQL function.
    return nullptr;
  }
  if (call->function_call_info() == nullptr ||
      !call->function_call_info()->Is<TemplatedSQLFunctionCall>()) {
    return nullptr;
  }
  return call->function_call_info()->GetAs<TemplatedSQLFunctionCall>();
}

class SqlAggregateFunctionInlineVisitor : public ResolvedASTRewriteVisitor {
 public:
  explicit SqlAggregateFunctionInlineVisitor(ColumnFactory& column_factory)
      : column_factory_(column_factory) {}

 private:
  // The data-structures representing concrete and template SQL functions are
  // different. This struct lets us gather the common bits so that inlining
  // logic can be more agnostic to whether the function was concrete or a
  // template.
  struct AggregateFnDetails {
    const ResolvedAggregateFunctionCall* call;
    const ResolvedExpr* expr;
    const std::vector<std::unique_ptr<const ResolvedComputedColumnBase>>&
        aggregate_expression_list;
    std::vector<std::string> arg_names;
    ResolvedColumn computed_column;
  };

  // These lists usually travel together as we handle UDAs on the same scan.
  struct UdaRewriteContext {
    // Pre-aggregate expressions collected for inlined UDAs.
    std::vector<std::unique_ptr<const ResolvedComputedColumn>>
        pre_aggregate_exprs;
    // Pre-aggregate columns, including those collected for inlined UDAs.
    // Initialized with columns from the original input scan. Expressions added
    // to `pre_aggregate_exprs` are appended to this list.
    std::vector<ResolvedColumn> pre_aggregate_cols;
    // The new list of aggregates. Includes aggregations that were not
    // rewritten.
    std::vector<std::unique_ptr<const ResolvedComputedColumnBase>>
        new_aggregates;
    // The column list produced for the list of aggregates post-rewrite.
    // Corresponds 1:1 with `new_aggregates`.
    std::vector<ResolvedColumn> new_aggr_col_list;
    // Post-aggregate expressions produced by the rewrite.
    std::vector<std::unique_ptr<const ResolvedComputedColumn>>
        post_aggregate_exprs;

    explicit UdaRewriteContext(
        absl::Span<const ResolvedColumn> pre_aggregate_cols)
        : pre_aggregate_cols(pre_aggregate_cols.begin(),
                             pre_aggregate_cols.end()) {}
  };

  // Returns true if the function body is SQL-defined.
  bool IsSqlDefined(const Function* function) {
    return function->Is<SQLFunctionInterface>() ||
           function->Is<TemplatedSQLFunction>();
  }

  // Returns true if the aggregate function `call` (or any of its nested
  // aggregate functions) contain a nested SQL-defined function.
  // Note that this does not check if `call` itself is a SQL-defined function;
  // it just checks for the existence of a nested SQL-defined function within
  // the aggregate function subtree.
  absl::StatusOr<bool> ContainsNestedSqlDefinedFunction(
      const ResolvedAggregateFunctionCall* call) {
    for (const auto& nested_agg : call->group_by_aggregate_list()) {
      GOOGLESQL_RET_CHECK(nested_agg->expr()->Is<ResolvedAggregateFunctionCall>());
      const auto& nested_agg_call =
          nested_agg->expr()->GetAs<ResolvedAggregateFunctionCall>();
      if (IsSqlDefined(nested_agg_call->function())) {
        return true;
      }
      GOOGLESQL_ASSIGN_OR_RETURN(bool contains_nested_sql_defined_function,
                       ContainsNestedSqlDefinedFunction(nested_agg_call));
      if (contains_nested_sql_defined_function) {
        return true;
      }
    }
    return false;
  }

  // Check to see if the aggregate function (or one of its nested aggregate
  // functions) is SQL-defined and return details required for inlining if so.
  // Returns std::nullopt if no SQL-defined functions are found.
  // Returns an error if the function (or one of its nested aggregate
  // functions) requires inlining but has a shape not supported by the inliner.
  absl::StatusOr<std::optional<AggregateFnDetails>> IsInlineable(
      const ResolvedAggregateFunctionCall* call) {
    const Function* function = call->function();
    const ParseLocationRange* error_location =
        call->GetParseLocationRangeOrNULL();

    // Check if any nested aggregate function is a SQL-defined function. The
    // inliner does not currently support inlining nested UDAs.
    GOOGLESQL_ASSIGN_OR_RETURN(bool contains_nested_sql_defined_function,
                     ContainsNestedSqlDefinedFunction(call));
    if (contains_nested_sql_defined_function) {
      return MakeSqlErrorAtStart(error_location)
             << "SQL function inliner cannot inline aggregate function "
             << function->SQLName() << " with nested UDA";
    }

    if (!IsSqlDefined(function)) {
      return std::nullopt;
    }
    if (call->error_mode() == ResolvedFunctionCall::SAFE_ERROR_MODE) {
      // TODO: Support SAFE mode calls using IFERROR.
      return MakeSqlErrorAtStart(error_location)
             << "SQL function inliner cannot inline aggregate function "
             << function->SQLName() << " with SAFE mode modifier";
    }
    if (call->distinct()) {
      // TODO: Decide semantics for this clause before inlining it.
      return MakeSqlErrorAtStart(error_location)
             << "SQL function inliner cannot inline aggregate function "
             << function->SQLName() << " with DISTINCT modifier";
    }
    if (call->limit() != nullptr) {
      // TODO: Decide semantics for this clause before inlining it.
      return MakeSqlErrorAtStart(error_location)
             << "SQL function inliner cannot inline aggregate function "
             << function->SQLName() << " with LIMIT modifier";
    }
    if (call->order_by_item_list_size() > 0) {
      // TODO: Decide semantics for this clause before inlining it.
      return MakeSqlErrorAtStart(error_location)
             << "SQL function inliner cannot inline aggregate function "
             << function->SQLName() << " with ORDER BY modifier";
    }
    if (call->having_modifier() != nullptr) {
      // TODO: Decide semantics for this clause before inlining it.
      return MakeSqlErrorAtStart(error_location)
             << "SQL function inliner cannot inline aggregate function "
             << function->SQLName() << " with HAVING modifier";
    }
    if (call->null_handling_modifier() ==
        ResolvedNonScalarFunctionCallBase::RESPECT_NULLS) {
      // TODO: Decide semantics for this clause before inlining it.
      return MakeSqlErrorAtStart(error_location)
             << "SQL function inliner cannot inline aggregate function "
             << function->SQLName() << " with RESPECT NULLS modifier";
    }
    if (call->null_handling_modifier() ==
        ResolvedNonScalarFunctionCallBase::IGNORE_NULLS) {
      // TODO: Decide semantics for this clause before inlining it.
      return MakeSqlErrorAtStart(error_location)
             << "SQL function inliner cannot inline aggregate function "
             << function->SQLName() << " with IGNORE NULLS modifier";
    }
    if (!call->group_by_list().empty()) {
      // TODO: Decide semantics for this clause before inlining it.
      return MakeSqlErrorAtStart(error_location)
             << "SQL function inliner cannot inline aggregate function "
             << function->SQLName() << " with GROUP BY modifier";
    }
    if (call->where_expr() != nullptr) {
      // TODO: Decide semantics for this clause before inlining it.
      return MakeSqlErrorAtStart(error_location)
             << "SQL function inliner cannot inline aggregate function "
             << function->SQLName() << " with WHERE filter modifier";
    }
    if (call->having_expr() != nullptr) {
      // TODO: Decide semantics for this clause before inlining it.
      return MakeSqlErrorAtStart(error_location)
             << "SQL function inliner cannot inline aggregate function "
             << function->SQLName() << " with HAVING filter modifier";
    }
    const TemplatedSQLFunctionCall* fn_call_info =
        GetInfoForTemplatedOrReResolvedSqlFunctionCall(call);
    if (fn_call_info != nullptr) {
      return AggregateFnDetails{
          .call = call,
          .expr = fn_call_info->expr(),
          .aggregate_expression_list =
              fn_call_info->aggregate_expression_list(),
          .arg_names = call->function()->Is<TemplatedSQLFunction>()
                           ? call->function()
                                 ->GetAs<TemplatedSQLFunction>()
                                 ->GetArgumentNames()
                           : call->function()
                                 ->GetAs<SQLFunctionInterface>()
                                 ->GetArgumentNames(),
      };
    } else if (function->Is<SQLFunctionInterface>()) {
      auto* fn = function->GetAs<SQLFunctionInterface>();
      // If a body were attached, we would have already treated it as a
      // templated function.
      GOOGLESQL_RET_CHECK(call->function_call_info() == nullptr ||
                !call->function_call_info()->Is<TemplatedSQLFunctionCall>());
      return AggregateFnDetails{
          .call = call,
          .expr = fn->FunctionExpression(),
          .aggregate_expression_list = *fn->aggregate_expression_list(),
          .arg_names = fn->GetArgumentNames(),
      };
    }
    GOOGLESQL_RET_CHECK_FAIL() << "Return should be unreachable.";
  }

  absl::Status RewriteAggregation(
      const AggregateFnDetails& details,
      std::unique_ptr<const ResolvedAggregateFunctionCall> aggr,
      UdaRewriteContext& context) {
    auto aggr_expr_builder = ToBuilder(std::move(aggr));
    auto aggr_args = aggr_expr_builder.release_argument_list();
    ArgNameToExprMap aggregate_args;
    ArgNameToExprMap non_aggregate_args;
    std::vector<std::unique_ptr<const ResolvedExpr>> owned_aggregate_arg_refs;
    FunctionSignature signature = aggr_expr_builder.signature();

    // This logic assumes no repeated args.
    GOOGLESQL_RET_CHECK_EQ(details.arg_names.size(), aggr_args.size());
    for (int i = 0; i < aggr_args.size(); ++i) {
      bool is_non_aggregate_arg =
          signature.arguments()[i].options().is_not_aggregate();
      std::unique_ptr<const ResolvedExpr>& arg = aggr_args[i];
      if (is_non_aggregate_arg) {
        // If we ever extend non-aggregate args beyond these types, the
        // rewriter will need to change to accommodate as-if-evaluated-once
        // semantics. The ResolvedAST is not expressive enough for that right
        // now without introducing an artificial array construction above the
        // aggregation which some query optimizers would not remove. The
        // expressive power that is needed is a lateral join with a single row
        // table on the LHS.
        const ResolvedExpr* without_cast = arg.get();
        // LINT.IfChange(non_aggregate_args_def)
        while (without_cast->node_kind() == RESOLVED_CAST) {
          without_cast = without_cast->GetAs<ResolvedCast>()->expr();
        }
        ResolvedNodeKind expr_kind = without_cast->node_kind();
        GOOGLESQL_RET_CHECK(expr_kind == RESOLVED_LITERAL ||
                  expr_kind == RESOLVED_PARAMETER ||
                  expr_kind == RESOLVED_ARGUMENT_REF);
        // LINT.ThenChange(../expr_resolver_helper.cc:non_aggregate_args_def)
        // This collection is used exclusively by the post-aggregate
        // expression.
        non_aggregate_args.emplace(details.arg_names[i], arg.get());
        // This collection is used for the arguments to the aggregate
        // functions. Non-aggregate args can be used there too, so we add
        // these args to both collections.
        aggregate_args.emplace(details.arg_names[i], arg.get());
      } else {
        // This is an aggregate arg.
        ResolvedColumn new_arg_column = column_factory_.MakeCol(
            absl::StrCat("$inlined_", details.call->function()->Name()),
            details.arg_names[i], arg->annotated_type());
        GOOGLESQL_ASSIGN_OR_RETURN(auto new_arg_computed_col,
                         ResolvedComputedColumnBuilder()
                             .set_column(new_arg_column)
                             .set_expr(std::move(arg))
                             .Build());
        context.pre_aggregate_exprs.push_back(std::move(new_arg_computed_col));
        context.pre_aggregate_cols.push_back(new_arg_column);
        owned_aggregate_arg_refs.push_back(
            MakeResolvedColumnRef(new_arg_column, /*is_correlated=*/false));
        aggregate_args.emplace(details.arg_names[i],
                               owned_aggregate_arg_refs.back().get());
      }
    }

    // SQL-defined aggregates have any aggregations internal to the function
    // body already factored out. Those aggregations will be promoted into the
    // new copy of the AggregateScan. Those are processed in this loop,
    // collecting the processed aggregations in `new_aggregates` and also the
    // columns they are written into in `new_aggr_col_list`. These lists will
    // later be used to build the new AggregateScan.

    // Aggregates that are internal to the function body, once copied, have
    // new column id. This map is used to replace references to those column
    // ids in the post-aggregate expression.
    ColumnReplacementMap internal_aggregate_remapping;
    ColumnReplacementMap no_replacements;
    ArgNameToScanBuilderMap no_table_args;
    for (auto& aggr_computed_col : details.aggregate_expression_list) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          auto new_aggr_computed_col,
          CopyResolvedASTAndRemapColumns(*aggr_computed_col, column_factory_,
                                         no_replacements));
      GOOGLESQL_ASSIGN_OR_RETURN(new_aggr_computed_col,
                       ResolvedArgumentRefReplacer::Replace(
                           std::move(new_aggr_computed_col), aggregate_args));
      internal_aggregate_remapping.emplace(aggr_computed_col->column(),
                                           new_aggr_computed_col->column());
      context.new_aggr_col_list.push_back(new_aggr_computed_col->column());
      context.new_aggregates.push_back(std::move(new_aggr_computed_col));
    }
    GOOGLESQL_ASSIGN_OR_RETURN(
        auto post_aggregate_function_body,
        CopyResolvedASTAndRemapColumns(*details.expr, column_factory_,
                                       internal_aggregate_remapping));
    GOOGLESQL_ASSIGN_OR_RETURN(
        auto post_aggregate_expr,
        ResolvedArgumentRefReplacer::Replace(
            std::move(post_aggregate_function_body), non_aggregate_args));
    GOOGLESQL_ASSIGN_OR_RETURN(auto post_aggregate_computed_col,
                     ResolvedComputedColumnBuilder()
                         .set_column(details.computed_column)
                         .set_expr(std::move(post_aggregate_expr))
                         .Build());
    context.post_aggregate_exprs.emplace_back(
        std::move(post_aggregate_computed_col));
    return absl::OkStatus();
  }

  absl::Status RewriteAggregations(
      std::vector<std::unique_ptr<const ResolvedComputedColumnBase>>
          old_aggregates,
      std::unique_ptr<const ResolvedScan>& input_scan,
      absl::flat_hash_map<const ResolvedAggregateFunctionCall*,
                          AggregateFnDetails>& calls_to_inline,
      absl::Span<const ResolvedColumn> post_aggregate_column_list,
      UdaRewriteContext& context) {
    absl::flat_hash_set<ResolvedColumn> columns_to_remove_from_aggr;

    for (auto& aggr_column : old_aggregates) {
      GOOGLESQL_RET_CHECK(aggr_column->Is<ResolvedComputedColumn>());
      auto aggr = aggr_column->GetAs<ResolvedComputedColumn>();
      GOOGLESQL_RET_CHECK(aggr->expr()->Is<ResolvedAggregateFunctionCall>());
      const ResolvedAggregateFunctionCall* aggr_function_call =
          aggr->expr()->GetAs<ResolvedAggregateFunctionCall>();

      if (!calls_to_inline.contains(aggr_function_call)) {
        context.new_aggregates.emplace_back(std::move(aggr_column));
        continue;
      }
      AggregateFnDetails& details = calls_to_inline.at(aggr_function_call);
      details.computed_column = aggr->column();
      columns_to_remove_from_aggr.insert(aggr->column());

      auto agg_call = absl::WrapUnique(
          ToBuilder(absl::WrapUnique(
                        aggr_column.release()->GetAs<ResolvedComputedColumn>()))
              .release_expr()
              .release()
              ->GetAs<ResolvedAggregateFunctionCall>());

      GOOGLESQL_RETURN_IF_ERROR(
          RewriteAggregation(details, std::move(agg_call), context));
    }

    for (const auto& old_aggr_col : post_aggregate_column_list) {
      if (!columns_to_remove_from_aggr.contains(old_aggr_col)) {
        context.new_aggr_col_list.push_back(old_aggr_col);
      }
    }

    if (!context.pre_aggregate_exprs.empty()) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          input_scan,
          ResolvedProjectScanBuilder()
              .set_input_scan(std::move(input_scan))
              .set_expr_list(std::move(context.pre_aggregate_exprs))
              .set_column_list(std::move(context.pre_aggregate_cols))
              .Build());
    }
    return absl::OkStatus();
  }

  absl::StatusOr<absl::flat_hash_map<const ResolvedAggregateFunctionCall*,
                                     AggregateFnDetails>>
  GetCallsToInline(const ResolvedAggregateScan* node) {
    absl::flat_hash_map<const ResolvedAggregateFunctionCall*,
                        AggregateFnDetails>
        calls_to_inline;
    for (const auto& column : node->aggregate_list()) {
      const auto* col = column->GetAs<ResolvedComputedColumnImpl>();
      GOOGLESQL_RET_CHECK(col->expr()->Is<ResolvedAggregateFunctionCall>());
      const ResolvedAggregateFunctionCall* aggr_function_call =
          col->expr()->GetAs<ResolvedAggregateFunctionCall>();
      GOOGLESQL_ASSIGN_OR_RETURN(std::optional<AggregateFnDetails> details,
                       IsInlineable(aggr_function_call));
      if (details.has_value()) {
        calls_to_inline.emplace(aggr_function_call, *details);
      }
    }
    return calls_to_inline;
  }

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedAggregateScan(
      std::unique_ptr<const ResolvedAggregateScan> node) override {
    GOOGLESQL_ASSIGN_OR_RETURN(auto calls_to_inline, GetCallsToInline(node.get()));
    if (calls_to_inline.empty()) {
      return node;
    }

    ResolvedAggregateScanBuilder aggr_builder = ToBuilder(std::move(node));

    // The post-aggregation Project will have the same column list as the input
    // aggregate scan. The new aggregate scan will not have columns associated
    // with re-written function calls.
    std::vector<ResolvedColumn> post_aggregate_column_list =
        aggr_builder.column_list();

    std::unique_ptr<const ResolvedScan> input_scan =
        aggr_builder.release_input_scan();

    UdaRewriteContext context(input_scan->column_list());
    GOOGLESQL_RETURN_IF_ERROR(RewriteAggregations(aggr_builder.release_aggregate_list(),
                                        input_scan, calls_to_inline,
                                        post_aggregate_column_list, context));

    return ResolvedProjectScanBuilder()
        .set_input_scan(
            std::move(aggr_builder)
                .set_input_scan(std::move(input_scan))
                .set_aggregate_list(std::move(context.new_aggregates))
                .set_column_list(std::move(context.new_aggr_col_list)))
        .set_expr_list(std::move(context.post_aggregate_exprs))
        .set_column_list(std::move(post_aggregate_column_list))
        .Build();
  }

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedAggregationThresholdAggregateScan(
      std::unique_ptr<const ResolvedAggregationThresholdAggregateScan> node)
      override {
    for (const auto& computed_column : node->aggregate_list()) {
      GOOGLESQL_RET_CHECK(computed_column->Is<ResolvedComputedColumnImpl>());
      auto computed_col = computed_column->GetAs<ResolvedComputedColumnImpl>();
      if (computed_col->expr()->Is<ResolvedAggregateFunctionCall>() &&
          (computed_col->expr()
               ->GetAs<ResolvedAggregateFunctionCall>()
               ->function()
               ->Is<SQLFunctionInterface>() ||
           computed_col->expr()
               ->GetAs<ResolvedAggregateFunctionCall>()
               ->function()
               ->Is<TemplatedSQLFunction>())) {
        return MakeSqlErrorAtStart(computed_col->expr()
                                       ->GetAs<ResolvedAggregateFunctionCall>()
                                       ->GetParseLocationRangeOrNULL())
               << "Aggregation threshold is not supported with user defined "
                  "aggregate function";
      }
    }
    return node;
  }

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedPivotScan(
      std::unique_ptr<const ResolvedPivotScan> node) override {
    absl::flat_hash_map<const ResolvedAggregateFunctionCall*,
                        AggregateFnDetails>
        calls_to_inline;
    for (const auto& expr : node->pivot_expr_list()) {
      GOOGLESQL_RET_CHECK(expr->Is<ResolvedAggregateFunctionCall>());
      const auto* call = expr->GetAs<ResolvedAggregateFunctionCall>();
      GOOGLESQL_ASSIGN_OR_RETURN(std::optional<AggregateFnDetails> details,
                       IsInlineable(call));
      if (details.has_value()) {
        calls_to_inline.emplace(call, *details);
      }
    }
    if (!calls_to_inline.empty()) {
      return absl::InvalidArgumentError(
          "SQL-defined aggregate functions are not supported in PIVOT");
    }
    return node;
  }

 private:
  ColumnFactory& column_factory_;
};

class SqlUdaInliner : public Rewriter {
 public:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, std::unique_ptr<const ResolvedNode> input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    GOOGLESQL_RET_CHECK(options.column_id_sequence_number() != nullptr);
    ColumnFactory column_factory(0, options.id_string_pool().get(),
                                 options.column_id_sequence_number());
    SqlAggregateFunctionInlineVisitor rewriter(column_factory);
    return rewriter.VisitAll(std::move(input));
  }

  std::string Name() const override { return "SqlUdaInliner"; }
};

}  // namespace

const Rewriter* GetSqlFunctionInliner() {
  static const auto* const kRewriter = new SqlFunctionInliner;
  return kRewriter;
}

const Rewriter* GetSqlTvfInliner() {
  static const auto* const kRewriter = new SqlTvfInliner;
  return kRewriter;
}

const Rewriter* GetSqlAggregateInliner() {
  static const auto* const kRewriter = new SqlUdaInliner;
  return kRewriter;
}

}  // namespace googlesql
