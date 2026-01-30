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

#ifndef GOOGLESQL_ANALYZER_RECURSIVE_QUERIES_H_
#define GOOGLESQL_ANALYZER_RECURSIVE_QUERIES_H_

#include <vector>

#include "googlesql/parser/parse_tree.h"
#include "googlesql/public/id_string.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"

namespace googlesql {

// The result of SortWithEntries().
struct WithEntrySortResult {
  // Each entry in a WITH clause, sorted so with each entry depends on those
  // earlier in the list. When multiple such sort-orders exist, the chosen order
  // is implementation-dependent, but determininistic.
  //
  // This list determines the order that WITH entries resolve in, since
  // resolving a WITH entry requires that all of its dependencies already be
  // resolved.
  std::vector<const ASTAliasedQuery*> sorted_entries;

  // The subset of WITH entries in <sorted_entries> which reference themself at
  // least once.
  absl::flat_hash_set<const ASTAliasedQuery*> self_recursive_entries;
};

// Sorts the entries in <with_clause> in dependency order, so that each entry
// appears after all entries it depends on. Entries which directly reference
// themself are allowed; the sort result includes a set of such entries,
// allowing them to be easily identified. Dependency cycles of length >= 2
// result in an error.
absl::StatusOr<WithEntrySortResult> SortWithEntries(
    const ASTWithClause* with_clause);

// Returns true if the given CREATE RECURSIVE VIEW statement is actually
// recursive.
absl::StatusOr<bool> IsViewSelfRecursive(
    const ASTCreateViewStatementBase* stmt);

// Returns whether the given parser AST node `node` contains a reference to the
// table with the given `name`.
//
// For example, suppose NodeType is ASTQueryExpression, which corresponds to a
// named subquery, e.g. the subquery in
//
// WITH RECURSIVE t AS (
//   SELECT 1
//   UNION ALL
//   SELECT * FROM T
// )
// ...
//
// and the name of the subquery `name` is {"t"}. The function will return true
// because the subquery contains a reference to itself, i.e. the "t" in "(FROM
// t)".
//
// Note however, if the reference to "t" is shadowed by an inner WITH alias,
// e.g.
//
// (
//   SELECT 1
//   |> UNION ALL (WITH t AS (...) FROM t)
// )
//
// the function will return false because the reference to "t" is not a self
// reference (it is referencing to a different "t").
//
// `NodeType` denotes the AST node kind for `node`, and must derive from
// ASTNode.
template <typename NodeType>
absl::StatusOr<bool> ContainsReferenceToTable(
    const NodeType* node, const std::vector<IdString>& name);

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_RECURSIVE_QUERIES_H_
