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

#include "googlesql/analyzer/all_rewriters.h"

#include "googlesql/analyzer/rewriters/aggregation_threshold_rewriter.h"
#include "googlesql/analyzer/rewriters/anonymization_rewriter.h"
#include "googlesql/analyzer/rewriters/builtin_function_inliner.h"
#include "googlesql/analyzer/rewriters/flatten_rewriter.h"
#include "googlesql/analyzer/rewriters/generalized_query_stmt_rewriter.h"
#include "googlesql/analyzer/rewriters/grouping_set_rewriter.h"
#include "googlesql/analyzer/rewriters/insert_dml_values_rewriter.h"
#include "googlesql/analyzer/rewriters/is_first_is_last_function_rewriter.h"
#include "googlesql/analyzer/rewriters/like_any_all_rewriter.h"
#include "googlesql/analyzer/rewriters/map_function_rewriter.h"
#include "googlesql/analyzer/rewriters/match_recognize_function_rewriter.h"
#include "googlesql/analyzer/rewriters/measure_type_rewriter.h"
#include "googlesql/analyzer/rewriters/multiway_unnest_rewriter.h"
#include "googlesql/analyzer/rewriters/nulliferror_function_rewriter.h"
#include "googlesql/analyzer/rewriters/order_by_and_limit_in_aggregate_rewriter.h"
#include "googlesql/analyzer/rewriters/pipe_assert_rewriter.h"
#include "googlesql/analyzer/rewriters/pipe_describe_rewriter.h"
#include "googlesql/analyzer/rewriters/pipe_if_rewriter.h"
#include "googlesql/analyzer/rewriters/pivot_rewriter.h"
#include "googlesql/analyzer/rewriters/registration.h"
#include "googlesql/analyzer/rewriters/row_type_rewriter.h"
#include "googlesql/analyzer/rewriters/sql_function_inliner.h"
#include "googlesql/analyzer/rewriters/sql_view_inliner.h"
#include "googlesql/analyzer/rewriters/subpipeline_stmt_rewriter.h"
#include "googlesql/analyzer/rewriters/typeof_function_rewriter.h"
#include "googlesql/analyzer/rewriters/unpivot_rewriter.h"
#include "googlesql/analyzer/rewriters/update_constructor_rewriter.h"
#include "googlesql/analyzer/rewriters/variadic_function_signature_expander.h"
#include "googlesql/analyzer/rewriters/with_expr_rewriter.h"
#include "googlesql/public/options.pb.h"
#include "absl/base/call_once.h"

namespace googlesql {

void RegisterBuiltinRewriters() {
  static absl::once_flag once_flag;
  absl::call_once(once_flag, [] {
    RewriteRegistry& r = RewriteRegistry::global_instance();

    // Rewriters are run in the order that they are registered. After all
    // rewriters have run, the resulting ResolvedAST is checked to see if more
    // rewrites are applicable. If there are, the rewriters are run again in the
    // order they are registered. The rewriters are applied until there is no
    // applicable rewrite.

    // This is a top-level statement transformation affecting only the
    // outermost nodes, so run it first.
    r.Register(ResolvedASTRewrite::REWRITE_SUBPIPELINE_STMT,
               GetSubpipelineStmtRewriter());

    // Functioning inlining runs first so that other rewriters can apply to
    // the function bodies that are inserted by this rule.
    r.Register(ResolvedASTRewrite::REWRITE_INLINE_SQL_FUNCTIONS,
               GetSqlFunctionInliner());
    r.Register(ResolvedASTRewrite::REWRITE_INLINE_SQL_UDAS,
               GetSqlAggregateInliner());
    r.Register(ResolvedASTRewrite::REWRITE_INLINE_SQL_TVFS, GetSqlTvfInliner());
    r.Register(ResolvedASTRewrite::REWRITE_INLINE_SQL_VIEWS,
               GetSqlViewInliner());

    r.Register(ResolvedASTRewrite::REWRITE_MEASURE_TYPE,
               GetMeasureTypeRewriter());

    r.Register(ResolvedASTRewrite::REWRITE_UPDATE_CONSTRUCTOR,
               GetUpdateConstructorRewriter());

    r.Register(ResolvedASTRewrite::REWRITE_FLATTEN, GetFlattenRewriter());
    // REWRITE_ROW_TYPE depends on REWRITE_FLATTEN happening first.
    r.Register(ResolvedASTRewrite::REWRITE_ROW_TYPE,
               GetRowTypeTableScanRewriter());
    r.Register(ResolvedASTRewrite::REWRITE_ANONYMIZATION,
               GetAnonymizationRewriter());
    r.Register(ResolvedASTRewrite::REWRITE_AGGREGATION_THRESHOLD,
               GetAggregationThresholdRewriter());
    r.Register(ResolvedASTRewrite::REWRITE_PROTO_MAP_FNS,
               GetMapFunctionRewriter());
    r.Register(ResolvedASTRewrite::REWRITE_UNPIVOT, GetUnpivotRewriter());
    r.Register(ResolvedASTRewrite::REWRITE_PIVOT, GetPivotRewriter());
    r.Register(ResolvedASTRewrite::REWRITE_TYPEOF_FUNCTION,
               GetTypeofFunctionRewriter());
    r.Register(ResolvedASTRewrite::REWRITE_NULLIFERROR_FUNCTION,
               GetNullIfErrorFunctionRewriter());
    r.Register(ResolvedASTRewrite::REWRITE_VARIADIC_FUNCTION_SIGNATURE_EXPANDER,
               GetVariadicFunctionSignatureExpander());
    r.Register(ResolvedASTRewrite::REWRITE_BUILTIN_FUNCTION_INLINER,
               GetBuiltinFunctionInliner());
    r.Register(ResolvedASTRewrite::REWRITE_LIKE_ANY_ALL,
               GetLikeAnyAllRewriter());
    r.Register(ResolvedASTRewrite::REWRITE_IS_FIRST_IS_LAST_FUNCTION,
               GetIsFirstIsLastFunctionRewriter());
    r.Register(ResolvedASTRewrite::REWRITE_MATCH_RECOGNIZE_FUNCTION,
               GetMatchRecognizeFunctionRewriter());

    r.Register(ResolvedASTRewrite::REWRITE_GROUPING_SET,
               GetGroupingSetRewriter());
    r.Register(ResolvedASTRewrite::REWRITE_INSERT_DML_VALUES,
               GetInsertDmlValuesRewriter());
    r.Register(ResolvedASTRewrite::REWRITE_MULTIWAY_UNNEST,
               GetMultiwayUnnestRewriter());
    r.Register(ResolvedASTRewrite::REWRITE_PIPE_ASSERT,
               GetPipeAssertRewriter());
    r.Register(ResolvedASTRewrite::REWRITE_PIPE_IF, GetPipeIfRewriter());
    r.Register(ResolvedASTRewrite::REWRITE_PIPE_DESCRIBE,
               GetPipeDescribeRewriter());
    // REWRITE_PIPE_IF must happen before REWRITE_GENERALIZED_QUERY_STMT.
    r.Register(ResolvedASTRewrite::REWRITE_GENERALIZED_QUERY_STMT,
               GetGeneralizedQueryStmtRewriter());
    r.Register(ResolvedASTRewrite::REWRITE_ORDER_BY_AND_LIMIT_IN_AGGREGATE,
               GetOrderByAndLimitInAggregateRewriter());

    // This rewriter should typically be the last in the rewrite sequence
    // because it cleans up after several other rewriters add ResolvedWithExprs.
    // Thus new rewriters should usually be added above this one.
    r.Register(ResolvedASTRewrite::REWRITE_WITH_EXPR, GetWithExprRewriter());
  });
}

}  // namespace googlesql
