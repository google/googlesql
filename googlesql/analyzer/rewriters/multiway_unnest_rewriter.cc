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

#include "googlesql/analyzer/rewriters/multiway_unnest_rewriter.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/functions/array_zip_mode.pb.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/make_node_vector.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_ast_deep_copy_visitor.h"
#include "googlesql/resolved_ast/resolved_ast_rewrite_visitor.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/rewrite_utils.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {

class MultiwayUnnestRewriteVisitor : public ResolvedASTRewriteVisitor {
 public:
  MultiwayUnnestRewriteVisitor(const AnalyzerOptions& analyzer_options,
                               Catalog& catalog, TypeFactory& type_factory)
      : type_factory_(type_factory),
        column_factory_(/*max_col_id=*/0,
                        analyzer_options.id_string_pool().get(),
                        analyzer_options.column_id_sequence_number()),
        fn_builder_(analyzer_options, catalog, type_factory) {}

 private:
  // States of pre-rewritten and post-rewritten input_scan columns, element
  // columns, and array offset columns.
  class State {
   public:
    static absl::StatusOr<State> Create(const ResolvedArrayScan& node) {
      GOOGLESQL_RET_CHECK_GE(node.element_column_list_size(), 2);
      State state(node.element_column_list_size());
      state.Init(node);
      return state;
    }

    const ResolvedColumn& pre_rewritten_array_offset_column() const {
      ABSL_DCHECK(HasPreRewrittenArrayOffsetColumn());
      return *pre_rewritten_array_offset_column_;
    }
    // Returns a list of column in the pre-rewrite ArrayScan's
    // `element_column_list`. The size is equal to the number of input arrays.
    const std::vector<ResolvedColumn>& pre_rewritten_element_columns() const {
      return pre_rewritten_element_columns_;
    }
    // Returns a list of columns that originate from the `column_list` of the
    // pre-rewrite ArrayScan's `input_scan`.
    const std::vector<ResolvedColumn>& input_scan_columns() const {
      return input_scan_columns_;
    }
    // Returns all rewritten element columns. The size is equal to the number
    // of input arrays.
    //
    // The i-th element represents the element_column_list[0] of the i-th
    // rewritten array scan.
    const std::vector<ResolvedColumn>& element_columns() const {
      return element_columns_;
    }
    // Returns a list of new columns that are assigned and point to the original
    // array expressions. The size is equal to the number of input arrays.
    //
    // The i-th element represents array_expr_list[i] of the pre-rewrite array
    // scan.
    const std::vector<ResolvedColumn>& with_expr_array_columns() const {
      return with_expr_array_columns_;
    }
    // Returns all array offset columns that come out of rewritten array scans.
    // The size is equal to the number of input arrays.
    //
    // The i-th element represents the offset column of the i-th rewritten array
    // scan.
    const std::vector<ResolvedColumn>& array_offset_columns() const {
      return array_offset_columns_;
    }
    // Returns all offset columns that come out of FULL JOIN USING. The size is
    // equal to the number of input arrays.
    //
    // The i-th element represents the final offset column for the JoinScan of
    // the (i-1)-th and i-th array scans (i is 0-based). It's computed as a
    // COALESCE of two offset columns.
    const std::vector<ResolvedColumn>& full_join_offset_columns() const {
      return full_join_offset_columns_;
    }
    // Returns the offset column that come out of the top level FULL JOIN USING.
    const ResolvedColumn& GetLastFullJoinOffsetColumn() const {
      return full_join_offset_columns_.back();
    }
    int element_column_count() const { return element_column_count_; }
    bool HasPreRewrittenArrayOffsetColumn() const {
      return pre_rewritten_array_offset_column_.has_value();
    }
    const ResolvedColumn& GetResultLengthColumn() const {
      return result_length_;
    }
    void SetResultLengthColumn(const ResolvedColumn& column) {
      result_length_ = column;
    }

    void SetWithExprArrayColumn(int i, const ResolvedColumn& column) {
      with_expr_array_columns_[i] = column;
    }
    void SetElementColumn(int i, const ResolvedColumn& column) {
      element_columns_[i] = column;
    }
    void SetArrayOffsetColumn(int i, const ResolvedColumn& column) {
      array_offset_columns_[i] = column;
    }
    void SetFullJoinOffsetColumn(int i, const ResolvedColumn& column) {
      full_join_offset_columns_[i] = column;
    }

    const StructType* struct_type() const {
      ABSL_DCHECK(struct_type_ != nullptr);
      return struct_type_;
    }

    void set_struct_type(const StructType* type) { struct_type_ = type; }

    const AnnotationMap* struct_annotation_map() const {
      return struct_annotation_map_;
    }

    void set_struct_annotation_map(const AnnotationMap* annotation_map) {
      struct_annotation_map_ = annotation_map;
    }

   private:
    void Init(const ResolvedArrayScan& node) {
      RecordPreRewrittenArrayOffsetColumn(node);
      RecordPreRewrittenElementColumns(node);
      RecordInputScanColumns(node);
      ResizeRewrittenStates();
    }

    void ResizeRewrittenStates() {
      with_expr_array_columns_.resize(element_column_count_);
      element_columns_.resize(element_column_count_);
      array_offset_columns_.resize(element_column_count_);
      full_join_offset_columns_.resize(element_column_count_);
    }

    void RecordPreRewrittenArrayOffsetColumn(const ResolvedArrayScan& node) {
      if (node.array_offset_column() != nullptr) {
        pre_rewritten_array_offset_column_ =
            node.array_offset_column()->column();
      }
    }

    void RecordPreRewrittenElementColumns(const ResolvedArrayScan& node) {
      pre_rewritten_element_columns_.resize(node.element_column_list_size());
      for (int i = 0; i < node.element_column_list_size(); ++i) {
        pre_rewritten_element_columns_[i] = node.element_column_list(i);
      }
    }

    // Record the column list from the ArrayScan's input_scan as a side channel.
    // They might or might not be used in the ArrayScan. But we will populate
    // these columns to the output column_list of rewritten scan.
    void RecordInputScanColumns(const ResolvedArrayScan& node) {
      absl::flat_hash_set<ResolvedColumn> local_column_set(
          node.element_column_list().begin(), node.element_column_list().end());
      if (node.array_offset_column() != nullptr) {
        local_column_set.insert(node.array_offset_column()->column());
      }
      for (const ResolvedColumn& column : node.column_list()) {
        if (!local_column_set.contains(column)) {
          input_scan_columns_.push_back(column);
        }
      }
    }

    explicit State(int element_column_count)
        : element_column_count_(element_column_count),
          pre_rewritten_array_offset_column_(std::nullopt),
          struct_type_(nullptr),
          struct_annotation_map_(nullptr) {}
    int element_column_count_;

    // Pre-rewrite states
    ResolvedColumnList pre_rewritten_element_columns_;
    std::optional<ResolvedColumn> pre_rewritten_array_offset_column_;
    ResolvedColumnList input_scan_columns_;

    // Post-rewrite states
    ResolvedColumnList with_expr_array_columns_;
    ResolvedColumn result_length_;
    ResolvedColumnList element_columns_;
    ResolvedColumnList array_offset_columns_;
    ResolvedColumnList full_join_offset_columns_;
    const StructType* struct_type_;
    const AnnotationMap* struct_annotation_map_;
  };

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedArrayScan(
      std::unique_ptr<const ResolvedArrayScan> node) override {
    if (node->array_expr_list_size() < 2) {
      return node;
    }
    // Populate pre-rewritten states and allocate space for post-rewrite states.
    GOOGLESQL_ASSIGN_OR_RETURN(State state, State::Create(*node));

    return BuildTopLevelRewrittenScan(node, state);
  }

  absl::StatusOr<std::unique_ptr<const ResolvedScan>>
  BuildTopLevelRewrittenScan(
      const std::unique_ptr<const ResolvedArrayScan>& original_array_scan,
      State& state) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedArrayScan> array_scan,
        BuildTopLevelArrayScanOfWithExpr(original_array_scan, state));

    // Map pre-rewrite element column names to rewritten GetStructFields.
    return BuildStructExpansionWithColumnReplacement(std::move(array_scan),
                                                     state);
  }

  absl::StatusOr<std::unique_ptr<const ResolvedArrayScan>>
  BuildTopLevelArrayScanOfWithExpr(
      const std::unique_ptr<const ResolvedArrayScan>& original_array_scan,
      State& state) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> with_expr,
        BuildWithExpr(original_array_scan->array_expr_list(),
                      original_array_scan->array_zip_mode(), state));

    ResolvedColumnList column_list = state.input_scan_columns();
    const ResolvedColumn array_element_col = column_factory_.MakeCol(
        "$array", "$with_expr_element",
        AnnotatedType(with_expr->type()->AsArray()->element_type(),
                      state.struct_annotation_map()));

    column_list.push_back(array_element_col);

    std::unique_ptr<const ResolvedScan> input_scan;
    if (original_array_scan->input_scan() != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(input_scan, ResolvedASTDeepCopyVisitor::Copy(
                                       original_array_scan->input_scan()));
    }
    std::unique_ptr<const ResolvedExpr> join_expr;
    if (original_array_scan->join_expr() != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(auto join_expr_copy,
                       ResolvedASTDeepCopyVisitor::Copy<const ResolvedExpr>(
                           original_array_scan->join_expr()));
      // Within the join_expr we'd like to refer to the original array columns
      // by their pre-rewrite names. But those don't exist until we're outside
      // the ArrayScan. If is_outer=True this join_expr must be done inside the
      // ArrayScan so we create new variables inside this new join_expr.
      auto with_expr_builder =
          ResolvedWithExprBuilder().set_type(join_expr_copy->type());
      GOOGLESQL_ASSIGN_OR_RETURN(auto column_replacement_map,
                       BuildColumnReplacementVector(state, array_element_col));
      ColumnReplacementMap prewritten_to_inside_with_column_map;
      for (auto&& [pre_rewrite_col, expr] : column_replacement_map) {
        const ResolvedColumn new_col = column_factory_.MakeCol(
            "$array", "$with_expr_field", expr->annotated_type());
        prewritten_to_inside_with_column_map.emplace(pre_rewrite_col, new_col);
        with_expr_builder.add_assignment_list(
            ResolvedComputedColumnBuilder().set_column(new_col).set_expr(
                std::move(expr)));
      }
      GOOGLESQL_ASSIGN_OR_RETURN(join_expr, std::move(with_expr_builder)
                                      .set_expr(RemapSpecifiedColumns(
                                          std::move(join_expr_copy),
                                          prewritten_to_inside_with_column_map))
                                      .Build());
    }

    return ResolvedArrayScanBuilder()
        .set_column_list(column_list)
        .set_input_scan(std::move(input_scan))
        .add_array_expr_list(std::move(with_expr))
        .add_element_column_list(array_element_col)
        .set_is_outer(original_array_scan->is_outer())
        .set_join_expr(std::move(join_expr))
        .Build();
  }

  // LEAST(arr1_len, arr2_len [, ... ] )
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> BuildLeastArrayLengthExpr(
      absl::Span<const std::unique_ptr<const ResolvedColumnRef>>
          arr_len_exprs) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::vector<std::unique_ptr<const ResolvedExpr>> copied_array_lens,
        CopyArrayLengthExpressions(arr_len_exprs));
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> least,
                     fn_builder_.Least(std::move(copied_array_lens)));
    return least;
  }

  // GREATEST(arr1_len, arr2_len [, ... ] )
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
  BuildGreatestArrayLengthExpr(
      absl::Span<const std::unique_ptr<const ResolvedColumnRef>>
          arr_len_exprs) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::vector<std::unique_ptr<const ResolvedExpr>> copied_array_lens,
        CopyArrayLengthExpressions(arr_len_exprs));
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> greatest,
                     fn_builder_.Greatest(std::move(copied_array_lens)));
    return greatest;
  }

  absl::StatusOr<std::vector<std::unique_ptr<const ResolvedExpr>>>
  CopyArrayLengthExpressions(
      absl::Span<const std::unique_ptr<const ResolvedColumnRef>> array_lens) {
    std::vector<std::unique_ptr<const ResolvedExpr>> copied_array_lens;
    copied_array_lens.reserve(array_lens.size());
    for (const auto& arr_len : array_lens) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> copied,
                       ResolvedASTDeepCopyVisitor::Copy(arr_len.get()));
      copied_array_lens.push_back(std::move(copied));
    }
    return copied_array_lens;
  }

  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> BuildStrictCheckExpr(
      absl::Span<const std::unique_ptr<const ResolvedColumnRef>> array_lens,
      const ResolvedColumn& mode) {
    // mode = 'STRICT'
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> equal,
        fn_builder_.Equal(MakeResolvedColumnRef(types::ArrayZipModeEnumType(),
                                                mode, /*is_correlated=*/false),
                          MakeResolvedLiteral(
                              types::ArrayZipModeEnumType(),
                              Value::Enum(types::ArrayZipModeEnumType(),
                                          functions::ArrayZipEnums::STRICT))));

    // LEAST(arr1_len, arr2_len [, ... ] )
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> least,
                     BuildLeastArrayLengthExpr(array_lens));

    // GREATEST(arr1_len, arr2_len [, ... ] )
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> greatest,
                     BuildGreatestArrayLengthExpr(array_lens));

    // LEAST(...) != GREATEST(...)
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> not_equal,
        fn_builder_.NotEqual(std::move(least), std::move(greatest)));

    // ERROR("...")
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> error,
        fn_builder_.Error(
            "Unnested arrays under STRICT mode must have equal lengths"));

    // IF(mode = 'STRICT' AND
    //      LEAST(arr1_len, arr2_len ) !=
    //      GREATEST(arr1_len, arr2_len ),
    //    ERROR('strict'),
    //    NULL)
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> condition,
                     fn_builder_.And(MakeNodeVector(std::move(equal),
                                                    std::move(not_equal))));
    return fn_builder_.If(
        std::move(condition), std::move(error),
        MakeResolvedLiteral(types::Int64Type(), Value::NullInt64()));
  }

  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> BuildResultLengthEpxr(
      absl::Span<const std::unique_ptr<const ResolvedColumnRef>> array_lengths,
      const ResolvedColumn& mode) {
    // mode = 'TRUNCATE'
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> equal,
                     fn_builder_.Equal(
                         MakeResolvedColumnRef(types::ArrayZipModeEnumType(),
                                               mode, /*is_correlated=*/false),
                         MakeResolvedLiteral(
                             types::ArrayZipModeEnumType(),
                             Value::Enum(types::ArrayZipModeEnumType(),
                                         functions::ArrayZipEnums::TRUNCATE))));

    // LEAST(arr1_len, arr2_len [, ... ] )
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> least,
                     BuildLeastArrayLengthExpr(array_lengths));

    // GREATEST(arr1_len, arr2_len [, ... ] )
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> greatest,
                     BuildGreatestArrayLengthExpr(array_lengths));

    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> if_expr,
                     fn_builder_.If(std::move(equal), std::move(least),
                                    std::move(greatest)));
    return if_expr;
  }

  // IF(mode_expr IS NULL, ERROR, mode_expr)
  absl::StatusOr<std::unique_ptr<ResolvedExpr>> BuildModeExpr(
      const ResolvedExpr* mode_expr, const ResolvedColumn& mode_col) {
    // ERROR("...")
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> error,
        fn_builder_.Error("UNNEST does not allow NULL mode argument",
                          types::ArrayZipModeEnumType()));

    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> mode_copied1,
                     ResolvedASTDeepCopyVisitor::Copy(mode_expr));
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> condition,
                     fn_builder_.IsNull(std::move(mode_copied1)));
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> mode_copied2,
                     ResolvedASTDeepCopyVisitor::Copy(mode_expr));
    return fn_builder_.If(std::move(condition), std::move(error),
                          std::move(mode_copied2));
  }

  // IF(array_expr IS NULL, 0, ARRAY_LENGTH(array_expr))
  absl::StatusOr<std::unique_ptr<ResolvedExpr>> BuildArrayLengthExpr(
      const ResolvedColumn& array_col) {
    // array_expr IS NULL
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> condition,
                     fn_builder_.IsNull(BuildResolvedColumnRef(array_col)));
    // ARRAY_LENGTH(array_expr)
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> array_length,
        fn_builder_.ArrayLength(BuildResolvedColumnRef(array_col)));
    return fn_builder_.If(
        std::move(condition),
        MakeResolvedLiteral(types::Int64Type(), Value::Int64(0)),
        std::move(array_length));
  }

  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> BuildWithExpr(
      absl::Span<const std::unique_ptr<const ResolvedExpr>> input_arrays,
      const ResolvedExpr* mode, State& state) {
    GOOGLESQL_RET_CHECK(mode != nullptr);
    GOOGLESQL_RET_CHECK_EQ(mode->type(), types::ArrayZipModeEnumType());
    GOOGLESQL_RET_CHECK_GE(input_arrays.size(), 2);

    std::vector<std::unique_ptr<const ResolvedColumnRef>> array_lens;
    std::vector<std::unique_ptr<const ResolvedComputedColumn>> assignment_list;
    assignment_list.reserve(input_arrays.size() + 1);
    for (int i = 0; i < input_arrays.size(); ++i) {
      // `arrN` expr
      ResolvedColumn arr_col =
          column_factory_.MakeCol("$with_expr", absl::StrCat("arr", i),
                                  input_arrays[i]->annotated_type());
      state.SetWithExprArrayColumn(i, arr_col);

      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> array_expr,
                       ResolvedASTDeepCopyVisitor::Copy(input_arrays[i].get()));
      assignment_list.push_back(
          MakeResolvedComputedColumn(arr_col, std::move(array_expr)));

      // `arrN_len` expr
      ResolvedColumn arr_len_col = column_factory_.MakeCol(
          "$with_expr", absl::StrCat("arr", i, "_len"), types::Int64Type());
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> arr_len_expr,
                       BuildArrayLengthExpr(arr_col));
      array_lens.push_back(MakeResolvedColumnRef(
          arr_len_expr->type(), arr_len_col, /*is_correlated=*/false));
      assignment_list.push_back(
          MakeResolvedComputedColumn(arr_len_col, std::move(arr_len_expr)));
    }

    // `mode` expr
    ResolvedColumn mode_col = column_factory_.MakeCol(
        "$with_expr", "mode", types::ArrayZipModeEnumType());
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> mode_expr,
                     BuildModeExpr(mode, mode_col));
    assignment_list.push_back(
        MakeResolvedComputedColumn(mode_col, std::move(mode_expr)));

    // `strict_check` expr
    ResolvedColumn strict_check_col = column_factory_.MakeCol(
        "$with_expr", "strict_check", types::Int64Type());
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> strict_check_expr,
                     BuildStrictCheckExpr(array_lens, mode_col));
    assignment_list.push_back(MakeResolvedComputedColumn(
        strict_check_col, std::move(strict_check_expr)));

    // `result_len` expr
    ResolvedColumn result_len_col =
        column_factory_.MakeCol("$with_expr", "result_len", types::Int64Type());
    state.SetResultLengthColumn(result_len_col);
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> result_len,
                     BuildResultLengthEpxr(array_lens, mode_col));
    assignment_list.push_back(
        MakeResolvedComputedColumn(result_len_col, std::move(result_len)));

    // Starting from here, we build the rewritten tree in a post-order traversal
    // way.
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> array_subquery,
                     BuildArraySubqueryExpr(state));
    const Type* array_type = array_subquery->type();
    const AnnotationMap* array_annotation_map =
        array_subquery->type_annotation_map();
    return ResolvedWithExprBuilder()
        .set_type(array_type)
        .set_type_annotation_map(array_annotation_map)
        .set_expr(std::move(array_subquery))
        .set_assignment_list(std::move(assignment_list))
        .Build();
  }

  // Build an ARRAY subquery expr representing
  //   ARRAY<STRUCT<arr1, arr2 [ , ... ] , offset>>
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> BuildArraySubqueryExpr(
      State& state) {
    // Build a top level ProjectScan wrapping the chain of ArrayScan joins.
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedProjectScan> project_scan,
                     BuildNestedProjectScanOfFullJoinUsing(state));

    // Wrap with FilterScan with predicate
    // `COALESCE(..., offsetN) < result_len`.
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedFilterScan> filter_scan,
        BuildFilterScanFromNestedProjectScan(std::move(project_scan), state));

    // Wrap with OrderByScan, ordering by the top level COALESCE column.
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedOrderByScan> order_by_scan,
        BuildOrderByScanOfFilterScan(std::move(filter_scan), state));

    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedScan> make_struct_scan,
        BuildMakeStructProjectScan(std::move(order_by_scan), state));
    GOOGLESQL_RET_CHECK_GT(state.struct_type()->num_fields(), 2);

    // `parameter_list` only needs to reference array columns and result_len
    // column from with expr's `assignment_list`.
    std::vector<std::unique_ptr<const ResolvedColumnRef>>
        subquery_parameter_list(state.element_column_count() + 1);
    for (int i = 0; i < state.element_column_count(); ++i) {
      subquery_parameter_list[i] = MakeResolvedColumnRef(
          state.with_expr_array_columns()[i], /*is_correlated=*/false);
    }
    subquery_parameter_list.back() = MakeResolvedColumnRef(
        state.GetResultLengthColumn().type(), state.GetResultLengthColumn(),
        /*is_correlated=*/false);

    const ArrayType* array_type = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(
        type_factory_.MakeArrayType(state.struct_type(), &array_type));
    const AnnotationMap* annotation_map = nullptr;
    if (state.struct_annotation_map() != nullptr) {
      std::unique_ptr<AnnotationMap> map = AnnotationMap::Create(array_type);
      GOOGLESQL_RETURN_IF_ERROR(
          map->AsStructMap()->CloneIntoField(0, state.struct_annotation_map()));
      GOOGLESQL_ASSIGN_OR_RETURN(annotation_map,
                       type_factory_.TakeOwnership(std::move(map)));
    }

    return ResolvedSubqueryExprBuilder()
        .set_type(array_type)
        .set_type_annotation_map(annotation_map)
        .set_subquery_type(ResolvedSubqueryExpr::ARRAY)
        .set_parameter_list(std::move(subquery_parameter_list))
        .set_subquery(std::move(make_struct_scan))
        .Build();
  }

  // STRUCT(arr1, arr2 [, ...], offset)
  // REQUIRES: all rewritten columns are initialized before calling this
  // function.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> MakeStructExpr(
      TypeFactory& type_factory, State& state) {
    std::vector<std::unique_ptr<const ResolvedExpr>> field_list;
    std::vector<StructField> struct_fields;
    for (int i = 0; i < state.element_column_count(); ++i) {
      const ResolvedColumn& rewritten_element_column =
          state.element_columns()[i];
      field_list.push_back(MakeResolvedColumnRef(rewritten_element_column,
                                                 /*is_correlated=*/false));
      struct_fields.push_back(
          {rewritten_element_column.name(), rewritten_element_column.type()});
    }

    const ResolvedColumn& rewritten_offset_column =
        state.GetLastFullJoinOffsetColumn();
    field_list.push_back(MakeResolvedColumnRef(rewritten_offset_column.type(),
                                               rewritten_offset_column,
                                               /*is_correlated=*/false));
    // It's OK to choose a fixed offset column name here as it will be mapped
    // to the pre-rewritten offset column in the final column replacement
    // ProjectScan.
    // WARNING: The offset column has be the last field. As the parent node
    // depends on the order of the struct field to map pre-rewrite element
    // columns back to rewritten get struct field columns.
    struct_fields.push_back({"offset", rewritten_offset_column.type()});
    const StructType* struct_type;
    GOOGLESQL_RETURN_IF_ERROR(type_factory.MakeStructType(struct_fields, &struct_type));
    state.set_struct_type(struct_type);

    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedMakeStruct> make_struct,
                     ResolvedMakeStructBuilder()
                         .set_type(struct_type)
                         .set_field_list(std::move(field_list))
                         .BuildMutable());
    GOOGLESQL_RETURN_IF_ERROR(
        fn_builder_.annotation_propagator().CheckAndPropagateAnnotations(
            /*error_node=*/nullptr, make_struct.get()));
    state.set_struct_annotation_map(make_struct->type_annotation_map());
    return make_struct;
  }

  absl::StatusOr<std::unique_ptr<const ResolvedProjectScan>>
  BuildMakeStructProjectScan(std::unique_ptr<const ResolvedScan> input_scan,
                             State& state) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> struct_expr,
                     MakeStructExpr(type_factory_, state));

    ResolvedColumnList column_list;
    ResolvedColumn make_struct_col = column_factory_.MakeCol(
        "$make_struct", "$struct", struct_expr->annotated_type());
    column_list.push_back(make_struct_col);

    return ResolvedProjectScanBuilder()
        .set_column_list(column_list)
        .set_input_scan(std::move(input_scan))
        .set_is_ordered(true)
        .add_expr_list(
            MakeResolvedComputedColumn(make_struct_col, std::move(struct_expr)))
        .Build();
  }

  // Build a OrderByScan on top of the filtered nested ProjectScan.
  absl::StatusOr<std::unique_ptr<const ResolvedOrderByScan>>
  BuildOrderByScanOfFilterScan(std::unique_ptr<const ResolvedScan> input_scan,
                               const State& state) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedOrderByItem> order_by_item,
        ResolvedOrderByItemBuilder()
            .set_column_ref(MakeResolvedColumnRef(
                types::Int64Type(), state.GetLastFullJoinOffsetColumn(),
                /*is_correlated=*/false))
            .Build());

    // Propagate the exact same column_list from its input_scan's column_list.
    ResolvedColumnList column_list = input_scan->column_list();
    return ResolvedOrderByScanBuilder()
        .set_column_list(column_list)
        .set_is_ordered(true)
        .set_input_scan(std::move(input_scan))
        .add_order_by_item_list(std::move(order_by_item))
        .Build();
  }

  // Build a FilterScan on top of the nested ProjectScan containing a chain of
  // joins among arrays. The filter_expr is:
  //   COALESCE(..., offsetN) < result_len
  absl::StatusOr<std::unique_ptr<const ResolvedFilterScan>>
  BuildFilterScanFromNestedProjectScan(
      std::unique_ptr<const ResolvedScan> input_scan, const State& state) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> filter_expr,
        fn_builder_.Less(
            MakeResolvedColumnRef(state.GetLastFullJoinOffsetColumn().type(),
                                  state.GetLastFullJoinOffsetColumn(),
                                  /*is_correlated=*/false),
            MakeResolvedColumnRef(state.GetResultLengthColumn().type(),
                                  state.GetResultLengthColumn(),
                                  /*is_correlated=*/true)));

    // Propagate the exact same column_list from its input_scan's column_list.
    ResolvedColumnList column_list = input_scan->column_list();
    return ResolvedFilterScanBuilder()
        .set_column_list(column_list)
        .set_input_scan(std::move(input_scan))
        .set_filter_expr(std::move(filter_expr))
        .Build();
  }

  // Build nested ProjectScans containing left-deep JoinScans of ArrayScans to
  // represent a chain of FULL JOIN of arrays.
  //     UNNEST(arr1) AS arr1 WITH OFFSET
  //     FULL JOIN
  //     UNNEST(arr2) AS arr2 WITH OFFSET
  //     USING (offset)
  //     [ ...
  //       FULL JOIN
  //       UNNEST(arrN) AS arrN WITH OFFSET
  //       USING (offset)
  //     ]
  absl::StatusOr<std::unique_ptr<const ResolvedProjectScan>>
  BuildNestedProjectScanOfFullJoinUsing(State& state) {
    std::unique_ptr<const ResolvedProjectScan> final_scan;
    for (int i = 0; i < state.element_column_count() - 1; ++i) {
      std::unique_ptr<const ResolvedScan> lhs = std::move(final_scan);
      if (i == 0) {
        // Lhs ArrayScan
        GOOGLESQL_ASSIGN_OR_RETURN(lhs, BuildSingletonUnnestArrayScan(
                                  /*index=*/i, state));
      }
      // Rhs ArrayScan
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedArrayScan> rhs,
                       BuildSingletonUnnestArrayScan(
                           /*index=*/i + 1, state));

      GOOGLESQL_ASSIGN_OR_RETURN(
          std::unique_ptr<const ResolvedProjectScan> project_scan,
          BuildProjectScanOfJoinOnCoalesce(std::move(lhs), std::move(rhs),
                                           /*lhs_index=*/i, state));
      final_scan = std::move(project_scan);
    }
    return final_scan;
  }

  // Map pre-written columns to expressions of `struct_column`.
  absl::StatusOr<std::vector<
      std::pair<ResolvedColumn, std::unique_ptr<const ResolvedExpr>>>>
  BuildColumnReplacementVector(State& state, ResolvedColumn struct_column) {
    const StructType* struct_type = state.struct_type();
    GOOGLESQL_RET_CHECK_EQ(state.element_column_count() + 1, struct_type->num_fields());

    std::vector<std::pair<ResolvedColumn, std::unique_ptr<const ResolvedExpr>>>
        result;
    result.reserve(state.HasPreRewrittenArrayOffsetColumn()
                       ? struct_type->num_fields()
                       : struct_type->num_fields() - 1);
    for (int i = 0; i < struct_type->num_fields(); ++i) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> expr,
                       BuildGetStructFieldExpr(struct_type, i, struct_column));

      ResolvedColumn pre_rewrite_column;
      if (i < struct_type->num_fields() - 1) {
        // Array element column.
        pre_rewrite_column = state.pre_rewritten_element_columns()[i];
      } else {
        // If the pre-rewritten UNNEST specified WITH OFFSET, it needs to be
        // placed back, computed by the rewritten full join array offset column.
        // Otherwise, skip it.
        if (!state.HasPreRewrittenArrayOffsetColumn()) {
          continue;
        }
        pre_rewrite_column = state.pre_rewritten_array_offset_column();
      }
      result.push_back(std::make_pair(pre_rewrite_column, std::move(expr)));
    }
    return result;
  }

  // Build a ProjectScan doing a struct expansion against the output of
  // UNNEST(ARRAY(SELECT AS STRUCT arr1, arr2 [ , ... ], offset)):
  //   ARRAY<STRUCT<arr1, arr2 [ , ... ], offset>>
  absl::StatusOr<std::unique_ptr<const ResolvedProjectScan>>
  BuildStructExpansionWithColumnReplacement(
      std::unique_ptr<const ResolvedArrayScan> array_scan, State& state) {
    GOOGLESQL_RET_CHECK_EQ(array_scan->element_column_list_size(), 1);
    const StructType* struct_type = state.struct_type();
    GOOGLESQL_RET_CHECK_EQ(state.element_column_count() + 1, struct_type->num_fields());

    GOOGLESQL_ASSIGN_OR_RETURN(auto column_replacement_map,
                     BuildColumnReplacementVector(
                         state, array_scan->element_column_list(0)));
    ResolvedColumnList column_list = state.input_scan_columns();
    std::vector<std::unique_ptr<const ResolvedComputedColumn>> expr_list;
    expr_list.reserve(column_replacement_map.size());
    for (auto&& [pre_rewrite_column, expr] :
         std::move(column_replacement_map)) {
      expr_list.push_back(
          MakeResolvedComputedColumn(pre_rewrite_column, std::move(expr)));
      column_list.push_back(pre_rewrite_column);
    }

    return ResolvedProjectScanBuilder()
        .set_column_list(column_list)
        .set_input_scan(std::move(array_scan))
        .set_expr_list(std::move(expr_list))
        .Build();
  }

  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> BuildGetStructFieldExpr(
      const StructType* struct_type, int field_index,
      const ResolvedColumn& source_column) {
    const AnnotationMap* type_annotation_map = nullptr;
    if (source_column.type_annotation_map() != nullptr &&
        field_index < struct_type->num_fields() - 1) {
      type_annotation_map =
          source_column.type_annotation_map()->AsStructMap()->field(
              field_index);
    }

    return ResolvedGetStructFieldBuilder()
        .set_type(struct_type->field(field_index).type)
        .set_type_annotation_map(type_annotation_map)
        .set_field_idx(field_index)
        .set_expr(MakeResolvedColumnRef(source_column,
                                        /*is_correlated=*/false))
        .Build();
  }

  // Build a ProjectScan wrapping a JoinScan that represents a left-deep
  //   <lhs> FULL JOIN <rhs_array_scan> ON <join_expr>
  absl::StatusOr<std::unique_ptr<const ResolvedProjectScan>>
  BuildProjectScanOfJoinOnCoalesce(std::unique_ptr<const ResolvedScan> lhs_scan,
                                   std::unique_ptr<const ResolvedScan> rhs_scan,
                                   int lhs_index, State& state) {
    GOOGLESQL_RET_CHECK_GE(lhs_index, 0);
    GOOGLESQL_RET_CHECK_LT(lhs_index, state.element_column_count() - 1);

    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedJoinScan> join_scan,
        BuildJoinScanWithUsing(std::move(lhs_scan), std::move(rhs_scan),
                               lhs_index, state));

    // Build `expr_list` field for ResolvedProjectScan.
    const ResolvedColumn& lhs_array_offset =
        lhs_index == 0 ? state.array_offset_columns().front()
                       : state.full_join_offset_columns()[lhs_index];
    const ResolvedColumn& rhs_array_offset =
        state.array_offset_columns()[lhs_index + 1];
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> coalesce,
        BuildCoalesceOfTwoArrayOffsets(lhs_array_offset, rhs_array_offset));
    ResolvedColumn full_join_offset_col =
        column_factory_.MakeCol("$full_join", "offset", types::Int64Type());
    std::vector<std::unique_ptr<const ResolvedComputedColumn>> expr_list;
    expr_list.push_back(
        MakeResolvedComputedColumn(full_join_offset_col, std::move(coalesce)));

    // Populate local state for newly created full join coalesce offset column.
    state.SetFullJoinOffsetColumn(lhs_index + 1, full_join_offset_col);

    // Populate the `column_list` with all columns from JoinScan and the newly
    // created full join offset column. It contains all array element columns
    // and offset columns for input arrays in index range [0, lhs_index + 1].
    ResolvedColumnList column_list = join_scan->column_list();
    column_list.push_back(full_join_offset_col);

    return ResolvedProjectScanBuilder()
        .set_column_list(std::move(column_list))
        .set_expr_list(std::move(expr_list))
        .set_input_scan(std::move(join_scan))
        .Build();
  }

  // Build a JoinScan that represents a left-deep
  //   <lhs> FULL JOIN <rhs_array_scan> <join_expr>
  absl::StatusOr<std::unique_ptr<const ResolvedJoinScan>>
  BuildJoinScanWithUsing(std::unique_ptr<const ResolvedScan> lhs_scan,
                         std::unique_ptr<const ResolvedScan> rhs_scan,
                         int lhs_index, const State& state) {
    if (lhs_index == 0) {
      GOOGLESQL_RET_CHECK(lhs_scan->Is<ResolvedArrayScan>());
    } else {
      GOOGLESQL_RET_CHECK(lhs_scan->Is<ResolvedProjectScan>());
    }
    GOOGLESQL_RET_CHECK(rhs_scan->Is<ResolvedArrayScan>());

    // We build `join_expr` by passing in offset column (lhs_index) and array
    // offset column (lhs_index + 1). Note that, if lhs_index is 0, we use array
    // offset column, otherwise, full join offset column is used.
    const ResolvedColumn& lhs_array_offset =
        lhs_index == 0 ? state.array_offset_columns().front()
                       : state.full_join_offset_columns()[lhs_index];
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> join_expr,
        BuildJoinExprOfTwoOffsets(
            /*lhs_array_offset=*/lhs_array_offset,
            /*rhs_array_offset=*/state.array_offset_columns()[lhs_index + 1]));

    // Populate element columns and offset columns to the output `column_list`.
    ResolvedColumnList column_list = lhs_scan->column_list();
    column_list.insert(column_list.end(), rhs_scan->column_list().begin(),
                       rhs_scan->column_list().end());

    return ResolvedJoinScanBuilder()
        .set_column_list(column_list)
        .set_join_type(ResolvedJoinScan::FULL)
        .set_left_scan(std::move(lhs_scan))
        .set_right_scan(std::move(rhs_scan))
        .set_join_expr(std::move(join_expr))
        .Build();
  }

  // Build the coalesce expr of two array offset columns.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
  BuildCoalesceOfTwoArrayOffsets(const ResolvedColumn& lhs_array_offset,
                                 const ResolvedColumn& rhs_array_offset) {
    std::vector<std::unique_ptr<const ResolvedExpr>> array_offsets(2);
    array_offsets[0] =
        MakeResolvedColumnRef(lhs_array_offset.type(), lhs_array_offset,
                              /*is_correlated=*/false);
    array_offsets[1] =
        MakeResolvedColumnRef(rhs_array_offset.type(), rhs_array_offset,
                              /*is_correlated=*/false);
    return fn_builder_.Coalesce(std::move(array_offsets));
  }

  absl::StatusOr<const AnnotationMap*> GetElementAnnotationMap(
      const Type* array_type, const AnnotationMap* array_annotation_map) {
    GOOGLESQL_RET_CHECK(array_type != nullptr);
    GOOGLESQL_RET_CHECK(array_type->IsArray());
    if (array_annotation_map == nullptr) {
      return nullptr;
    }
    GOOGLESQL_RET_CHECK(array_annotation_map->IsArrayMap());
    if (!array_annotation_map->IsTopLevelColumnAnnotationEmpty()) {
      return absl::InvalidArgumentError(
          "Input arrays to UNNEST cannot have annotations on the arrays "
          "themselves");
    }
    return array_annotation_map->AsStructMap()->field(0);
  }

  // Build a ResolvedArrayScan out of the `array_expr` as the `index`-th
  // argument in the original UNNEST.
  absl::StatusOr<std::unique_ptr<const ResolvedArrayScan>>
  BuildSingletonUnnestArrayScan(int index, State& state) {
    GOOGLESQL_RET_CHECK_GE(index, 0);
    GOOGLESQL_RET_CHECK_LT(index, state.element_column_count());

    // `array_expr` is a column reference to the `index`-th `arrN` expression in
    // the with expr.
    const ResolvedColumn& array_expr_column =
        state.with_expr_array_columns()[index];
    GOOGLESQL_ASSIGN_OR_RETURN(
        const AnnotationMap* element_annotation_map,
        GetElementAnnotationMap(array_expr_column.type(),
                                array_expr_column.type_annotation_map()));

    const ResolvedColumn array_element_col = column_factory_.MakeCol(
        "$array", absl::StrCat("arr", index),
        AnnotatedType(array_expr_column.type()->AsArray()->element_type(),
                      element_annotation_map));
    const ResolvedColumn array_position_col =
        column_factory_.MakeCol("$array_offset", "offset", types::Int64Type());
    ResolvedColumnList column_list = {array_element_col, array_position_col};

    // Populate local states for newly created array columns.
    state.SetElementColumn(index, array_element_col);
    state.SetArrayOffsetColumn(index, array_position_col);

    return ResolvedArrayScanBuilder()
        .set_column_list(column_list)
        .add_array_expr_list(MakeResolvedColumnRef(array_expr_column.type(),
                                                   array_expr_column,
                                                   /*is_correlated=*/true))
        .add_element_column_list(array_element_col)
        .set_array_offset_column(MakeResolvedColumnHolder(array_position_col))
        .Build();
  }

  // Build the `join_expr` for a ResolvedJoinScan:
  //   lhs_array_offset = rhs_array_offset
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> BuildJoinExprOfTwoOffsets(
      const ResolvedColumn& lhs_array_offset,
      const ResolvedColumn& rhs_array_offset) {
    return fn_builder_.Equal(
        MakeResolvedColumnRef(lhs_array_offset.type(), lhs_array_offset,
                              /*is_correlated=*/false),
        MakeResolvedColumnRef(rhs_array_offset.type(), rhs_array_offset,
                              /*is_correlated=*/false));
  }

  TypeFactory& type_factory_;
  ColumnFactory column_factory_;
  FunctionCallBuilder fn_builder_;
};

}  // namespace

class MultiwayUnnestRewriter : public Rewriter {
 public:
  std::string Name() const override { return "MultiwayUnnestRewriter"; }

  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, std::unique_ptr<const ResolvedNode> input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    GOOGLESQL_RET_CHECK(options.id_string_pool() != nullptr);
    GOOGLESQL_RET_CHECK(options.column_id_sequence_number() != nullptr);
    MultiwayUnnestRewriteVisitor rewriter(options, catalog, type_factory);
    return rewriter.VisitAll(std::move(input));
  };
};

const Rewriter* GetMultiwayUnnestRewriter() {
  static const auto* const kRewriter = new MultiwayUnnestRewriter;
  return kRewriter;
}

}  // namespace googlesql
