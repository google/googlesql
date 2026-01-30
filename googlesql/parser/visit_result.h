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

#ifndef GOOGLESQL_PARSER_VISIT_RESULT_H_
#define GOOGLESQL_PARSER_VISIT_RESULT_H_

#include <functional>

#include "absl/status/statusor.h"
#include "googlesql/base/status.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

class ASTNode;
class VisitResult {
 public:
  // Indicates that no new visit actions should be performed.
  static VisitResult Empty() { return VisitResult(nullptr, nullptr, false); }

  // Indicates that the children of <node> should be visited next.
  static VisitResult VisitChildren(const ASTNode* node) {
    return VisitResult(node, nullptr, false);
  }

  // Indicates that the children of <node> should be visited next; then,
  // <continuation> should be invoked.
  static VisitResult VisitChildren(const ASTNode* node,
                                   std::function<absl::Status()> continuation) {
    return VisitResult(node, continuation, false);
  }

  // Indicates that the traversal should terminate immediately, without invoking
  // any queued visitors.  Unlike returning a failed status, the status of the
  // overall visit operation is still Ok.
  static VisitResult Terminate() { return VisitResult(nullptr, nullptr, true); }

  VisitResult() : VisitResult(nullptr, nullptr, false) {}
  VisitResult(const VisitResult&) = default;
  VisitResult& operator=(const VisitResult&) = default;
  VisitResult(VisitResult&&) = default;

  // Node whose children to visit; nullptr if no child visits are needed.
  const ASTNode* node_for_child_visit() const { return node_; }

  // Action to perform after all children are visited; nullptr if not needed.
  std::function<absl::Status()> continuation() const { return continuation_; }

  // Returns true if the traversal should terminate.
  bool should_terminate() const { return terminate_; }

 private:
  VisitResult(const ASTNode* node, std::function<absl::Status()> continuation,
              bool terminate)
      : node_(node), continuation_(continuation), terminate_(terminate) {}

  // Node to visit the children of, null to not perform any more visits.
  // Children are visited depth-first in in the order they appear.
  const ASTNode* node_ = nullptr;

  // Function to be invoked after all children have been visited.  nullptr to
  // skip.
  std::function<absl::Status()> continuation_;

  // True to terminate the visit operation immediately, without invoking the
  // visitor for child nodes already queued.
  bool terminate_ = false;
};
}  // namespace googlesql

#endif  // GOOGLESQL_PARSER_VISIT_RESULT_H_
