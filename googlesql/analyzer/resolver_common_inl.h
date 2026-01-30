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

#ifndef GOOGLESQL_ANALYZER_RESOLVER_COMMON_INL_H_
#define GOOGLESQL_ANALYZER_RESOLVER_COMMON_INL_H_

#include <memory>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
// This header includes the common macros used in the resolver*.cc files.
#include "googlesql/analyzer/resolver.h"
#include "googlesql/common/thread_stack.h"  
#include "googlesql/parser/parse_tree.h"
#include "googlesql/public/function.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/status/status.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

#ifndef __EMSCRIPTEN__
#define RETURN_ERROR_IF_OUT_OF_STACK_SPACE()                                   \
  GOOGLESQL_RETURN_IF_NOT_ENOUGH_STACK(                                        \
      "Out of stack space due to deeply nested query expression during query " \
      "resolution")
#else
#define RETURN_ERROR_IF_OUT_OF_STACK_SPACE()
#endif  // __EMSCRIPTEN__

template <class NODE_TYPE>
absl::Status Resolver::ResolveHintsForNode(const ASTHint* ast_hints,
                                           NODE_TYPE* resolved_node) {
  if (ast_hints != nullptr) {
    std::vector<std::unique_ptr<const ResolvedOption>> hints;
    GOOGLESQL_RETURN_IF_ERROR(ResolveHintAndAppend(ast_hints, &hints));

    for (auto& hint : hints) {
      resolved_node->add_hint_list(std::move(hint));
    }
  }
  return absl::OkStatus();
}

inline bool IsMeasureAggFunction(const Function* function) {
  return function->NumSignatures() == 1 &&
         function->signatures()[0].context_id() == FN_AGG &&
         function->IsGoogleSQLBuiltin();
}

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_RESOLVER_COMMON_INL_H_
