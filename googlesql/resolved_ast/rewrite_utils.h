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

#ifndef GOOGLESQL_RESOLVED_AST_REWRITE_UTILS_H_
#define GOOGLESQL_RESOLVED_AST_REWRITE_UTILS_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "googlesql/analyzer/annotation_propagator.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/coercer.h"
#include "googlesql/public/function.h"
#include "googlesql/public/functions/differential_privacy.pb.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_rewrite_visitor.h"
#include "googlesql/resolved_ast/resolved_ast_visitor.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

// Returns a copy of 'expr' where all references to columns that are not
// internal to 'expr' as correlated. This is useful when moving a scalar
// expression into a new subquery expression.
template <class T>
absl::StatusOr<std::unique_ptr<T>> CorrelateColumnRefs(const T& expr) {
  GOOGLESQL_ASSIGN_OR_RETURN(auto correlated, CorrelateColumnRefsImpl(expr));
  GOOGLESQL_RET_CHECK(correlated->template Is<T>());
  return absl::WrapUnique(correlated.release()->template GetAs<T>());
}

// Type-erased implementation details of CorrelateColumnRefs template.
absl::StatusOr<std::unique_ptr<ResolvedExpr>> CorrelateColumnRefsImpl(
    const ResolvedExpr& expr);

// Fills column_refs with a copy of all ResolvedColumnRef nodes under 'node'
// which are not below a subquery.
//
// If `correlate` is true, the column refs are correlated regardless of whether
// or not they are in the original node tree.
absl::Status CollectColumnRefs(
    const ResolvedNode& node,
    std::vector<std::unique_ptr<const ResolvedColumnRef>>* column_refs,
    bool correlate = false);

// Removes column refs from `column_refs` that are not used in `node`.
// Uses `CollectColumnRefs` for collecting used column references in `node`.
absl::Status RemoveUnusedColumnRefs(
    const ResolvedNode& node,
    std::vector<std::unique_ptr<const ResolvedColumnRef>>& column_refs);

// Sorts and removes duplicates from the ResolvedColumnRefs in 'column_refs'.
// This is used in conjunction with 'CollectColumnRefs' to construct an
// appropriate parameter list for a subquery expression. Among other potential
// uses.
void SortUniqueColumnRefs(
    std::vector<std::unique_ptr<const ResolvedColumnRef>>& column_refs);

// Helper that composes 'CollectColumnRefs' and 'SortUniqueColumnRefs'
absl::Status CollectSortUniqueColumnRefs(
    const ResolvedNode& node,
    std::vector<std::unique_ptr<const ResolvedColumnRef>>& column_refs,
    bool correlate = false);

// A map to keep track of columns that are replaced during an application of
// 'CopyResolvedAstAndRemapColumns'
using ColumnReplacementMap =
    absl::flat_hash_map</*column_in_input=*/ResolvedColumn,
                        /*column_in_output=*/ResolvedColumn>;

// Performs a deep copy of 'input_tree' replacing all of the ResolvedColumns in
// that tree either with ResolvedColumns as specified by 'column_map' or by new
// columns allocated from 'column_factory' for any column not found in
// 'column_map'.
//
// 'column_map' is both an input and output parameter. As an input parameter,
//     it allows invoking code to specify explicit replacements for certain
//     columns in 'input_tree'. As an output parameter, it returns to invoking
//     code all the columns allocated from 'column_factory' during the copy.
template <class T>
absl::StatusOr<std::unique_ptr<T>> CopyResolvedASTAndRemapColumns(
    const T& input_tree, ColumnFactory& column_factory,
    ColumnReplacementMap& column_map) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ResolvedNode> ret,
                   CopyResolvedASTAndRemapColumnsImpl(
                       input_tree, column_factory, column_map));
  GOOGLESQL_RET_CHECK(ret->Is<T>());
  return absl::WrapUnique(ret.release()->GetAs<T>());
}

// Implementation of the above template.
absl::StatusOr<std::unique_ptr<ResolvedNode>>
CopyResolvedASTAndRemapColumnsImpl(const ResolvedNode& input_tree,
                                   ColumnFactory& column_factory,
                                   ColumnReplacementMap& column_map);

// Performs a shallow copy of `input_tree` replacing all of the ResolvedColumns
// in that tree either with ResolvedColumns as specified by `column_map` or by
// new columns allocated from `column_factory` for any column not found in
// `column_map`.
//
// `column_map` is both an input and output parameter. As an input parameter,
//     it allows invoking code to specify explicit replacements for certain
//     columns in `input_tree`. As an output parameter, it returns to invoking
//     code all the columns allocated from `column_factory` during the copy.
template <class T>
absl::StatusOr<std::unique_ptr<const T>> RemapAllColumns(
    std::unique_ptr<const T> input_tree, ColumnFactory& column_factory,
    ColumnReplacementMap& column_map) {
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedNode> ret,
      RemapColumnsImpl(std::move(input_tree), &column_factory, column_map));
  GOOGLESQL_RET_CHECK(ret->Is<T>());
  return absl::WrapUnique(ret.release()->GetAs<T>());
}

// Performs a shallow copy of `input_tree` replacing all of the ResolvedColumns
// in `input_tree` with ResolvedColumns as specified by `column_map`. For
// columns not found in `column_map`, they remain the same.
//
// `column_map` allows invoking code to specify explicit replacements for
//     certain columns in `input_tree`.
template <class T>
absl::StatusOr<std::unique_ptr<const T>> RemapSpecifiedColumns(
    std::unique_ptr<const T> input_tree, ColumnReplacementMap& column_map) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedNode> ret,
                   RemapColumnsImpl(std::move(input_tree),
                                    /*column_factory=*/nullptr, column_map));
  GOOGLESQL_RET_CHECK(ret->Is<T>());
  return absl::WrapUnique(ret.release()->GetAs<T>());
}

// Implementation of in-place column remapping.
absl::StatusOr<std::unique_ptr<const ResolvedNode>> RemapColumnsImpl(
    std::unique_ptr<const ResolvedNode> input_tree,
    ColumnFactory* column_factory, ColumnReplacementMap& column_map);

// Returns a new column list based on `column_list` replaced with new column ids
// if specified by the column replacement map.
ResolvedColumnList RemapColumnList(const ResolvedColumnList& column_list,
                                   ColumnReplacementMap& column_map);

// Helper function used when deep copying a plan. Takes a 'scan' and
// replaces all of its ResolvedColumns, including in child scans recursively.
// Some columns produced by the 'scan' are remapped to new columns based on
// 'target_column_indices' and 'replacement_columns_to_use'. All other columns
// in the 'scan' and its descendants are replaced by new columns allocated by
// 'column_factory'.
//
// 'target_column_indices' corresponds 1:1 with 'replacement_columns_to_use',
// and maps entries in the 'scan' 'column_list()' to the appropriate
// replacement columns.
//
// Columns in 'replacement_columns_to_use' must have been allocated from
// 'column_factory'.
//
// Ultimately, the copied/returned plan will have all column references
// allocated by 'column_factory', either through the explicit remapping or via
// new allocations.
absl::StatusOr<std::unique_ptr<ResolvedScan>> ReplaceScanColumns(
    ColumnFactory& column_factory, const ResolvedScan& scan,
    absl::Span<const int> target_column_indices,
    absl::Span<const ResolvedColumn> replacement_columns_to_use);

// Creates a new set of replacement columns to the given list.
// Useful when replacing columns for a ResolvedExecuteAsRole node.
std::vector<ResolvedColumn> CreateReplacementColumns(
    ColumnFactory& column_factory,
    absl::Span<const ResolvedColumn> column_list);

// Helper for rewriters to check whether a needed built-in function is part
// of the catalog. This is useful to generate good error messages when a
// needed function is not found.
absl::StatusOr<bool> CatalogSupportsBuiltinFunction(
    absl::string_view function_name, const AnalyzerOptions& analyzer_options,
    Catalog& catalog);

// Helper to check that engines support the required IFERROR and NULLIFERROR
// functions that are used to implement SAFE mode in rewriters. If the required
// built-in function signatures are located, this returns ok. If they are not
// found, or found but not built-in, then this returns kUnimplemented. If the
// catalog returns any error code other than kNotFound that error is returned to
// the caller.
absl::Status CheckCatalogSupportsSafeMode(
    absl::string_view function_name, const AnalyzerOptions& analyzer_options,
    Catalog& catalog);

// Checks whether the ResolvedAST has ResolvedGroupingCall nodes.
absl::StatusOr<bool> HasGroupingCallNode(const ResolvedNode* node);

// Contains helper functions that reduce boilerplate in rewriting rules logic
// related to constructing new ResolvedFunctionCall instances.
// TODO: Move FunctionCallBuilder class from rewriter utils
// to a separate utility. FunctionCallBuilder is fairly generic to be used at
// other places - especially in unit tests to create resolved function nodes.
class FunctionCallBuilder {
 public:
  FunctionCallBuilder(const AnalyzerOptions& analyzer_options, Catalog& catalog,
                      TypeFactory& type_factory)
      : analyzer_options_(analyzer_options),
        catalog_(catalog),
        type_factory_(type_factory),
        annotation_propagator_(
            AnnotationPropagator(analyzer_options, type_factory)),
        coercer_(&type_factory, &analyzer_options.language(), &catalog) {}

  // Helper to check that engines support the required IFERROR and NULLIFERROR
  // functions that are used to implement SAFE mode in rewriters. If the
  // required built-in function signatures are located, this returns ok. If they
  // are not found, or found but not built-in, then this returns kUnimplemented.
  // If the catalog returns any error code other than kNotFound that error is
  // returned to the caller.
  absl::Status CheckCatalogSupportsSafeMode(absl::string_view fn_name) {
    return googlesql::CheckCatalogSupportsSafeMode(fn_name, analyzer_options_,
                                                   catalog_);
  }

  AnnotationPropagator& annotation_propagator() {
    return annotation_propagator_;
  }

  // Construct ResolvedFunctionCall for IF(<condition>, <then_case>,
  // <else_case>)
  //
  // Requires: condition is a bool returning expression and then_case and
  //           else_case return equal types.
  //
  // The signature for the built-in function "IF" must be available in <catalog>
  // or an error status is returned.
  absl::StatusOr<std::unique_ptr<ResolvedFunctionCall>> If(
      std::unique_ptr<const ResolvedExpr> condition,
      std::unique_ptr<const ResolvedExpr> then_case,
      std::unique_ptr<const ResolvedExpr> else_case);

  // Construct ResolvedFunctionCall for <arg> IS NULL
  //
  // The signature for the built-in function "$is_null" must be available in
  // <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<ResolvedFunctionCall>> IsNull(
      std::unique_ptr<const ResolvedExpr> arg);

  // Construct ResolvedFunctionCall for <arg> IS NOT NULL
  //
  // The signature for the built-in functions "$is_null" and "$not"must be
  // available in <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> IsNotNull(
      std::unique_ptr<const ResolvedExpr> arg);

  // Construct a ResolvedFunctionCall for arg[0] IS NULL OR arg[1] IS NULL OR ..
  //
  // Like `IsNull`, a built-in function "$is_null" must be available.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> AnyIsNull(
      std::vector<std::unique_ptr<const ResolvedExpr>> args);

  // Construct a ResolvedFunctionCall for IFERROR(try_expr, handle_expr)
  //
  // Requires: try_expr and handle_expr must return equal types.
  //
  // The signature for the built-in function "IFERROR" must be available in
  // <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> IfError(
      std::unique_ptr<const ResolvedExpr> try_expr,
      std::unique_ptr<const ResolvedExpr> handle_expr);

  // Construct a ResolvedFunctionCall for ERROR(error_text).
  //
  // The signature for the built-in function "error" must be available in
  // <catalog> or an error status is returned.
  // If `target_type` is supplied, set the return type of ERROR function to
  // `target_type` if needed.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> Error(
      const std::string& error_text, const Type* target_type = nullptr);

  // Construct a ResolvedFunctionCall for ERROR(error_expr).
  //
  // Requires: error_expr has STRING type.
  //
  // The signature for the built-in function "error" must be available in
  // <catalog> or an error status is returned.
  // If `target_type` is supplied, set the return type of ERROR function to
  // `target_type` if needed.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> Error(
      std::unique_ptr<const ResolvedExpr> error_expr,
      const Type* target_type = nullptr);

  // Constructs a ResolvedFunctionCall for the $make_array function to create an
  // array for a list of elements. If `cast_elements_if_needed` is true, the
  // elements will be coerced to the element type of the array.
  //
  // Requires: If `cast_elements_if_needed` is false, each element in `elements`
  // must have the same type as `element_type`.
  //
  // The signature for the built-in function "$make_array" must be available in
  // <catalog> or an error status is returned
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> MakeArray(
      const Type* element_type,
      std::vector<std::unique_ptr<const ResolvedExpr>> elements,
      bool cast_elements_if_needed = false);

  // Constructs a ResolvedFunctionCall for ARRAY_FIRST_N function which
  // returns first `n` elements from `array`.
  //
  // Requires:
  // - `array` is ARRAY<T>.
  // - `n` is INT64.
  //
  // The signature for the built-in function "array_first_n" must be
  // available in <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> ArrayFirstN(
      std::unique_ptr<const ResolvedExpr> array,
      std::unique_ptr<const ResolvedExpr> n);

  // Constructs a ResolvedFunctionCall for ARRAY_CONCAT(arrays...)
  //
  // Requires: Each array must have the same type. There must be at least one
  // array.
  //
  // The signature for the built-in function "array_concat" must be available in
  // <catalog> or an error status is returned
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> ArrayConcat(
      std::vector<std::unique_ptr<const ResolvedExpr>> arrays);

  // Constructs a ResolvedFunctionCall for <input> LIKE <pattern>
  //
  // Requires: <input> and <pattern> must have STRING or BYTES and their types
  // must match
  //
  // The signature for the built-in function "$like" must be available in
  // <catalog> or an error status is returned
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> Like(
      std::unique_ptr<ResolvedExpr> input,
      std::unique_ptr<ResolvedExpr> pattern);

  // Constructs the following expression:
  //   CASE
  //     WHEN <conditions[0]> THEN <results[0]>
  //     WHEN <conditions[1]> THEN <results[1]>
  //     ...
  //   ELSE
  //     <else_result>
  //   END;
  //
  // Requires:
  //  - <conditions> and <results> cannot be empty and must be the same length.
  //  - Elements of <conditions> and <results> must not be nullptr.
  //  - Elements of <conditions> must have type BOOL.
  //  - Elements of <results> must have the same type.
  //  - If <else_result> is nullptr, the constructed CASE expression will have
  //      no ELSE clause. Otherwise, <else_result> must have the same type as
  //      elements in <result>.
  //  - The signature for the built-in function "$case_no_value" must be
  //      available in <catalog>.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> CaseNoValue(
      std::vector<std::unique_ptr<const ResolvedExpr>> conditions,
      std::vector<std::unique_ptr<const ResolvedExpr>> results,
      std::unique_ptr<const ResolvedExpr> else_result);

  // Constructs a ResolvedFunctionCall for NOT <expression>
  //
  // Requires: The type of <expression> is a BOOL
  //
  // The signature for the built-in function "$not" must be available in
  // <catalog> or an error status is returned
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> Not(
      std::unique_ptr<const ResolvedExpr> expression);

  // Construct a ResolvedFunctionCall for <left_expr> = <right_expr>.
  //
  // Requires: <left_expr> and <right_expr> must return equal types AND
  //           the type supports equality.
  //
  // The signature for the built-in function "$equal" must be available in
  // <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> Equal(
      std::unique_ptr<const ResolvedExpr> left_expr,
      std::unique_ptr<const ResolvedExpr> right_expr);

  // Construct a ResolvedFunctionCall for <left_expr> != <right_expr>.
  //
  // Requires: <left_expr> and <right_expr> must return equal types AND
  //           the type supports equality.
  //
  // The signature for the built-in function "$not_equal" must be available in
  // <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> NotEqual(
      std::unique_ptr<const ResolvedExpr> left_expr,
      std::unique_ptr<const ResolvedExpr> right_expr);

  // Construct a ResolvedFunctionCall for LEAST(REPEATED <expressions>).
  //
  // Requires: All elements in <expressions> must have the same type which
  //           supports ordering.
  //
  // The signature for the built-in function "least" must be available in
  // <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> Least(
      std::vector<std::unique_ptr<const ResolvedExpr>> expressions);

  // Construct a ResolvedFunctionCall for GREATEST(REPEATED <expressions>).
  //
  // Requires: All elements in <expressions> must have the same type which
  //           supports ordering.
  //
  // The signature for the built-in function "greatest" must be available in
  // <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> Greatest(
      std::vector<std::unique_ptr<const ResolvedExpr>> expressions);

  // Construct a ResolvedFunctionCall for COALESCE(REPEATED <expressions>).
  //
  // Requires: Elements in <expressions> must have types which are implicitly
  //           coercible to a common supertype.
  //
  // The signature for the built-in function "coalesce" must be available in
  // <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> Coalesce(
      std::vector<std::unique_ptr<const ResolvedExpr>> expressions);

  // Construct a ResolvedFunctionCall for <left_expr> < <right_expr>.
  //
  // Requires: <left_expr> and <right_expr> must return equal types AND
  //           the type supports comparison (aka. ordering).
  //
  // The signature for the built-in function "$less" must be available in
  // <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> Less(
      std::unique_ptr<const ResolvedExpr> left_expr,
      std::unique_ptr<const ResolvedExpr> right_expr);
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> LessOrEqual(
      std::unique_ptr<const ResolvedExpr> left_expr,
      std::unique_ptr<const ResolvedExpr> right_expr);

  // Construct a ResolvedFunctionCall for <left_expr> >= <right_expr>.
  //
  // Requires: Both `left_expr` and `right_expr` must be order-able types. The
  // types of `left_expr` and `right_expr` can be different if the exact
  // signature is available in the `catalog`.
  //
  // The signature for the built-in function "$greater_or_equal" must be
  // available in `catalog` or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> GreaterOrEqual(
      std::unique_ptr<const ResolvedExpr> left_expr,
      std::unique_ptr<const ResolvedExpr> right_expr);
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> Greater(
      std::unique_ptr<const ResolvedExpr> left_expr,
      std::unique_ptr<const ResolvedExpr> right_expr);

  // Construct a ResolvedFunctionCall for `a` * `b`.
  //
  // Requires `a` is of type Int64.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
  Int64MultiplyByLiteral(std::unique_ptr<const ResolvedExpr> a, int b);

  // Construct a ResolvedFunctionCall for `a` + `b`.
  // Requires `a` is of type Int64.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> Int64AddLiteral(
      std::unique_ptr<const ResolvedExpr> a, int b);

  // Construct a ResolvedFunctionCall for `minuend` - `subtrahend`.
  //
  // Requires: `minuend` and `subtrahend` must be of types compatible with one
  // of the function signatures of the built-in function "$subtract" present in
  // the `catalog`.
  //
  // The signature for the built-in function "$subtract" must be available in
  // `catalog` or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> Subtract(
      std::unique_ptr<const ResolvedExpr> minuend,
      std::unique_ptr<const ResolvedExpr> subtrahend);

  // Construct a ResolvedFunctionCall for `minuend` - `subtrahend` such that
  // if there is in overflow, then the result will be NULL.
  //
  // Requires: `minuend` and `subtrahend` must be of types compatible with one
  // of the function signatures of the built-in function "safe_subtract"
  // present in the `catalog`.
  //
  // The signature for the built-in function "safe_subtract" must be available
  // in `catalog` or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> SafeSubtract(
      std::unique_ptr<const ResolvedExpr> minuend,
      std::unique_ptr<const ResolvedExpr> subtrahend);

  // Construct a ResolvedFunctionCall for
  //  expressions[0] AND expressions[1] AND ... AND expressions[N-1]
  // where N is the number of expressions.
  //
  // Requires: N >= 2 and all expressions return BOOL.
  //
  // The signature for the built-in function "$and" must be available in
  // <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> And(
      std::vector<std::unique_ptr<const ResolvedExpr>> expressions);

  // Construct a ResolvedFunctionCall for
  //  expressions[0] OR expressions[1] OR ... OR expressions[N-1]
  // where N is the number of expressions.
  //
  // Requires: N >= 2 and all expressions return BOOL.
  //
  // The signature for the built-in function "$or" must be available in
  // <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> Or(
      std::vector<std::unique_ptr<const ResolvedExpr>> expressions);

  // Construct a ResolvedFunctionCall which is a nested series of binary
  // addition:
  //  ((expressions[0] ADD expressions[1]) ADD ... ADD expressions[N-1])
  // where N is the number of expressions.
  //
  // Requires: N >= 2 and all expressions return INT64.
  //
  // The signature for the built-in function "$add" must be available in
  // <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
  NestedBinaryInt64Add(
      std::vector<std::unique_ptr<const ResolvedExpr>> expressions);

  // Construct a ResolvedFunctionCall which is a nested series of binary
  // addition:
  //  ((expressions[0] ADD expressions[1]) ADD ... ADD expressions[N-1])
  // where N is the number of expressions.
  //
  // Requires: N >= 2 and all expressions return the same numerical type.
  // Supported numerical types are: INT64, UINT64, DOUBLE, NUMERIC, BIGNUMERIC.
  //
  // The signature for the built-in function "$add" must be available in
  // <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> NestedBinaryAdd(
      const Type* type,
      std::vector<std::unique_ptr<const ResolvedExpr>> expressions);

  // Construct a ResolvedAnalyticFunctionCall for ROW_NUMBER()
  absl::StatusOr<std::unique_ptr<const ResolvedAnalyticFunctionCall>>
  RowNumber();

  // Construct a ResolvedFunctionCall for
  //  <node_expr> IS SOURCE|DESTINATION OF <edge_expr>
  // based on <is_source_node_predicate>.
  //
  // Requires: node_expr is of GraphElementType(node) AND
  //           edge_expr is of GraphElementType(edge)
  //
  // The signature for the built-in function "$is_source_node" or
  // "$is_dest_node" must be available in <catalog> or an error status is
  // returned.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> IsNodeEndpoint(
      std::unique_ptr<const ResolvedExpr> node_expr,
      std::unique_ptr<const ResolvedExpr> edge_expr,
      bool is_source_node_predicate);

  // Construct a ResolvedFunctionCall for the conjunction of
  // $equal(expressions[0], x) for x in expressions[1:].
  //
  // Requires: N >= 2 and all expressions return GraphElementType.
  //
  // The signature for the built-in function "$equal" must be available in
  // <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
  AreEqualGraphElements(
      std::vector<std::unique_ptr<const ResolvedExpr>> element_expressions);

  // Construct a ResolvedFunctionCall for ARRAY_LENGTH(array_expr).
  //
  // Requires: array_expr is of ARRAY<T> type.
  //
  // The signature for the built-in function "array_length" must be available in
  // <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> ArrayLength(
      std::unique_ptr<const ResolvedExpr> array_expr);

  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> ArrayIsDistinct(
      std::unique_ptr<const ResolvedExpr> array_expr);

  // Constructs a ResolvedFunctionCall for ARRAY[OFFSET(offset_expr)].
  //
  // Requires:
  // - `array_expr` is ARRAY.
  // - `offset_expr` is INT64.
  //
  // The signature for the built-in function "$array_at_offset" must be
  // available in <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> ArrayAtOffset(
      std::unique_ptr<const ResolvedExpr> array_expr,
      std::unique_ptr<const ResolvedExpr> offset_expr);

  // Constructs a ResolvedFunctionCall for
  // ARRAY_SLICE(array_expr, start_offset_expr, end_offset_expr).
  //
  // Requires:
  // - `array_expr` is ARRAY.
  // - `start_offset_expr` is INT64.
  // - `end_offset_expr` is INT64.
  //
  // The signature for the built-in function "array_slice" must be
  // available in <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> ArraySlice(
      std::unique_ptr<const ResolvedExpr> array_expr,
      std::unique_ptr<const ResolvedExpr> start_offset_expr,
      std::unique_ptr<const ResolvedExpr> end_offset_expr);

  // Constructs a ResolvedFunctionCall for ARRAY_TO_STRING(array_expr,
  // delimiter_expr).
  //
  // Requires:
  // - `array_expr` is ARRAY of STRING or BYTES.
  // - `delimiter_expr` is STRING or BYTES.
  // - `array_expr`'s element type and `delimiter_expr` have the same SQL type.
  //
  // The signature for the built-in function "array_to_string" must be
  // available in <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> ArrayToString(
      std::unique_ptr<const ResolvedExpr> array_expr,
      std::unique_ptr<const ResolvedExpr> delimiter_expr);

  // Constructs a ResolvedFunctionCall for the MOD(dividend, divisor).
  //
  // Requires: `dividend_expr` and `divisor_expr` must be of the same type and
  // are of one of the following types: [INT64, UINT64, NUMERIC, BIGNUMERIC].
  //
  // The signature for the built-in function "mod" must be available in
  // <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> Mod(
      std::unique_ptr<const ResolvedExpr> dividend_expr,
      std::unique_ptr<const ResolvedExpr> divisor_expr);

  // Construct a ResolvedAggregateFunctionCall for COUNT(expression) which has
  // the option to be a distinct count.
  //
  // The signature for the built-in function "count" must be available in
  // <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedAggregateFunctionCall>> Count(
      std::unique_ptr<const ResolvedExpr> expr, bool is_distinct = false);

  // Constructs a ResolvedAggregateFunctionCall or ResolvedAnalyticFunctionCall
  // for COUNT(*), and has the option to be COUNT(DISTINCT).
  // The analytic option sets the frame to be UNBOUNDED PRECEDING and
  // UNBOUNDED FOLLOWING. The caller can modify the frame as needed.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> CountStar(
      bool is_distinct = false, bool is_analytic = false);

  // Constructs a ResolvedFunctionCall for $with_side_effects(expr, payload).
  //
  // Requires: `payload` must be a bytes column.
  //
  // The signature for the built-in function "$with_side_effects" must be
  // available in <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> WithSideEffects(
      std::unique_ptr<const ResolvedExpr> expr,
      std::unique_ptr<const ResolvedExpr> payload);

  // Constructs a ResolvedFunctionCall for `any_value(input_expr [,
  // having_min_modifier(MIN, having_min_max_expr)])`. `having_min_modifier` is
  // optional, created only when `having_min_max_expr` is not nullptr.
  //
  // The signature for the built-in function "any_value" must be
  // available in <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedAggregateFunctionCall>> AnyValue(
      std::unique_ptr<const ResolvedExpr> input_expr,
      std::unique_ptr<const ResolvedExpr> having_min_max_expr,
      bool is_max = false);

  // Constructs a ResolvedAggregateFunctionCall for
  // `ARRAY_AGG(input_expr [HAVING having_kind having_expr])`.
  // If `having_expr` is nullptr, no HAVING clause is added.
  //
  // Requires:
  // - `input_expr` has non-ARRAY type.
  // - `having_expr` has type that supports ordering.
  //
  // The signature for the built-in function "array_agg" must be
  // available in <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedAggregateFunctionCall>> ArrayAgg(
      std::unique_ptr<const ResolvedExpr> input_expr,
      std::unique_ptr<const ResolvedExpr> having_expr,
      ResolvedAggregateHavingModifier::HavingModifierKind having_kind =
          ResolvedAggregateHavingModifier::INVALID);

  // Constructs a ResolvedFunctionCall for `CONCAT(input_expr[, input_expr,
  // ...])`.
  //
  // `elements` must have at least one element, and all elements must have the
  // same SQL type STRING OR BYTES.
  //
  // The signature for the built-in function "CONCAT" must be available in
  // `catalog_` or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> Concat(
      std::vector<std::unique_ptr<const ResolvedExpr>> elements);

  // Constructs an expression that generates null if an array is empty,
  // otherwise returns the array.
  //
  // Generates:
  //   WITH($result AS <array_expr>,
  //       IF(ARRAY_LENGTH($result) >= 1, $result, NULL))
  //
  // Requires: `array_expr` is of ARRAY type.
  //
  // The signature for the built-in function "array_length", "not", and
  // "$greater_or_equal" must be available in `catalog_` or an error status is
  // returned.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> MakeNullIfEmptyArray(
      ColumnFactory& column_factory,
      std::unique_ptr<const ResolvedExpr> array_expr);

  // Constructs a ResolvedFunctionCall for $unchecked_path(components...) where
  // the returned path has type `path_type`.
  //
  // Requires:
  // - `components` is a list of GraphElementType that can form a path of type
  //   `path_type`: starting and ending with a node; alternating between nodes
  //   and edges.
  // - `components` must have an odd number of elements.
  //
  // The signature for the built-in function "$unchecked_path" must be
  // available in <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> UncheckedPathCreate(
      const GraphPathType* path_type,
      std::vector<std::unique_ptr<const ResolvedExpr>> components);

  // Constructs a ResolvedFunctionCall for $unchecked_path_concat(paths...)
  // where the returned path has type `path_type`.
  //
  // Requires:
  // - `paths` is a list of GraphPathType each of which is coercible to
  //   `path_type`.
  //
  // The signature for the built-in function "$unchecked_path_concat" must be
  // available in <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> UncheckedPathConcat(
      const GraphPathType* path_type,
      std::vector<std::unique_ptr<const ResolvedExpr>> paths);

  // Constructs a ResolvedFunctionCall for PATH_FIRST(path).
  //
  // Requires:
  // - `path` is of GraphPathType.
  //
  // The signature for the built-in function "path_first" must be
  // available in <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> PathFirst(
      std::unique_ptr<const ResolvedExpr> path);

  // Constructs a ResolvedFunctionCall for PATH_LAST(path).
  //
  // Requires:
  // - `path` is of GraphPathType.
  //
  // The signature for the built-in function "path_last" must be
  // available in <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> PathLast(
      std::unique_ptr<const ResolvedExpr> path);

  // Constructs a ResolvedFunctionCall for NODES(path).
  //
  // Requires:
  // - `path` is of GraphPathType.
  //
  // The signature for the built-in function "nodes" must be
  // available in <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> PathNodes(
      /*absl_nonnull*/ std::unique_ptr<const ResolvedExpr> path);

  // Constructs a ResolvedFunctionCall for EDGES(path).
  //
  // Requires:
  // - `path` is of GraphPathType.
  //
  // The signature for the built-in function "edges" must be
  // available in <catalog> or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> PathEdges(
      std::unique_ptr<const ResolvedExpr> path);

  // Constructs a ResolvedFunctionCall for
  //  `extract_for_dp_approx_count_distinct(partital_merge_result,
  //  noisy_count_distinct_perivacy_ids_expr) --> report_format`.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
  ExtractForDpApproxCountDistinct(
      std::unique_ptr<const ResolvedColumnRef> partital_merge_result,
      std::unique_ptr<const ResolvedExpr> noisy_count_distinct_privacy_ids_expr,
      std::optional<functions::DifferentialPrivacyEnums::ReportFormat>
          report_format);

  // Constructs a ResolvedLiteral for 0 with the given type.
  // Requires that `type` is a numerical type.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> CreateTypedLiteralZero(
      const Type* type);

  // Constructs a ResolvedAnalyticFunctionCall that applies the window function
  // IS_FIRST(k) to the input expression.
  //
  // Requires:
  // - `arg_k` is a literal or parameter of type INT64.
  //
  // The signature for the built-in function `IS_FIRST` must be
  // available in `catalog` or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedAnalyticFunctionCall>> IsFirstK(
      /*absl_nonnull*/ std::unique_ptr<const ResolvedExpr> arg_k);

  // Constructs a ResolvedFunctionCall for
  // `<left_expr> IS NOT DISTINCT FROM <right_expr>`.
  //
  // Requires: Both `left_expr` and `right_expr` must have the same type.
  //
  // The signature for the built-in function "$is_not_distinct_from" must be
  // available in `catalog` or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> IsNotDistinctFrom(
      std::unique_ptr<const ResolvedExpr> left_expr,
      std::unique_ptr<const ResolvedExpr> right_expr);

  // Constructs a ResolvedAggregateFunctionCall for
  // `HLL_COUNT.INIT(expr)`.
  //
  // Requires: `expr` must have a type that supports HLL_COUNT.INIT.
  //
  // The signature for the built-in function "HLL_COUNT.INIT" must be
  // available in `catalog` or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedAggregateFunctionCall>> HllInit(
      std::unique_ptr<const ResolvedExpr> expr);

  // Constructs a ResolvedAggregateFunctionCall for
  // `HLL_COUNT.MERGE_PARTIAL(expr)`.
  //
  // The signature for the built-in function "HLL_COUNT.MERGE_PARTIAL" must be
  // available in `catalog` or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedAggregateFunctionCall>>
  HllMergePartial(std::unique_ptr<const ResolvedExpr> expr);

  // Constructs a ResolvedAggregateFunctionCall for
  // `HLL_COUNT.MERGE(expr)`.
  //
  // The signature for the built-in function "HLL_COUNT.MERGE" must be
  // available in `catalog` or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedAggregateFunctionCall>> HllMerge(
      std::unique_ptr<const ResolvedExpr> expr);

  // Constructs a ResolvedFunctionCall for
  // `HLL_COUNT.EXTRACT(expr)`.
  //
  // The signature for the built-in function "HLL_COUNT.EXTRACT" must be
  // available in `catalog` or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> HllExtract(
      std::unique_ptr<const ResolvedExpr> expr);

  absl::Status PropagateAnnotationsAndProcessCollationList(
      ResolvedFunctionCallBase* fn_call);

 private:
  static AnnotationPropagator BuildAnnotationPropagator(
      const AnalyzerOptions& analyzer_options, TypeFactory& type_factory) {
    return AnnotationPropagator(analyzer_options, type_factory);
  }

  // Construct a ResolvedFunctionCall for
  //  expressions[0] OP expressions[1] OP ... OP expressions[N-1]
  // where N is the number of expressions.
  //
  // Requires: N >= 2 AND all expressions return `expr_type` AND
  //           the nary logic function returns `expr_type`.
  //
  // The signature for the built-in function `op_catalog_name` must be available
  // in `catalog` or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> NaryLogic(
      absl::string_view op_catalog_name, FunctionSignatureId op_function_id,
      std::vector<std::unique_ptr<const ResolvedExpr>> expressions,
      const Type* expr_type);

  // Construct a ResolvedFunctionCall which is a nested series of binary
  // operations:
  //  ((expressions[0] OP expressions[1]) OP ... OP expressions[N-1])
  // where N is the number of expressions.
  //
  // Requires: N >= 2 AND all expressions return `expr_type` AND
  //           the nary logic function returns `expr_type`.
  //
  // The signature for the built-in function `op_catalog_name` must be available
  // in `catalog` or an error status is returned.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> NestedBinaryOp(
      absl::string_view op_catalog_name, FunctionSignatureId op_function_id,
      std::vector<std::unique_ptr<const ResolvedExpr>> expressions,
      const Type* expr_type);

  // Construct a ResolvedFunctionCall of
  //  builtin_function_name(REPEATED <expressions>)
  // whose arguments have the same type and supports ordering.
  // The function argument types for the args and the result type have an
  // original argument kind ANY_1.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>>
  FunctionCallWithSameTypeArgumentsSupportingOrdering(
      std::vector<std::unique_ptr<const ResolvedExpr>> expressions,
      absl::string_view builtin_function_name);

  // Helper that controls the error message when built-in functions are not
  // found in the catalog.
  absl::Status GetBuiltinFunctionFromCatalog(absl::string_view function_name,
                                             const Function** fn_out);

  // Returns a built-in function from the catalog, given its name path.
  // Overwrite the error message if the function is not found.
  absl::Status GetBuiltinFunctionFromCatalog(
      absl::Span<const std::string> function_path, const Function** fn_out);

  // Shared logic for Less and LessOrEqual.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> Less(
      std::unique_ptr<const ResolvedExpr> left_expr,
      std::unique_ptr<const ResolvedExpr> right_expr, bool or_equal);

  // Shared logic for Greater and GreaterOrEqual.
  absl::StatusOr<std::unique_ptr<const ResolvedFunctionCall>> Greater(
      std::unique_ptr<const ResolvedExpr> left_expr,
      std::unique_ptr<const ResolvedExpr> right_expr, bool or_equal);

  const AnalyzerOptions& analyzer_options_;
  Catalog& catalog_;

  TypeFactory& type_factory_;
  AnnotationPropagator annotation_propagator_;
  Coercer coercer_;
};

bool IsBuiltInFunctionIdEq(const ResolvedFunctionCallBase* function_call,
                           FunctionSignatureId function_signature_id);

// Generate an Unimplemented error message - if possible, attach a location.
// Note, Rewriters uniquely need this ability, the resolver generally
// has access to parser ASTNode objects, which more reliably have a
// location.
googlesql_base::StatusBuilder MakeUnimplementedErrorAtNode(const ResolvedNode* node);

// Returns a set of correlated referenced columns associated with `node`.
// This is used since we don't want to rewrite
// correlated columns that could be accessed outside of the node to rewrite.
//
// Note that `node` itself might contain inner subquery and correlated
// references for that inner subquery: those columns should NOT be added into
// `column_set` because they're internal columns to `node`.
absl::StatusOr<absl::flat_hash_set<ResolvedColumn>> GetCorrelatedColumnSet(
    const ResolvedNode& node);

// Checks each expression in the `expressions` list for correlated column
// references that link to a scope outside of that expression. This check is
// performed by leveraging googlesql::GetCorrelatedColumnSet for each
// expression.
// If any expression contains such an external correlation, this function
// returns a user-facing 'Unimplemented' error, including the location of the
// `tvf_node`. Otherwise, it returns OkStatus.
// See the comment for googlesql::GetCorrelatedColumnSet for details on how
// external correlations are distinguished from internal ones (like those within
// nested subqueries).
absl::Status ValidateArgumentsDoNotContainCorrelation(
    const ResolvedTVFScan* tvf_node, absl::string_view function_name,
    absl::Span<const ResolvedExpr* /*absl_nonnull*/ const> expressions);

// Wrapper to help build a ResolvedColumnRef for the given column.
std::unique_ptr<ResolvedColumnRef> BuildResolvedColumnRef(
    const ResolvedColumn& column);

// Get the maximum ResolvedColumn id from the given node.
absl::StatusOr<int> GetMaxColumnId(const ResolvedNode* node);

// Visitor that can be used to detect if a ResolvedAST contains a ResolvedColumn
// or a ResolvedExpressionColumn.
//
// Example usage:
//   ContainsResolvedColumnVisitor visitor;
//   auto visitor_output = visitor.VisitAll(std::move(node));
//   const bool contains_resolved_column = visitor.ContainsResolvedColumn();
//   const bool contains_resolved_expression_column =
//       visitor.ContainsResolvedExpressionColumn();
class ContainsResolvedColumnVisitor : public ResolvedASTRewriteVisitor {
 public:
  ContainsResolvedColumnVisitor() = default;
  // Neither copyable nor movable.
  ContainsResolvedColumnVisitor(const ContainsResolvedColumnVisitor&) = delete;
  ContainsResolvedColumnVisitor& operator=(
      const ContainsResolvedColumnVisitor&) = delete;

  bool ContainsResolvedColumn() const { return contains_resolved_column_; }

  bool ContainsResolvedExpressionColumn() const {
    return contains_resolved_expression_column_;
  }

 private:
  absl::Status PreVisitResolvedColumn(
      const googlesql::ResolvedColumn&) override {
    contains_resolved_column_ = true;
    return absl::OkStatus();
  }

  absl::Status PreVisitResolvedExpressionColumn(
      const googlesql::ResolvedExpressionColumn&) override {
    contains_resolved_expression_column_ = true;
    return absl::OkStatus();
  }

  bool contains_resolved_column_ = false;
  bool contains_resolved_expression_column_ = false;
};

}  // namespace googlesql

// Visitor to detect if a node contains WithScan.
class WithScanVisitor : public googlesql::ResolvedASTVisitor {
 public:
  static absl::StatusOr<bool> ContainsWithScan(
      const googlesql::ResolvedNode& node) {
    WithScanVisitor visitor;
    GOOGLESQL_RETURN_IF_ERROR(node.Accept(&visitor));
    return visitor.contains_with_scan_;
  }

 private:
  absl::Status VisitResolvedWithScan(
      const googlesql::ResolvedWithScan* node) override {
    contains_with_scan_ = true;
    return absl::OkStatus();
  }

  bool contains_with_scan_ = false;
};

#endif  // GOOGLESQL_RESOLVED_AST_REWRITE_UTILS_H_
