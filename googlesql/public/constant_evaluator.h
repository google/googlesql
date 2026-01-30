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

#ifndef GOOGLESQL_PUBLIC_CONSTANT_EVALUATOR_H_
#define GOOGLESQL_PUBLIC_CONSTANT_EVALUATOR_H_

#include "googlesql/common/constant_utils.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/status/statusor.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {

// Interface for engine callbacks that evaluate a constant expression in a
// statement of a GoogleSQL module.
class ConstantEvaluator {
 public:
  virtual ~ConstantEvaluator() = default;

  // Evaluates `constant_expression` and returns the resulting value.
  virtual absl::StatusOr<Value> Evaluate(
      const ResolvedExpr& constant_expression) = 0;

  // Evaluates `constant_expression` and returns the resulting value.
  // REQUIRES: `constant_expression` is an analysis time constant.
  absl::StatusOr<Value> EvaluateAnalysisConstant(
      const ResolvedExpr& constant_expression) {
    GOOGLESQL_RET_CHECK(IsAnalysisConstant(&constant_expression))
        << "Expression is not an analysis time constant: "
        << constant_expression.DebugString();
    return Evaluate(constant_expression);
  }
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_CONSTANT_EVALUATOR_H_
