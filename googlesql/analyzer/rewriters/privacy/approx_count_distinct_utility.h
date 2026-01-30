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

#ifndef GOOGLESQL_ANALYZER_REWRITERS_PRIVACY_APPROX_COUNT_DISTINCT_UTILITY_H_
#define GOOGLESQL_ANALYZER_REWRITERS_PRIVACY_APPROX_COUNT_DISTINCT_UTILITY_H_

#include <memory>

#include "googlesql/analyzer/query_resolver_helper.h"
#include "googlesql/analyzer/resolver.h"
#include "googlesql/proto/anon_output_with_report.pb.h"
#include "googlesql/proto/internal_error_location.pb.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/functions/differential_privacy.pb.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/base/check.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"

namespace googlesql {
namespace differential_privacy {
namespace approx_count_distinct_utility {

// Scans the aggregate list of `node` for `approx_count_distinct` calls. If a
// specific call is identified to count unique privacy ids, then that call is
// replaced by the following simpler call that counts distinct privacy ids:
// (DP_)SUM(1, constribution_bounds_per_group =>(0,1)).
absl::StatusOr<std::unique_ptr<ResolvedDifferentialPrivacyAggregateScan>>
ReplaceApproxCountDistinctOfPrivacyIdCallsBySimplerAggregations(
    std::unique_ptr<ResolvedDifferentialPrivacyAggregateScan> node,
    const ResolvedExpr* uid_expr, Resolver* resolver,
    TypeFactory* type_factory);

// Returns true if `aggregate_list` contains a DP approx_count_distinct call.
// False otherwise.
absl::StatusOr<bool> HasApproxCountDistinctAggregation(
    absl::Span<const std::unique_ptr<const ResolvedComputedColumnBase>>
        aggregate_list);

// Performs the rewrites specified in
// (broken link).
absl::StatusOr<std::unique_ptr<ResolvedScan>>
PerformApproxCountDistinctRewrites(
    std::unique_ptr<ResolvedDifferentialPrivacyAggregateScan> dp_scan,
    const ResolvedExpr* noisy_count_distinct_privacy_ids_expr,
    bool has_approx_count_distinct, Resolver* resolver,
    ColumnFactory* allocator, AnalyzerOptions* analyzer_options,
    Catalog* catalog, TypeFactory* type_factory);

// Performs the rewrites specified in
// (broken link).
absl::StatusOr<std::unique_ptr<ResolvedScan>>
PerformApproxCountDistinctRewrites(
    std::unique_ptr<ResolvedAnonymizedAggregateScan> dp_scan,
    const ResolvedExpr* noisy_count_distinct_privacy_ids_expr,
    bool has_approx_count_distinct, Resolver* resolver,
    ColumnFactory* allocator, AnalyzerOptions* analyzer_options,
    Catalog* catalog, TypeFactory* type_factory);

}  // namespace approx_count_distinct_utility
}  // namespace differential_privacy
}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_REWRITERS_PRIVACY_APPROX_COUNT_DISTINCT_UTILITY_H_
