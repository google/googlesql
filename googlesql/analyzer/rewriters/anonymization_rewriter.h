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

#ifndef GOOGLESQL_ANALYZER_REWRITERS_ANONYMIZATION_REWRITER_H_
#define GOOGLESQL_ANALYZER_REWRITERS_ANONYMIZATION_REWRITER_H_

#include <memory>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "absl/base/attributes.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"

namespace googlesql {

// Results from anonymization rewrite.
struct RewriteForAnonymizationOutput {
  using TableScanToAnonAggrScanMap =
      absl::flat_hash_map<const ResolvedTableScan*,
                          const ResolvedAnonymizedAggregateScan*>;

  using TableScanToDPAggrScanMap =
      absl::flat_hash_map<const ResolvedTableScan*,
                          const ResolvedDifferentialPrivacyAggregateScan*>;
  // Rewritten input handling anonymization per below.
  std::unique_ptr<const ResolvedNode> node;
  // A map from ResolvedTableScan to ResolvedAnonymizedAggregateScan.
  TableScanToAnonAggrScanMap table_scan_to_anon_aggr_scan_map;
  TableScanToDPAggrScanMap table_scan_to_dp_aggr_scan_map;
};

// Given an AST statement resolved by the analyzer, transform the AST to reflect
// the semantics defined in (broken link). Also does
// additional validation, for example enforcing that only anonymized aggregation
// functions are used.
// TODO: perform all validation in the initial analyzer call
//
// Resolving an AST containing an anonymization clause with the differential
// privacy semantic rewrites in one pass is difficult. Instead, the analyzer
// returns a naively resolved AST containing a ResolvedAnonymizedAggregateScan
// that either requires special handling to correctly execute, or must be passed
// through this rewriter to apply the differential privacy semantic rewrites
// before executing.
//
// Calls to RewriteForAnonymization are not idempotent. Don't parse and rewrite
// a query, unparse with SqlBuilder, and parse and rewrite again.
// TODO: add a state enum to ResolvedAnonymizedAggregateScan to
// track rewrite status
//
// The 'max_column_id' should be passed from the AnalyzerOutput that generated
// the ResolvedStatement to avoid reusing column ids.
//
// Does not take ownership of 'query', 'catalog', or, 'type_factory'.
ABSL_DEPRECATED("Use RewriteResolvedAst() instead")
absl::StatusOr<RewriteForAnonymizationOutput> RewriteForAnonymization(
    const ResolvedNode& query, Catalog* catalog, TypeFactory* type_factory,
    const AnalyzerOptions& analyzer_options, ColumnFactory& column_factory);

// Returns a pointer to the anonymization rewriter.
const Rewriter* GetAnonymizationRewriter();

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_REWRITERS_ANONYMIZATION_REWRITER_H_
