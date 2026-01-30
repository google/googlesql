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

#ifndef GOOGLESQL_PUBLIC_MEASURE_EXPRESSION_H_
#define GOOGLESQL_PUBLIC_MEASURE_EXPRESSION_H_

#include <memory>
#include <vector>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace googlesql {

// Analyzes `measure_expr` against non-measure columns of `table` and returns
// the resolved expression.
//
// `measure_expr` is an aggregate expression that defines the measure; see
// measure_expr.test for examples of valid measure expressions.
//
// Pre-conditions:
//   - `table` must not have any duplicate column names.
//   - `analyzer_options` must not have any expression columns or a lookup
//     expression callback.
//
// Post-conditions:
//   - `analyzer_output` will be populated with the resolved expression; the
//     caller must ensure that `analyzer_output` outlives the returned
//     ResolvedExpr.
absl::StatusOr<const ResolvedExpr*> AnalyzeMeasureExpression(
    absl::string_view measure_expr, const Table& table, Catalog& catalog,
    TypeFactory& type_factory, AnalyzerOptions analyzer_options,
    std::unique_ptr<const AnalyzerOutput>& analyzer_output);

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_MEASURE_EXPRESSION_H_
