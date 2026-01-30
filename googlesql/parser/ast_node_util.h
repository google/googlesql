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

#ifndef GOOGLESQL_PARSER_AST_NODE_UTIL_H_
#define GOOGLESQL_PARSER_AST_NODE_UTIL_H_

#include "googlesql/parser/ast_node.h"
#include "absl/status/statusor.h"

namespace googlesql {
// Returns the maximum depth of the parse tree from <node> to any leaf,
// inclusive.
//
// Example:
// BinaryExpression(+) [0-11]      => GetMaxParseTreeDepth() returns 3
//   IntLiteral(1) [0-1]           => GetMaxParseTreeDepth() returns 1
//   BinaryExpression(+) [5-10]    => GetMaxParseTreeDepth() returns 2
//     IntLiteral(2) [5-6]         => GetMaxParseTreeDepth() returns 1
//     IntLiteral(3) [9-10]        => GetMaxParseTreeDepth() returns 1
//
// This function is non-recursive and safe to call in small-stack environments.
// It can be used to prepare for recursive algorithms that might overflow the
// stack later, by either rejecting the query or increasing the stack size.
//
// Note: Returns failed status only in unexpected catastrophic error.
absl::StatusOr<int> GetMaxParseTreeDepth(const ASTNode* node);
}  // namespace googlesql

#endif  // GOOGLESQL_PARSER_AST_NODE_UTIL_H_
