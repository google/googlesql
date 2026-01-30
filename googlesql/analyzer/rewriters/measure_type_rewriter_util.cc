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

#include "googlesql/analyzer/rewriters/measure_type_rewriter_util.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/measure_utils.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_ast_deep_copy_visitor.h"
#include "googlesql/resolved_ast/resolved_ast_rewrite_visitor.h"
#include "googlesql/resolved_ast/resolved_ast_visitor.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/rewrite_utils.h"
#include "googlesql/base/case.h"
#include "absl/algorithm/container.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

static constexpr int kReferencedColumnsFieldIndex = 0;
static constexpr int kKeyColumnsFieldIndex = 1;

////////////////////////////////////////////////////////////////////////
// Utility functions.
////////////////////////////////////////////////////////////////////////

bool IsMeasureAggFunction(const ResolvedExpr* expr) {
  if (!expr->Is<ResolvedAggregateFunctionCall>()) {
    return false;
  }
  const ResolvedAggregateFunctionCall* agg_fn =
      expr->GetAs<ResolvedAggregateFunctionCall>();
  const Function* function = agg_fn->function();
  return function->NumSignatures() == 1 &&
         function->signatures()[0].context_id() == FN_AGG &&
         function->IsGoogleSQLBuiltin();
}

absl::StatusOr<ResolvedColumn> GetInvokedMeasureColumn(
    const ResolvedAggregateFunctionCall* aggregate_fn) {
  GOOGLESQL_RET_CHECK(aggregate_fn != nullptr);
  GOOGLESQL_RET_CHECK(aggregate_fn->argument_list().size() == 1);
  const ResolvedExpr* arg = aggregate_fn->argument_list()[0].get();
  GOOGLESQL_RET_CHECK(arg->Is<ResolvedColumnRef>());
  return arg->GetAs<ResolvedColumnRef>()->column();
}

////////////////////////////////////////////////////////////////////////
// Logic to find unsupported query shapes for the measure type rewriter.
////////////////////////////////////////////////////////////////////////

// Returns an error if `input` contains a query shape that is unsupported by
// the measure type rewriter.
class UnsupportedQueryShapeFinder : public ResolvedASTVisitor {
 public:
  static absl::Status HasUnsupportedQueryShape(const ResolvedNode* input) {
    // First, gather information about measure columns that need to be expanded.
    UnsupportedQueryShapeFinder unsupport_query_shape_finder;
    return input->Accept(&unsupport_query_shape_finder);
  }

  // Find measure columns invoked via the `AGG` function and place them in
  // `invoked_measure_columns_`.
  absl::Status VisitResolvedSubqueryExpr(
      const ResolvedSubqueryExpr* node) override {
    if (node->subquery_type() == ResolvedSubqueryExpr::SCALAR) {
      GOOGLESQL_RET_CHECK(node->subquery()->column_list_size() == 1);
      if (node->subquery()->column_list(0).type()->IsMeasureType()) {
        return absl::UnimplementedError(
            "Measure type rewriter does not support scalar subqueries that "
            "emit measure columns");
      }
    }
    return DefaultVisit(node);
  }

  // TODO: b/350555383  - Support this shape in the future
  absl::Status VisitResolvedWithScan(const ResolvedWithScan* node) override {
    for (const std::unique_ptr<const ResolvedWithEntry>& with_entry :
         node->with_entry_list()) {
      absl::flat_hash_set<ResolvedColumn> projected_cols;
      if (absl::c_any_of(with_entry->with_subquery()->column_list(),
                         [&projected_cols](const ResolvedColumn& column) {
                           return !projected_cols.insert(column).second;
                         })) {
        return absl::UnimplementedError(
            "Measure type rewriter does not support WITH scans emitting "
            "duplicate measure columns");
      }
    }
    return DefaultVisit(node);
  }

  // TODO: b/350555383  - Support this shape in the future
  absl::Status VisitResolvedJoinScan(const ResolvedJoinScan* node) override {
    if (node->is_lateral() &&
        absl::c_any_of(node->column_list(), [](const ResolvedColumn& column) {
          return IsOrContainsMeasure(column.type());
        })) {
      return absl::UnimplementedError(
          "Measure type rewriter does not support LATERAL joins that emit "
          "measure columns");
    }
    return DefaultVisit(node);
  }

  // TODO: b/350555383  - Support this shape in the future
  absl::Status VisitResolvedAggregateFunctionCall(
      const ResolvedAggregateFunctionCall* node) override {
    if (IsMeasureAggFunction(node)) {
      GOOGLESQL_RET_CHECK(node->argument_list().size() == 1);
      const ResolvedExpr* arg = node->argument_list()[0].get();
      if (!arg->Is<ResolvedColumnRef>()) {
        // The measure rewriter currently assumes that the argument to the AGG
        // function invocation is a column ref. The MeasureColumnRewriter makes
        // this assumption as well, since it skips mapping measure typed columns
        // to closure columns if the measure typed column is rooted within an
        // AGG function call sub-tree. Removing this check will require relaxing
        // that assumption.
        return absl::UnimplementedError(
            "Measure type rewriter expects argument to AGG function to be a "
            "direct column reference");
      }
    }
    return DefaultVisit(node);
  }

  // TODO: b/350555383  - Support this shape in the future
  absl::Status VisitResolvedWithExpr(const ResolvedWithExpr* node) override {
    if (IsOrContainsMeasure(node->expr()->type())) {
      return absl::UnimplementedError(
          "Measure type rewriter does not support WITH expressions emitting a "
          "measure type");
    }
    return DefaultVisit(node);
  }

  // TODO: b/350555383  - Support this shape in the future
  absl::Status VisitResolvedMeasureGroup(
      const ResolvedMeasureGroup* node) override {
    for (const auto& computed_column : node->aggregate_list()) {
      if (IsMeasureAggFunction(computed_column->expr())) {
        return absl::UnimplementedError(
            "Measure type rewriter does not support aggregating measures in a "
            "MATCH_RECOGNIZE scan");
      }
    }
    return DefaultVisit(node);
  }

 private:
  UnsupportedQueryShapeFinder() = default;
  UnsupportedQueryShapeFinder(const UnsupportedQueryShapeFinder&) = delete;
  UnsupportedQueryShapeFinder& operator=(const UnsupportedQueryShapeFinder&) =
      delete;
};

absl::Status HasUnsupportedQueryShape(const ResolvedNode* input) {
  return UnsupportedQueryShapeFinder::HasUnsupportedQueryShape(input);
}

// Validates the struct field names in `key_columns_struct_type` are the same
// as those in `row_identity_column_names`.
static absl::Status CheckEqualRowIdentityColumnNames(
    const StructType* key_columns_struct_type,
    const absl::btree_set<std::string, googlesql_base::CaseLess>&
        row_identity_column_names) {
  absl::btree_set<std::string, googlesql_base::CaseLess>
      key_columns_field_names;
  for (const StructField& field : key_columns_struct_type->fields()) {
    GOOGLESQL_RET_CHECK(key_columns_field_names.insert(field.name).second);
  }
  GOOGLESQL_RET_CHECK(key_columns_field_names == row_identity_column_names);
  return absl::OkStatus();
}

// `MultiLevelAggregateRewriter` rewrites a measure expression to use
// multi-level aggregation to grain-lock and avoid overcounting. A measure
// expression is a scalar expression over one or more constituent aggregate
// functions (e.g. SUM(X) / SUM(Y) + (<scalar_subquery>)), and so the resulting
// rewritten expression has 2 components to it:
//
// 1. A list of constituent aggregate functions that are rewritten to use
//    multi-level aggregation to grain-lock. These aggregate functions need to
//    be computed by the AggregateScan.
//
// 2. A scalar expression over the constituent aggregate functions. This
//    expression needs to be computed by a ProjectScan over the AggregateScan.
//
// The scalar expression is rewritten to use column references to the
// constituent aggregate functions. The constituent aggregate functions are
// themselves rewritten to use multi-level aggregation to grain-lock and avoid
// overcounting.
class MultiLevelAggregateRewriter : public ResolvedASTRewriteVisitor {
 public:
  MultiLevelAggregateRewriter(
      const Function* any_value_fn, FunctionCallBuilder& function_call_builder,
      const LanguageOptions& language_options, ColumnFactory& column_factory,
      TypeFactory& type_factory, ResolvedColumn struct_column,
      const absl::btree_set<std::string, googlesql_base::CaseLess>&
          row_identity_column_names,
      bool struct_column_refs_are_correlated)
      : any_value_fn_(any_value_fn),
        function_call_builder_(function_call_builder),
        language_options_(language_options),
        column_factory_(column_factory),
        type_factory_(type_factory),
        struct_column_(struct_column),
        row_identity_column_names_(row_identity_column_names),
        struct_column_refs_are_correlated_(struct_column_refs_are_correlated) {
        };
  MultiLevelAggregateRewriter(const MultiLevelAggregateRewriter&) = delete;
  MultiLevelAggregateRewriter& operator=(const MultiLevelAggregateRewriter&) =
      delete;

  absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
  RewriteMultiLevelAggregate(std::unique_ptr<const ResolvedExpr> measure_expr) {
    constituent_aggregate_count_ = 0;
    constituent_aggregate_list_.clear();

    // Extract constituent aggregates from the measure expression and rewrite
    // the measure expression to reference the constituent aggregates.
    std::vector<std::unique_ptr<const ResolvedComputedColumnBase>>
        temp_constituent_aggregates;
    GOOGLESQL_ASSIGN_OR_RETURN(measure_expr,
                     ExtractTopLevelAggregates(std::move(measure_expr),
                                               temp_constituent_aggregates,
                                               column_factory_));

    // Rewrite the constituent aggregates.
    for (std::unique_ptr<const ResolvedComputedColumnBase>&
             constituent_aggregate : temp_constituent_aggregates) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::unique_ptr<const ResolvedNode> rewritten_constituent_aggregate,
          VisitAll(std::move(constituent_aggregate)));
      GOOGLESQL_RET_CHECK(
          rewritten_constituent_aggregate->Is<ResolvedComputedColumnBase>());
      constituent_aggregate_list_.push_back(
          absl::WrapUnique(rewritten_constituent_aggregate.release()
                               ->GetAs<ResolvedComputedColumnBase>()));
    }
    // Return the measure expression.
    return measure_expr;
  }

  std::vector<std::unique_ptr<const ResolvedComputedColumnBase>>
  release_constituent_aggregate_list() {
    return std::move(constituent_aggregate_list_);
  }

 protected:
  absl::Status PreVisitResolvedAggregateFunctionCall(
      const googlesql::ResolvedAggregateFunctionCall&) override {
    aggregate_function_depth_++;
    return absl::OkStatus();
  }

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedAggregateFunctionCall(
      std::unique_ptr<const ResolvedAggregateFunctionCall> node) override {
    auto cleanup = absl::MakeCleanup([this] { aggregate_function_depth_--; });
    // If we are within a subquery, then we don't need to grain-lock the
    // aggregate function.
    if (subquery_depth_ > 0) {
      return node;
    }
    // Inject the WHERE modifier to discard NULL STRUCT values.
    GOOGLESQL_ASSIGN_OR_RETURN(node, MaybeInjectWhereModifier(std::move(node)));
    // Only perform the ANY_VALUE multi-level aggregation rewrite for aggregate
    // functions that have an empty `group_by_aggregate_list`.
    if (!node->group_by_aggregate_list().empty()) {
      return node;
    }
    // TODO: b/350555383 - How do we handle `generic_argument_list` ?
    if (!node->generic_argument_list().empty()) {
      return absl::UnimplementedError(
          "Measure type rewrite does not currently support generic arguments");
    }
    if (!node->group_by_list().empty()) {
      // `group_by_list` is not empty, but `group_by_aggregate_list` is empty.
      // This means that the aggregate function is a leaf node aggregate
      // function that only references grouping consts or correlated columns
      // (e.g. SUM(1 + e GROUP BY e)). We don't need to perform the ANY_VALUE
      // multi-level aggregation rewrite since the aggregate function is
      // guaranteed to see exactly 1 row per group.
      return node;
    }

    // If here, both `group_by_list` and `group_by_aggregate_list` are empty.
    // This is a plain aggregate function that needs to be rewritten to
    // grain-lock.
    ResolvedAggregateFunctionCallBuilder aggregate_function_call_builder =
        ToBuilder(std::move(node));

    // Step 1: Release the argument list, and wrap applicable arguments with an
    // ANY_VALUE aggregate function call. These aggregate function calls will be
    // placed in the `group_by_aggregate_list` of the rewritten aggregate
    // function call.
    std::vector<std::unique_ptr<const ResolvedExpr>> original_argument_list =
        aggregate_function_call_builder.release_argument_list();
    std::vector<std::unique_ptr<const ResolvedExpr>> rewritten_argument_list;
    std::vector<std::unique_ptr<const ResolvedComputedColumnBase>>
        group_by_aggregate_list;
    for (int i = 0; i < original_argument_list.size(); ++i) {
      std::unique_ptr<const ResolvedExpr> argument =
          std::move(original_argument_list[i]);
      // If the `argument` subtree does not contain a ResolvedColumn, then we
      // don't need to wrap it with an ANY_VALUE aggregate function call, and we
      // can simply add it to the rewritten argument list. This behavior is
      // necessary to correctly transform aggregate functions that expect some
      // of their arguments to be literals or parameters (e.g. STRING_AGG), or
      // aggregate functions with special arguments (e.g. BIT_XOR with
      // BitwiseAggMode ENUM).
      GOOGLESQL_ASSIGN_OR_RETURN(const bool arg_contains_resolved_column,
                       ContainsResolvedColumn(argument.get()));
      if (!arg_contains_resolved_column) {
        rewritten_argument_list.push_back(std::move(argument));
        continue;
      }
      const Type* argument_type = argument->type();
      std::vector<std::unique_ptr<const ResolvedExpr>> any_value_argument_list;
      any_value_argument_list.push_back(std::move(argument));
      FunctionSignature any_value_signature({argument_type, 1},
                                            {{argument_type, 1}}, FN_ANY_VALUE);
      auto resolved_any_value_aggregate_function_call =
          MakeResolvedAggregateFunctionCall(
              argument_type, any_value_fn_, any_value_signature,
              std::move(any_value_argument_list), /*generic_argument_list=*/{},
              aggregate_function_call_builder.error_mode(), /*distinct=*/false,
              ResolvedNonScalarFunctionCallBase::DEFAULT_NULL_HANDLING,
              /*where_expr=*/nullptr, /*having_modifier=*/nullptr,
              /*order_by_item_list=*/{}, /*limit=*/nullptr,
              /*function_call_info=*/nullptr, /*group_by_list=*/{},
              /*group_by_aggregate_list=*/{}, /*having_expr=*/nullptr);
      ResolvedColumn any_value_column = column_factory_.MakeCol(
          "$aggregate", absl::StrCat("$any_value_grain_lock_", i),
          argument_type);
      group_by_aggregate_list.push_back(MakeResolvedComputedColumn(
          any_value_column,
          std::move(resolved_any_value_aggregate_function_call)));
      rewritten_argument_list.push_back(MakeResolvedColumnRef(
          any_value_column.type(), any_value_column, /*is_correlated=*/false));
    }

    // Step 2: Compute the `group_by_list`. This should be based on
    // `GetStructField` accessing the `kKeyColumnsFieldIndex` field of the
    // `struct_column_`.
    std::vector<std::unique_ptr<const ResolvedComputedColumn>> group_by_list;
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> grain_lock_key_expr,
                     CreateGrainLockingKey());
    const Type* grain_lock_key_type = grain_lock_key_expr->type();
    group_by_list.push_back(MakeResolvedComputedColumn(
        column_factory_.MakeCol("$groupbymod", "grain_lock_key",
                                grain_lock_key_type),
        std::move(grain_lock_key_expr)));

    // Step 3: Set the `group_by_aggregate_list`, `group_by_list` and
    // `argument_list` on the rewritten aggregate function call.
    aggregate_function_call_builder.set_argument_list(
        std::move(rewritten_argument_list));
    aggregate_function_call_builder.set_group_by_aggregate_list(
        std::move(group_by_aggregate_list));
    aggregate_function_call_builder.set_group_by_list(std::move(group_by_list));

    // Step 4: Push the rewritten aggregate function call into
    // `computed_aggregate_list_`, and return a column reference to it.
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedAggregateFunctionCall>
                         rewritten_aggregate_function,
                     std::move(aggregate_function_call_builder).Build());
    return rewritten_aggregate_function;
  }

  absl::Status PreVisitResolvedSubqueryExpr(
      const googlesql::ResolvedSubqueryExpr&) override {
    subquery_depth_++;
    return absl::OkStatus();
  }

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedSubqueryExpr(
      std::unique_ptr<const ResolvedSubqueryExpr> node) override {
    subquery_depth_--;
    return node;
  }

 private:
  // Returns an expression that will serve as the grain locking key.
  //
  // If all row identity columns of `struct_column_` are needed for grain
  // locking, e.g., when the measure column uses the source table's row identity
  // columns, this function returns a GetStructField expression that
  // accesses the `kKeyColumnsFieldIndex` field of `struct_column_`.
  //
  // If only a subset of row identity columns are needed, then this function
  // returns a MakeStruct expression that creates a new struct containing only
  // the needed row identity columns.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> CreateGrainLockingKey() {
    GOOGLESQL_RET_CHECK(struct_column_.type()->IsStruct());
    GOOGLESQL_RET_CHECK(struct_column_.type()->AsStruct()->num_fields() == 2);

    const StructField& key_columns_field =
        struct_column_.type()->AsStruct()->field(kKeyColumnsFieldIndex);

    const StructType* key_columns_struct_type =
        key_columns_field.type->AsStruct();

    std::unique_ptr<const ResolvedExpr> grain_lock_key_expr;
    GOOGLESQL_RET_CHECK_GE(key_columns_struct_type->num_fields(),
                 row_identity_column_names_.size());
    if (key_columns_struct_type->num_fields() ==
        row_identity_column_names_.size()) {
      // All available row identity columns are needed; use the "key_columns"
      // sub-struct directly.
      //
      // Correctness check: the field names and the row identity columns
      // must match.
      GOOGLESQL_DCHECK_OK(CheckEqualRowIdentityColumnNames(key_columns_struct_type,
                                                 row_identity_column_names_));
      grain_lock_key_expr = MakeResolvedGetStructField(
          key_columns_field.type, MakeStructColumnRef(), kKeyColumnsFieldIndex);
    } else {
      // This measure column only needs some of the row identity columns;
      std::vector<StructField> grain_lock_struct_fields;
      std::vector<std::unique_ptr<const ResolvedExpr>>
          grain_lock_struct_field_exprs;
      for (const std::string& field_name : row_identity_column_names_) {
        bool is_ambiguous = false;
        int field_idx = -1;
        const StructField* field = key_columns_struct_type->FindField(
            field_name, &is_ambiguous, &field_idx);
        GOOGLESQL_RET_CHECK(field != nullptr) << "Cannot find field " << field_name
                                    << " from the key_columns struct: "
                                    << key_columns_struct_type->DebugString();
        GOOGLESQL_RET_CHECK(!is_ambiguous)
            << field_name << " is ambiguous, key_columns struct: "
            << key_columns_struct_type->DebugString();

        grain_lock_struct_fields.push_back(
            StructField(field_name, field->type));
        grain_lock_struct_field_exprs.push_back(MakeResolvedGetStructField(
            field->type,
            MakeResolvedGetStructField(key_columns_field.type,
                                       MakeStructColumnRef(),
                                       kKeyColumnsFieldIndex),
            field_idx));
      }

      if (row_identity_column_names_.size() == 1) {
        // If there is only one row identity column name, we access it directly
        // via GetStructField.
        grain_lock_key_expr = std::move(grain_lock_struct_field_exprs[0]);
      } else {
        // Create a new struct out of those needed row identity columns and use
        // it as the grain locking key.
        const StructType* grain_lock_struct_type = nullptr;
        GOOGLESQL_RETURN_IF_ERROR(type_factory_.MakeStructTypeFromVector(
            grain_lock_struct_fields, &grain_lock_struct_type));
        grain_lock_key_expr = MakeResolvedMakeStruct(
            grain_lock_struct_type, std::move(grain_lock_struct_field_exprs));
      }
    }
    return grain_lock_key_expr;
  }

  // Modify the aggregate function call to inject a WHERE modifier to discard
  // NULL STRUCT values. NULL STRUCT values may be introduced if the measure
  // propagates past OUTER JOINs. NULL STRUCT values represent invalid captured
  // measure context / state and hence must be discarded.
  absl::StatusOr<std::unique_ptr<const ResolvedAggregateFunctionCall>>
  MaybeInjectWhereModifier(
      std::unique_ptr<const ResolvedAggregateFunctionCall> node) {
    // If `aggregate_function_depth_` == 1 && subquery_depth_ == 0, then we are
    // currently within a top-level aggregate function call, and a WHERE
    // modifier should be injected to discard NULL struct column values. Only
    // inject the WHERE modifier if the aggregate filtering is enabled.
    if (aggregate_function_depth_ == 1 && subquery_depth_ == 0 &&
        language_options_.LanguageFeatureEnabled(FEATURE_AGGREGATE_FILTERING)) {
      // Measure validator should have already verified that there is no
      // WHERE clause on the aggregate function call.
      GOOGLESQL_RET_CHECK(node->where_expr() == nullptr);
      ResolvedAggregateFunctionCallBuilder aggregate_function_call_builder =
          ToBuilder(std::move(node));
      GOOGLESQL_ASSIGN_OR_RETURN(auto struct_is_not_null,
                       function_call_builder_.IsNotNull(MakeStructColumnRef()));
      aggregate_function_call_builder.set_where_expr(
          std::move(struct_is_not_null));
      return std::move(aggregate_function_call_builder).Build();
    }
    return node;
  }

  std::unique_ptr<ResolvedColumnRef> MakeStructColumnRef() {
    return MakeResolvedColumnRef(
        struct_column_.type(), struct_column_,
        /*is_correlated=*/struct_column_refs_are_correlated_);
  }

  absl::StatusOr<const bool> ContainsResolvedColumn(const ResolvedExpr* expr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedNode> copied_arg,
                     ResolvedASTDeepCopyVisitor::Copy(expr));
    ContainsResolvedColumnVisitor contains_resolved_column_visitor;
    auto unused =
        contains_resolved_column_visitor.VisitAll(std::move(copied_arg));
    return contains_resolved_column_visitor.ContainsResolvedColumn();
  }

  // A pointer to the `ANY_VALUE` function in the catalog used for the rewrite.
  const Function* any_value_fn_ = nullptr;
  // Used to create new function calls for the rewrite.
  FunctionCallBuilder& function_call_builder_;
  // Used to determine if `FEATURE_AGGREGATE_FILTERING` is enabled.
  // If enabled, then the rewrite will inject a WHERE modifier to discard
  // NULL values for the special STRUCT-typed column.
  const LanguageOptions& language_options_;
  // Used to create new columns.
  ColumnFactory& column_factory_;

  // Used to create new types, e.g., in `CreateGrainLockingKey()` to create
  // struct types for the grain locking key if a measure column uses
  // column-level row identity columns.
  TypeFactory& type_factory_;

  // The special STRUCT-typed column that contains the grouping keys needed for
  // grain-locking.
  ResolvedColumn struct_column_;

  // Names of row identity columns for the measure being rewritten.
  //
  // These names match `Column::Name()` for consistency in the printed
  // resolved AST.
  const absl::btree_set<std::string, googlesql_base::CaseLess>&
      row_identity_column_names_;

  // A list of (rewritten) constituent aggregates that compose a measure
  // expression.
  std::vector<std::unique_ptr<const ResolvedComputedColumnBase>>
      constituent_aggregate_list_;
  // Used purely for naming constituent aggregate columns.
  uint64_t constituent_aggregate_count_ = 0;
  // If `subquery_depth_` > 0, then we are currently within a subquery and
  // any aggregate functions should not be rewritten to grain-lock.
  uint64_t subquery_depth_ = 0;
  // If `aggregate_function_depth_` == 1 && subquery_depth_ == 0, then we are
  // currently within a top-level aggregate function call, and a WHERE modifier
  // should be injected to discard NULL struct column values.
  uint64_t aggregate_function_depth_ = 0;
  // Indicates whether references to `struct_column_` are correlated. This
  // is true when the measure column is being invoked in a correlated context;
  // e.g. AGG(correlated_reference_to_measure_column).
  bool struct_column_refs_are_correlated_;
};

// `StructColumnReferenceRewriter` rewrites a measure expression to reference
// columns from the STRUCT-typed column used to replace the measure column.
class StructColumnReferenceRewriter : public ResolvedASTDeepCopyVisitor {
 public:
  static absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
  RewriteMeasureExpression(const ResolvedExpr* measure_expr,
                           ResolvedColumn struct_column,
                           bool struct_column_refs_are_correlated) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedExpr> measure_expr_copy,
                     ResolvedASTDeepCopyVisitor::Copy(measure_expr));
    StructColumnReferenceRewriter rewriter(struct_column,
                                           struct_column_refs_are_correlated);

    // Rewrite the measure expression to reference columns from
    // `struct_column`.
    GOOGLESQL_RETURN_IF_ERROR(measure_expr_copy->Accept(&rewriter));
    return rewriter.ConsumeRootNode<ResolvedExpr>();
  }

 protected:
  absl::Status VisitResolvedSubqueryExpr(
      const ResolvedSubqueryExpr* node) override {
    // First, process the `in_expr` field. The `in_expr` does not see the
    // parameter list, so we must process it first, before we push a new
    // CorrelatedParameterInfo onto the stack.
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> rewritten_in_expr,
                     ProcessNode(node->in_expr()));

    // Now, push a new CorrelatedParameterInfo onto the stack and process the
    // `subquery` field.
    correlated_parameter_info_list_.push_back(CorrelatedParameterInfo());
    auto cleanup_correlated_parameter_info = absl::MakeCleanup(
        [this] { correlated_parameter_info_list_.pop_back(); });
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedScan> rewritten_subquery_scan,
        ProcessNode(node->subquery()));

    // Make a copy of the subquery expr, and set the `subquery` and `in_expr`
    // fields.
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedSubqueryExpr> copied_subquery_expr,
        ResolvedASTDeepCopyVisitor::Copy(node));
    ResolvedSubqueryExprBuilder subquery_expr_builder =
        ToBuilder(std::move(copied_subquery_expr));
    subquery_expr_builder.set_subquery(std::move(rewritten_subquery_scan));
    subquery_expr_builder.set_in_expr(std::move(rewritten_in_expr));
    GOOGLESQL_RET_CHECK(!correlated_parameter_info_list_.empty());
    if (correlated_parameter_info_list_.back()
            .add_struct_column_to_parameter_list) {
      std::unique_ptr<ResolvedColumnRef> struct_column_ref =
          MakeResolvedColumnRef(
              struct_column_.type(), struct_column_,
              /*is_correlated=*/
              correlated_parameter_info_list_.back().is_correlated);
      subquery_expr_builder.add_parameter_list(std::move(struct_column_ref));
    }
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<ResolvedSubqueryExpr> rewritten_subquery_expr,
        std::move(subquery_expr_builder).BuildMutable());
    PushNodeToStack(std::move(rewritten_subquery_expr));
    return absl::OkStatus();
  }

  absl::Status VisitResolvedInlineLambda(
      const ResolvedInlineLambda* node) override {
    // Push a new CorrelatedParameterInfo onto the stack and process the
    // `body` of the lambda.
    correlated_parameter_info_list_.push_back(CorrelatedParameterInfo());
    auto cleanup_correlated_parameter_info = absl::MakeCleanup(
        [this] { correlated_parameter_info_list_.pop_back(); });
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> rewritten_lambda_body,
                     ProcessNode(node->body()));

    // Make a copy of the lambda, set the `body` field and augment the parameter
    // list with the struct column if needed.
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedInlineLambda> copied_lambda,
                     ResolvedASTDeepCopyVisitor::Copy(node));
    ResolvedInlineLambdaBuilder lambda_builder =
        ToBuilder(std::move(copied_lambda));
    lambda_builder.set_body(std::move(rewritten_lambda_body));
    GOOGLESQL_RET_CHECK(!correlated_parameter_info_list_.empty());
    if (correlated_parameter_info_list_.back()
            .add_struct_column_to_parameter_list) {
      std::unique_ptr<ResolvedColumnRef> struct_column_ref =
          MakeResolvedColumnRef(
              struct_column_.type(), struct_column_,
              /*is_correlated=*/
              correlated_parameter_info_list_.back().is_correlated);
      lambda_builder.add_parameter_list(std::move(struct_column_ref));
    }
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedInlineLambda> rewritten_lambda,
                     std::move(lambda_builder).BuildMutable());
    PushNodeToStack(std::move(rewritten_lambda));
    return absl::OkStatus();
  }

  absl::Status VisitResolvedExpressionColumn(
      const ResolvedExpressionColumn* node) override {
    // If we visit an ExpressionColumn, then we need to augment the parameter
    // list of any enclosing subqueries or lambdas to include the struct column.
    for (int i = 0; i < correlated_parameter_info_list_.size(); ++i) {
      correlated_parameter_info_list_[i].add_struct_column_to_parameter_list =
          true;
      if (i > 0 || struct_column_refs_are_correlated_) {
        correlated_parameter_info_list_[i].is_correlated = true;
      }
    }
    // Make a column ref to the struct column. If
    // `struct_column_refs_are_correlated` is true, OR we are within a subquery,
    // then the column ref is correlated.
    //
    // struct_column_ref = ColumnRef(
    //   type=STRUCT<STRUCT<referenced_columns>, STRUCT<key_columns>>
    // )
    std::unique_ptr<ResolvedColumnRef> struct_column_ref =
        MakeResolvedColumnRef(
            struct_column_.type(), struct_column_,
            /*is_correlated=*/struct_column_refs_are_correlated_ ||
                !correlated_parameter_info_list_.empty());
    // +-GetStructField
    //  +-type=STRUCT<referenced_columns>
    //  +-expr=
    //  | +-<struct_column_ref>
    //  +-field_idx=0
    GOOGLESQL_RET_CHECK_EQ(struct_column_ref->type()->AsStruct()->num_fields(), 2);
    const StructField& referenced_columns_field =
        struct_column_ref->type()->AsStruct()->field(
            kReferencedColumnsFieldIndex);
    std::unique_ptr<ResolvedGetStructField> get_struct_field_expr =
        MakeResolvedGetStructField(referenced_columns_field.type,
                                   std::move(struct_column_ref),
                                   kReferencedColumnsFieldIndex);

    // +-GetStructField
    //  +-type=<output_type>
    //  +-expr=
    //  | +-GetStructField
    //  |  +-type=STRUCT<referenced_columns>
    //  |  +-expr=
    //  |  | +-<struct_column_ref>
    //  |  +-field_idx=0
    //  +-field_idx=<field_index>
    bool is_ambiguous = false;
    int field_index = -1;
    const StructField* field =
        get_struct_field_expr->type()->AsStruct()->FindField(
            node->name(), &is_ambiguous, &field_index);
    GOOGLESQL_RET_CHECK(field != nullptr);
    GOOGLESQL_RET_CHECK(!is_ambiguous);
    GOOGLESQL_RET_CHECK(field_index >= 0);
    PushNodeToStack(MakeResolvedGetStructField(
        field->type, std::move(get_struct_field_expr), field_index));
    return absl::OkStatus();
  }

 private:
  explicit StructColumnReferenceRewriter(ResolvedColumn struct_column,
                                         bool struct_column_refs_are_correlated)
      : struct_column_(struct_column),
        struct_column_refs_are_correlated_(struct_column_refs_are_correlated) {}
  StructColumnReferenceRewriter(const StructColumnReferenceRewriter&) = delete;
  StructColumnReferenceRewriter& operator=(
      const StructColumnReferenceRewriter&) = delete;

  // `CorrelatedParameterInfo` is used to track information about correlated
  // parameters for both subqueries and lambdas.
  struct CorrelatedParameterInfo {
    bool add_struct_column_to_parameter_list = false;
    bool is_correlated = false;
  };

  ResolvedColumn struct_column_;
  // If `struct_column_refs_are_correlated_` is true, then any references to
  // `struct_column_` are treated as correlated. Should only be true when the
  // measure column is being invoked in a correlated context;
  // e.g. AGG(correlated_reference_to_measure_column).
  bool struct_column_refs_are_correlated_;
  std::vector<CorrelatedParameterInfo> correlated_parameter_info_list_;
};

absl::StatusOr<RewriteMeasureExprResult> RewriteMeasureExpr(
    const ResolvedExpr* measure_expr, ResolvedColumn struct_column,
    const absl::btree_set<std::string, googlesql_base::CaseLess>&
        row_identity_column_names,
    bool struct_column_refs_are_correlated, const Function* any_value_fn,
    FunctionCallBuilder& function_call_builder,
    const LanguageOptions& language_options, ColumnFactory& column_factory,
    TypeFactory& type_factory) {
  // Remap column ids in the measure expression to use new column ids
  // allocated by `column_factory`. Since the measure expression was
  // analyzed in a different context, it's column ids will be invalid in
  // the current query.
  ColumnReplacementMap column_replacement_map;
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> rewritten_measure_expr,
                   CopyResolvedASTAndRemapColumns(*measure_expr, column_factory,
                                                  column_replacement_map));

  // Rewrite the measure expression to reference columns from
  // `struct_column`.
  GOOGLESQL_ASSIGN_OR_RETURN(rewritten_measure_expr,
                   StructColumnReferenceRewriter::RewriteMeasureExpression(
                       rewritten_measure_expr.get(), struct_column,
                       struct_column_refs_are_correlated));

  // Rewrite the measure expression to use multi-level aggregation to
  // grain-lock and avoid overcounting.
  MultiLevelAggregateRewriter multi_level_aggregate_rewriter(
      any_value_fn, function_call_builder, language_options, column_factory,
      type_factory, struct_column, row_identity_column_names,
      struct_column_refs_are_correlated);
  GOOGLESQL_ASSIGN_OR_RETURN(rewritten_measure_expr,
                   multi_level_aggregate_rewriter.RewriteMultiLevelAggregate(
                       std::move(rewritten_measure_expr)));
  return RewriteMeasureExprResult{
      .rewritten_measure_expr = std::move(rewritten_measure_expr),
      .constituent_aggregate_list =
          multi_level_aggregate_rewriter.release_constituent_aggregate_list()};
}

}  // namespace googlesql
