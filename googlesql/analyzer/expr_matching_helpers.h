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

#ifndef GOOGLESQL_ANALYZER_EXPR_MATCHING_HELPERS_H_
#define GOOGLESQL_ANALYZER_EXPR_MATCHING_HELPERS_H_

#include <stddef.h>

#include "googlesql/analyzer/name_scope.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/language_options.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "absl/base/attributes.h"
#include "absl/status/statusor.h"

namespace googlesql {

// An enum that represents the result of testing whether two expressions are
// same.
enum class TestIsSameExpressionForGroupByResult {
  // Two expressions are considered equal.
  kEqual,
  // Two expressions are considered different.
  kNotEqual,
  // It's unknown whether two expressions are equal or not. For now, this only
  // happens when the expression kind is not supported for comparison, but is
  // the same, and expression output type is the same.
  kUnknown,
};

// Checks whether two expressions are equal for the purpose of allowing
// SELECT expr FROM ... GROUP BY expr, and group by expression deduplication.
// This is a shorthand of TestIsSameExpressionForGroupBy function to return a
// bool result instead. The function treats the testing result kUnknown value as
// if the two expressions are not equal.
// `language_options` is used to determine whether certain AST nodes are
// supported for comparison.
absl::StatusOr<bool> IsSameExpressionForGroupBy(
    const ResolvedExpr* expr1, const ResolvedExpr* expr2,
    const LanguageOptions& language_options);

// DEPRECATED: Same as above, but uses empty language options.
// TODO: b/406852830 - Remove this function once spanner is migrated to pass
// LanguageOptions into the IsSameExpressionForGroupBy function.
ABSL_DEPRECATED(
    "Use the IsSameExpressionForGroupBy function taking language options.")
absl::StatusOr<bool> IsSameExpressionForGroupBy(const ResolvedExpr* expr1,
                                                const ResolvedExpr* expr2);

// Checks whether two expressions are equal for the purpose of allowing
// SELECT expr FROM ... GROUP BY expr. This is also used by group-by
// expressions deduplication.
// Comparison is done by traversing the ResolvedExpr tree and making sure all
// the nodes are the same, except that volatile functions (i.e. RAND()) are
// never considered equal.
// The comparison result will be equal or not-equal if two expressions are
// completely checked. If some nodes or properties are not listed in the switch
// cases and not checked, the result will be unknown. Function callers can
// handle the unknown cases separately from the known-false cases.
// <language_options> is used to determine whether certain AST nodes are
// supported for comparison.
absl::StatusOr<TestIsSameExpressionForGroupByResult>
TestIsSameExpressionForGroupBy(const ResolvedExpr* expr1,
                               const ResolvedExpr* expr2,
                               const LanguageOptions& language_options);

// DEPRECATED: Same as above, but uses empty language options.
// TODO: b/406852830 - Remove this function once spanner is migrated to pass
// LanguageOptions into the TestIsSameExpressionForGroupBy function.
ABSL_DEPRECATED(
    "Use the TestIsSameExpressionForGroupBy function taking language options.")
absl::StatusOr<TestIsSameExpressionForGroupByResult>
TestIsSameExpressionForGroupBy(const ResolvedExpr* expr1,
                               const ResolvedExpr* expr2);

// Checks whether the expression references any non-local and non-correlated
// column.
absl::StatusOr<bool> ExprReferencesNonCorrelatedColumn(
    const ResolvedExpr& expr);

// Returns true if all path expressions in `expr`, subject to the caveats that
// follow, have a prefix in `expected_prefixes`. The only path expressions we
// consider in `expr` are those that start with non-correlated column references
// that are not bound in `expr`, and only contain simple proto or simple struct
// field accesses.
//
// `expr`: The expression in which we are looking for path expressions.
// `expected_prefixes`: Contains the expected prefixes we are looking for in
//     `expr` as field name sequences.
// `id_string_pool`: Used to allocate IdStrings that are used internally.
absl::StatusOr<bool> AllPathsInExprHaveExpectedPrefixes(
    const ResolvedExpr& expr, const ValidFieldInfoMap& expected_prefixes,
    IdStringPool* id_string_pool);

// Analyzes an expression, and if it is logically a path expression (of
// one or more names) then returns true, along with the `source_column`
// where the path expression starts and a `name_path` that identifies the path
// name list along with the `target_column` that the entire path expression
// resolves to.
// If the expression is not a path expression then sets `source_column`
// to be uninitialized and returns false.
// `id_string_pool` is used to allocate newly created id strings for
// `name_path`.
bool GetSourceColumnAndNamePath(const ResolvedExpr* resolved_expr,
                                ResolvedColumn target_column,
                                ResolvedColumn* source_column,
                                bool* is_correlated, ValidNamePath* name_path,
                                IdStringPool* id_string_pool);

// Analyzes an expression, and if it is logically a path expression (of
// one or more names) starting from an argument reference then:
//
// - Return a pointer to the argument reference and
// - Populates `name_path` with the path name list and the `target_column` that
//   the entire path expression resolves to.
//
// Otherwise, return a nullptr.
const ResolvedArgumentRef* GetSourceArgumentRefAndNamePath(
    const ResolvedExpr* resolved_expr, ResolvedColumn target_column,
    ValidNamePath* name_path, IdStringPool* id_string_pool);

// Detect if the node contains an array path that works with the singleton table
// array path implicit UNNEST feature.
// Returns true when the ColumnRef is found and set the `column_id` to the found
// column. Otherwise, returns false.
bool ContainsTableArrayNamePathWithFreeColumnRef(const ResolvedExpr* node,
                                                 int* column_id);

// Hashing function for field paths, which enables faster set lookups and
// insertions.
size_t FieldPathHash(const ResolvedExpr* expr);

enum class FieldPathMatchingOption {
  kExpression,
  kFieldPath,
  kUpdateItemElement
};

// This function determines whether <field_path1> and <field_path2> are
// generalized path expressions that point to the same field.
//
// If the FieldPathMatchingOption::kExpression option is specified, this
// function returns true if <field_path1> and <field_path2> are interchangeable
// generalized path expressions. This considers specialized field accesses
// (currently, the only such case is PROTO_DEFAULT_IF_NULL) as well as ensures
// the descriptors for any proto types involved come from the same descriptor
// pool. This option guarantees <field_path1> and <field_path2> evaluate to the
// same result.
//
// If the FieldPathMatchingOption::kFieldPath option is specified, this function
// returns true if <field_path1> and <field_path2> read the same field. This
// option does consider whether the has_bit of the field is being accessed by
// both <field_path1> and <field_path2>. However, it does not consider any
// specialized field accesses. Therefore, this option does not guarantee
// <field_path1> and <field_path2> evaluate to the same result.
//
// If the FieldPathMatchingOption::kUpdateItemElement option is specified this
// function will return true if <field_path1> and <field_path2> represent the
// same field path up until the root `RESOLVED_COLUMN_REF` which may differ.
// This accounts for the special case of an multiple paths in an UPDATE ... SET
// statement where field paths share the same literal in a subscript.
bool IsSameFieldPath(const ResolvedExpr* field_path1,
                     const ResolvedExpr* field_path2,
                     FieldPathMatchingOption match_option);

// Field path hashing operator for containers.
struct FieldPathHashOperator {
  size_t operator()(const ResolvedExpr* expr) const {
    return FieldPathHash(expr);
  }
};

// Field path expression equality operator for containers.
struct FieldPathExpressionEqualsOperator {
  bool operator()(const ResolvedExpr* expr1, const ResolvedExpr* expr2) const {
    return IsSameFieldPath(expr1, expr2, FieldPathMatchingOption::kExpression);
  }
};

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_EXPR_MATCHING_HELPERS_H_
