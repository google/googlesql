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

#ifndef GOOGLESQL_ANALYZER_COLUMN_CYCLE_DETECTOR_H_
#define GOOGLESQL_ANALYZER_COLUMN_CYCLE_DETECTOR_H_

#include <optional>
#include <vector>

#include "googlesql/public/id_string.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"

namespace googlesql {

class ASTNode;

// This class is used to detect cycles between columns. For example, the
// following table definition is invalid:
//
// CREATE TABLE T(
//   a as b,
//   b as c,
//   c as a
// );
// The above table is invalid because there is a cycle, which is a->b->c
//                                                               ^-----|
//
// Use the following pseudo code for checking cycles in the above example:
//
// ColumnCycleDetector cd(...);
//
// GOOGLESQL_RETURN_IF_ERROR(cd->VisitNewColumn("a"));
// auto finish_a =
//     absl::MakeCleanup([&cd] { GOOGLESQL_CHECK_OK(cd.FinishCurrentColumn()); });
// GOOGLESQL_RETURN_IF_ERROR(cd.AddDependencyOn("b"));
//
// GOOGLESQL_RETURN_IF_ERROR(cd->VisitNewColumn("b"));
// auto finish_b =
//     absl::MakeCleanup([&cd] { GOOGLESQL_CHECK_OK(cd.FinishCurrentColumn()); });
// GOOGLESQL_RETURN_IF_ERROR(cd.AddDependencyOn("c"));
//
// GOOGLESQL_RETURN_IF_ERROR(cd->VisitNewColumn("c"));
// auto finish_c =
//     absl::MakeCleanup([&cd] { GOOGLESQL_CHECK_OK(cd.FinishCurrentColumn()); });
// GOOGLESQL_RETURN_IF_ERROR(cd.AddDependencyOn("a")); // <--- This will fail.
//
// PS. This cycle detector gets instantated for every generated column, but
// redundant column expression resolution is avoided since previously resolved
// column definitions are cached and re-used for subsequent column resolution
// steps.
class ColumnCycleDetector {
 public:
  explicit ColumnCycleDetector(const ASTNode* ast_node) : ast_node_(ast_node) {}

  // Delete copy constructor and assignment operator
  ColumnCycleDetector(const ColumnCycleDetector&) = delete;
  ColumnCycleDetector& operator=(const ColumnCycleDetector&) = delete;

  ~ColumnCycleDetector();

  // Visits 'column'. RET_CHECKs if this method has already been called with the
  // same 'column'. Also requires that 'AddDependencyOn' has been called to
  // reflect that the current column has a dependency on 'column'.
  absl::Status VisitNewColumn(const IdString& column);

  // Finishes visiting the current column and resets the current column to the
  // previously visited column (if there is one). It is an error to call this
  // method more times than VisitNewColumn() has been called. Each call to
  // VisitNewColumn() should be matched by a call to FinishCurrentColumn(). If
  // you miss calling FinishCurrentColumn() it will hit a ABSL_DCHECK on the
  // destructor.
  absl::Status FinishCurrentColumn();

  // Attempts to add a dependency for 'current_column()' on 'column',
  // returning an error if that introduces a cycle.
  absl::Status AddDependencyOn(const IdString& column);

 private:
  // Tracks the current column of the depth-first search. This contains the last
  // column passed to VisitNewColumn() or empty (if never called).
  std::optional<IdString> current_column() const;

  // ASTNode that defines the column definition. It's used for creating error
  // with context during the AddDependencyOn() call.
  const ASTNode* ast_node_;

  // Edges of the graph. Required so that we account for an edge only once.
  // For example CREATE TABLE t (a AS ADD(b, b));
  // We want to add the edge a->b only once here.
  absl::flat_hash_map<IdString, absl::flat_hash_set<IdString, IdStringHash>,
                      IdStringHash>
      edges_;

  // Set of columns being visited.
  absl::flat_hash_set<IdString, IdStringHash> visiting_;
  // Stack of elements being visited. Contains the same elements as 'visiting_'.
  std::vector<IdString> visiting_stack_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_COLUMN_CYCLE_DETECTOR_H_
