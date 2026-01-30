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

#ifndef GOOGLESQL_COMMON_MATCH_RECOGNIZE_EDGE_MATCHERS_H_
#define GOOGLESQL_COMMON_MATCH_RECOGNIZE_EDGE_MATCHERS_H_

#include <optional>
#include <ostream>
#include <string>

#include "googlesql/common/match_recognize/compiled_nfa.h"
#include "googlesql/common/match_recognize/nfa.h"
#include "gmock/gmock.h"
namespace googlesql::functions::match_recognize {

void PrintTo(const PatternVariableId& id, std::ostream* os);
void PrintTo(const Edge& edge, std::ostream* os);
std::string DescribeIsEdge(bool negation, int edge_number, NFAState from,
                           NFAState to,
                           std::optional<PatternVariableId> consumed,
                           bool head_anchor, bool tail_anchor);

MATCHER_P3(IsEdge, edge_number, from, to,
           DescribeIsEdge(negation, edge_number, from, to, std::nullopt,
                          /*head_anchor=*/false, /*tail_anchor=*/false)) {
  return arg.edge_number == edge_number && arg.from == from && arg.to == to &&
         !arg.is_head_anchored && !arg.is_tail_anchored;
}

MATCHER_P4(IsEdge, edge_number, from, to, consumed,
           DescribeIsEdge(negation, edge_number, from, to, consumed,
                          /*head_anchor=*/false, /*tail_anchor=*/false)) {
  return arg.edge_number == edge_number && arg.from == from && arg.to == to &&
         !arg.is_head_anchored && !arg.is_tail_anchored &&
         arg.consumed == std::optional<PatternVariableId>(consumed);
}

MATCHER_P3(IsHeadAnchoredEdge, edge_number, from, to,
           DescribeIsEdge(negation, edge_number, from, to, std::nullopt,
                          /*head_anchor=*/true, /*tail_anchor=*/false)) {
  return arg.edge_number == edge_number && arg.from == from && arg.to == to &&
         arg.is_head_anchored && !arg.is_tail_anchored;
}

MATCHER_P4(IsHeadAnchoredEdge, edge_number, from, to, consumed,
           DescribeIsEdge(negation, edge_number, from, to, consumed,
                          /*head_anchor=*/true, /*tail_anchor=*/false)) {
  return arg.edge_number == edge_number && arg.from == from && arg.to == to &&
         arg.is_head_anchored && !arg.is_tail_anchored &&
         arg.consumed == std::optional<PatternVariableId>(consumed);
}

MATCHER_P3(IsTailAnchoredEdge, edge_number, from, to,
           DescribeIsEdge(negation, edge_number, from, to, std::nullopt,
                          /*head_anchor=*/false, /*tail_anchor=*/true)) {
  return arg.edge_number == edge_number && arg.from == from && arg.to == to &&
         !arg.is_head_anchored && arg.is_tail_anchored;
}

MATCHER_P4(IsTailAnchoredEdge, edge_number, from, to, consumed,
           DescribeIsEdge(negation, edge_number, from, to, consumed,
                          /*head_anchor=*/false, /*tail_anchor=*/true)) {
  return arg.edge_number == edge_number && arg.from == from && arg.to == to &&
         !arg.is_head_anchored && arg.is_tail_anchored &&
         arg.consumed == std::optional<PatternVariableId>(consumed);
}

}  // namespace googlesql::functions::match_recognize
#endif  // GOOGLESQL_COMMON_MATCH_RECOGNIZE_EDGE_MATCHERS_H_
