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

#ifndef GOOGLESQL_ANALYZER_REWRITERS_MEASURE_SOURCE_SCAN_REWRITE_UTIL_H_
#define GOOGLESQL_ANALYZER_REWRITERS_MEASURE_SOURCE_SCAN_REWRITE_UTIL_H_

#include <memory>

#include "googlesql/analyzer/rewriters/measure_collector.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "absl/status/statusor.h"

namespace googlesql {

// Rewrites the given resolved_ast as follows:
//
// For each scan `s` that defines an AGG'ed measure column `mi`, replace it with
// a ProjectScan whose
// - input_scan = s,
// - expr_list: contains a struct ResolvedComputedColumn representing the
//   closure information of all AGG'ed measure column definitions on this scan.
// - column_list: contains every column from `s.column_list()` except the
//   AGG'ed measure columns, plus the struct column. Note that the column list
//   of `s` may be modified to include additional columns needed to compute the
//   closure struct column.

absl::StatusOr<std::unique_ptr<const ResolvedNode>> AddClosures(
    MeasureCollector& measure_collector,
    std::unique_ptr<const ResolvedNode> resolved_ast, TypeFactory& type_factory,
    ColumnFactory& column_factory);

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_REWRITERS_MEASURE_SOURCE_SCAN_REWRITE_UTIL_H_
