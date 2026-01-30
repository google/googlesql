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

#ifndef GOOGLESQL_COMMON_CONSTANT_UTILS_H_
#define GOOGLESQL_COMMON_CONSTANT_UTILS_H_

#include "googlesql/resolved_ast/resolved_node.h"

namespace googlesql {

// Returns true if `node` is an expression that is constant during analysis
// time.
bool IsAnalysisConstant(const ResolvedNode* node);

// Returns true if `node` is an expression that is constant during planning
// time. Value is guaranteed to be immutable. The expression is constant across
// all executions of a prepared statement.
bool IsImmutableConstant(const ResolvedNode* node);

// Returns true if `node` is an expression whose value is guaranteed to be
// stable. In other words, the expression is constant within one invocation of a
// prepared statement.
bool IsStableConstant(const ResolvedNode* node);

// Returns true if `node` is an expression that is constant within the local
// query context (i.e., its constness level is at most QUERY_CONST).
bool IsQueryConstant(const ResolvedNode* node);

}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_CONSTANT_UTILS_H_
