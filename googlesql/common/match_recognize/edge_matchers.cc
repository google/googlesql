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

#include "googlesql/common/match_recognize/edge_matchers.h"

#include <optional>
#include <ostream>
#include <string>

#include "googlesql/common/match_recognize/compiled_nfa.h"
#include "googlesql/common/match_recognize/nfa.h"
#include "absl/strings/str_cat.h"

namespace googlesql::functions::match_recognize {
using ::testing::PrintToString;

void PrintTo(const PatternVariableId& id, std::ostream* os) {
  *os << "V" << id.value();
}

void PrintTo(const Edge& edge, std::ostream* os) {
  *os << "A ";
  if (edge.is_head_anchored) {
    *os << "head-anchored";
  } else if (edge.is_tail_anchored) {
    *os << "tail-anchored edge";
  } else {
    *os << "non-anchored edge";
  }

  *os << " numbered " << edge.edge_number << " from " << absl::StrCat(edge.from)
      << " to " << absl::StrCat(edge.to);
  if (edge.consumed.has_value()) {
    *os << " consuming " << PrintToString(*edge.consumed);
  }
}

std::string DescribeIsEdge(bool negation, int edge_number, NFAState from,
                           NFAState to,
                           std::optional<PatternVariableId> consumed,
                           bool head_anchor, bool tail_anchor) {
  std::string result;
  absl::StrAppend(&result, "is");
  if (negation) {
    absl::StrAppend(&result, " not");
  }
  if (head_anchor) {
    absl::StrAppend(&result, " a head-anchored edge");
  } else if (tail_anchor) {
    absl::StrAppend(&result, " a tail-anchored edge");
  } else {
    absl::StrAppend(&result, " a non-anchored edge");
  }
  absl::StrAppend(&result, " numbered ", edge_number, " from ", from, " to ",
                  to);
  if (consumed.has_value()) {
    absl::StrAppend(&result, " consuming ", PrintToString(*consumed));
  }
  return result;
}
}  // namespace googlesql::functions::match_recognize
