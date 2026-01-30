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

#ifndef GOOGLESQL_COMMON_INTERNAL_PROPERTY_GRAPH_H_
#define GOOGLESQL_COMMON_INTERNAL_PROPERTY_GRAPH_H_

#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/base/check.h"

namespace googlesql {

class InternalPropertyGraph {
 public:
  InternalPropertyGraph() = delete;
  InternalPropertyGraph(const InternalPropertyGraph&) = delete;
  InternalPropertyGraph& operator=(const InternalPropertyGraph&) = delete;

  template <typename T>
  static void InternalSetResolvedExpr(T* obj,
                                      const ResolvedExpr* resolved_expr) {
    ABSL_CHECK(obj != nullptr) << "obj is nullptr";                      // Crash OK
    ABSL_CHECK(resolved_expr != nullptr) << "resolved_expr is nullptr";  // Crash OK
    obj->resolved_expr_ = resolved_expr;
  }
};

}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_INTERNAL_PROPERTY_GRAPH_H_
