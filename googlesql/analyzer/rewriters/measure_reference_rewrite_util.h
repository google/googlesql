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

#ifndef GOOGLESQL_ANALYZER_REWRITERS_MEASURE_REFERENCE_REWRITE_UTIL_H_
#define GOOGLESQL_ANALYZER_REWRITERS_MEASURE_REFERENCE_REWRITE_UTIL_H_

#include <memory>

#include "googlesql/analyzer/rewriters/measure_collector.h"
#include "googlesql/public/function.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/rewrite_utils.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace googlesql {

// Traverses `resolved_ast` and marks measures as AGG'ed in `measure_collector`
// if they are invoked in an AGG() call.
absl::Status MarkAggedMeasures(const ResolvedNode* resolved_ast,
                               MeasureCollector& measure_collector);

// Rewrites measure columns in the given resolved AST. Specifically, it
//
// - Replaces measure columns with the corresponding struct-typed closure
//   columns, which encapsulates the required information to evaluate
//   AGG(measure).
//
// - Rewrites AGG(measure) function calls into a multi-level aggregation
//   expression that computes the aggregate value and does proper grain locking.
absl::StatusOr<std::unique_ptr<const ResolvedNode>> RewriteMeasureColumns(
    std::unique_ptr<const ResolvedNode> resolved_ast,
    MeasureCollector& measure_collector, const Function* any_value_fn,
    FunctionCallBuilder& function_call_builder,
    const LanguageOptions& language_options, ColumnFactory& column_factory,
    TypeFactory& type_factory);

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_REWRITERS_MEASURE_REFERENCE_REWRITE_UTIL_H_
