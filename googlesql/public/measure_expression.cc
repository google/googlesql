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

#include "googlesql/public/measure_expression.h"

#include <memory>

#include "googlesql/common/measure_analysis_utils.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace googlesql {

absl::StatusOr<const ResolvedExpr*> AnalyzeMeasureExpression(
    absl::string_view measure_expr, const Table& table, Catalog& catalog,
    TypeFactory& type_factory, AnalyzerOptions analyzer_options,
    std::unique_ptr<const AnalyzerOutput>& analyzer_output) {
  return AnalyzeMeasureExpressionInternal(measure_expr, table, catalog,
                                          type_factory, analyzer_options,
                                          analyzer_output);
}

}  // namespace googlesql
