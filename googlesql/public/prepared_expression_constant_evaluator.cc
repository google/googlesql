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

#include "googlesql/public/prepared_expression_constant_evaluator.h"

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/evaluator.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/status/statusor.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

absl::StatusOr<Value> PreparedExpressionConstantEvaluator::Evaluate(
    const ResolvedExpr& constant_expression) {
  PreparedExpression expr(&constant_expression, eval_options_);
  AnalyzerOptions analyzer_options(language_options_);
  GOOGLESQL_RETURN_IF_ERROR(expr.Prepare(analyzer_options, /*catalog=*/nullptr));
  return expr.Execute();
}

}  // namespace googlesql
