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

#include "googlesql/parser/ast_node_util.h"

#include <algorithm>
#include <stack>

#include "googlesql/parser/parse_tree_visitor.h"
#include "googlesql/parser/visit_result.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {
class MaxTreeDepthVisitor : public NonRecursiveParseTreeVisitor {
 public:
  absl::StatusOr<VisitResult> defaultVisit(const ASTNode* node) override {
    return VisitResult::VisitChildren(
        node, [this, node]() { return ProcessChildren(node); });
  }

  // Returns the maximum depth of any tree node encountered.
  // After we are finished visiting, this is simply the only value on the stack,
  // which corresponds to the first node visited.
  absl::StatusOr<int> max_depth() const {
    GOOGLESQL_RET_CHECK_EQ(stack_.size(), 1);
    return stack_.top();
  }

 private:
  absl::Status ProcessChildren(const ASTNode* node) {
    // Each visit to a node leaves that node's maximum tree depth pushed on the
    // stack. So, pop all of the child nodes and calculate the max depth of the
    // current node, which is simply 1 + max child depth (or 1 if there are no
    // children).
    int max_child_depth = 0;
    for (int i = 0; i < node->num_children(); ++i) {
      GOOGLESQL_RET_CHECK(!stack_.empty());
      max_child_depth = std::max(max_child_depth, stack_.top());
      stack_.pop();
    }
    stack_.push(max_child_depth + 1);
    return absl::OkStatus();
  }

  std::stack<int> stack_;
};
}  // namespace

absl::StatusOr<int> GetMaxParseTreeDepth(const ASTNode* node) {
  MaxTreeDepthVisitor visitor;
  GOOGLESQL_RETURN_IF_ERROR(node->TraverseNonRecursive(&visitor));
  return visitor.max_depth();
}
}  // namespace googlesql
