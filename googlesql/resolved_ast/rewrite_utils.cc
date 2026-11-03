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

#include "googlesql/resolved_ast/rewrite_utils.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/errors.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/annotation/collation.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/coercer.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/functions/differential_privacy.pb.h"
#include "googlesql/public/input_argument_type.h"
#include "googlesql/public/interval_value.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/time_series_tvf_util.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/make_node_vector.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_ast_deep_copy_visitor.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "googlesql/resolved_ast/resolved_ast_helper.h"
#include "googlesql/resolved_ast/resolved_ast_rewrite_visitor.h"
#include "googlesql/resolved_ast/resolved_ast_visitor.h"
#include "googlesql/resolved_ast/resolved_collation.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "absl/algorithm/container.h"
#include "absl/base/nullability.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "googlesql/base/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_builder.h"

namespace googlesql {
namespace {

struct ConcreteArgument {
  const Type* type;
  size_t num_occurrences;
};

static absl::StatusOr<FunctionSignature> MakeConcreteSignature(
    const FunctionSignature& signature, const Type* result_type,
    std::vector<ConcreteArgument> concrete_args) {
  // Some best-effort checks.
  if (signature.result_type().kind() == ARG_KIND_EXPR_FIXED) {
    GOOGLESQL_RET_CHECK(result_type->Equals(signature.result_type().type()))
        << "result_type: " << result_type->DebugString()
        << " signature.result_type(): "
        << signature.result_type().type()->DebugString();
  }
  GOOGLESQL_RET_CHECK_LE(concrete_args.size(), signature.arguments().size());

  std::vector<FunctionArgumentType> args;
  args.reserve(signature.arguments().size());
  for (int i = 0; i < concrete_args.size(); ++i) {
    const auto& [type, num_occurrences] = concrete_args[i];
    GOOGLESQL_RET_CHECK_GE(num_occurrences, 1);
    args.emplace_back(type, signature.argument(i).options(), num_occurrences);
    args.back().set_original_kind(signature.argument(i).kind());
  }
  // All the remaining unsupplied arguments must be optional or repeated.
  for (int i = static_cast<int>(concrete_args.size());
       i < signature.arguments().size(); ++i) {
    GOOGLESQL_RET_CHECK(signature.argument(i).optional() ||
              signature.argument(i).repeated())
        << "argument " << i << " is required but not supplied: "
        << signature.argument(i).DebugString() << "\n"
        << "signature: " << signature.DebugString();
  }

  FunctionArgumentType result(result_type, signature.result_type().options(),
                              /*num_occurrences=*/1);
  result.set_original_kind(signature.result_type().kind());

  FunctionSignature concrete_signature(std::move(result), std::move(args),
                                       signature.context_id(),
                                       signature.options());
  GOOGLESQL_RET_CHECK(concrete_signature.IsConcrete());
  return concrete_signature;
}

static const FunctionSignature* GetSignature(const Function* function,
                                             FunctionSignatureId signature_id) {
  ABSL_DCHECK(function != nullptr);
  ABSL_DCHECK(function->IsGoogleSQLBuiltin());
  for (const FunctionSignature& sig : function->signatures()) {
    if (sig.context_id() == signature_id) {
      return &sig;
    }
  }
  return nullptr;
}

// REQUIRES: `function` is a GoogleSQL built-in function.
// Retrieves the target signature, and uses that signature to construct a
// concrete signature with the given concrete arguments types.
static absl::StatusOr<FunctionSignature> MakeConcreteSignature(
    const Function* function, FunctionSignatureId signature_id,
    const Type* result_type, std::vector<ConcreteArgument> concrete_args) {
  GOOGLESQL_RET_CHECK(function != nullptr);
  GOOGLESQL_RET_CHECK(function->IsGoogleSQLBuiltin());

  const FunctionSignature* signature = GetSignature(function, signature_id);
  GOOGLESQL_RET_CHECK(signature != nullptr)
      << "Signature ID " << FunctionSignatureId_Name(signature_id)
      << " not found on function " << function->FullName();
  return MakeConcreteSignature(*signature, result_type,
                               std::move(concrete_args));
}

// A visitor to check whether the ResolvedAST has ResolvedGroupingCall nodes.
class GroupingCallDetectorVisitor : public ResolvedASTVisitor {
 public:
  explicit GroupingCallDetectorVisitor(bool* has_grouping_call)
      : has_grouping_call_(has_grouping_call) {}

  absl::Status VisitResolvedAggregateScan(
      const ResolvedAggregateScan* node) override {
    if (!node->grouping_call_list().empty()) {
      *has_grouping_call_ = true;
    }
    return DefaultVisit(node);
  }

 private:
  bool* has_grouping_call_;
};

// A visitor that changes ResolvedColumnRef nodes to be correlated.
class CorrelateColumnRefVisitor : public ResolvedASTDeepCopyVisitor {
 private:
  // Logic that determines whether an individual ResolvedColumnRef should be
  // marked as correlated in the output tree.
  bool ShouldBeCorrelated(const ResolvedColumnRef& ref) {
    if (in_subquery_or_lambda_ || local_columns_.contains(ref.column())) {
      // Columns in 'local_columns_' and columns inside subqueries or lambda
      // bodies are fully local to the expression. We shouldn't change the
      // is_correlated state for local columns.
      return ref.is_correlated();
    }
    return true;
  }

  std::unique_ptr<ResolvedColumnRef> CorrelateColumnRef(
      const ResolvedColumnRef& ref) {
    return MakeResolvedColumnRef(ref.column(), ShouldBeCorrelated(ref));
  }

  template <class T>
  void CorrelateParameterList(T* node) {
    for (auto& column_ref : node->parameter_list()) {
      const_cast<ResolvedColumnRef*>(column_ref.get())
          ->set_is_correlated(ShouldBeCorrelated(*column_ref));
    }
  }

  absl::Status VisitResolvedColumnRef(const ResolvedColumnRef* node) override {
    PushNodeToStack(CorrelateColumnRef(*node));
    return absl::OkStatus();
  }

  absl::Status VisitResolvedSubqueryExpr(
      const ResolvedSubqueryExpr* node) override {
    {
      ++in_subquery_or_lambda_;
      absl::Cleanup decrementer = [&] { --in_subquery_or_lambda_; };
      GOOGLESQL_RETURN_IF_ERROR(
          ResolvedASTDeepCopyVisitor::VisitResolvedSubqueryExpr(node));
    }

    // If this is the first lambda or subquery encountered, we need to correlate
    // the column references in the parameter list and for the in expression.
    // Column references of outer columns are already correlated.
    if (in_subquery_or_lambda_ == 0) {
      std::unique_ptr<ResolvedSubqueryExpr> expr =
          ConsumeTopOfStack<ResolvedSubqueryExpr>();
      CorrelateParameterList(expr.get());
      if (expr->in_expr() != nullptr) {
        GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> in_expr,
                         ProcessNode(expr->in_expr()));
        expr->set_in_expr(std::move(in_expr));
      }
      PushNodeToStack(std::move(expr));
    }
    return absl::OkStatus();
  }

  absl::Status VisitResolvedInlineLambda(
      const ResolvedInlineLambda* node) override {
    {
      ++in_subquery_or_lambda_;
      absl::Cleanup decrementer = [&] { --in_subquery_or_lambda_; };
      GOOGLESQL_RETURN_IF_ERROR(
          ResolvedASTDeepCopyVisitor::VisitResolvedInlineLambda(node));
    }

    // If this is the first lambda or subquery encountered, we need to correlate
    // the column references in the parameter list. Column references of outer
    // columns are already correlated.
    if (in_subquery_or_lambda_ == 0) {
      std::unique_ptr<ResolvedInlineLambda> expr =
          ConsumeTopOfStack<ResolvedInlineLambda>();
      CorrelateParameterList(expr.get());
      PushNodeToStack(std::move(expr));
    }
    return absl::OkStatus();
  }

  absl::Status VisitResolvedWithExpr(const ResolvedWithExpr* node) override {
    // Exclude the assignment columns because they are internal.
    for (int i = 0; i < node->assignment_list_size(); ++i) {
      local_columns_.insert(node->assignment_list(i)->column());
    }
    return ResolvedASTDeepCopyVisitor::VisitResolvedWithExpr(node);
  }

  absl::Status VisitResolvedArrayAggregate(
      const ResolvedArrayAggregate* node) override {
    // Exclude the element column because it is internal.
    local_columns_.insert(node->element_column());
    // And exclude the compute columns.
    for (const std::unique_ptr<const ResolvedComputedColumn>& computed_column :
         node->pre_aggregate_computed_column_list()) {
      local_columns_.insert(computed_column->column());
    }
    return ResolvedASTDeepCopyVisitor::VisitResolvedArrayAggregate(node);
  }

  // Columns that are local to an expression -- that is they are defined,
  // populated, and consumed fully within the expression -- should not be
  // correlated by this code.
  absl::flat_hash_set<ResolvedColumn> local_columns_;

  // Tracks if we're inside a subquery. We stop correlating when we're inside a
  // subquery as column references are either already correlated or don't need
  // to be.
  int in_subquery_or_lambda_ = 0;
};

// A visitor which collects the ResolvedColumnRef that are referenced, but not
// local to this expression.
class ColumnRefCollectorOwned : public ColumnRefVisitor {
 public:
  explicit ColumnRefCollectorOwned(
      std::vector<std::unique_ptr<const ResolvedColumnRef>>* column_refs,
      bool correlate)
      : column_refs_(column_refs), correlate_(correlate) {}

 private:
  absl::Status VisitResolvedColumnRef(const ResolvedColumnRef* node) override {
    if (!IsLocalColumn(node->column())) {
      column_refs_->push_back(MakeResolvedColumnRef(
          node->column(), correlate_ || node->is_correlated()));
    }
    return absl::OkStatus();
  }

  std::vector<std::unique_ptr<const ResolvedColumnRef>>* column_refs_;
  bool correlate_;
};

// Prunes unreferenced column references from `parameter_list` on correlation
// boundaries bottom-up across a Resolved AST.
class UnusedCorrelatedParameterPruner : public ResolvedASTRewriteVisitor {
 private:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedSubqueryExpr(
      std::unique_ptr<const ResolvedSubqueryExpr> node) override {
    if (node->parameter_list().empty()) {
      return node;
    }
    auto builder = ToBuilder(std::move(node));
    std::vector<std::unique_ptr<const ResolvedColumnRef>> params =
        builder.release_parameter_list();
    GOOGLESQL_RETURN_IF_ERROR(RemoveUnusedColumnRefs(*builder.subquery(), params));
    return std::move(builder).set_parameter_list(std::move(params)).Build();
  }

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedInlineLambda(
      std::unique_ptr<const ResolvedInlineLambda> node) override {
    if (node->parameter_list().empty()) {
      return node;
    }
    auto builder = ToBuilder(std::move(node));
    std::vector<std::unique_ptr<const ResolvedColumnRef>> params =
        builder.release_parameter_list();
    GOOGLESQL_RETURN_IF_ERROR(RemoveUnusedColumnRefs(*builder.body(), params));
    return std::move(builder).set_parameter_list(std::move(params)).Build();
  }

  absl::StatusOr<std::unique_ptr<const ResolvedNode>> PostVisitResolvedJoinScan(
      std::unique_ptr<const ResolvedJoinScan> node) override {
    if (node->parameter_list().empty()) {
      return node;
    }
    auto builder = ToBuilder(std::move(node));
    std::vector<std::unique_ptr<const ResolvedColumnRef>> params =
        builder.release_parameter_list();
    GOOGLESQL_RETURN_IF_ERROR(RemoveUnusedColumnRefs(*builder.right_scan(), params));
    return std::move(builder).set_parameter_list(std::move(params)).Build();
  }

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedGraphCallScan(
      std::unique_ptr<const ResolvedGraphCallScan> node) override {
    if (node->subquery() == nullptr || node->parameter_list().empty()) {
      return node;
    }
    auto builder = ToBuilder(std::move(node));
    std::vector<std::unique_ptr<const ResolvedColumnRef>> params =
        builder.release_parameter_list();
    GOOGLESQL_RETURN_IF_ERROR(RemoveUnusedColumnRefs(*builder.subquery(), params));
    return std::move(builder).set_parameter_list(std::move(params)).Build();
  }
};

static absl::Status SetCollationList(const LanguageOptions& language_options,
                                     ResolvedFunctionCallBase& resolved_call) {
  GOOGLESQL_RET_CHECK(resolved_call.collation_list().empty());
  if (!language_options.LanguageFeatureEnabled(FEATURE_COLLATION_SUPPORT)) {
    return absl::OkStatus();
  }

  if (!resolved_call.signature().options().uses_operation_collation()) {
    // This function doesn't use collation in its operation, so leave the
    // collation_list empty.
    return absl::OkStatus();
  }
  GOOGLESQL_ASSIGN_OR_RETURN(auto annotation_map,
                   CollationAnnotation().GetCollationFromFunctionArguments(
                       /*error_location=*/nullptr, resolved_call,
                       FunctionEnums::AFFECTS_OPERATION));
  if (annotation_map != nullptr && !annotation_map->Empty()) {
    GOOGLESQL_ASSIGN_OR_RETURN(ResolvedCollation resolved_collation,
                     ResolvedCollation::MakeResolvedCollation(*annotation_map));
    resolved_call.add_collation_list(std::move(resolved_collation));
  }
  return absl::OkStatus();
}

// Visitor to detect if a node contains WithScan.
class WithScanVisitor : public googlesql::ResolvedASTVisitor {
 public:
  WithScanVisitor() : contains_with_scan_(false) {}
  bool contains_with_scan() const { return contains_with_scan_; }

 private:
  absl::Status VisitResolvedWithScan(
      const googlesql::ResolvedWithScan* node) override {
    contains_with_scan_ = true;
    return absl::OkStatus();
  }

  bool contains_with_scan_ = false;
};

}  // namespace

absl::StatusOr<std::unique_ptr<ResolvedExpr>> CorrelateColumnRefsImpl(
    const ResolvedExpr& expr) {
  CorrelateColumnRefVisitor correlator;
  GOOGLESQL_RETURN_IF_ERROR(expr.Accept(&correlator));
  return correlator.ConsumeRootNode<ResolvedExpr>();
}

absl::Status CollectColumnRefs(
    const ResolvedNode& node,
    std::vector<std::unique_ptr<const ResolvedColumnRef>>* column_refs,
    bool correlate) {
  ColumnRefCollectorOwned column_ref_collector(column_refs, correlate);
  return node.Accept(&column_ref_collector);
}

absl::Status RemoveUnusedColumnRefs(
    const ResolvedNode& node,
    std::vector<std::unique_ptr<const ResolvedColumnRef>>& column_refs) {
  std::vector<std::unique_ptr<const ResolvedColumnRef>> refs;
  GOOGLESQL_RETURN_IF_ERROR(CollectColumnRefs(node, &refs));
  absl::flat_hash_set<int> referenced_column_ids;
  for (const auto& ref : refs) {
    referenced_column_ids.insert(ref->column().column_id());
  }

  column_refs.erase(std::remove_if(column_refs.begin(), column_refs.end(),
                                   [&](const auto& ref) {
                                     return !referenced_column_ids.contains(
                                         ref->column().column_id());
                                   }),
                    column_refs.end());
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<const ResolvedNode>>
PruneUnusedCorrelatedParameters(std::unique_ptr<const ResolvedNode> node) {
  UnusedCorrelatedParameterPruner pruner;
  return pruner.VisitAll(std::move(node));
}

void SortUniqueColumnRefs(
    std::vector<std::unique_ptr<const ResolvedColumnRef>>& column_refs) {
  // Compare two referenced columns.
  auto cmp = [](const std::unique_ptr<const ResolvedColumnRef>& l,
                const std::unique_ptr<const ResolvedColumnRef>& r) {
    if (l->column().column_id() != r->column().column_id()) {
      return l->column().column_id() < r->column().column_id();
    }
    return l->is_correlated() < r->is_correlated();
  };

  auto eq = [](const std::unique_ptr<const ResolvedColumnRef>& l,
               const std::unique_ptr<const ResolvedColumnRef>& r) {
    return l->column().column_id() == r->column().column_id() &&
           l->is_correlated() == r->is_correlated();
  };

  // Erase any duplicates from the referenced columns list.
  std::sort(column_refs.begin(), column_refs.end(), cmp);
  column_refs.erase(std::unique(column_refs.begin(), column_refs.end(), eq),
                    column_refs.end());
}

absl::Status CollectSortUniqueColumnRefs(
    const ResolvedNode& node,
    std::vector<std::unique_ptr<const ResolvedColumnRef>>& column_refs,
    bool correlate) {
  GOOGLESQL_RETURN_IF_ERROR(CollectColumnRefs(node, &column_refs, correlate));
  SortUniqueColumnRefs(column_refs);
  return absl::OkStatus();
}

// A shallow-copy rewriter that replaces column ids allocated by a different
// ColumnFactory and remaps the columns so that columns in the copy are
// allocated by `column_factory`.
class ColumnRemappingResolvedASTRewriter : public ResolvedASTRewriteVisitor {
 public:
  ColumnRemappingResolvedASTRewriter(
      ColumnReplacementMap& column_map,
      ColumnFactory* /*absl_nullable*/ column_factory)
      : column_map_(column_map), column_factory_(column_factory) {}

  absl::StatusOr<ResolvedColumn> PostVisitResolvedColumn(
      const ResolvedColumn& column) override {
    if (column_factory_ == nullptr) {
      auto it = column_map_.find(column);
      if (it != column_map_.end()) {
        return it->second;
      }
      return column;
    }

    auto [it, inserted] = column_map_.try_emplace(column);
    if (inserted) {
      it->second = column_factory_->MakeCol(column.table_name(), column.name(),
                                            column.annotated_type());
    }
    return it->second;
  }

 private:
  // Map from the column ID in the input ResolvedAST to the column allocated
  // from `column_factory_`.
  ColumnReplacementMap& column_map_;

  // All ResolvedColumns in the copied ResolvedAST will have new column ids
  // allocated by `column_factory_`. If this is a nullptr, ignore columns that
  // are not in `column_map_`.
  ColumnFactory* /*absl_nullable*/ column_factory_;
};

// A visitor that copies a ResolvedAST with columns ids allocated by a
// different ColumnFactory and remaps the columns so that columns in the copy
// are allocated by 'column_factory'.
class ColumnRemappingResolvedASTDeepCopyVisitor
    : public ResolvedASTDeepCopyVisitor {
 public:
  ColumnRemappingResolvedASTDeepCopyVisitor(ColumnReplacementMap& column_map,
                                            ColumnFactory& column_factory)
      : column_map_(column_map), column_factory_(column_factory) {}

  absl::StatusOr<ResolvedColumn> CopyResolvedColumn(
      const ResolvedColumn& column) override {
    auto [it, inserted] = column_map_.try_emplace(column);
    if (inserted) {
      it->second = column_factory_.MakeCol(column.table_name(), column.name(),
                                           column.annotated_type());
    }
    return it->second;
  }

 private:
  // Map from the column ID in the input ResolvedAST to the column allocated
  // from column_factory_.
  ColumnReplacementMap& column_map_;

  // All ResolvedColumns in the copied ResolvedAST will have new column ids
  // allocated by ColumnFactory.
  ColumnFactory& column_factory_;
};

absl::StatusOr<std::unique_ptr<ResolvedNode>>
CopyResolvedASTAndRemapColumnsImpl(const ResolvedNode& input_tree,
                                   ColumnFactory& column_factory,
                                   ColumnReplacementMap& column_map) {
  ColumnRemappingResolvedASTDeepCopyVisitor visitor(column_map, column_factory);
  GOOGLESQL_RETURN_IF_ERROR(input_tree.Accept(&visitor));
  return visitor.ConsumeRootNode<ResolvedNode>();
}

absl::StatusOr<std::unique_ptr<const ResolvedNode>> RemapColumnsImpl(
    std::unique_ptr<const ResolvedNode> input_tree,
    ColumnFactory* /*absl_nullable*/ column_factory,
    ColumnReplacementMap& column_map) {
  ColumnRemappingResolvedASTRewriter rewriter(column_map, column_factory);
  return rewriter.VisitAll(std::move(input_tree));
}

ResolvedColumnList RemapColumnList(const ResolvedColumnList& column_list,
                                   ColumnReplacementMap& column_map) {
  ResolvedColumnList new_output_column_list;
  for (const auto& col : column_list) {
    auto it = column_map.find(col);
    if (it != column_map.end()) {
      new_output_column_list.push_back(it->second);
    } else {
      new_output_column_list.push_back(col);
    }
  }
  return new_output_column_list;
}

absl::Status FunctionCallBuilder::PropagateAnnotationsAndProcessCollationList(
    ResolvedFunctionCallBase* fn_call) {
  GOOGLESQL_RETURN_IF_ERROR(annotation_propagator_.CheckAndPropagateAnnotations(
      /*error_node=*/nullptr, fn_call));
  return SetCollationList(analyzer_options_.language(), *fn_call);
}

// TODO: Propagate annotations correctly for this function, if
// needed, after creating resolved function node.
absl::StatusOr<std::unique_ptr<ResolvedFunctionCall>> FunctionCallBuilder::If(
    std::unique_ptr<const ResolvedExpr> condition,
    std::unique_ptr<const ResolvedExpr> then_case,
    std::unique_ptr<const ResolvedExpr> else_case) {
  GOOGLESQL_RET_CHECK_NE(condition.get(), nullptr);
  GOOGLESQL_RET_CHECK_NE(then_case.get(), nullptr);
  GOOGLESQL_RET_CHECK_NE(else_case.get(), nullptr);
  GOOGLESQL_RET_CHECK(condition->type()->IsBool());
  GOOGLESQL_RET_CHECK(then_case->type()->Equals(else_case->type()))
      << "Inconsistent types of then_case and else_case: "
      << then_case->type()->DebugString() << " vs "
      << else_case->type()->DebugString();

  const Function* if_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("if", &if_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(FunctionSignature concrete_signature,
                   MakeConcreteSignature(if_fn, FN_IF, then_case->type(),
                                         {{condition->type(), 1},
                                          {then_case->type(), 1},
                                          {else_case->type(), 1}}));

  const Type* result_type = then_case->type();
  std::vector<std::unique_ptr<const ResolvedExpr>> if_args(3);
  if_args[0] = std::move(condition);
  if_args[1] = std::move(then_case);
  if_args[2] = std::move(else_case);
  auto if_call = MakeResolvedFunctionCall(
      result_type, if_fn, concrete_signature, std::move(if_args),
      ResolvedFunctionCall::DEFAULT_ERROR_MODE);

  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(if_call.get()));
  return if_call;
}

absl::StatusOr<std::unique_ptr<ResolvedFunctionCall>>
FunctionCallBuilder::FromProto(std::unique_ptr<const ResolvedExpr> arg,
                               FunctionSignatureId signature_id,
                               const Type* target_type) {
  GOOGLESQL_RET_CHECK_NE(arg.get(), nullptr);
  GOOGLESQL_RET_CHECK(arg->type()->IsProto());
  GOOGLESQL_RET_CHECK_NE(target_type, nullptr);

  const Function* from_proto_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(catalog_.FindFunction({"from_proto"}, &from_proto_fn,
                                        analyzer_options_.find_options()));
  GOOGLESQL_RET_CHECK_NE(from_proto_fn, nullptr)
      << "Required function \"from_proto\" not available.";

  const FunctionSignature* matched_sig = nullptr;
  if (from_proto_fn->IsGoogleSQLBuiltin()) {
    matched_sig = GetSignature(from_proto_fn, signature_id);
  }

  GOOGLESQL_RET_CHECK_NE(matched_sig, nullptr)
      << "Could not find matching from_proto signature for proto "
      << arg->type()->DebugString() << " and result type "
      << target_type->DebugString();

  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature concrete_signature,
      MakeConcreteSignature(*matched_sig, target_type, {{arg->type(), 1}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> from_proto_args;
  from_proto_args.push_back(std::move(arg));
  auto from_proto_call = MakeResolvedFunctionCall(
      target_type, from_proto_fn, std::move(concrete_signature),
      std::move(from_proto_args), ResolvedFunctionCall::DEFAULT_ERROR_MODE);

  GOOGLESQL_RETURN_IF_ERROR(
      PropagateAnnotationsAndProcessCollationList(from_proto_call.get()));
  return from_proto_call;
}

absl::StatusOr<std::unique_ptr<ResolvedScan>> ReplaceScanColumns(
    ColumnFactory& column_factory, const ResolvedScan& scan,
    absl::Span<const int> target_column_indices,
    absl::Span<const ResolvedColumn> replacement_columns_to_use) {
  // Initialize a map from the column ids in the VIEW/TVF definition to the
  // column ids in the invoking query to remap the columns that were consumed
  // by the TableScan.
  GOOGLESQL_RET_CHECK_EQ(replacement_columns_to_use.size(), target_column_indices.size());
  ColumnReplacementMap column_map;
  // Initialize a vector of <index, column> pairs for columns that are
  // duplicated in the output column list. This is used to add computed columns
  // to the output column list so that the duplicated columns can be resolved.
  struct DuplicateInfo {
    // Index of target_column_indices containing the index of the duplicate
    // column.
    int index;
    // Canonical column to map duplicates to.
    ResolvedColumn canonical_column;
  };
  std::vector<DuplicateInfo> duplicate_column_indices;
  for (int i = 0; i < target_column_indices.size(); ++i) {
    int column_idx = target_column_indices[i];
    GOOGLESQL_RET_CHECK_GT(scan.column_list_size(), column_idx);
    const ResolvedColumn& original_column = scan.column_list(column_idx);
    const auto [column, success] =
        column_map.insert({original_column, replacement_columns_to_use[i]});
    if (!success) {
      duplicate_column_indices.push_back(DuplicateInfo{i, column->second});
    }
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ResolvedScan> new_scan,
      CopyResolvedASTAndRemapColumns(scan, column_factory, column_map));

  // Creates a ProjectScan that maps duplicated columns such as [col#1, col#1]
  // to [col#1, col#2], where col#2 is a ColumnRef(col#1).
  if (!duplicate_column_indices.empty()) {
    std::vector<ResolvedColumn> columns(new_scan->column_list());
    std::vector<std::unique_ptr<const ResolvedComputedColumn>> computed_columns;
    computed_columns.reserve(duplicate_column_indices.size());

    for (const DuplicateInfo& duplicate_info : duplicate_column_indices) {
      int index = duplicate_info.index;
      computed_columns.push_back(MakeResolvedComputedColumn(
          replacement_columns_to_use[index],
          MakeResolvedColumnRef(duplicate_info.canonical_column,
                                /*is_correlated=*/false)));
      columns[target_column_indices[index]] = replacement_columns_to_use[index];
    }

    return MakeResolvedProjectScan(columns, std::move(computed_columns),
                                   std::move(new_scan));
  }

  return new_scan;
}

std::vector<ResolvedColumn> CreateReplacementColumns(
    ColumnFactory& column_factory, absl::Span<const ResolvedColumn> column_list,
    ColumnReplacementMap& replaced_column_map) {
  std::vector<ResolvedColumn> replacement_columns;
  replacement_columns.reserve(column_list.size());

  for (const ResolvedColumn& old_column : column_list) {
    const ResolvedColumn new_column = column_factory.MakeCol(
        old_column.table_name(), old_column.name(), old_column.type());
    replacement_columns.push_back(new_column);
    replaced_column_map.insert({new_column, old_column});
  }

  return replacement_columns;
}

// Creates a new set of columns for a ResolvedExecuteAsRoleScan.
//
// For each column in `input_scan_columns`, it is replaced with the
// corresponding column from `column_map` if it exists, and it has not been seen
// before; otherwise, a new column with the same name, table name, and type is
// allocated by `column_factory`.
static std::vector<ResolvedColumn> CreateColumnsForExecuteAsRoleScan(
    ColumnFactory& column_factory, const ColumnReplacementMap& column_map,
    absl::Span<const ResolvedColumn> input_scan_columns) {
  absl::flat_hash_set<ResolvedColumn> seen_columns;
  std::vector<ResolvedColumn> new_columns;
  new_columns.reserve(input_scan_columns.size());
  for (const ResolvedColumn& input_column : input_scan_columns) {
    if (auto it = column_map.find(input_column);
        it != column_map.cend() && seen_columns.insert(input_column).second) {
      new_columns.push_back(it->second);
    } else {
      new_columns.push_back(column_factory.MakeCol(
          input_column.table_name(), input_column.name(), input_column.type()));
    }
  }
  return new_columns;
}

absl::StatusOr</*absl_nonnull*/ std::unique_ptr<ResolvedExecuteAsRoleScan>>
CreateResolvedExecuteAsRoleScan(
    ColumnFactory& column_factory, const ColumnReplacementMap& column_map,
    /*absl_nonnull*/ std::unique_ptr<const ResolvedScan> body_scan,
    const Table* /*absl_nullable*/ original_inlined_view,
    const TableValuedFunction* /*absl_nullable*/ original_inlined_tvf) {
  GOOGLESQL_RET_CHECK(
      (original_inlined_view != nullptr && original_inlined_tvf == nullptr) ||
      (original_inlined_view == nullptr && original_inlined_tvf != nullptr))
      << "Only one of original_inlined_view or original_inlined_tvf should be "
         "set.";
  std::vector<ResolvedColumn> new_columns = CreateColumnsForExecuteAsRoleScan(
      column_factory, column_map, body_scan->column_list());

  return MakeResolvedExecuteAsRoleScan(new_columns, std::move(body_scan),
                                       original_inlined_view,
                                       original_inlined_tvf);
}

absl::StatusOr<std::unique_ptr<ResolvedFunctionCall>>
FunctionCallBuilder::IsNull(std::unique_ptr<const ResolvedExpr> arg) {
  GOOGLESQL_RET_CHECK_NE(arg.get(), nullptr);

  const Function* is_null_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("$is_null", &is_null_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature concrete_signature,
      MakeConcreteSignature(is_null_fn, FN_IS_NULL, types::BoolType(),
                            {{arg->type(), 1}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> is_null_args(1);
  is_null_args[0] = std::move(arg);
  auto call = MakeResolvedFunctionCall(
      types::BoolType(), is_null_fn, concrete_signature,
      std::move(is_null_args), ResolvedFunctionCall::DEFAULT_ERROR_MODE);
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::IsNotNull(std::unique_ptr<const ResolvedExpr> arg) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> is_null,
                   IsNull(std::move(arg)));
  return Not(std::move(is_null));
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::AnyIsNull(
    std::vector<std::unique_ptr<const ResolvedExpr>> args) {
  std::vector<std::unique_ptr<const ResolvedExpr>> is_nulls;
  is_nulls.reserve(args.size());
  for (const auto& arg : args) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> arg_copy,
                     ResolvedASTDeepCopyVisitor::Copy(arg.get()));
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> is_null,
                     IsNull(std::move(arg_copy)));
    is_nulls.push_back(std::move(is_null));
  }
  return Or(std::move(is_nulls));
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::IfNull(std::unique_ptr<const ResolvedExpr> arg,
                            std::unique_ptr<const ResolvedExpr> null_case) {
  GOOGLESQL_RET_CHECK_NE(arg.get(), nullptr);
  GOOGLESQL_RET_CHECK_NE(null_case.get(), nullptr);
  GOOGLESQL_RET_CHECK(arg->type()->Equals(null_case->type()))
      << "Expected arg->type()->Equals(null_case->type()) to be true, but it "
         "was false. arg->type(): "
      << arg->type()->DebugString()
      << ", null_case->type(): " << null_case->type()->DebugString();
  if (arg->type_annotation_map() != nullptr &&
      null_case->type_annotation_map() != nullptr) {
    GOOGLESQL_RET_CHECK(
        arg->type_annotation_map()->Equals(*null_case->type_annotation_map()))
        << "Expected arg->type_annotation_map()->Equals("
        << "null_case->type_annotation_map()) to be true, but it was false. "
        << "arg->type(): " << arg->type()->DebugString()
        << ", null_case->type(): " << null_case->type()->DebugString();
  }

  const Function* ifnull_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("ifnull", &ifnull_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature concrete_signature,
      MakeConcreteSignature(ifnull_fn, FN_IFNULL, arg->type(),
                            {{arg->type(), 1}, {null_case->type(), 1}}));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto ifnull_call,
      ResolvedFunctionCallBuilder()
          .set_type(arg->type())
          .set_function(ifnull_fn)
          .set_signature(std::move(concrete_signature))
          .add_argument_list(std::move(arg))
          .add_argument_list(std::move(null_case))
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(
      PropagateAnnotationsAndProcessCollationList(ifnull_call.get()));
  return ifnull_call;
}

// TODO: Propagate annotations correctly for this function, if
// needed, after creating resolved function node.
absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::IfError(std::unique_ptr<const ResolvedExpr> try_expr,
                             std::unique_ptr<const ResolvedExpr> handle_expr) {
  GOOGLESQL_RET_CHECK_NE(try_expr.get(), nullptr);
  GOOGLESQL_RET_CHECK_NE(handle_expr.get(), nullptr);
  GOOGLESQL_RET_CHECK(try_expr->type()->Equals(handle_expr->type()))
      << "Expected try_expr->type().Equals(handle_expr->type()) to be true, "
      << "but it was false. try_expr->type(): "
      << try_expr->type()->DebugString()
      << ", handle_expr->type(): " << handle_expr->type()->DebugString();

  const Function* iferror_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("iferror", &iferror_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature concrete_signature,
      MakeConcreteSignature(iferror_fn, FN_IFERROR, try_expr->type(),
                            {{try_expr->type(), 1}, {handle_expr->type(), 1}}));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto iferror_call,
      ResolvedFunctionCallBuilder()
          .set_type(try_expr->type())
          .set_function(iferror_fn)
          .set_signature(std::move(concrete_signature))
          .add_argument_list(std::move(try_expr))
          .add_argument_list(std::move(handle_expr))
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(
      PropagateAnnotationsAndProcessCollationList(iferror_call.get()));
  return iferror_call;
}

// TODO: Propagate annotations correctly for this function, if
// needed, after creating resolved function node.
absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::NullIfError(std::unique_ptr<const ResolvedExpr> expr) {
  GOOGLESQL_RET_CHECK_NE(expr.get(), nullptr);

  const Function* nulliferror_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(
      GetBuiltinFunctionFromCatalog("nulliferror", &nulliferror_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(FunctionSignature concrete_signature,
                   MakeConcreteSignature(nulliferror_fn, FN_NULLIFERROR,
                                         expr->type(), {{expr->type(), 1}}));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto nulliferror_call,
      ResolvedFunctionCallBuilder()
          .set_type(expr->type())
          .set_function(nulliferror_fn)
          .set_signature(std::move(concrete_signature))
          .add_argument_list(std::move(expr))
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(
      PropagateAnnotationsAndProcessCollationList(nulliferror_call.get()));
  return nulliferror_call;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::Error(const std::string& error_text,
                           const Type* target_type) {
  std::unique_ptr<const ResolvedExpr> error_expr =
      MakeResolvedLiteral(types::StringType(), Value::StringValue(error_text));
  return Error(std::move(error_expr), target_type);
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::Error(std::unique_ptr<const ResolvedExpr> error_expr,
                           const Type* target_type) {
  GOOGLESQL_RET_CHECK_NE(error_expr.get(), nullptr);
  GOOGLESQL_RET_CHECK(error_expr->type()->IsString());

  const Function* error_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("error", &error_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(FunctionSignature concrete_signature,
                   MakeConcreteSignature(error_fn, FN_ERROR, types::Int64Type(),
                                         {{error_expr->type(), 1}}));

  // FN_ERROR is a special case, as the concrete signature can return any
  // type. It is handled as a special case in the resolver.
  if (target_type == nullptr) {
    target_type = types::Int64Type();
  }
  concrete_signature.SetConcreteResultType(target_type);

  // Normal resolution treats ERROR() as a special case and doesn't propagate
  // annotations on it, so we don't here either.
  return ResolvedFunctionCallBuilder()
      .set_type(target_type)
      .set_function(error_fn)
      .set_signature(std::move(concrete_signature))
      .add_argument_list(std::move(error_expr))
      .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
      .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
      .Build();
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::MakeArray(
    const Type* element_type,
    std::vector<std::unique_ptr<const ResolvedExpr>> elements,
    bool cast_elements_if_needed) {
  GOOGLESQL_RET_CHECK(element_type != nullptr);
  const Function* make_array_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("$make_array", &make_array_fn));
  GOOGLESQL_RET_CHECK(make_array_fn != nullptr);

  GOOGLESQL_ASSIGN_OR_RETURN(
      const ArrayType* array_type,
      type_factory_.MakeArrayType(element_type, analyzer_options_.language()));
  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature make_array_signature,
      MakeConcreteSignature(make_array_fn, FN_MAKE_ARRAY, array_type,
                            {{element_type, elements.size()}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> cast_elements;
  for (auto& element : elements) {
    const bool cast_needed =
        cast_elements_if_needed && !element->type()->Equals(element_type);
    cast_elements.push_back(
        cast_needed ? MakeResolvedCast(element_type, std::move(element),
                                       /*return_null_on_error=*/false)
                    : std::move(element));
  }

  std::unique_ptr<ResolvedFunctionCall> resolved_function =
      MakeResolvedFunctionCall(array_type, make_array_fn, make_array_signature,
                               std::move(cast_elements),
                               ResolvedFunctionCall::DEFAULT_ERROR_MODE);
  GOOGLESQL_RETURN_IF_ERROR(
      PropagateAnnotationsAndProcessCollationList(resolved_function.get()));
  return resolved_function;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::JsonObject(
    std::vector<std::unique_ptr<const ResolvedExpr>> keys,
    std::vector<std::unique_ptr<const ResolvedExpr>> values) {
  GOOGLESQL_RET_CHECK_EQ(keys.size(), values.size());
  GOOGLESQL_RET_CHECK(!keys.empty());
  const Function* json_object_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(
      GetBuiltinFunctionFromCatalog("json_object", &json_object_fn));
  GOOGLESQL_RET_CHECK(json_object_fn != nullptr);

  const FunctionSignature* signature =
      GetSignature(json_object_fn, FN_JSON_OBJECT);
  GOOGLESQL_RET_CHECK(signature != nullptr);
  GOOGLESQL_RET_CHECK_EQ(signature->arguments().size(), 2);

  FunctionArgumentTypeList concrete_args;
  concrete_args.reserve(keys.size() * 2);
  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.reserve(keys.size() * 2);

  for (size_t i = 0; i < keys.size(); ++i) {
    GOOGLESQL_RET_CHECK(keys[i] != nullptr);
    GOOGLESQL_RET_CHECK(keys[i]->type()->IsString());
    GOOGLESQL_RET_CHECK(values[i] != nullptr);

    FunctionArgumentType key_arg(keys[i]->type(),
                                 signature->argument(0).options(),
                                 /*num_occurrences=*/1);
    key_arg.set_original_kind(signature->argument(0).kind());
    concrete_args.push_back(std::move(key_arg));

    FunctionArgumentType val_arg(values[i]->type(),
                                 signature->argument(1).options(),
                                 /*num_occurrences=*/1);
    val_arg.set_original_kind(signature->argument(1).kind());
    concrete_args.push_back(std::move(val_arg));

    args.push_back(std::move(keys[i]));
    args.push_back(std::move(values[i]));
  }

  FunctionArgumentType result_arg(types::JsonType(),
                                  signature->result_type().options(),
                                  /*num_occurrences=*/1);
  result_arg.set_original_kind(signature->result_type().kind());

  FunctionSignature concrete_signature(
      std::move(result_arg), std::move(concrete_args), signature->context_id(),
      signature->options());
  GOOGLESQL_RET_CHECK(concrete_signature.IsConcrete());

  std::vector<InputArgumentType> input_arguments;
  input_arguments.reserve(args.size());
  for (const auto& arg : args) {
    input_arguments.push_back(InputArgumentType(arg->type()));
  }
  GOOGLESQL_RET_CHECK_OK(json_object_fn->CheckPostResolutionArgumentConstraints(
      concrete_signature, input_arguments, analyzer_options_.language()));

  std::unique_ptr<ResolvedFunctionCall> resolved_function =
      MakeResolvedFunctionCall(types::JsonType(), json_object_fn,
                               std::move(concrete_signature), std::move(args),
                               ResolvedFunctionCall::DEFAULT_ERROR_MODE);
  GOOGLESQL_RETURN_IF_ERROR(
      PropagateAnnotationsAndProcessCollationList(resolved_function.get()));
  return resolved_function;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::JsonSet(
    std::unique_ptr<const ResolvedExpr> json_doc,
    std::vector<std::unique_ptr<const ResolvedExpr>> paths,
    std::vector<std::unique_ptr<const ResolvedExpr>> values) {
  GOOGLESQL_RET_CHECK(json_doc != nullptr);
  GOOGLESQL_RET_CHECK(json_doc->type()->IsJson());
  GOOGLESQL_RET_CHECK_EQ(paths.size(), values.size());
  GOOGLESQL_RET_CHECK(!paths.empty());

  const Function* json_set_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("json_set", &json_set_fn));
  GOOGLESQL_RET_CHECK(json_set_fn != nullptr);

  const FunctionSignature* signature = GetSignature(json_set_fn, FN_JSON_SET);
  GOOGLESQL_RET_CHECK(signature != nullptr);
  GOOGLESQL_RET_CHECK_EQ(signature->arguments().size(), 6);

  FunctionArgumentTypeList concrete_args;
  concrete_args.reserve(std::max<size_t>(6, paths.size() * 2 + 2));
  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.reserve(paths.size() * 2 + 2);

  // Argument 0: json_doc
  FunctionArgumentType doc_arg(json_doc->type(),
                               signature->argument(0).options(),
                               /*num_occurrences=*/1);
  doc_arg.set_original_kind(signature->argument(0).kind());
  concrete_args.push_back(std::move(doc_arg));
  args.push_back(std::move(json_doc));

  // Arguments 1 & 2: first path and first value
  GOOGLESQL_RET_CHECK(paths[0] != nullptr);
  GOOGLESQL_RET_CHECK(paths[0]->type()->IsString());
  GOOGLESQL_RET_CHECK(values[0] != nullptr);

  FunctionArgumentType first_path_arg(paths[0]->type(),
                                      signature->argument(1).options(),
                                      /*num_occurrences=*/1);
  first_path_arg.set_original_kind(signature->argument(1).kind());
  concrete_args.push_back(std::move(first_path_arg));

  FunctionArgumentType first_val_arg(values[0]->type(),
                                     signature->argument(2).options(),
                                     /*num_occurrences=*/1);
  first_val_arg.set_original_kind(signature->argument(2).kind());
  concrete_args.push_back(std::move(first_val_arg));

  args.push_back(std::move(paths[0]));
  args.push_back(std::move(values[0]));

  // Arguments 3 & 4: repeated path and value pairs
  if (paths.size() == 1) {
    FunctionArgumentType rep_path_arg(signature->argument(3).type(),
                                      signature->argument(3).options(),
                                      /*num_occurrences=*/0);
    rep_path_arg.set_original_kind(signature->argument(3).kind());
    concrete_args.push_back(std::move(rep_path_arg));

    FunctionArgumentType rep_val_arg(signature->argument(4).kind(),
                                     signature->argument(4).options(),
                                     /*num_occurrences=*/0);
    rep_val_arg.set_original_kind(signature->argument(4).kind());
    concrete_args.push_back(std::move(rep_val_arg));
  } else {
    for (size_t i = 1; i < paths.size(); ++i) {
      GOOGLESQL_RET_CHECK(paths[i] != nullptr);
      GOOGLESQL_RET_CHECK(paths[i]->type()->IsString());
      GOOGLESQL_RET_CHECK(values[i] != nullptr);

      FunctionArgumentType rep_path_arg(paths[i]->type(),
                                        signature->argument(3).options(),
                                        /*num_occurrences=*/1);
      rep_path_arg.set_original_kind(signature->argument(3).kind());
      concrete_args.push_back(std::move(rep_path_arg));

      FunctionArgumentType rep_val_arg(values[i]->type(),
                                       signature->argument(4).options(),
                                       /*num_occurrences=*/1);
      rep_val_arg.set_original_kind(signature->argument(4).kind());
      concrete_args.push_back(std::move(rep_val_arg));

      args.push_back(std::move(paths[i]));
      args.push_back(std::move(values[i]));
    }
  }

  // Argument 5: create_if_missing (optional BOOL with default true)
  FunctionArgumentTypeOptions create_if_missing_options =
      signature->argument(5).options();
  create_if_missing_options.clear_default();
  FunctionArgumentType create_if_missing_arg(
      types::BoolType(), create_if_missing_options, /*num_occurrences=*/1);
  create_if_missing_arg.set_original_kind(signature->argument(5).kind());
  concrete_args.push_back(std::move(create_if_missing_arg));
  args.push_back(MakeResolvedLiteral(types::BoolType(), Value::Bool(true)));

  FunctionArgumentType result_arg(types::JsonType(),
                                  signature->result_type().options(),
                                  /*num_occurrences=*/1);
  result_arg.set_original_kind(signature->result_type().kind());

  FunctionSignature concrete_signature(
      std::move(result_arg), std::move(concrete_args), signature->context_id(),
      signature->options());
  GOOGLESQL_RET_CHECK(concrete_signature.IsConcrete());

  std::vector<InputArgumentType> input_arguments;
  input_arguments.reserve(args.size());
  for (const auto& arg : args) {
    input_arguments.push_back(InputArgumentType(arg->type()));
  }
  GOOGLESQL_RET_CHECK_OK(json_set_fn->CheckPostResolutionArgumentConstraints(
      concrete_signature, input_arguments, analyzer_options_.language()));

  std::unique_ptr<ResolvedFunctionCall> resolved_function =
      MakeResolvedFunctionCall(types::JsonType(), json_set_fn,
                               std::move(concrete_signature), std::move(args),
                               ResolvedFunctionCall::DEFAULT_ERROR_MODE);
  GOOGLESQL_RETURN_IF_ERROR(
      PropagateAnnotationsAndProcessCollationList(resolved_function.get()));
  return resolved_function;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::JsonRemove(
    std::unique_ptr<const ResolvedExpr> json_doc,
    std::vector<std::unique_ptr<const ResolvedExpr>> paths) {
  GOOGLESQL_RET_CHECK(json_doc != nullptr);
  GOOGLESQL_RET_CHECK(json_doc->type()->IsJson());
  GOOGLESQL_RET_CHECK(!paths.empty());

  const Function* json_remove_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(
      GetBuiltinFunctionFromCatalog("json_remove", &json_remove_fn));
  GOOGLESQL_RET_CHECK(json_remove_fn != nullptr);

  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.reserve(paths.size() + 1);
  args.push_back(std::move(json_doc));
  for (auto& path : paths) {
    GOOGLESQL_RET_CHECK(path != nullptr);
    GOOGLESQL_RET_CHECK(path->type()->IsString());
    args.push_back(std::move(path));
  }

  // The signature of JSON_REMOVE is (JSON, STRING, repeated STRING). Only
  // include the trailing repeated STRING argument when more than one path is
  // provided; MakeConcreteSignature fills in 0 occurrences for omitted
  // trailing repeated arguments.
  std::vector<ConcreteArgument> concrete_args = {{types::JsonType(), 1},
                                                 {types::StringType(), 1}};
  if (paths.size() > 1) {
    concrete_args.push_back({types::StringType(), paths.size() - 1});
  }
  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature concrete_signature,
      MakeConcreteSignature(json_remove_fn, FN_JSON_REMOVE, types::JsonType(),
                            std::move(concrete_args)));

  std::unique_ptr<ResolvedFunctionCall> resolved_function =
      MakeResolvedFunctionCall(types::JsonType(), json_remove_fn,
                               std::move(concrete_signature), std::move(args),
                               ResolvedFunctionCall::DEFAULT_ERROR_MODE);
  GOOGLESQL_RETURN_IF_ERROR(
      PropagateAnnotationsAndProcessCollationList(resolved_function.get()));
  return resolved_function;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::ElementDefinitionName(
    std::unique_ptr<const ResolvedExpr> graph_element) {
  GOOGLESQL_RET_CHECK(graph_element != nullptr);
  GOOGLESQL_RET_CHECK(graph_element->type()->IsGraphElement());

  const Function* fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(
      GetBuiltinFunctionFromCatalog("element_definition_name", &fn));
  GOOGLESQL_RET_CHECK(fn != nullptr);

  const Type* element_type = graph_element->type();
  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature concrete_signature,
      MakeConcreteSignature(fn, FN_ELEMENT_DEFINITION_NAME, types::StringType(),
                            {{element_type, 1}}));

  std::unique_ptr<ResolvedFunctionCall> resolved_function =
      MakeResolvedFunctionCall(types::StringType(), fn,
                               std::move(concrete_signature),
                               MakeNodeVector(std::move(graph_element)),
                               ResolvedFunctionCall::DEFAULT_ERROR_MODE);
  GOOGLESQL_RETURN_IF_ERROR(
      PropagateAnnotationsAndProcessCollationList(resolved_function.get()));
  return resolved_function;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::ArrayFirstN(std::unique_ptr<const ResolvedExpr> array,
                                 std::unique_ptr<const ResolvedExpr> n) {
  GOOGLESQL_RET_CHECK(array->type()->IsArray());
  GOOGLESQL_RET_CHECK(n->type()->IsInt64());

  const Function* array_first_n_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(
      GetBuiltinFunctionFromCatalog("array_first_n", &array_first_n_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature concrete_signature,
      MakeConcreteSignature(array_first_n_fn, FN_ARRAY_FIRST_N, array->type(),
                            {{array->type(), 1}, {n->type(), 1}}));

  const Type* result_type = array->type();
  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(std::move(array));
  args.push_back(std::move(n));

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ResolvedFunctionCall> resolved_function,
      ResolvedFunctionCallBuilder()
          .set_type(result_type)
          .set_function(array_first_n_fn)
          .set_signature(std::move(concrete_signature))
          .set_argument_list(std::move(args))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());

  GOOGLESQL_RETURN_IF_ERROR(
      PropagateAnnotationsAndProcessCollationList(resolved_function.get()));
  return resolved_function;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::ArrayConcat(
    std::vector<std::unique_ptr<const ResolvedExpr>> arrays) {
  GOOGLESQL_RET_CHECK_GT(arrays.size(), 0) << "There must be at least one array";
  const Function* array_concat_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(
      GetBuiltinFunctionFromCatalog("array_concat", &array_concat_fn));

  // Construct arguments type and result type to pass to FunctionSignature.
  const Type* type = arrays[0]->type();

  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature array_concat_signature,
      MakeConcreteSignature(array_concat_fn, FN_ARRAY_CONCAT, type,
                            {{type, 1}, {type, arrays.size() - 1}}));

  std::unique_ptr<ResolvedFunctionCall> resolved_function =
      MakeResolvedFunctionCall(type, array_concat_fn, array_concat_signature,
                               std::move(arrays),
                               ResolvedFunctionCall::DEFAULT_ERROR_MODE);
  GOOGLESQL_RETURN_IF_ERROR(
      PropagateAnnotationsAndProcessCollationList(resolved_function.get()));
  return resolved_function;
}

// TODO: Propagate annotations correctly for this function, if
// needed, after creating resolved function node.
absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::Like(std::unique_ptr<ResolvedExpr> input,
                          std::unique_ptr<ResolvedExpr> pattern) {
  GOOGLESQL_RET_CHECK_NE(input.get(), nullptr);
  GOOGLESQL_RET_CHECK_NE(pattern.get(), nullptr);
  GOOGLESQL_RET_CHECK(input->type()->Equals(pattern->type()))
      << "input type does not match pattern type. input->type(): "
      << input->type()->DebugString()
      << ", pattern->type(): " << pattern->type()->DebugString();

  FunctionSignatureId context_id;
  if (input->type()->Equals(types::StringType())) {
    context_id = FN_STRING_LIKE;
  } else if (input->type()->Equals(types::BytesType())) {
    context_id = FN_BYTE_LIKE;
  } else {
    GOOGLESQL_RET_CHECK_FAIL() << "input type is not STRING or BYTES. input->type(): "
                     << input->type()->DebugString();
  }

  const Function* like_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("$like", &like_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature like_signature,
      MakeConcreteSignature(like_fn, context_id, types::BoolType(),
                            {{input->type(), 1}, {pattern->type(), 1}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> like_fn_args(2);
  like_fn_args[0] = std::move(input);
  like_fn_args[1] = std::move(pattern);

  auto call = MakeResolvedFunctionCall(
      types::BoolType(), like_fn, like_signature, std::move(like_fn_args),
      ResolvedFunctionCall::DEFAULT_ERROR_MODE);
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

// TODO: Propagate annotations correctly for this function, if
// needed, after creating resolved function node.
absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
FunctionCallBuilder::CaseNoValue(
    std::vector<std::unique_ptr<const ResolvedExpr>> conditions,
    std::vector<std::unique_ptr<const ResolvedExpr>> results,
    std::unique_ptr<const ResolvedExpr> else_result) {
  GOOGLESQL_RET_CHECK_GT(conditions.size(), 0);
  GOOGLESQL_RET_CHECK_EQ(conditions.size(), results.size());

  const Type* result_type = results[0]->type();

  GOOGLESQL_RET_CHECK(else_result != nullptr);
  GOOGLESQL_RET_CHECK(else_result->type()->Equals(result_type));

  const Function* case_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("$case_no_value", &case_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature case_signature,
      MakeConcreteSignature(case_fn, FN_CASE_NO_VALUE, result_type,
                            {{types::BoolType(), conditions.size()},
                             {result_type, results.size()},
                             {else_result->type(), 1}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> case_fn_args;
  for (int i = 0; i < conditions.size(); ++i) {
    GOOGLESQL_RET_CHECK(conditions[i]->type()->IsBool());
    GOOGLESQL_RET_CHECK(results[i]->type()->Equals(result_type));
    case_fn_args.push_back(std::move(conditions[i]));
    case_fn_args.push_back(std::move(results[i]));
  }
  case_fn_args.push_back(std::move(else_result));

  auto call = MakeResolvedFunctionCall(
      result_type, case_fn, case_signature, std::move(case_fn_args),
      ResolvedFunctionCall::DEFAULT_ERROR_MODE);
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::Not(std::unique_ptr<const ResolvedExpr> expression) {
  GOOGLESQL_RET_CHECK_NE(expression.get(), nullptr);
  GOOGLESQL_RET_CHECK(expression->type()->Equals(types::BoolType()))
      << "Type of expression is not a BOOL: expression->type(): "
      << expression->type()->DebugString();

  const Function* not_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("$not", &not_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(FunctionSignature not_signature,
                   MakeConcreteSignature(not_fn, FN_NOT, types::BoolType(),
                                         {{types::BoolType(), 1}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> not_fn_args(1);
  not_fn_args[0] = std::move(expression);

  auto call = MakeResolvedFunctionCall(
      types::BoolType(), not_fn, not_signature, std::move(not_fn_args),
      ResolvedFunctionCall::DEFAULT_ERROR_MODE);
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::Equal(std::unique_ptr<const ResolvedExpr> left_expr,
                           std::unique_ptr<const ResolvedExpr> right_expr) {
  GOOGLESQL_RET_CHECK_NE(left_expr.get(), nullptr);
  GOOGLESQL_RET_CHECK_NE(right_expr.get(), nullptr);
  GOOGLESQL_RET_CHECK(left_expr->type()->Equals(right_expr->type()));
  GOOGLESQL_RET_CHECK(left_expr->type()->SupportsEquality());

  const Function* equal_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("$equal", &equal_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature equal_signature,
      MakeConcreteSignature(equal_fn, FN_EQUAL, types::BoolType(),
                            {{left_expr->type(), 1}, {right_expr->type(), 1}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> equal_fn_args(2);
  equal_fn_args[0] = std::move(left_expr);
  equal_fn_args[1] = std::move(right_expr);

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto call,
      ResolvedFunctionCallBuilder()
          .set_type(types::BoolType())
          .set_function(equal_fn)
          .set_signature(equal_signature)
          .set_argument_list(std::move(equal_fn_args))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::NotEqual(std::unique_ptr<const ResolvedExpr> left_expr,
                              std::unique_ptr<const ResolvedExpr> right_expr) {
  GOOGLESQL_RET_CHECK_NE(left_expr.get(), nullptr);
  GOOGLESQL_RET_CHECK_NE(right_expr.get(), nullptr);
  GOOGLESQL_RET_CHECK(left_expr->type()->Equals(right_expr->type()));
  GOOGLESQL_RET_CHECK(left_expr->type()->SupportsEquality());

  const Function* not_equal_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("$not_equal", &not_equal_fn));

  // Only this signature has collation enabled in function signature options.
  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature not_equal_signature,
      MakeConcreteSignature(not_equal_fn, FN_NOT_EQUAL, types::BoolType(),
                            {{left_expr->type(), 1}, {right_expr->type(), 1}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.reserve(2);
  args.push_back(std::move(left_expr));
  args.push_back(std::move(right_expr));

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ResolvedFunctionCall> resolved_function,
      ResolvedFunctionCallBuilder()
          .set_type(types::BoolType())
          .set_function(not_equal_fn)
          .set_signature(not_equal_signature)
          .set_argument_list(std::move(args))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(
      PropagateAnnotationsAndProcessCollationList(resolved_function.get()));
  return resolved_function;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::FunctionCallWithSameTypeArgumentsSupportingOrdering(
    std::vector<std::unique_ptr<const ResolvedExpr>> expressions,
    absl::string_view builtin_function_name) {
  GOOGLESQL_RET_CHECK_GE(expressions.size(), 1);
  GOOGLESQL_RET_CHECK_NE(expressions[0].get(), nullptr);

  const Type* type = expressions[0]->type();
  GOOGLESQL_RET_CHECK(type->SupportsOrdering(analyzer_options_.language(),
                                   /*type_description=*/nullptr));
  for (int i = 1; i < expressions.size(); ++i) {
    GOOGLESQL_RET_CHECK(expressions[i]->type()->Equals(type))
        << "Type of expression " << i << " is not the same as the first one: "
        << expressions[i]->type()->DebugString() << " vs "
        << type->DebugString();
  }
  const Function* fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog(builtin_function_name, &fn));
  GOOGLESQL_RET_CHECK(fn != nullptr);

  GOOGLESQL_RET_CHECK_EQ(fn->signatures().size(), 1);
  const FunctionSignature* catalog_signature = fn->GetSignature(0);
  GOOGLESQL_RET_CHECK(catalog_signature != nullptr);

  // Construct arguments type and result type to pass to FunctionSignature.
  GOOGLESQL_ASSIGN_OR_RETURN(FunctionSignature concrete_signature,
                   MakeConcreteSignature(*catalog_signature, type,
                                         {{type, expressions.size()}}));

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ResolvedFunctionCall> resolved_function,
      ResolvedFunctionCallBuilder()
          .set_type(type)
          .set_function(fn)
          .set_signature(std::move(concrete_signature))
          .set_argument_list(std::move(expressions))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(
      PropagateAnnotationsAndProcessCollationList(resolved_function.get()));
  return resolved_function;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::Least(
    std::vector<std::unique_ptr<const ResolvedExpr>> expressions) {
  return FunctionCallWithSameTypeArgumentsSupportingOrdering(
      std::move(expressions), "least");
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::Greatest(
    std::vector<std::unique_ptr<const ResolvedExpr>> expressions) {
  return FunctionCallWithSameTypeArgumentsSupportingOrdering(
      std::move(expressions), "greatest");
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::Coalesce(
    std::vector<std::unique_ptr<const ResolvedExpr>> expressions) {
  GOOGLESQL_RET_CHECK_GE(expressions.size(), 1);
  GOOGLESQL_RET_CHECK_NE(expressions[0].get(), nullptr);

  InputArgumentTypeSet arg_set;
  for (int i = 0; i < expressions.size(); ++i) {
    arg_set.Insert(InputArgumentType(expressions[i]->type()));
  }
  const Type* super_type = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(coercer_.GetCommonSuperType(arg_set, &super_type));

  const Function* coalesce_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("coalesce", &coalesce_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(FunctionSignature coalesce_signature,
                   MakeConcreteSignature(coalesce_fn, FN_COALESCE, super_type,
                                         {{super_type, expressions.size()}}));

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ResolvedFunctionCall> resolved_function,
      ResolvedFunctionCallBuilder()
          .set_type(super_type)
          .set_function(coalesce_fn)
          .set_signature(coalesce_signature)
          .set_argument_list(std::move(expressions))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());

  GOOGLESQL_RETURN_IF_ERROR(
      PropagateAnnotationsAndProcessCollationList(resolved_function.get()));
  return resolved_function;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::Less(std::unique_ptr<const ResolvedExpr> left_expr,
                          std::unique_ptr<const ResolvedExpr> right_expr,
                          bool or_equal) {
  GOOGLESQL_RET_CHECK_NE(left_expr.get(), nullptr);
  GOOGLESQL_RET_CHECK_NE(right_expr.get(), nullptr);
  GOOGLESQL_RET_CHECK(left_expr->type()->Equals(right_expr->type()))
      << "Type of expression are not the same: "
      << left_expr->type()->DebugString() << " vs "
      << right_expr->type()->DebugString();
  std::string unused_type_description;
  GOOGLESQL_RET_CHECK(left_expr->type()->SupportsOrdering(analyzer_options_.language(),
                                                &unused_type_description));
  if (or_equal) {
    GOOGLESQL_RET_CHECK(
        left_expr->type()->SupportsEquality(analyzer_options_.language()));
  }

  // Only those signature has collation enabled in function signature options.
  FunctionSignatureId signature_id = or_equal ? FN_LESS_OR_EQUAL : FN_LESS;

  const Function* less_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog(
      or_equal ? "$less_or_equal" : "$less", &less_fn));

  // Only this signature has collation enabled in function signature options.
  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature less_signature,
      MakeConcreteSignature(less_fn, signature_id, types::BoolType(),
                            {{left_expr->type(), 1}, {right_expr->type(), 1}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.reserve(2);
  args.push_back(std::move(left_expr));
  args.push_back(std::move(right_expr));

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ResolvedFunctionCall> resolved_function,
      ResolvedFunctionCallBuilder()
          .set_type(types::BoolType())
          .set_function(less_fn)
          .set_signature(less_signature)
          .set_argument_list(std::move(args))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());
  // We don't need to propagate type annotation map for this function because
  // the return type is not STRING.
  GOOGLESQL_RETURN_IF_ERROR(
      PropagateAnnotationsAndProcessCollationList(resolved_function.get()));
  return resolved_function;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::Less(std::unique_ptr<const ResolvedExpr> left_expr,
                          std::unique_ptr<const ResolvedExpr> right_expr) {
  return Less(std::move(left_expr), std::move(right_expr), /*or_equal=*/false);
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::LessOrEqual(
    std::unique_ptr<const ResolvedExpr> left_expr,
    std::unique_ptr<const ResolvedExpr> right_expr) {
  return Less(std::move(left_expr), std::move(right_expr), /*or_equal=*/true);
}

namespace {

// Represents the input argument type to CONCAT.
enum class ConcatInputArgKind {
  // The input argument type to CONCAT is invalid.
  kInvalid,
  // The input argument type to CONCAT is STRING.
  kString,
  // The input argument type to CONCAT is BYTES.
  kBytes,
};

// Converts the given `type` to the corresponding `ConcatInputArgKind`.
ConcatInputArgKind GetConcatInputArgKind(const Type* type) {
  if (type->IsString()) {
    return ConcatInputArgKind::kString;
  }
  if (type->IsBytes()) {
    return ConcatInputArgKind::kBytes;
  }
  return ConcatInputArgKind::kInvalid;
}

// Validates the input arguments to CONCAT:
// - The input `elements` is not empty.
// - All input arguments have the same type, which is either STRING or BYTES.
absl::Status ValidateConcatInputArgs(
    absl::Span<const std::unique_ptr<const ResolvedExpr>> elements) {
  GOOGLESQL_RET_CHECK(!elements.empty());

  const Type* type = elements[0]->type();
  const ConcatInputArgKind input_kind = GetConcatInputArgKind(type);
  GOOGLESQL_RET_CHECK(input_kind != ConcatInputArgKind::kInvalid)
      << "Invalid element type: " << type->DebugString();

  for (const auto& element : elements) {
    GOOGLESQL_RET_CHECK(GetConcatInputArgKind(element->type()) == input_kind)
        << "Input elements contain different types: "
        << element->type()->DebugString() << " vs " << type->DebugString();
  }
  return absl::OkStatus();
}

// Returns the CONCAT signature that matches the given `input_kind`.
// Returns an error if no such signature is found.
absl::StatusOr<const FunctionSignature*> GetConcatFunctionSignature(
    const Function* concat_fn, ConcatInputArgKind input_kind) {
  FunctionSignatureId target_signature_id;
  switch (input_kind) {
    case ConcatInputArgKind::kString:
      target_signature_id = FN_CONCAT_STRING;
      break;
    case ConcatInputArgKind::kBytes:
      target_signature_id = FN_CONCAT_BYTES;
      break;
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Invalid input kind";
  }

  const FunctionSignature* signature =
      GetSignature(concat_fn, target_signature_id);
  if (signature != nullptr) {
    return signature;
  }
  GOOGLESQL_RET_CHECK_FAIL() << "Cannot find CONCAT signature with input argument type "
                   << (input_kind == ConcatInputArgKind::kString ? "STRING"
                                                                 : "BYTES")
                   << " in the catalog";
}

}  // namespace

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::Concat(
    std::vector<std::unique_ptr<const ResolvedExpr>> elements) {
  GOOGLESQL_RETURN_IF_ERROR(ValidateConcatInputArgs(elements));

  const Type* type = elements[0]->type();
  ConcatInputArgKind input_kind = GetConcatInputArgKind(type);

  const Function* concat_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("concat", &concat_fn));
  GOOGLESQL_RET_CHECK_EQ(concat_fn->signatures().size(), 2);

  GOOGLESQL_ASSIGN_OR_RETURN(const FunctionSignature* catalog_signature,
                   GetConcatFunctionSignature(concat_fn, input_kind));
  GOOGLESQL_RET_CHECK(catalog_signature != nullptr);

  GOOGLESQL_RET_CHECK_EQ(catalog_signature->arguments().size(), 2);

  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature concat_signature,
      MakeConcreteSignature(
          *catalog_signature, catalog_signature->result_type().type(),
          {{catalog_signature->argument(0).type(), 1},
           {catalog_signature->argument(1).type(), elements.size() - 1}}));

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ResolvedFunctionCall> resolved_function,
      ResolvedFunctionCallBuilder()
          .set_type(catalog_signature->result_type().type())
          .set_function(concat_fn)
          .set_signature(concat_signature)
          .set_argument_list(std::move(elements))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(
      PropagateAnnotationsAndProcessCollationList(resolved_function.get()));
  return resolved_function;
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
FunctionCallBuilder::MakeNullIfEmptyArray(
    ColumnFactory& column_factory,
    std::unique_ptr<const ResolvedExpr> array_expr) {
  GOOGLESQL_RET_CHECK(array_expr != nullptr);
  const Type* array_type = array_expr->type();
  GOOGLESQL_RET_CHECK(array_type->IsArray());
  // TODO: We should support DeferredComputedColumns here.
  ResolvedColumn out_column =
      column_factory.MakeCol("null_if_empty_array", "$out", array_type);
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> array_length,
                   ArrayLength(MakeResolvedColumnRef(out_column,
                                                     /*is_correlated=*/false)));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> array_non_empty,
                   GreaterOrEqual(std::move(array_length),
                                  MakeResolvedLiteral(Value::Int64(1))));
  const AnnotationMap* output_annotation_map =
      array_expr->type_annotation_map();
  return ResolvedWithExprBuilder()
      .add_assignment_list(ResolvedComputedColumnBuilder()
                               .set_column(out_column)
                               .set_expr(std::move(array_expr)))
      .set_expr(If(std::move(array_non_empty),
                   MakeResolvedColumnRef(out_column,
                                         /*is_correlated=*/false),
                   MakeResolvedLiteral(Value::Null(array_type))))
      .set_type(array_type)
      .set_type_annotation_map(output_annotation_map)
      .Build();
}

namespace {
absl::StatusOr<FunctionSignature> GetBinaryFunctionSignatureFromArgumentTypes(
    const Function* function, const Type* left_expr_type,
    const Type* right_expr_type) {
  // Go through the list of possible function signatures and check if a
  // signature with 2 arguments that match the types `left_expr_type` and
  // `right_expr_type` is present. If so, return the signature. Otherwise return
  // an error.
  for (const FunctionSignature& signature : function->signatures()) {
    const FunctionArgumentTypeList& function_argument_type_list =
        signature.arguments();
    if (function_argument_type_list.size() != 2) {
      continue;
    }
    if (function_argument_type_list[0].type() == nullptr ||
        function_argument_type_list[1].type() == nullptr) {
      // Types can be null, if they are unspecified (e.g. ANY).
      // Such types are ignored here since an exact match is desired.
      continue;
    }
    if (left_expr_type->Equals(function_argument_type_list[0].type()) &&
        right_expr_type->Equals(function_argument_type_list[1].type())) {
      // If left and right have types, we make them concrete by setting
      // `num_occurrences` to 1.
      return MakeConcreteSignature(
          signature, signature.result_type().type(),
          {{function_argument_type_list[0].type(), 1},
           {function_argument_type_list[1].type(), 1}});
    }
  }
  GOOGLESQL_RET_CHECK_FAIL() << "No builtin function with name " << function->Name()
                   << " and argument types " << left_expr_type->DebugString()
                   << " and " << right_expr_type->DebugString() << " available";
}
}  // namespace

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::Greater(std::unique_ptr<const ResolvedExpr> left_expr,
                             std::unique_ptr<const ResolvedExpr> right_expr,
                             bool or_equal) {
  GOOGLESQL_RET_CHECK_NE(left_expr.get(), nullptr);
  GOOGLESQL_RET_CHECK_NE(right_expr.get(), nullptr);

  std::string unused_type_description;
  GOOGLESQL_RET_CHECK(left_expr->type()->SupportsOrdering(analyzer_options_.language(),
                                                &unused_type_description))
      << "GreaterOrEqual called for non-order-able type "
      << left_expr->type()->DebugString();
  GOOGLESQL_RET_CHECK(right_expr->type()->SupportsOrdering(analyzer_options_.language(),
                                                 &unused_type_description))
      << "GreaterOrEqual called for non-order-able type "
      << right_expr->type()->DebugString();

  const Function* greater_or_equal_function = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog(
      or_equal ? "$greater_or_equal" : "$greater", &greater_or_equal_function));

  std::unique_ptr<FunctionSignature> signature;
  if (!left_expr->type()->Equals(right_expr->type())) {
    // Unequal types can happen, but are only supported if the respective
    // function signature can be found in the catalog. An example of this is the
    // signature FunctionSignatureId::FN_GREATER_OR_EQUAL_INT64_UINT64.
    GOOGLESQL_ASSIGN_OR_RETURN(
        FunctionSignature unequal_types_signature,
        GetBinaryFunctionSignatureFromArgumentTypes(
            greater_or_equal_function, left_expr->type(), right_expr->type()));
    signature = std::make_unique<FunctionSignature>(unequal_types_signature);
  } else {
    const FunctionSignature* symmetric_signature;
    for (const FunctionSignature& signature :
         greater_or_equal_function->signatures()) {
      if (signature.arguments().size() == 2 &&
          signature.argument(0).kind() == ARG_KIND_EXPR_ANY_1 &&
          signature.argument(1).kind() == ARG_KIND_EXPR_ANY_1) {
        symmetric_signature = &signature;
        break;
      }
    }
    GOOGLESQL_RET_CHECK(symmetric_signature != nullptr);
    GOOGLESQL_ASSIGN_OR_RETURN(FunctionSignature concrete_signature,
                     MakeConcreteSignature(
                         *symmetric_signature, types::BoolType(),
                         {{left_expr->type(), 1}, {right_expr->type(), 1}}));
    signature = std::make_unique<FunctionSignature>(concrete_signature);
  }

  std::vector<std::unique_ptr<const ResolvedExpr>> arguments;
  arguments.reserve(2);
  arguments.emplace_back(std::move(left_expr));
  arguments.emplace_back(std::move(right_expr));

  auto call = MakeResolvedFunctionCall(
      signature->result_type().type(), greater_or_equal_function, *signature,
      std::move(arguments), ResolvedFunctionCall::DEFAULT_ERROR_MODE);
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::Greater(std::unique_ptr<const ResolvedExpr> left_expr,
                             std::unique_ptr<const ResolvedExpr> right_expr) {
  return Greater(std::move(left_expr), std::move(right_expr),
                 /*or_equal=*/false);
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::GreaterOrEqual(
    std::unique_ptr<const ResolvedExpr> left_expr,
    std::unique_ptr<const ResolvedExpr> right_expr) {
  return Greater(std::move(left_expr), std::move(right_expr),
                 /*or_equal=*/true);
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::Add(std::unique_ptr<const ResolvedExpr> left_expr,
                         std::unique_ptr<const ResolvedExpr> right_expr) {
  GOOGLESQL_RET_CHECK_NE(left_expr.get(), nullptr);
  GOOGLESQL_RET_CHECK_NE(right_expr.get(), nullptr);
  const Function* add_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("$add", &add_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(FunctionSignature signature,
                   GetBinaryFunctionSignatureFromArgumentTypes(
                       add_fn, left_expr->type(), right_expr->type()));

  std::vector<std::unique_ptr<const ResolvedExpr>> arguments;
  arguments.reserve(2);
  arguments.emplace_back(std::move(left_expr));
  arguments.emplace_back(std::move(right_expr));

  auto call = MakeResolvedFunctionCall(
      signature.result_type().type(), add_fn, signature, std::move(arguments),
      ResolvedFunctionCall::DEFAULT_ERROR_MODE);
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::Subtract(std::unique_ptr<const ResolvedExpr> minuend,
                              std::unique_ptr<const ResolvedExpr> subtrahend) {
  GOOGLESQL_RET_CHECK_NE(minuend.get(), nullptr);
  GOOGLESQL_RET_CHECK_NE(subtrahend.get(), nullptr);
  const Function* subtract_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("$subtract", &subtract_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(FunctionSignature signature,
                   GetBinaryFunctionSignatureFromArgumentTypes(
                       subtract_fn, minuend->type(), subtrahend->type()));

  std::vector<std::unique_ptr<const ResolvedExpr>> arguments;
  arguments.reserve(2);
  arguments.emplace_back(std::move(minuend));
  arguments.emplace_back(std::move(subtrahend));

  auto call = MakeResolvedFunctionCall(
      signature.result_type().type(), subtract_fn, signature,
      std::move(arguments), ResolvedFunctionCall::DEFAULT_ERROR_MODE);
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::Int64AddLiteral(std::unique_ptr<const ResolvedExpr> a,
                                     int b) {
  GOOGLESQL_RET_CHECK(a != nullptr);
  const Type* int64_type = types::Int64Type();
  GOOGLESQL_RET_CHECK(a->type()->Equals(int64_type));
  const Function* add_func = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("$add", &add_func));
  GOOGLESQL_ASSIGN_OR_RETURN(FunctionSignature signature,
                   MakeConcreteSignature(add_func, FN_ADD_INT64, int64_type,
                                         {{int64_type, 1}, {int64_type, 1}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> arguments;
  arguments.reserve(2);
  arguments.emplace_back(std::move(a));
  arguments.emplace_back(MakeResolvedLiteral(int64_type, Value::Int64(b)));

  auto call = MakeResolvedFunctionCall(
      signature.result_type().type(), add_func, signature, std::move(arguments),
      ResolvedFunctionCall::DEFAULT_ERROR_MODE);
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::Int64MultiplyByLiteral(
    std::unique_ptr<const ResolvedExpr> a, int b) {
  GOOGLESQL_RET_CHECK(a != nullptr);
  const Type* int64_type = types::Int64Type();
  GOOGLESQL_RET_CHECK(a->type()->Equals(int64_type));
  const Function* multiply_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("$multiply", &multiply_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature signature,
      MakeConcreteSignature(multiply_fn, FN_MULTIPLY_INT64, int64_type,
                            {{int64_type, 1}, {int64_type, 1}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> arguments;
  arguments.reserve(2);
  arguments.emplace_back(std::move(a));
  arguments.emplace_back(MakeResolvedLiteral(int64_type, Value::Int64(b)));

  auto call = MakeResolvedFunctionCall(
      signature.result_type().type(), multiply_fn, signature,
      std::move(arguments), ResolvedFunctionCall::DEFAULT_ERROR_MODE);
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::SafeSubtract(
    std::unique_ptr<const ResolvedExpr> minuend,
    std::unique_ptr<const ResolvedExpr> subtrahend) {
  GOOGLESQL_RET_CHECK_NE(minuend.get(), nullptr);
  GOOGLESQL_RET_CHECK_NE(subtrahend.get(), nullptr);
  const Function* safe_subtract_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(
      GetBuiltinFunctionFromCatalog("safe_subtract", &safe_subtract_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(FunctionSignature signature,
                   GetBinaryFunctionSignatureFromArgumentTypes(
                       safe_subtract_fn, minuend->type(), subtrahend->type()));

  std::vector<std::unique_ptr<const ResolvedExpr>> arguments;
  arguments.reserve(2);
  arguments.emplace_back(std::move(minuend));
  arguments.emplace_back(std::move(subtrahend));

  // Below, the error mode is chosen to be `DEFAULT_ERROR_MODE`, because
  // F1 does not support `SAFE_ERROR_MODE` in combination with `SAFE_SUBTRACT`.
  auto call = MakeResolvedFunctionCall(
      signature.result_type().type(), safe_subtract_fn, signature,
      std::move(arguments), ResolvedFunctionCall::DEFAULT_ERROR_MODE);
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::And(
    std::vector<std::unique_ptr<const ResolvedExpr>> expressions) {
  return NaryLogic("$and", FN_AND, std::move(expressions), types::BoolType());
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::Or(
    std::vector<std::unique_ptr<const ResolvedExpr>> expressions) {
  return NaryLogic("$or", FN_OR, std::move(expressions), types::BoolType());
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::NaryLogic(
    absl::string_view op_catalog_name, FunctionSignatureId op_function_id,
    std::vector<std::unique_ptr<const ResolvedExpr>> expressions,
    const Type* expr_type) {
  GOOGLESQL_RET_CHECK_GE(expressions.size(), 2);
  GOOGLESQL_RET_CHECK(absl::c_all_of(expressions, [expr_type](const auto& expr) {
    return expr->type()->Equals(expr_type);
  }));

  const Function* fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog(op_catalog_name, &fn));
  GOOGLESQL_ASSIGN_OR_RETURN(FunctionSignature signature,
                   MakeConcreteSignature(
                       fn, op_function_id, expr_type,
                       // FN_AND and FN_OR use 1 fixed arg and another repeated.
                       {{expr_type, 1}, {expr_type, expressions.size() - 1}}));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto call,
      ResolvedFunctionCallBuilder()
          .set_type(expr_type)
          .set_function(fn)
          .set_signature(signature)
          .set_argument_list(std::move(expressions))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::NestedBinaryOp(
    absl::string_view op_catalog_name, FunctionSignatureId op_function_id,
    std::vector<std::unique_ptr<const ResolvedExpr>> expressions,
    const Type* expr_type) {
  GOOGLESQL_RET_CHECK_GE(expressions.size(), 2);
  GOOGLESQL_RET_CHECK(absl::c_all_of(expressions, [expr_type](const auto& expr) {
    return expr->type()->Equals(expr_type);
  }));

  const Function* fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog(op_catalog_name, &fn));
  GOOGLESQL_ASSIGN_OR_RETURN(FunctionSignature signature,
                   MakeConcreteSignature(fn, op_function_id, expr_type,
                                         {{expr_type, 1}, {expr_type, 1}}));
  std::unique_ptr<const ResolvedExpr> result = std::move(expressions[0]);
  auto function_call_info = std::make_shared<ResolvedFunctionCallInfo>();
  for (size_t i = 1; i < expressions.size(); ++i) {
    std::vector<std::unique_ptr<const ResolvedExpr>> args(2);
    args[0] = std::move(result);
    args[1] = std::move(expressions[i]);
    GOOGLESQL_ASSIGN_OR_RETURN(
        auto call, ResolvedFunctionCallBuilder()
                       .set_type(expr_type)
                       .set_function(fn)
                       .set_signature(signature)
                       .set_argument_list(std::move(args))
                       .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
                       .set_function_call_info(function_call_info)
                       .BuildMutable());
    GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
    result = std::move(call);
  }
  return absl::WrapUnique(result.release()->GetAs<ResolvedFunctionCall>());
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
FunctionCallBuilder::CreateTypedLiteralZero(const Type* type) {
  GOOGLESQL_RET_CHECK(type->IsNumerical());
  switch (type->kind()) {
    case TYPE_INT64:
      return MakeResolvedLiteral(type, Value::Int64(0));
    case TYPE_UINT64:
      return MakeResolvedLiteral(type, Value::Uint64(0));
    case TYPE_DOUBLE:
      return MakeResolvedLiteral(type, Value::Double(0));
    case TYPE_NUMERIC:
      return MakeResolvedLiteral(type, Value::Numeric(NumericValue(0)));
    case TYPE_BIGNUMERIC:
      return MakeResolvedLiteral(type, Value::BigNumeric(BigNumericValue(0)));
    default: {
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected argument type in CreateTypedLiteralZero: "
                       << type->DebugString();
    }
  }
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::NestedBinaryInt64Add(
    std::vector<std::unique_ptr<const ResolvedExpr>> expressions) {
  return NestedBinaryOp("$add", FN_ADD_INT64, std::move(expressions),
                        types::Int64Type());
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::NestedBinaryAdd(
    const Type* type,
    std::vector<std::unique_ptr<const ResolvedExpr>> expressions) {
  GOOGLESQL_RET_CHECK(type->IsNumerical());
  FunctionSignatureId function_signature_id;
  switch (type->kind()) {
    case TYPE_INT64:
      function_signature_id = FN_ADD_INT64;
      break;
    case TYPE_UINT64:
      function_signature_id = FN_ADD_UINT64;
      break;
    case TYPE_DOUBLE:
      function_signature_id = FN_ADD_DOUBLE;
      break;
    case TYPE_NUMERIC:
      function_signature_id = FN_ADD_NUMERIC;
      break;
    case TYPE_BIGNUMERIC:
      function_signature_id = FN_ADD_BIGNUMERIC;
      break;
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected argument type in NestedBinaryAdd: "
                       << type->DebugString();
  }
  return NestedBinaryOp("$add", function_signature_id, std::move(expressions),
                        type);
}

// Construct a ResolvedAnalyticFunctionCall for ROW_NUMBER()
absl::StatusOr<std::unique_ptr<const ResolvedAnalyticFunctionCall>>
FunctionCallBuilder::RowNumber() {
  const Function* fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("row_number", &fn));
  GOOGLESQL_RET_CHECK(fn != nullptr);
  GOOGLESQL_RET_CHECK(fn->IsGoogleSQLBuiltin());
  GOOGLESQL_RET_CHECK(fn->IsAnalytic());
  GOOGLESQL_ASSIGN_OR_RETURN(FunctionSignature concrete_signature,
                   MakeConcreteSignature(fn, FN_ROW_NUMBER, types::Int64Type(),
                                         /*concrete_args=*/{}));

  GOOGLESQL_ASSIGN_OR_RETURN(auto call,
                   ResolvedAnalyticFunctionCallBuilder()
                       .set_type(type_factory_.get_int64())
                       .set_function(fn)
                       .set_signature(std::move(concrete_signature))
                       .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
                       .set_window_frame(nullptr)
                       .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::IsNodeEndpoint(
    std::unique_ptr<const ResolvedExpr> node_expr,
    std::unique_ptr<const ResolvedExpr> edge_expr,
    bool is_source_node_predicate) {
  GOOGLESQL_RET_CHECK(node_expr->type()->IsGraphElement());
  GOOGLESQL_RET_CHECK(node_expr->type()->AsGraphElement()->IsNode());
  GOOGLESQL_RET_CHECK(edge_expr->type()->IsGraphElement());
  GOOGLESQL_RET_CHECK(edge_expr->type()->AsGraphElement()->IsEdge());

  const Function* node_endpoint_fn = nullptr;
  absl::string_view fn_name =
      is_source_node_predicate ? "$is_source_node" : "$is_dest_node";
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog(fn_name, &node_endpoint_fn));

  FunctionSignatureId fn_id =
      is_source_node_predicate ? FN_IS_SOURCE_NODE : FN_IS_DEST_NODE;

  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature concrete_signature,
      MakeConcreteSignature(node_endpoint_fn, fn_id, types::BoolType(),
                            {{node_expr->type(), 1}, {edge_expr->type(), 1}}));
  std::vector<std::unique_ptr<const ResolvedExpr>> expressions(2);
  expressions[0] = std::move(node_expr);
  expressions[1] = std::move(edge_expr);
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto call,
      ResolvedFunctionCallBuilder()
          .set_type(types::BoolType())
          .set_function(node_endpoint_fn)
          .set_signature(std::move(concrete_signature))
          .set_argument_list(std::move(expressions))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

// Returns the common supertype of the two given GraphElement types.
absl::StatusOr<const googlesql::Type*> GetCommonGraphElementSuperType(
    const googlesql::Type* type1, const googlesql::Type* type2,
    Coercer& coercer) {
  GOOGLESQL_RET_CHECK(type1 != nullptr);
  GOOGLESQL_RET_CHECK(type2 != nullptr);
  InputArgumentTypeSet types;
  InputArgumentType arg_type1(type1, /*is_query_parameter=*/false,
                              /*is_literal_for_constness=*/false);
  InputArgumentType arg_type2(type2, /*is_query_parameter=*/false,
                              /*is_literal_for_constness=*/true);
  types.Insert(arg_type1);
  types.Insert(arg_type2);
  const Type* common_supertype = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(coercer.GetCommonSuperType(types, &common_supertype));
  return common_supertype;
}

// Returns a cast of the given expr to the given type, or the expr itself if
// the types are already the same.
absl::StatusOr<std::unique_ptr<const ResolvedExpr>> CastGraphElementIfNeeded(
    std::unique_ptr<const ResolvedExpr> expr, const Type* to_type) {
  GOOGLESQL_RET_CHECK(expr != nullptr);
  GOOGLESQL_RET_CHECK(to_type != nullptr);

  // The to_type is already the same as the expr type, return the expr
  // directly.
  if (expr->type()->Equals(to_type)) {
    return std::move(expr);
  }
  TypeModifiers type_modifiers;
  return MakeResolvedCast(to_type, std::move(expr),
                          /*return_null_on_error=*/false);
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::AreEqualGraphElements(
    std::vector<std::unique_ptr<const ResolvedExpr>> element_expressions) {
  GOOGLESQL_RET_CHECK_GE(element_expressions.size(), 2);
  bool is_node =
      element_expressions.front()->type()->IsGraphElement() &&
      element_expressions.front()->type()->AsGraphElement()->IsNode();
  GOOGLESQL_RET_CHECK(absl::c_all_of(
      element_expressions,
      [is_node](const std::unique_ptr<const ResolvedExpr>& expr) {
        return expr->type()->IsGraphElement() &&
               expr->type()->AsGraphElement()->IsNode() == is_node;
      }));

  std::vector<std::unique_ptr<const ResolvedExpr>> equalities;
  equalities.reserve(element_expressions.size() - 1);
  for (int i = 1; i < element_expressions.size(); ++i) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> copy,
        ResolvedASTDeepCopyVisitor::Copy(element_expressions[0].get()));
    std::unique_ptr<const ResolvedExpr> lhs = std::move(copy);
    std::unique_ptr<const ResolvedExpr> rhs = std::move(element_expressions[i]);
    // Supertype case.
    if (!lhs->type()->Equals(rhs->type())) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          const googlesql::Type* common_type,
          GetCommonGraphElementSuperType(lhs->type(), rhs->type(), coercer_));
      GOOGLESQL_ASSIGN_OR_RETURN(lhs,
                       CastGraphElementIfNeeded(std::move(lhs), common_type));
      GOOGLESQL_ASSIGN_OR_RETURN(rhs,
                       CastGraphElementIfNeeded(std::move(rhs), common_type));
    }

    std::vector<std::unique_ptr<const ResolvedExpr>> args(2);
    args[0] = std::move(lhs);
    args[1] = std::move(rhs);

    const Function* equal_fn = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("$equal", &equal_fn));
    GOOGLESQL_RET_CHECK(equal_fn != nullptr);
    GOOGLESQL_ASSIGN_OR_RETURN(
        FunctionSignature concrete_signature,
        MakeConcreteSignature(
            equal_fn, is_node ? FN_EQUAL_GRAPH_NODE : FN_EQUAL_GRAPH_EDGE,
            types::BoolType(), {{args[0]->type(), 1}, {args[1]->type(), 1}}));
    GOOGLESQL_ASSIGN_OR_RETURN(
        auto equality,
        ResolvedFunctionCallBuilder()
            .set_type(types::BoolType())
            .set_function(equal_fn)
            .set_signature(std::move(concrete_signature))
            .set_argument_list(std::move(args))
            .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
            .set_function_call_info(
                std::make_shared<ResolvedFunctionCallInfo>())
            .BuildMutable());
    GOOGLESQL_RETURN_IF_ERROR(
        PropagateAnnotationsAndProcessCollationList(equality.get()));
    if (element_expressions.size() == 2) {
      return equality;
    }
    equalities.emplace_back(std::move(equality));
  }
  return And(std::move(equalities));
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::ArrayLength(
    std::unique_ptr<const ResolvedExpr> array_expr) {
  GOOGLESQL_RET_CHECK_NE(array_expr.get(), nullptr);
  GOOGLESQL_RET_CHECK(array_expr->type()->IsArray());
  const Function* array_length_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(
      GetBuiltinFunctionFromCatalog("array_length", &array_length_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature concrete_signature,
      MakeConcreteSignature(array_length_fn, FN_ARRAY_LENGTH,
                            types::Int64Type(), {{array_expr->type(), 1}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(std::move(array_expr));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto call,
      ResolvedFunctionCallBuilder()
          .set_type(types::Int64Type())
          .set_function(array_length_fn)
          .set_signature(std::move(concrete_signature))
          .set_argument_list(std::move(args))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::ArrayIsDistinct(
    std::unique_ptr<const ResolvedExpr> array_expr) {
  GOOGLESQL_RET_CHECK_NE(array_expr.get(), nullptr);
  GOOGLESQL_RET_CHECK(array_expr->type()->IsArray());
  const Function* array_is_distinct_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("array_is_distinct",
                                                &array_is_distinct_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature concrete_signature,
      MakeConcreteSignature(array_is_distinct_fn, FN_ARRAY_IS_DISTINCT,
                            types::BoolType(), {{array_expr->type(), 1}}));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto call,
      ResolvedFunctionCallBuilder()
          .set_type(types::BoolType())
          .set_function(array_is_distinct_fn)
          .set_signature(std::move(concrete_signature))
          .add_argument_list(std::move(array_expr))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::ArrayAtOffset(
    std::unique_ptr<const ResolvedExpr> array_expr,
    std::unique_ptr<const ResolvedExpr> offset_expr) {
  GOOGLESQL_RET_CHECK(array_expr->type()->IsArray());
  GOOGLESQL_RET_CHECK_EQ(offset_expr->type(), types::Int64Type());

  const Function* array_at_offset_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(
      GetBuiltinFunctionFromCatalog("$array_at_offset", &array_at_offset_fn));
  const Type* element_type = array_expr->type()->AsArray()->element_type();
  GOOGLESQL_ASSIGN_OR_RETURN(FunctionSignature concrete_signature,
                   MakeConcreteSignature(
                       array_at_offset_fn, FN_ARRAY_AT_OFFSET, element_type,
                       {{array_expr->type(), 1}, {offset_expr->type(), 1}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(std::move(array_expr));
  args.push_back(std::move(offset_expr));

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ResolvedFunctionCall> resolved_function,
      ResolvedFunctionCallBuilder()
          .set_type(element_type)
          .set_function(array_at_offset_fn)
          .set_signature(std::move(concrete_signature))
          .set_argument_list(std::move(args))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());

  GOOGLESQL_RETURN_IF_ERROR(
      PropagateAnnotationsAndProcessCollationList(resolved_function.get()));
  return resolved_function;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::ArraySlice(
    std::unique_ptr<const ResolvedExpr> array_expr,
    std::unique_ptr<const ResolvedExpr> start_offset_expr,
    std::unique_ptr<const ResolvedExpr> end_offset_expr) {
  GOOGLESQL_RET_CHECK(array_expr->type()->IsArray());
  GOOGLESQL_RET_CHECK(start_offset_expr->type()->IsInt64());
  GOOGLESQL_RET_CHECK(end_offset_expr->type()->IsInt64());

  const Function* array_slice_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(
      GetBuiltinFunctionFromCatalog("array_slice", &array_slice_fn));
  const Type* result_type = array_expr->type();
  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature concrete_signature,
      MakeConcreteSignature(array_slice_fn, FN_ARRAY_SLICE, result_type,
                            {{result_type, 1},
                             {start_offset_expr->type(), 1},
                             {end_offset_expr->type(), 1}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(std::move(array_expr));
  args.push_back(std::move(start_offset_expr));
  args.push_back(std::move(end_offset_expr));

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ResolvedFunctionCall> resolved_function,
      ResolvedFunctionCallBuilder()
          .set_type(result_type)
          .set_function(array_slice_fn)
          .set_signature(std::move(concrete_signature))
          .set_argument_list(std::move(args))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());

  GOOGLESQL_RETURN_IF_ERROR(
      PropagateAnnotationsAndProcessCollationList(resolved_function.get()));
  return resolved_function;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::In(
    std::unique_ptr<const ResolvedExpr> search_expr,
    std::vector<std::unique_ptr<const ResolvedExpr>> in_exprs) {
  // The IN list syntax requires a valid left-hand expression supporting
  // equality and at least one right-hand candidate expression.
  GOOGLESQL_RET_CHECK_NE(search_expr.get(), nullptr);
  GOOGLESQL_RET_CHECK(!in_exprs.empty());
  GOOGLESQL_RET_CHECK(search_expr->type()->SupportsEquality());

  for (const auto& expr : in_exprs) {
    GOOGLESQL_RET_CHECK_NE(expr.get(), nullptr);
    GOOGLESQL_RET_CHECK(search_expr->type()->Equals(expr->type()))
        << "Expected search_expr->type()->Equals(expr->type()) to be true, "
        << "but it was false. search_expr->type(): "
        << search_expr->type()->DebugString()
        << ", expr->type(): " << expr->type()->DebugString();
    GOOGLESQL_RET_CHECK(AnnotationMap::Equals(expr->type_annotation_map(),
                                    search_expr->type_annotation_map()));
  }

  const Function* in_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("$in", &in_fn));

  // The FN_IN signature takes one scalar search expression and a repeated
  // argument descriptor matching the number of elements in `in_exprs`.
  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature concrete_signature,
      MakeConcreteSignature(
          in_fn, FN_IN, types::BoolType(),
          {{search_expr->type(), 1}, {search_expr->type(), in_exprs.size()}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.reserve(in_exprs.size() + 1);
  args.push_back(std::move(search_expr));
  for (auto& expr : in_exprs) {
    args.push_back(std::move(expr));
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ResolvedFunctionCall> resolved_function,
      ResolvedFunctionCallBuilder()
          .set_type(types::BoolType())
          .set_function(in_fn)
          .set_signature(std::move(concrete_signature))
          .set_argument_list(std::move(args))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());

  // Populate operation collation on the function call from argument
  // annotations.
  GOOGLESQL_RETURN_IF_ERROR(
      PropagateAnnotationsAndProcessCollationList(resolved_function.get()));
  return resolved_function;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::InArray(std::unique_ptr<const ResolvedExpr> search_expr,
                             std::unique_ptr<const ResolvedExpr> array_expr) {
  GOOGLESQL_RET_CHECK(search_expr != nullptr);
  GOOGLESQL_RET_CHECK(array_expr != nullptr);
  GOOGLESQL_RET_CHECK(array_expr->type()->IsArray());
  GOOGLESQL_RET_CHECK(array_expr->type()->AsArray()->element_type()->Equals(
      search_expr->type()));

  const Function* in_array_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("$in_array", &in_array_fn));

  GOOGLESQL_ASSIGN_OR_RETURN(FunctionSignature concrete_signature,
                   MakeConcreteSignature(
                       in_array_fn, FN_IN_ARRAY, types::BoolType(),
                       {{search_expr->type(), 1}, {array_expr->type(), 1}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(std::move(search_expr));
  args.push_back(std::move(array_expr));

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ResolvedFunctionCall> resolved_function,
      ResolvedFunctionCallBuilder()
          .set_type(types::BoolType())
          .set_function(in_array_fn)
          .set_signature(std::move(concrete_signature))
          .set_argument_list(std::move(args))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());

  GOOGLESQL_RETURN_IF_ERROR(
      PropagateAnnotationsAndProcessCollationList(resolved_function.get()));
  return resolved_function;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::ArrayIncludesAll(
    std::unique_ptr<const ResolvedExpr> array_expr,
    std::unique_ptr<const ResolvedExpr> search_expr) {
  GOOGLESQL_RET_CHECK(search_expr != nullptr);
  GOOGLESQL_RET_CHECK(array_expr != nullptr);
  GOOGLESQL_RET_CHECK(array_expr->type()->IsArray());
  GOOGLESQL_RET_CHECK(array_expr->type()->AsArray()->element_type()->Equals(
      search_expr->type()->AsArray()->element_type()));

  const Function* array_includes_all_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("array_includes_all",
                                                &array_includes_all_fn));

  GOOGLESQL_RET_CHECK_EQ(array_includes_all_fn->signatures().size(), 1);
  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature concrete_signature,
      MakeConcreteSignature(
          array_includes_all_fn, FN_ARRAY_INCLUDES_ALL, types::BoolType(),
          {{array_expr->type(), 1}, {search_expr->type(), 1}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(std::move(array_expr));
  args.push_back(std::move(search_expr));

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ResolvedFunctionCall> resolved_function,
      ResolvedFunctionCallBuilder()
          .set_type(types::BoolType())
          .set_function(array_includes_all_fn)
          .set_signature(std::move(concrete_signature))
          .set_argument_list(std::move(args))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());

  GOOGLESQL_RETURN_IF_ERROR(
      PropagateAnnotationsAndProcessCollationList(resolved_function.get()));
  return resolved_function;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::ArrayIncludesAny(
    std::unique_ptr<const ResolvedExpr> array_expr,
    std::unique_ptr<const ResolvedExpr> search_expr) {
  GOOGLESQL_RET_CHECK(search_expr != nullptr);
  GOOGLESQL_RET_CHECK(array_expr != nullptr);
  GOOGLESQL_RET_CHECK(array_expr->type()->IsArray());
  GOOGLESQL_RET_CHECK(array_expr->type()->AsArray()->element_type()->Equals(
      search_expr->type()->AsArray()->element_type()));

  const Function* array_includes_any_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("array_includes_any",
                                                &array_includes_any_fn));

  GOOGLESQL_RET_CHECK_EQ(array_includes_any_fn->signatures().size(), 1);
  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature concrete_signature,
      MakeConcreteSignature(
          array_includes_any_fn, FN_ARRAY_INCLUDES_ANY, types::BoolType(),
          {{array_expr->type(), 1}, {search_expr->type(), 1}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(std::move(array_expr));
  args.push_back(std::move(search_expr));

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ResolvedFunctionCall> resolved_function,
      ResolvedFunctionCallBuilder()
          .set_type(types::BoolType())
          .set_function(array_includes_any_fn)
          .set_signature(std::move(concrete_signature))
          .set_argument_list(std::move(args))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());

  GOOGLESQL_RETURN_IF_ERROR(
      PropagateAnnotationsAndProcessCollationList(resolved_function.get()));
  return resolved_function;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::ArrayToString(
    std::unique_ptr<const ResolvedExpr> array_expr,
    std::unique_ptr<const ResolvedExpr> delimiter_expr) {
  GOOGLESQL_RET_CHECK(array_expr != nullptr);
  GOOGLESQL_RET_CHECK(delimiter_expr != nullptr);
  GOOGLESQL_RET_CHECK(array_expr->type()->IsArray());
  GOOGLESQL_RET_CHECK(delimiter_expr->type()->IsString() ||
            delimiter_expr->type()->IsBytes());
  GOOGLESQL_RET_CHECK_EQ(array_expr->type()->AsArray()->element_type(),
               delimiter_expr->type());

  const Function* array_to_string_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(
      GetBuiltinFunctionFromCatalog("array_to_string", &array_to_string_fn));

  GOOGLESQL_RET_CHECK_EQ(array_to_string_fn->signatures().size(), 2);
  const FunctionSignature* catalog_signature = nullptr;
  for (const FunctionSignature& signature : array_to_string_fn->signatures()) {
    if (signature.result_type().type() == delimiter_expr->type()) {
      catalog_signature = &signature;
      break;
    }
  }
  GOOGLESQL_RET_CHECK(catalog_signature != nullptr);
  GOOGLESQL_RET_CHECK_EQ(catalog_signature->arguments().size(), 3);

  const Type* result_type = delimiter_expr->type();
  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature concrete_signature,
      MakeConcreteSignature(*catalog_signature, result_type,
                            {{array_expr->type(), 1}, {result_type, 1}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(std::move(array_expr));
  args.push_back(std::move(delimiter_expr));

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ResolvedFunctionCall> resolved_function,
      ResolvedFunctionCallBuilder()
          .set_type(result_type)
          .set_function(array_to_string_fn)
          .set_signature(std::move(concrete_signature))
          .set_argument_list(std::move(args))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());

  GOOGLESQL_RETURN_IF_ERROR(
      PropagateAnnotationsAndProcessCollationList(resolved_function.get()));
  return resolved_function;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::RegexpContainsString(
    std::unique_ptr<const ResolvedExpr> value_expr,
    std::unique_ptr<const ResolvedExpr> regexp_expr) {
  GOOGLESQL_RET_CHECK(value_expr != nullptr);
  GOOGLESQL_RET_CHECK(regexp_expr != nullptr);
  GOOGLESQL_RET_CHECK(value_expr->type()->IsString());
  GOOGLESQL_RET_CHECK(regexp_expr->type()->IsString());

  const Function* regexp_contains_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(
      GetBuiltinFunctionFromCatalog("regexp_contains", &regexp_contains_fn));

  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature concrete_signature,
      MakeConcreteSignature(
          regexp_contains_fn, FN_REGEXP_CONTAINS_STRING, types::BoolType(),
          {{value_expr->type(), 1}, {regexp_expr->type(), 1}}));
  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(std::move(value_expr));
  args.push_back(std::move(regexp_expr));

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ResolvedFunctionCall> resolved_function,
      ResolvedFunctionCallBuilder()
          .set_type(types::BoolType())
          .set_function(regexp_contains_fn)
          .set_signature(std::move(concrete_signature))
          .set_argument_list(std::move(args))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());

  GOOGLESQL_RETURN_IF_ERROR(
      PropagateAnnotationsAndProcessCollationList(resolved_function.get()));
  return resolved_function;
}

// Returns the FunctionSignatureId of MOD corresponding to `input_type`.
static absl::StatusOr<FunctionSignatureId> GetModSignatureIdForInputType(
    const Function* mod_fn, const Type* input_type) {
  if (input_type == types::Int64Type()) {
    return FN_MOD_INT64;
  }
  if (input_type == types::Uint64Type()) {
    return FN_MOD_UINT64;
  }
  if (input_type == types::NumericType()) {
    return FN_MOD_NUMERIC;
  }
  if (input_type == types::BigNumericType()) {
    return FN_MOD_BIGNUMERIC;
  }
  return absl::InvalidArgumentError(absl::StrCat(
      "Unsupported input type for mod: ", input_type->DebugString()));
}

// Returns the FunctionSignature of MOD corresponding to `input_type`.
static absl::StatusOr<const FunctionSignature*> GetModSignature(
    const Function* mod_fn, const Type* input_type) {
  GOOGLESQL_ASSIGN_OR_RETURN(FunctionSignatureId mod_signature_id,
                   GetModSignatureIdForInputType(mod_fn, input_type));
  const FunctionSignature* catalog_signature = nullptr;
  for (const FunctionSignature& signature : mod_fn->signatures()) {
    if (signature.context_id() == mod_signature_id) {
      catalog_signature = &signature;
      break;
    }
  }
  if (catalog_signature == nullptr) {
    switch (mod_signature_id) {
      case FN_MOD_NUMERIC:
        return absl::InvalidArgumentError(
            "The provided catalog does not have the FN_MOD_NUMERIC "
            "signature. "
            "Did you forget to enable FEATURE_NUMERIC_TYPE?");
      case FN_MOD_BIGNUMERIC:
        return absl::InvalidArgumentError(
            "The provided catalog does not have the FN_MOD_BIGNUMERIC "
            "signature. Did you forget to enable FEATURE_BIGNUMERIC_TYPE?");
      default:
        GOOGLESQL_RET_CHECK_FAIL();
    }
  }
  return catalog_signature;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::Mod(std::unique_ptr<const ResolvedExpr> dividend_expr,
                         std::unique_ptr<const ResolvedExpr> divisor_expr) {
  GOOGLESQL_RET_CHECK_EQ(dividend_expr->type(), divisor_expr->type());
  const Type* input_type = dividend_expr->type();

  const Function* mod_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("mod", &mod_fn));

  GOOGLESQL_ASSIGN_OR_RETURN(const FunctionSignature* catalog_signature,
                   GetModSignature(mod_fn, input_type));
  GOOGLESQL_RET_CHECK_EQ(catalog_signature->arguments().size(), 2);

  GOOGLESQL_ASSIGN_OR_RETURN(FunctionSignature concrete_signature,
                   MakeConcreteSignature(*catalog_signature, input_type,
                                         {{dividend_expr->type(), 1},
                                          {divisor_expr->type(), 1}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(std::move(dividend_expr));
  args.push_back(std::move(divisor_expr));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto call,
      ResolvedFunctionCallBuilder()
          .set_type(catalog_signature->result_type().type())
          .set_function(mod_fn)
          .set_signature(std::move(concrete_signature))
          .set_argument_list(std::move(args))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedAggregateFunctionCall>>
FunctionCallBuilder::Count(std::unique_ptr<const ResolvedExpr> expr,
                           bool is_distinct) {
  const Function* count_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("count", &count_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(FunctionSignature concrete_signature,
                   MakeConcreteSignature(count_fn, FN_COUNT, types::Int64Type(),
                                         {{expr->type(), 1}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> count_fn_args;
  count_fn_args.push_back(std::move(expr));

  GOOGLESQL_ASSIGN_OR_RETURN(auto call,
                   ResolvedAggregateFunctionCallBuilder()
                       .set_type(types::Int64Type())
                       .set_function(count_fn)
                       .set_signature(std::move(concrete_signature))
                       .set_argument_list(std::move(count_fn_args))
                       .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
                       .set_distinct(is_distinct)
                       .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
FunctionCallBuilder::CountStar(bool is_distinct, bool is_analytic) {
  const Function* count_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("$count_star", &count_fn));
  GOOGLESQL_RET_CHECK(count_fn != nullptr);
  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature concrete_signature,
      MakeConcreteSignature(count_fn, FN_COUNT_STAR, types::Int64Type(),
                            /*concrete_args=*/{}));

  if (is_analytic) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        auto call,
        ResolvedAnalyticFunctionCallBuilder()
            .set_type(type_factory_.get_int64())
            .set_function(count_fn)
            .set_signature(std::move(concrete_signature))
            .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
            .set_distinct(is_distinct)
            .set_window_frame(
                ResolvedWindowFrameBuilder()
                    .set_start_expr(
                        ResolvedWindowFrameExprBuilder()
                            .set_boundary_type(
                                ResolvedWindowFrameExpr::UNBOUNDED_PRECEDING)
                            .set_expression(nullptr))
                    .set_end_expr(
                        ResolvedWindowFrameExprBuilder()
                            .set_boundary_type(
                                ResolvedWindowFrameExpr::UNBOUNDED_FOLLOWING)
                            .set_expression(nullptr))
                    .set_frame_unit(ResolvedWindowFrame::ROWS))
            .BuildMutable());
    GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
    return call;
  }

  GOOGLESQL_ASSIGN_OR_RETURN(auto call,
                   ResolvedAggregateFunctionCallBuilder()
                       .set_type(type_factory_.get_int64())
                       .set_function(count_fn)
                       .set_signature(std::move(concrete_signature))
                       .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
                       .set_distinct(is_distinct)
                       .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::WithSideEffects(
    std::unique_ptr<const ResolvedExpr> expr,
    std::unique_ptr<const ResolvedExpr> payload) {
  const Type* type = expr->type();
  const Function* with_side_effects_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("$with_side_effects",
                                                &with_side_effects_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(
      const FunctionSignature concrete_signature,
      MakeConcreteSignature(with_side_effects_fn, FN_WITH_SIDE_EFFECTS, type,
                            {{type, 1}, {types::BytesType(), 1}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(std::move(expr));
  args.push_back(std::move(payload));
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto call,
      ResolvedFunctionCallBuilder()
          .set_type(type)
          .set_function(with_side_effects_fn)
          .set_signature(std::move(concrete_signature))
          .set_argument_list(std::move(args))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedAggregateFunctionCall>>
FunctionCallBuilder::AnyValue(
    std::unique_ptr<const ResolvedExpr> input_expr,
    std::unique_ptr<const ResolvedExpr> having_min_max_expr, bool is_max) {
  GOOGLESQL_RET_CHECK(input_expr != nullptr);
  const Type* input_type = input_expr->type();
  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(std::move(input_expr));

  const Function* any_value_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("any_value", &any_value_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(FunctionSignature concrete_signature,
                   MakeConcreteSignature(any_value_fn, FN_ANY_VALUE, input_type,
                                         {{input_type, 1}}));

  std::unique_ptr<const ResolvedAggregateHavingModifier> resolved_having =
      having_min_max_expr != nullptr
          ? MakeResolvedAggregateHavingModifier(
                is_max ? ResolvedAggregateHavingModifier::MAX
                       : ResolvedAggregateHavingModifier::MIN,
                std::move(having_min_max_expr))
          : nullptr;

  GOOGLESQL_ASSIGN_OR_RETURN(auto call,
                   ResolvedAggregateFunctionCallBuilder()
                       .set_type(input_type)
                       .set_function(any_value_fn)
                       .set_signature(std::move(concrete_signature))
                       .set_argument_list(std::move(args))
                       .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
                       .set_having_modifier(std::move(resolved_having))
                       .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedAggregateFunctionCall>>
FunctionCallBuilder::ArrayAgg(
    std::unique_ptr<const ResolvedExpr> input_expr,
    std::unique_ptr<const ResolvedExpr> having_expr,
    ResolvedAggregateHavingModifier::HavingModifierKind having_kind) {
  GOOGLESQL_RET_CHECK(input_expr != nullptr);
  const Type* input_type = input_expr->type();
  GOOGLESQL_ASSIGN_OR_RETURN(
      const Type* array_type,
      type_factory_.MakeArrayType(input_type, analyzer_options_.language()));
  const Function* array_agg_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("array_agg", &array_agg_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(FunctionSignature array_agg_fn_sig,
                   MakeConcreteSignature(array_agg_fn, FN_ARRAY_AGG, array_type,
                                         {{input_type, 1}}));

  auto builder = ResolvedAggregateFunctionCallBuilder()
                     .set_type(array_type)
                     .set_function(array_agg_fn)
                     .set_signature(array_agg_fn_sig)
                     .add_argument_list(std::move(input_expr))
                     .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE);
  if (having_expr != nullptr) {
    builder.set_having_modifier(ResolvedAggregateHavingModifierBuilder()
                                    .set_having_expr(std::move(having_expr))
                                    .set_kind(having_kind));
  }
  GOOGLESQL_ASSIGN_OR_RETURN(auto call, std::move(builder).BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
FunctionCallBuilder::UncheckedPathCreate(
    const GraphPathType* path_type,
    std::vector<std::unique_ptr<const ResolvedExpr>> components) {
  GOOGLESQL_RET_CHECK_EQ(components.size() % 2, 1)
      << "Number of columns must be odd. Given: "
      << absl::StrJoin(components, ", ",
                       [](std::string* out,
                          const std::unique_ptr<const ResolvedExpr>& expr) {
                         absl::StrAppend(out, expr->DebugString());
                       });
  for (int i = 0; i < components.size(); ++i) {
    GOOGLESQL_RET_CHECK(components[i]->type()->IsGraphElement())
        << "Index " << i
        << " is not the right type: " << components[i]->type()->DebugString();
    GOOGLESQL_RET_CHECK_EQ(i % 2 == 0, components[i]->type()->AsGraphElement()->IsNode())
        << "Index " << i
        << " is not the right type: " << components[i]->type()->DebugString();
    const GraphElementType* expected_type =
        i % 2 == 0 ? path_type->node_type() : path_type->edge_type();
    if (!components[i]->type()->Equals(expected_type)) {
      components[i] = MakeResolvedCast(expected_type, std::move(components[i]),
                                       /*return_null_on_error=*/false);
    }
  }
  std::vector<ConcreteArgument> arg_types;
  arg_types.emplace_back(components[0]->type(), 1);
  if (components.size() > 1) {
    GOOGLESQL_RET_CHECK_GE(components.size(), 3);
    arg_types.emplace_back(components[1]->type(), components.size() / 2);
    arg_types.emplace_back(components[2]->type(), components.size() / 2);
  }

  const Function* path_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("$unchecked_path", &path_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(FunctionSignature concrete_signature,
                   MakeConcreteSignature(path_fn, FN_UNCHECKED_PATH_CREATE,
                                         path_type, arg_types));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto call,
      ResolvedFunctionCallBuilder()
          .set_type(path_type)
          .set_function(path_fn)
          .set_signature(std::move(concrete_signature))
          .set_argument_list(std::move(components))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
FunctionCallBuilder::UncheckedPathConcat(
    const GraphPathType* path_type,
    std::vector<std::unique_ptr<const ResolvedExpr>> paths) {
  GOOGLESQL_RET_CHECK(path_type != nullptr);
  GOOGLESQL_RET_CHECK_GE(paths.size(), 1);
  for (auto& path : paths) {
    if (!path->type()->Equals(path_type)) {
      path = MakeResolvedCast(path_type, std::move(path),
                              /*return_null_on_error=*/false);
    }
  }
  if (paths.size() == 1) {
    return std::move(paths[0]);
  }

  const Function* path_concat_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(
      GetBuiltinFunctionFromCatalog("$unchecked_path_concat", &path_concat_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature concrete_signature,
      MakeConcreteSignature(path_concat_fn, FN_UNCHECKED_CONCAT_PATH, path_type,
                            {{path_type, 1}, {path_type, paths.size() - 1}}));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto call,
      ResolvedFunctionCallBuilder()
          .set_type(path_type)
          .set_function(path_concat_fn)
          .set_signature(std::move(concrete_signature))
          .set_argument_list(std::move(paths))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
FunctionCallBuilder::PathFirst(std::unique_ptr<const ResolvedExpr> path) {
  GOOGLESQL_RET_CHECK(path != nullptr);
  GOOGLESQL_RET_CHECK(path->type()->IsGraphPath());
  const Type* return_type = path->type()->AsGraphPath()->node_type();

  const Function* path_first_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("path_first", &path_first_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(FunctionSignature concrete_signature,
                   MakeConcreteSignature(path_first_fn, FN_PATH_FIRST,
                                         return_type, {{path->type(), 1}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(std::move(path));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto call,
      ResolvedFunctionCallBuilder()
          .set_type(return_type)
          .set_function(path_first_fn)
          .set_signature(std::move(concrete_signature))
          .set_argument_list(std::move(args))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
FunctionCallBuilder::PathLast(std::unique_ptr<const ResolvedExpr> path) {
  GOOGLESQL_RET_CHECK(path != nullptr);
  GOOGLESQL_RET_CHECK(path->type()->IsGraphPath());
  const Type* return_type = path->type()->AsGraphPath()->node_type();

  const Function* path_last_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("path_last", &path_last_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(FunctionSignature concrete_signature,
                   MakeConcreteSignature(path_last_fn, FN_PATH_LAST,
                                         return_type, {{path->type(), 1}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(std::move(path));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto call,
      ResolvedFunctionCallBuilder()
          .set_type(return_type)
          .set_function(path_last_fn)
          .set_signature(std::move(concrete_signature))
          .set_argument_list(std::move(args))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
FunctionCallBuilder::PathNodes(
    /*absl_nonnull*/ std::unique_ptr<const ResolvedExpr> path) {
  GOOGLESQL_RET_CHECK(path->type()->IsGraphPath());
  GOOGLESQL_ASSIGN_OR_RETURN(
      const Type* return_type,
      type_factory_.MakeArrayType(path->type()->AsGraphPath()->node_type(),
                                  analyzer_options_.language()));

  const Function* path_nodes_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("nodes", &path_nodes_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(FunctionSignature concrete_signature,
                   MakeConcreteSignature(path_nodes_fn, FN_PATH_NODES,
                                         return_type, {{path->type(), 1}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(std::move(path));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto call,
      ResolvedFunctionCallBuilder()
          .set_type(return_type)
          .set_function(path_nodes_fn)
          .set_signature(std::move(concrete_signature))
          .set_argument_list(std::move(args))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
FunctionCallBuilder::PathEdges(std::unique_ptr<const ResolvedExpr> path) {
  GOOGLESQL_RET_CHECK(path != nullptr);
  GOOGLESQL_RET_CHECK(path->type()->IsGraphPath());
  GOOGLESQL_ASSIGN_OR_RETURN(
      const Type* return_type,
      type_factory_.MakeArrayType(path->type()->AsGraphPath()->edge_type(),
                                  analyzer_options_.language()));

  const Function* path_edges_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("edges", &path_edges_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(FunctionSignature concrete_signature,
                   MakeConcreteSignature(path_edges_fn, FN_PATH_EDGES,
                                         return_type, {{path->type(), 1}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(std::move(path));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto call,
      ResolvedFunctionCallBuilder()
          .set_type(return_type)
          .set_function(path_edges_fn)
          .set_signature(std::move(concrete_signature))
          .set_argument_list(std::move(args))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<bool> CatalogSupportsBuiltinFunction(
    absl::string_view function_name, const AnalyzerOptions& analyzer_options,
    Catalog& catalog) {
  const Function* fn;
  absl::Status find_status = catalog.FindFunction(
      {std::string(function_name)}, &fn, analyzer_options.find_options());
  if (find_status.ok()) {
    return fn != nullptr && fn->IsGoogleSQLBuiltin();
  }
  if (absl::IsNotFound(find_status)) {
    return false;
  }
  return find_status;
}

absl::Status CheckCatalogSupportsSafeMode(
    absl::string_view function_name, const AnalyzerOptions& analyzer_options,
    Catalog& catalog) {
  GOOGLESQL_ASSIGN_OR_RETURN(
      bool supports_safe_mode,
      CatalogSupportsBuiltinFunction("NULLIFERROR", analyzer_options, catalog));
  // In case NULLIFERROR is supported through rewrite (a common case) then we
  // also need to check for the IFERROR function.
  if (supports_safe_mode && analyzer_options.enabled_rewrites().contains(
                                REWRITE_NULLIFERROR_FUNCTION)) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        supports_safe_mode,
        CatalogSupportsBuiltinFunction("IFERROR", analyzer_options, catalog));
  }
  if (!supports_safe_mode) {
    return absl::UnimplementedError(absl::StrCat(
        "SAFE mode calls to ", function_name, " are not supported."));
  }
  return absl::OkStatus();
}

// Checks whether the ResolvedAST has grouping function related nodes.
absl::StatusOr<bool> HasGroupingCallNode(const ResolvedNode* node) {
  bool has_grouping_call = false;
  GroupingCallDetectorVisitor visitor(&has_grouping_call);
  GOOGLESQL_RETURN_IF_ERROR(node->Accept(&visitor));
  return has_grouping_call;
}

absl::StatusOr<bool> ContainsWithScan(const googlesql::ResolvedNode& node) {
  WithScanVisitor visitor;
  GOOGLESQL_RETURN_IF_ERROR(node.Accept(&visitor));
  return visitor.contains_with_scan();
}

bool HasNonTerminalInsertScan(const ResolvedNode* node) {
  if (node == nullptr) {
    return false;
  }
  std::vector<const ResolvedNode*> insert_scans;
  node->GetDescendantsSatisfying(
      &ResolvedNode::Is<googlesql::ResolvedInsertScan>, &insert_scans);
  if (insert_scans.empty()) {
    return false;
  }
  std::vector<const ResolvedNode*> finish_scans;
  node->GetDescendantsSatisfying(&ResolvedNode::Is<ResolvedFinishScan>,
                                 &finish_scans);

  // Terminal insert scans are insert scans directly followed by a finish scan.
  absl::flat_hash_set<const ResolvedInsertScan*> terminal_insert_scans;
  for (const ResolvedNode* finish_scan_node : finish_scans) {
    const auto* finish_scan = finish_scan_node->GetAs<ResolvedFinishScan>();
    if (finish_scan->input_scan() != nullptr &&
        finish_scan->input_scan()->Is<ResolvedInsertScan>()) {
      terminal_insert_scans.insert(
          finish_scan->input_scan()->GetAs<ResolvedInsertScan>());
    }
  }

  // If any insert scan is not directly followed by a finish scan, then this
  // plan contains non-terminal insert scan.
  return (absl::c_any_of(
      insert_scans,
      [&terminal_insert_scans](const ResolvedNode* insert_scan_node) {
        return !terminal_insert_scans.contains(
            insert_scan_node->GetAs<ResolvedInsertScan>());
      }));
}

absl::Status FunctionCallBuilder::GetBuiltinFunctionFromCatalog(
    absl::string_view function_name, const Function** fn_out) {
  GOOGLESQL_RET_CHECK_NE(fn_out, nullptr);
  GOOGLESQL_RET_CHECK_EQ(*fn_out, nullptr);
  return GetBuiltinFunctionFromCatalog(
      std::vector<std::string>{std::string(function_name)}, fn_out);
}

absl::Status FunctionCallBuilder::GetBuiltinFunctionFromCatalog(
    absl::Span<const std::string> function_path, const Function** fn_out) {
  GOOGLESQL_RET_CHECK_NE(fn_out, nullptr);
  GOOGLESQL_RET_CHECK_EQ(*fn_out, nullptr);
  GOOGLESQL_RETURN_IF_ERROR(catalog_.FindFunction(function_path, fn_out,
                                        analyzer_options_.find_options()));
  if (fn_out == nullptr || *fn_out == nullptr ||
      !(*fn_out)->IsGoogleSQLBuiltin()) {
    return absl::NotFoundError(
        absl::Substitute("Required built-in function \"$0\" not available.",
                         absl::StrJoin(function_path, ".")));
  }
  return absl::OkStatus();
}

bool IsBuiltInFunctionIdEq(const ResolvedFunctionCallBase* const function_call,
                           FunctionSignatureId function_signature_id) {
  ABSL_DCHECK(function_call->function() != nullptr)
      << "Expected function_call->function() to not be null";
  return function_call->function() != nullptr &&
         function_call->signature().context_id() == function_signature_id &&
         function_call->function()->IsGoogleSQLBuiltin();
}

googlesql_base::StatusBuilder MakeUnimplementedErrorAtNode(const ResolvedNode* node) {
  googlesql_base::StatusBuilder builder = googlesql_base::UnimplementedErrorBuilder();
  if (node != nullptr && node->GetParseLocationOrNULL() != nullptr) {
    builder.AttachPayload(
        node->GetParseLocationOrNULL()->start().ToInternalErrorLocation());
  }
  return builder;
}

// Visitor that collects correlated columns.
class CorrelatedColumnRefCollector : public ResolvedASTVisitor {
 public:
  const absl::flat_hash_set<ResolvedColumn>& GetCorrelatedColumns() const {
    return correlated_columns_;
  }

 private:
  absl::Status VisitResolvedColumnRef(const ResolvedColumnRef* ref) override {
    const ResolvedColumn& col = ref->column();
    // Only collect the external columns when they are correlated within the
    // visited node. We ignore the internal columns who also appear in a
    // correlated reference because it is used in a nested subquery.
    //
    // For example, for the following query and the visited node.
    // select (
    //   select (                             <= Visited node
    //       select
    //       from InnerTable
    //       where InnerTable.col = Table.col and
    //             InnerTable.col = OuterTable.col
    //       limit 1
    //   ) from Table
    // ) from OuterTable
    //
    //  Here for the visited node, OuterTable.col is returned as a correlated
    //  column; however Table.col will NOT be returned because Table.col is
    //  only correlated in the inner subquery of the visited node.
    if (ref->is_correlated()) {
      if (!uncorrelated_column_ids_.contains(col.column_id())) {
        correlated_columns_.insert(col);
      }
    } else {
      correlated_columns_.erase(col);
      uncorrelated_column_ids_.insert(col.column_id());
    }
    return absl::OkStatus();
  }

  absl::flat_hash_set<ResolvedColumn> correlated_columns_;
  absl::flat_hash_set<int> uncorrelated_column_ids_;
};

absl::StatusOr<absl::flat_hash_set<ResolvedColumn>> GetCorrelatedColumnSet(
    const ResolvedNode& node) {
  absl::flat_hash_set<ResolvedColumn> column_set;
  CorrelatedColumnRefCollector visitor;
  GOOGLESQL_RETURN_IF_ERROR(node.Accept(&visitor));
  for (const ResolvedColumn& column : visitor.GetCorrelatedColumns()) {
    column_set.insert(column);
  }
  return column_set;
}

absl::Status ValidateArgumentsDoNotContainCorrelation(
    const ResolvedTVFScan* tvf_node, absl::string_view function_name,
    absl::Span<const ResolvedExpr* /*absl_nonnull*/ const> expressions) {
  for (const ResolvedExpr* expr : expressions) {
    GOOGLESQL_RET_CHECK(expr != nullptr)
        << "Expression in ValidateArgumentsDoNotContainCorrelation should not "
           "be null";

    GOOGLESQL_ASSIGN_OR_RETURN(absl::flat_hash_set<ResolvedColumn> correlated_columns,
                     GetCorrelatedColumnSet(*expr));
    if (!correlated_columns.empty()) {
      return MakeUnimplementedErrorAtNode(tvf_node)
             << function_name
             << " arguments must not contain correlated references to columns "
                "defined outside the scope of the argument expression itself";
    }
  }
  return absl::OkStatus();
}

absl::Status ValidateTvfIsBuiltinAndSignatureMatches(
    const ResolvedTVFScan* node, absl::string_view function_name,
    int64_t function_context_id) {
  GOOGLESQL_RET_CHECK(node != nullptr);
  GOOGLESQL_RET_CHECK(node->tvf() != nullptr) << "node's TVF is null";
  GOOGLESQL_RET_CHECK(node->tvf()->IsGoogleSQLBuiltin())
      << "TVF is not a GoogleSQL builtin: " << node->tvf()->DebugString();

  for (const auto& signature : node->tvf()->signatures()) {
    if (signature.context_id() == function_context_id) {
      return absl::OkStatus();
    }
  }

  GOOGLESQL_RET_CHECK_FAIL() << "TVF signature for " << function_name
                   << " is not supported by node: "
                   << node->tvf()->DebugString();
}

absl::StatusOr<const ResolvedColumn*> FindAndValidateTimestampColumnInScan(
    const ResolvedNode& error_location, absl::string_view ts_col_name,
    const ResolvedScan& input_scan, absl::string_view function_name,
    ProductMode product_mode) {
  const ResolvedColumn* ts_column = nullptr;
  for (const auto& col : input_scan.column_list()) {
    if (googlesql_base::CaseEqual(col.name(), ts_col_name)) {
      if (ts_column != nullptr) {
        return MakeSqlErrorAtStart(error_location.GetParseLocationRangeOrNULL())
               << "timestamp_column '" << ts_col_name
               << "' is ambiguous in the input table";
      } else {
        ts_column = &col;
      }
    }
  }

  if (ts_column == nullptr) {
    return MakeSqlErrorAtStart(error_location.GetParseLocationRangeOrNULL())
           << "Timestamp column '" << ts_col_name
           << "' not found in the input table for " << function_name;
  }
  if (!ts_column->type()->IsTimestamp()) {
    return MakeSqlErrorAtStart(error_location.GetParseLocationRangeOrNULL())
           << "The timestamp column provided to " << function_name
           << " must be of type TIMESTAMP, but column '" << ts_col_name
           << "' has type " << ts_column->type()->ShortTypeName(product_mode);
  }
  return ts_column;
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
BuildTimestampColumnExpression(const ResolvedTimestampColumnPath& path,
                               const ResolvedColumn& root_column,
                               FunctionCallBuilder& fn_builder) {
  std::unique_ptr<const ResolvedExpr> current_expr =
      MakeResolvedColumnRef(root_column.type(), root_column,
                            /*is_correlated=*/false);

  for (const auto& step : path.steps) {
    if (step.kind == TypeFieldPathStep::STRUCT_FIELD) {
      current_expr = MakeResolvedGetStructField(
          step.type, std::move(current_expr), step.struct_field_index);
    } else if (step.kind == TypeFieldPathStep::PROTO_FIELD) {
      const Type* step_type = nullptr;
      GOOGLESQL_RETURN_IF_ERROR(fn_builder.type_factory().GetProtoFieldType(
          step.proto_field_descriptor,
          /*catalog_name_path=*/absl::Span<const std::string>(), &step_type));
      current_expr = MakeResolvedGetProtoField(
          step_type, std::move(current_expr), step.proto_field_descriptor,
          /*default_value=*/Value::Null(step_type),
          /*get_has_bit=*/false,
          /*format=*/
          ProtoType::GetFormatAnnotation(step.proto_field_descriptor),
          /*return_default_value_when_unset=*/false);
    } else {
      return absl::InternalError("Unsupported TypeFieldPathStep kind");
    }
  }

  // We already validated in ResolveTimestampColumnPath() that if this is a
  // proto, it is specifically `google.protobuf.Timestamp`.
  if (current_expr->type()->IsProto()) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        auto from_proto_expr,
        fn_builder.FromProto(std::move(current_expr), FN_FROM_PROTO_TIMESTAMP,
                             googlesql::types::TimestampType()));
    current_expr = std::move(from_proto_expr);
  }

  return current_expr;
}

std::unique_ptr<ResolvedColumnRef> BuildResolvedColumnRef(
    const ResolvedColumn& column) {
  return MakeResolvedColumnRef(column, /*is_correlated=*/false);
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::Extract(std::unique_ptr<const ResolvedExpr> interval_expr,
                             functions::DateTimestampPart part) {
  GOOGLESQL_RET_CHECK(interval_expr != nullptr);
  GOOGLESQL_RET_CHECK(interval_expr->type()->IsInterval())
      << "Expected INTERVAL type for interval_expr, but got "
      << interval_expr->type()->DebugString();

  const Function* extract_func = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("$extract", &extract_func));

  // Create a ResolvedLiteral for the DateTimestampPart enum.
  const EnumType* date_part_enum_type = types::DatePartEnumType();
  auto part_literal = MakeResolvedLiteral(
      date_part_enum_type, Value::Enum(date_part_enum_type, part));

  std::vector<std::unique_ptr<const ResolvedExpr>> arguments;
  arguments.reserve(2);
  arguments.push_back(std::move(interval_expr));
  arguments.push_back(std::move(part_literal));

  const Type* result_type = types::Int64Type();
  std::vector<ConcreteArgument> concrete_args = {{arguments[0]->type(), 1},
                                                 {arguments[1]->type(), 1}};
  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature concrete_signature,
      MakeConcreteSignature(extract_func, FN_EXTRACT_FROM_INTERVAL, result_type,
                            std::move(concrete_args)));
  GOOGLESQL_ASSIGN_OR_RETURN(auto call,
                   ResolvedFunctionCallBuilder()
                       .set_type(result_type)
                       .set_function(extract_func)
                       .set_signature(concrete_signature)
                       .set_argument_list(std::move(arguments))
                       .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
                       .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::TimestampBucket(
    std::vector<std::unique_ptr<const ResolvedExpr>> arguments) {
  GOOGLESQL_RET_CHECK(arguments.size() == 2 || arguments.size() == 3);
  GOOGLESQL_RET_CHECK(arguments[0] != nullptr);
  GOOGLESQL_RET_CHECK(arguments[0]->type()->IsTimestamp())
      << "Expected TIMESTAMP type for first argument to TIMESTAMP_BUCKET, but "
         "got "
      << arguments[0]->type()->DebugString();
  GOOGLESQL_RET_CHECK(arguments[1] != nullptr);
  GOOGLESQL_RET_CHECK(arguments[1]->type()->IsInterval())
      << "Expected INTERVAL type for second argument to TIMESTAMP_BUCKET, but "
         "got "
      << arguments[1]->type()->DebugString();
  if (arguments.size() == 3) {
    GOOGLESQL_RET_CHECK(arguments[2] != nullptr);
    GOOGLESQL_RET_CHECK(arguments[2]->type()->IsTimestamp())
        << "Expected TIMESTAMP type for third argument (origin) to "
           "TIMESTAMP_BUCKET, but got "
        << arguments[2]->type()->DebugString();
  }

  const Function* timestamp_bucket_func = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("timestamp_bucket",
                                                &timestamp_bucket_func));

  const Type* result_type = types::TimestampType();
  std::vector<ConcreteArgument> concrete_args;
  concrete_args.reserve(arguments.size());
  for (const auto& arg : arguments) {
    concrete_args.push_back({arg->type(), 1});
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature concrete_signature,
      MakeConcreteSignature(timestamp_bucket_func, FN_TIMESTAMP_BUCKET,
                            result_type, std::move(concrete_args)));

  GOOGLESQL_ASSIGN_OR_RETURN(auto call,
                   ResolvedFunctionCallBuilder()
                       .set_type(result_type)
                       .set_function(timestamp_bucket_func)
                       .set_signature(concrete_signature)
                       .set_argument_list(std::move(arguments))
                       .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
                       .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
FunctionCallBuilder::IntervalToNanos(
    std::unique_ptr<const ResolvedExpr> interval_expr,
    absl::string_view function_name) {
  GOOGLESQL_RET_CHECK(interval_expr != nullptr);
  GOOGLESQL_RET_CHECK(interval_expr->type()->IsInterval());

  // This function builds the equivalent of the following pseudo-SQL:
  // IF (
  //   EXTRACT(MONTH FROM interval_expr) != 0 OR
  //     EXTRACT(YEAR FROM interval_expr) != 0,
  //   ERROR("..."),
  //   EXTRACT(DAY FROM interval_expr) * kNanosInDay + ... +
  //     EXTRACT(NANOSECOND FROM interval_expr)
  // )
  //
  // Restrict input types to prevent AST bloat and duplicate evaluation.
  // Because this function extracts 7 different date parts from the input
  // (5 for addition and 2 for validation), it must deep-copy `interval_expr`
  // 7 times. If `interval_expr` were a complex expression, deep-copying it
  // would break run-once semantics and cause the engine to evaluate the same
  // potentially non-deterministic expression multiple times.
  GOOGLESQL_RET_CHECK(interval_expr->Is<ResolvedColumnRef>() ||
            interval_expr->Is<ResolvedLiteral>())
      << "IntervalToNanos caller must extract the interval into a CTE or "
         "similar to satisfy run-once semantics before passing it here, as "
         "this function duplicates the AST node multiple times. Called for "
      << function_name << ".";

  struct PartConfig {
    functions::DateTimestampPart part;
    int64_t multiplier;
  };

  // Since EXTRACT(NANOSECOND) returns the entire sub-second fraction
  // (0-999999999), we omit MILLISECOND and MICROSECOND to avoid
  // double-counting.
  // Note: This calculation can overflow if the interval is very large
  // (e.g., more than ~292 years in days).
  std::vector<PartConfig> parts = {
      {functions::DAY, static_cast<int64_t>(IntervalValue::kNanosInDay)},
      {functions::HOUR, static_cast<int64_t>(IntervalValue::kNanosInHour)},
      {functions::MINUTE, static_cast<int64_t>(IntervalValue::kNanosInMinute)},
      {functions::SECOND, static_cast<int64_t>(IntervalValue::kNanosInSecond)},
      {functions::NANOSECOND, 1LL}};
  std::vector<std::unique_ptr<const ResolvedExpr>> parts_exprs;
  for (const auto& part_config : parts) {
    GOOGLESQL_ASSIGN_OR_RETURN(auto interval_expr_copy,
                     ResolvedASTDeepCopyVisitor::Copy(interval_expr.get()));

    GOOGLESQL_ASSIGN_OR_RETURN(auto extracted_val,
                     Extract(std::move(interval_expr_copy), part_config.part));

    std::unique_ptr<const ResolvedExpr> part_in_nanos;
    if (part_config.multiplier == 1) {
      part_in_nanos = std::move(extracted_val);
    } else {
      GOOGLESQL_ASSIGN_OR_RETURN(
          part_in_nanos,
          NestedBinaryOp(
              "$multiply", FN_MULTIPLY_INT64,
              MakeNodeVector(
                  std::move(extracted_val),
                  MakeResolvedLiteral(types::Int64Type(),
                                      Value::Int64(part_config.multiplier))),
              types::Int64Type()));
    }
    parts_exprs.push_back(std::move(part_in_nanos));
  }

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> total_nanos,
                   NestedBinaryAdd(types::Int64Type(), std::move(parts_exprs)));

  // Add a runtime check to ERROR() if there is a MONTH or YEAR part in the
  // interval.
  GOOGLESQL_ASSIGN_OR_RETURN(auto interval_expr_copy_for_month,
                   ResolvedASTDeepCopyVisitor::Copy(interval_expr.get()));
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto extracted_month,
      Extract(std::move(interval_expr_copy_for_month), functions::MONTH));
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto month_not_zero,
      NotEqual(std::move(extracted_month),
               MakeResolvedLiteral(types::Int64Type(), Value::Int64(0))));

  GOOGLESQL_ASSIGN_OR_RETURN(auto interval_expr_copy_for_year,
                   ResolvedASTDeepCopyVisitor::Copy(interval_expr.get()));
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto extracted_year,
      Extract(std::move(interval_expr_copy_for_year), functions::YEAR));
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto year_not_zero,
      NotEqual(std::move(extracted_year),
               MakeResolvedLiteral(types::Int64Type(), Value::Int64(0))));

  std::vector<std::unique_ptr<const ResolvedExpr>> check_parts_exprs;
  check_parts_exprs.push_back(std::move(month_not_zero));
  check_parts_exprs.push_back(std::move(year_not_zero));
  GOOGLESQL_ASSIGN_OR_RETURN(auto has_unsupported_parts,
                   Or(std::move(check_parts_exprs)));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto error_call,
      Error(absl::StrCat(
                "Intervals with MONTH or YEAR parts are not supported in ",
                function_name, "."),
            types::Int64Type()));

  return If(std::move(has_unsupported_parts), std::move(error_call),
            std::move(total_nanos));
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::GenerateTimestampArrayWithDatePart(
    std::unique_ptr<const ResolvedExpr> start_timestamp,
    std::unique_ptr<const ResolvedExpr> end_timestamp,
    std::unique_ptr<const ResolvedExpr> step,
    std::unique_ptr<const ResolvedExpr> date_part) {
  GOOGLESQL_RET_CHECK(start_timestamp != nullptr);
  GOOGLESQL_RET_CHECK(start_timestamp->type()->IsTimestamp())
      << "Expected TIMESTAMP type for start_timestamp, but got "
      << start_timestamp->type()->DebugString();
  GOOGLESQL_RET_CHECK(end_timestamp != nullptr);
  GOOGLESQL_RET_CHECK(end_timestamp->type()->IsTimestamp())
      << "Expected TIMESTAMP type for end_timestamp, but got "
      << end_timestamp->type()->DebugString();
  GOOGLESQL_RET_CHECK(step != nullptr);
  GOOGLESQL_RET_CHECK(step->type()->IsInt64()) << "Expected INT64 type for step, but got "
                                     << step->type()->DebugString();

  GOOGLESQL_RET_CHECK(date_part != nullptr);
  GOOGLESQL_RET_CHECK(date_part->type()->Equivalent(types::DatePartEnumType()))
      << "Expected ENUM<googlesql.functions.DateTimestampPart> type for "
         "date_part, but got "
      << date_part->type()->DebugString();

  const Function* generate_timestamp_array_func = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog(
      "generate_timestamp_array", &generate_timestamp_array_func));

  std::vector<std::unique_ptr<const ResolvedExpr>> arguments;
  arguments.reserve(4);
  arguments.push_back(std::move(start_timestamp));
  arguments.push_back(std::move(end_timestamp));
  arguments.push_back(std::move(step));
  arguments.push_back(std::move(date_part));

  const Type* result_type = types::TimestampArrayType();

  std::vector<ConcreteArgument> concrete_args;
  concrete_args.reserve(arguments.size());
  for (const auto& arg : arguments) {
    concrete_args.push_back({arg->type(), 1});
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature concrete_signature,
      MakeConcreteSignature(generate_timestamp_array_func,
                            FN_GENERATE_TIMESTAMP_ARRAY, result_type,
                            std::move(concrete_args)));

  GOOGLESQL_ASSIGN_OR_RETURN(auto call,
                   ResolvedFunctionCallBuilder()
                       .set_type(result_type)
                       .set_function(generate_timestamp_array_func)
                       .set_signature(concrete_signature)
                       .set_argument_list(std::move(arguments))
                       .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
                       .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

namespace {
absl::StatusOr<FunctionSignature>
ExtractForDpApproxCountDistinctFunctionSignatureForReportType(
    const Function& function,
    std::optional<functions::DifferentialPrivacyEnums::ReportFormat>
        report_format) {
  // Find function signature with the correct return type.
  auto it = std::find_if(
      function.signatures().begin(), function.signatures().end(),
      [report_format](const FunctionSignature& signature) {
        if (!report_format.has_value()) {
          return signature.context_id() ==
                 FN_DIFFERENTIAL_PRIVACY_EXTRACT_FOR_DP_APPROX_COUNT_DISTINCT;
        }
        switch (*report_format) {
          case functions::DifferentialPrivacyEnums::PROTO:
            return signature.context_id() ==
                   FN_DIFFERENTIAL_PRIVACY_EXTRACT_FOR_DP_APPROX_COUNT_DISTINCT_REPORT_PROTO;  // NOLINT
          case functions::DifferentialPrivacyEnums::JSON:
            return signature.context_id() ==
                   FN_DIFFERENTIAL_PRIVACY_EXTRACT_FOR_DP_APPROX_COUNT_DISTINCT_REPORT_JSON;  // NOLINT
          default:
            return false;
        }
      });
  // Unknown report format.
  GOOGLESQL_RET_CHECK(it != function.signatures().end());
  return *it;
}
}  // namespace

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::ExtractForDpApproxCountDistinct(
    std::unique_ptr<const ResolvedColumnRef> partital_merge_result,
    std::unique_ptr<const ResolvedExpr> noisy_count_distinct_privacy_ids_expr,
    std::optional<functions::DifferentialPrivacyEnums::ReportFormat>
        report_format) {
  GOOGLESQL_RET_CHECK_NE(partital_merge_result.get(), nullptr);
  GOOGLESQL_RET_CHECK_NE(noisy_count_distinct_privacy_ids_expr.get(), nullptr);

  const Function* extract_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog(
      "$differential_privacy_extract_for_dp_approx_count_distinct",
      &extract_fn));

  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature catalog_signature,
      ExtractForDpApproxCountDistinctFunctionSignatureForReportType(
          *extract_fn, report_format));

  std::vector<std::unique_ptr<const ResolvedExpr>> arguments;
  arguments.emplace_back(std::move(partital_merge_result));
  arguments.emplace_back(std::move(noisy_count_distinct_privacy_ids_expr));

  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature concrete_signature,
      MakeConcreteSignature(catalog_signature,
                            catalog_signature.result_type().type(),
                            {{catalog_signature.argument(0).type(), 1},
                             {catalog_signature.argument(1).type(), 1}}));

  GOOGLESQL_RET_CHECK(concrete_signature.HasConcreteArguments());

  GOOGLESQL_RET_CHECK(concrete_signature.IsConcrete());

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto call,
      ResolvedFunctionCallBuilder()
          .set_type(concrete_signature.result_type().type())
          .set_function(extract_fn)
          .set_signature(std::move(concrete_signature))
          .set_argument_list(std::move(arguments))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedAnalyticFunctionCall>>
FunctionCallBuilder::IsFirstK(
    /*absl_nonnull*/ std::unique_ptr<const ResolvedExpr> arg_k) {
  GOOGLESQL_RET_CHECK(arg_k->type()->IsInt64());
  GOOGLESQL_RET_CHECK(arg_k->Is<ResolvedLiteral>() || arg_k->Is<ResolvedParameter>());
  const Function* analytic_function = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(
      GetBuiltinFunctionFromCatalog("is_first", &analytic_function));

  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.emplace_back(std::move(arg_k));
  GOOGLESQL_RET_CHECK_EQ(analytic_function->NumSignatures(), 1);
  const FunctionSignature* catalog_signature =
      GetSignature(analytic_function, FN_IS_FIRST);
  GOOGLESQL_RET_CHECK(catalog_signature != nullptr);

  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature concrete_signature,
      MakeConcreteSignature(*catalog_signature,
                            catalog_signature->result_type().type(),
                            {{catalog_signature->argument(0).type(), 1}}));

  auto call = MakeResolvedAnalyticFunctionCall(
      concrete_signature.result_type().type(), analytic_function,
      concrete_signature, std::move(args), {},
      ResolvedFunctionCallBase::DEFAULT_ERROR_MODE, false,
      ResolvedNonScalarFunctionCallBase::DEFAULT_NULL_HANDLING,
      /*where_expr=*/nullptr,
      /*window_frame=*/nullptr);
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::IsNotDistinctFrom(
    std::unique_ptr<const ResolvedExpr> left_expr,
    std::unique_ptr<const ResolvedExpr> right_expr) {
  GOOGLESQL_RET_CHECK_NE(left_expr, nullptr);
  GOOGLESQL_RET_CHECK_NE(right_expr, nullptr);
  GOOGLESQL_RET_CHECK(left_expr->type()->Equals(right_expr->type()))
      << "Inconsistent types of left_expr and right_expr: "
      << left_expr->type()->DebugString() << " vs "
      << right_expr->type()->DebugString();

  const Function* is_not_distinct_from_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog("$is_not_distinct_from",
                                                &is_not_distinct_from_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature concrete_signature,
      MakeConcreteSignature(is_not_distinct_from_fn, FN_NOT_DISTINCT,
                            types::BoolType(),
                            {{left_expr->type(), 1}, {right_expr->type(), 1}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> args;
  args.push_back(std::move(left_expr));
  args.push_back(std::move(right_expr));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto call,
      ResolvedFunctionCallBuilder()
          .set_type(types::BoolType())
          .set_function(is_not_distinct_from_fn)
          .set_signature(std::move(concrete_signature))
          .set_argument_list(std::move(args))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());

  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));

  return call;
}

static absl::StatusOr<enum googlesql::FunctionSignatureId>
GetHllInitFunctionSignatureId(const Type* input_type) {
  switch (input_type->kind()) {
    case TYPE_INT32:
      return FN_HLL_COUNT_INIT_INT64;
    case TYPE_INT64:
      return FN_HLL_COUNT_INIT_INT64;
    case TYPE_UINT64:
      return FN_HLL_COUNT_INIT_UINT64;
    case TYPE_STRING:
      return FN_HLL_COUNT_INIT_STRING;
    case TYPE_BYTES:
      return FN_HLL_COUNT_INIT_BYTES;
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unsupported input argument type for HLL_COUNT.INIT: "
                       << input_type->DebugString();
  }
}

absl::StatusOr<std::unique_ptr<const ResolvedAggregateFunctionCall>>
FunctionCallBuilder::HllInit(std::unique_ptr<const ResolvedExpr> expr) {
  GOOGLESQL_ASSIGN_OR_RETURN(enum googlesql::FunctionSignatureId hll_init_fn_sig_id,
                   GetHllInitFunctionSignatureId(expr->type()));
  const Function* hll_init_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog(
      std::vector<std::string>{"hll_count", "init"}, &hll_init_fn));
  const FunctionSignature* catalog_signature =
      GetSignature(hll_init_fn, hll_init_fn_sig_id);
  GOOGLESQL_RET_CHECK(catalog_signature != nullptr);
  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature concrete_signature,
      MakeConcreteSignature(*catalog_signature,
                            catalog_signature->result_type().type(),
                            {{expr->type(), 1}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> hll_init_fn_args;
  hll_init_fn_args.push_back(std::move(expr));

  GOOGLESQL_ASSIGN_OR_RETURN(auto call,
                   ResolvedAggregateFunctionCallBuilder()
                       .set_type(types::BytesType())
                       .set_function(hll_init_fn)
                       .set_signature(std::move(concrete_signature))
                       .set_argument_list(std::move(hll_init_fn_args))
                       .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
                       .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedAggregateFunctionCall>>
FunctionCallBuilder::HllMergePartial(std::unique_ptr<const ResolvedExpr> expr) {
  const Function* hll_merge_fn = nullptr;
  std::vector<std::string> function_name = {"hll_count", "merge_partial"};
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog(function_name, &hll_merge_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature concrete_signature,
      MakeConcreteSignature(hll_merge_fn, FN_HLL_COUNT_MERGE_PARTIAL,
                            types::BytesType(), {{expr->type(), 1}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> hll_merge_fn_args;
  hll_merge_fn_args.push_back(std::move(expr));

  GOOGLESQL_ASSIGN_OR_RETURN(auto call,
                   ResolvedAggregateFunctionCallBuilder()
                       .set_type(types::BytesType())
                       .set_function(hll_merge_fn)
                       .set_signature(std::move(concrete_signature))
                       .set_argument_list(std::move(hll_merge_fn_args))
                       .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
                       .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedAggregateFunctionCall>>
FunctionCallBuilder::HllMerge(std::unique_ptr<const ResolvedExpr> expr) {
  const Function* hll_merge_fn = nullptr;
  std::vector<std::string> function_name = {"hll_count", "merge"};
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog(function_name, &hll_merge_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature concrete_signature,
      MakeConcreteSignature(hll_merge_fn, FN_HLL_COUNT_MERGE,
                            types::Int64Type(), {{expr->type(), 1}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> hll_merge_fn_args;
  hll_merge_fn_args.push_back(std::move(expr));

  GOOGLESQL_ASSIGN_OR_RETURN(auto call,
                   ResolvedAggregateFunctionCallBuilder()
                       .set_type(types::Int64Type())
                       .set_function(hll_merge_fn)
                       .set_signature(std::move(concrete_signature))
                       .set_argument_list(std::move(hll_merge_fn_args))
                       .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
                       .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
FunctionCallBuilder::HllExtract(std::unique_ptr<const ResolvedExpr> expr) {
  const Function* hll_extract_fn = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(GetBuiltinFunctionFromCatalog(
      std::vector<std::string>{"hll_count", "extract"}, &hll_extract_fn));
  GOOGLESQL_ASSIGN_OR_RETURN(
      FunctionSignature concrete_signature,
      MakeConcreteSignature(hll_extract_fn, FN_HLL_COUNT_EXTRACT,
                            types::Int64Type(), {{expr->type(), 1}}));

  std::vector<std::unique_ptr<const ResolvedExpr>> hll_extract_fn_args;
  hll_extract_fn_args.push_back(std::move(expr));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto call,
      ResolvedFunctionCallBuilder()
          .set_type(types::Int64Type())
          .set_function(hll_extract_fn)
          .set_signature(std::move(concrete_signature))
          .set_argument_list(std::move(hll_extract_fn_args))
          .set_error_mode(ResolvedFunctionCall::DEFAULT_ERROR_MODE)
          .set_function_call_info(std::make_shared<ResolvedFunctionCallInfo>())
          .BuildMutable());
  GOOGLESQL_RETURN_IF_ERROR(PropagateAnnotationsAndProcessCollationList(call.get()));
  return call;
}

// Visitor to detect the max column id in a ResolvedAST.
class MaxColumnIdVisitor : public googlesql::ResolvedASTVisitor {
 public:
  static absl::StatusOr<int> GetMaxColumnId(
      const googlesql::ResolvedNode* node) {
    MaxColumnIdVisitor visitor;
    GOOGLESQL_RETURN_IF_ERROR(node->Accept(&visitor));
    return visitor.get_max_column_id();
  }

  absl::Status VisitResolvedColumn(
      const googlesql::ResolvedColumn& column) override {
    if (column.column_id() > max_column_id_) {
      max_column_id_ = std::max(max_column_id_, column.column_id());
    }
    return absl::OkStatus();
  }

  int get_max_column_id() { return max_column_id_; }

 private:
  MaxColumnIdVisitor() = default;
  int max_column_id_ = 0;
};

absl::StatusOr<int> GetMaxColumnId(const googlesql::ResolvedNode* node) {
  return MaxColumnIdVisitor::GetMaxColumnId(node);
}

absl::Status CteQueryNameGenerator::VisitResolvedWithEntry(
    const ResolvedWithEntry* node) {
  used_names_.insert(absl::AsciiStrToLower(node->with_query_name()));
  return ResolvedASTVisitor::VisitResolvedWithEntry(node);
}

std::string CteQueryNameGenerator::MakeUniqueCteName(
    absl::string_view base_name) {
  if (base_name.empty()) {
    base_name = "cte";
  }
  std::string lower_base = absl::AsciiStrToLower(base_name);
  if (used_names_.insert(lower_base).second) {
    return std::string(base_name);
  }

  std::string candidate;
  do {
    ++max_seen_index_;
    candidate = absl::StrCat(base_name, "_", max_seen_index_);
  } while (!used_names_.insert(absl::AsciiStrToLower(candidate)).second);

  return candidate;
}

// Visitor that remaps CTE names in a subtree. This is used while inlining
// lambda bodies to ensure that nested CTEs with the same name as the outer
// query's CTEs are remapped to unique names.
class CteRemapperVisitor : public ResolvedASTVisitor {
 public:
  // cte_counter tracks the index for remapping CTE names and is updated as we
  // remap CTE names.
  explicit CteRemapperVisitor(int& cte_counter) : cte_counter_(cte_counter) {}

 protected:
  // Sets up a new name for each CTE.
  absl::Status VisitResolvedWithScan(const ResolvedWithScan* node) override {
    GOOGLESQL_RET_CHECK(!node->recursive())
        << "Recursive CTEs inside inlined SQL functions are not supported: "
        << node->DebugString();

    size_t old_size = active_ctes_.size();

    for (const auto& with_entry : node->with_entry_list()) {
      GOOGLESQL_RETURN_IF_ERROR(with_entry->with_subquery()->Accept(this));

      std::string old_cte_name = with_entry->with_query_name();
      std::string new_cte_name =
          absl::StrCat("$inlined_cte_", cte_counter_++, "_", old_cte_name);

      active_ctes_.push_back(std::make_pair(old_cte_name, new_cte_name));

      const_cast<ResolvedWithEntry*>(with_entry.get())
          ->set_with_query_name(new_cte_name);
    }

    GOOGLESQL_RETURN_IF_ERROR(node->query()->Accept(this));

    // Pop the CTEs that were added to the stack for this WithScan.
    active_ctes_.resize(old_size);

    return absl::OkStatus();
  }

  // Remaps the CTE references in the subquery to the new names.
  absl::Status VisitResolvedWithRefScan(
      const ResolvedWithRefScan* node) override {
    // Walk backwards through the active CTE names.
    for (auto it = active_ctes_.rbegin(); it != active_ctes_.rend(); ++it) {
      if (it->first == node->with_query_name()) {
        const_cast<ResolvedWithRefScan*>(node)->set_with_query_name(it->second);
        break;
      }
    }
    return DefaultVisit(node);
  }

 private:
  // Counter used for generating unique CTE names.
  int& cte_counter_;

  // Map from old name to new name for currently active CTEs.
  std::vector<std::pair<std::string, std::string>> active_ctes_;
};

absl::Status RemapCteNames(ResolvedNode* node, int& cte_counter) {
  CteRemapperVisitor visitor(cte_counter);
  return node->Accept(&visitor);
}

}  // namespace googlesql
