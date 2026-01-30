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

#ifndef GOOGLESQL_COMMON_MATCH_RECOGNIZE_ROW_EDGE_LIST_H_
#define GOOGLESQL_COMMON_MATCH_RECOGNIZE_ROW_EDGE_LIST_H_

#include <optional>
#include <string>
#include <vector>

#include "googlesql/common/match_recognize/compiled_nfa.h"
#include "googlesql/common/match_recognize/nfa.h"
#include "absl/base/nullability.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql::functions::match_recognize {
// Data structure which allows each edge in an NFA to be marked or unmarked for
// each row in a row sequence. Used to implement steps 4-6 in
// (broken link).
class RowEdgeList {
 public:
  // The edge_numbering provided must outlive this object.
  explicit RowEdgeList(const CompiledNFA* /*absl_nonnull*/ nfa) : nfa_(*nfa) {}

  // As copying can be expensive when dealing with large partitions, allow
  // objects of this class to be moved, but not copied.
  RowEdgeList(const RowEdgeList&) = delete;
  RowEdgeList& operator=(const RowEdgeList&) = delete;
  RowEdgeList(RowEdgeList&&) = default;

  // Returns the total number of rows we have an edge list for.
  int num_rows() const;

  // Returns the total number of edges in the graph. This is the number of bits
  // we keep track of for each row.
  int num_edges() const { return nfa_.num_edges(); }

  // Removes all rows from this data structure. The next row added will have
  // row number 0 again.
  void ClearRows();

  // Adds a row, with all edges unmarked. Returns the row number of the newly
  // added row.
  int AddRow();

  // Marks the given edge for the given row.
  void MarkEdge(int row_number, int edge_number);

  // Unmarks the given edge for the given row.
  void UnmarkEdge(int row_number, int edge_number);

  // Returns true if the given edge is marked for the given row.
  bool IsMarked(int row_number, int edge_number) const;

  // Returns a pointer to the highest-precedence marked edge whose source state
  // is `from_state`, or nullptr, if no marked edge from `from_state` exists.
  const Edge* /*absl_nullable*/ GetHighestPrecedenceMarkedEdge(
      int row_number, NFAState from_state) const;

  // Returns a string representation of all the edge details, along with which
  // edges are marked/unmarked for each row.
  std::string DebugString() const;

 private:
  // Returns the index into `bits_` associated with the given
  // row number/edge number combination.
  int GetBitIndex(int row_number, int edge_number) const;

  const CompiledNFA& nfa_;

  // One bit per row, per edge. Ordered by row number first, then edge number.
  std::vector<bool> bits_;
};
}  // namespace googlesql::functions::match_recognize
#endif  // GOOGLESQL_COMMON_MATCH_RECOGNIZE_ROW_EDGE_LIST_H_
