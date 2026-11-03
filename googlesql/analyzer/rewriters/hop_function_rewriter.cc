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

#include "googlesql/analyzer/rewriters/hop_function_rewriter.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/analyzer/builtin_only_catalog.h"
#include "googlesql/analyzer/substitute.h"
#include "googlesql/common/errors.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output_properties.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/time_series_tvf_util.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_ast_rewrite_visitor.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/rewrite_utils.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

struct HopFunctionArguments {
  std::vector<ResolvedColumn> tvf_column_list;
  std::vector<int> column_index_list;
  std::vector<ResolvedColumn> argument_column_list;
  std::unique_ptr<const ResolvedScan> input_relation;
  ResolvedTimestampColumnPath timestamp_column;
  std::unique_ptr<const ResolvedExpr> window_size_expr;
  std::unique_ptr<const ResolvedExpr> step_size_expr;
  std::unique_ptr<const ResolvedExpr> origin_expr;
};

class HopRewriteVisitor : public ResolvedASTRewriteVisitor {
 public:
  HopRewriteVisitor(const AnalyzerOptions& analyzer_options, Catalog& catalog,
                    TypeFactory& type_factory, ColumnFactory& column_factory,
                    CteQueryNameGenerator& cte_name_generator)
      : analyzer_options_(analyzer_options),
        catalog_(catalog),
        type_factory_(type_factory),
        column_factory_(column_factory),
        cte_name_generator_(cte_name_generator),
        fn_builder_(analyzer_options, catalog, type_factory),
        product_mode_(analyzer_options.language().product_mode()) {}

 private:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> PostVisitResolvedTVFScan(
      std::unique_ptr<const ResolvedTVFScan> node) override {
    if (node->tvf()->IsGoogleSQLBuiltin() &&
        node->function_call_signature() != nullptr &&
        node->function_call_signature()->context_id() == googlesql::FN_HOP) {
      return RewriteHop(std::move(node));
    }
    return std::move(node);
  }

  // ==========================================================================
  // HOP HELPERS
  // ==========================================================================

  absl::StatusOr<std::unique_ptr<const ResolvedNode>> RewriteHop(
      std::unique_ptr<const ResolvedTVFScan> node) {
    // Step 1: Validate input.
    GOOGLESQL_RETURN_IF_ERROR(
        ValidateTvfIsBuiltinAndSignatureMatches(node.get(), "HOP", FN_HOP));

    // Step 2: Extract and validate arguments.
    GOOGLESQL_ASSIGN_OR_RETURN(auto args, ExtractHopArguments(std::move(node)));

    // Step 3: Build the parameters CTE.
    GOOGLESQL_ASSIGN_OR_RETURN(auto params_cte,
                     BuildHopParametersCTE(std::move(args->window_size_expr),
                                           std::move(args->step_size_expr),
                                           std::move(args->origin_expr)));

    absl::string_view cte_name = params_cte->with_query_name();
    const ResolvedColumnList& cte_cols =
        params_cte->with_subquery()->column_list();
    // Step 4: Build the main query to project the final output.
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedScan> main_scan,
                     BuildHopFinalProjection(*args, cte_cols, cte_name,
                                             std::move(args->input_relation)));

    return ResolvedWithScanBuilder()
        .set_column_list(args->tvf_column_list)
        .add_with_entry_list(std::move(params_cte))
        .set_query(std::move(main_scan))
        .set_recursive(false)
        .Build();
  }

  absl::StatusOr<std::unique_ptr<HopFunctionArguments>> ExtractHopArguments(
      std::unique_ptr<const ResolvedTVFScan> node) {
    constexpr int kInputRelationArgIndex = 0;
    constexpr int kTimestampColArgIndex = 1;
    constexpr int kWindowSizeArgIndex = 2;
    constexpr int kStepSizeArgIndex = 3;
    constexpr int kOriginArgIndex = 4;

    // The HOP signature handled by this rewriter requires exactly 5 arguments:
    // input table, timestamp column, window_size, step_size, and origin
    // (analyzer injects default origin if omitted).
    GOOGLESQL_RET_CHECK_EQ(node->argument_list_size(), 5);

    // Argument 0: Input relation.
    const ResolvedTVFArgument* input_arg =
        node->argument_list(kInputRelationArgIndex);
    GOOGLESQL_RET_CHECK(input_arg != nullptr);
    const ResolvedScan* input_scan = input_arg->scan();
    GOOGLESQL_RET_CHECK(input_scan != nullptr);

    // Argument 1: Timestamp Column.
    const ResolvedTVFArgument* ts_arg =
        node->argument_list(kTimestampColArgIndex);
    GOOGLESQL_RET_CHECK(ts_arg != nullptr);
    const ResolvedExpr* ts_col_expr = ts_arg->expr();
    GOOGLESQL_RET_CHECK(ts_col_expr != nullptr);
    GOOGLESQL_RET_CHECK(ts_col_expr->node_kind() == RESOLVED_LITERAL);
    const ResolvedLiteral* ts_col_literal =
        ts_col_expr->GetAs<ResolvedLiteral>();
    const Value& ts_col_val = ts_col_literal->value();
    GOOGLESQL_RET_CHECK(!ts_col_val.is_null());
    GOOGLESQL_RET_CHECK(ts_col_val.type()->IsString());
    const std::string ts_col_name = ts_col_val.string_value();

    // Resolve and validate the timestamp column path.
    const TVFRelation& input_relation =
        node->signature()->argument(0).relation();
    absl::StatusOr<ResolvedTimestampColumnPath> timestamp_column =
        ResolveTimestampColumnPath(input_relation, ts_col_name);
    if (!timestamp_column.ok()) {
      GOOGLESQL_RET_CHECK_EQ(timestamp_column.status().code(),
                   absl::StatusCode::kInvalidArgument)
          << timestamp_column.status();
      return MakeSqlErrorAtStart(ts_arg->GetParseLocationRangeOrNULL())
             << timestamp_column.status().message();
    }

    // Argument 2: Window size.
    const ResolvedTVFArgument* window_arg =
        node->argument_list(kWindowSizeArgIndex);
    GOOGLESQL_RET_CHECK(window_arg != nullptr);
    const ResolvedExpr* window_size_expr = window_arg->expr();
    GOOGLESQL_RET_CHECK(window_size_expr != nullptr);

    // Argument 3: Step size.
    const ResolvedTVFArgument* step_arg =
        node->argument_list(kStepSizeArgIndex);
    GOOGLESQL_RET_CHECK(step_arg != nullptr);
    const ResolvedExpr* step_size_expr = step_arg->expr();
    GOOGLESQL_RET_CHECK(step_size_expr != nullptr);

    // Argument 4: Origin.
    const ResolvedTVFArgument* origin_arg =
        node->argument_list(kOriginArgIndex);
    GOOGLESQL_RET_CHECK(origin_arg != nullptr);
    const ResolvedExpr* origin_expr = origin_arg->expr();
    GOOGLESQL_RET_CHECK(origin_expr != nullptr);

    // Ensure arguments don't contain correlations to outer scopes. The rewriter
    // puts the argument expressions into a CTE, and GoogleSQL currently does
    // not support correlated references in a CTE. See b/244184304 and
    // (broken link).
    GOOGLESQL_RETURN_IF_ERROR(ValidateArgumentsDoNotContainCorrelation(
        node.get(), "HOP", {window_size_expr, step_size_expr, origin_expr}));

    std::vector<ResolvedColumn> argument_column_list =
        input_arg->argument_column_list();
    std::vector<int> column_index_list = node->column_index_list();
    ResolvedTVFScanBuilder builder = ToBuilder(std::move(node));
    auto args = std::make_unique<HopFunctionArguments>();
    args->tvf_column_list = builder.release_column_list();
    args->column_index_list = std::move(column_index_list);
    args->argument_column_list = std::move(argument_column_list);
    args->timestamp_column = *std::move(timestamp_column);

    std::vector<std::unique_ptr<const ResolvedTVFArgument>> argument_list =
        builder.release_argument_list();
    GOOGLESQL_RET_CHECK_EQ(argument_list.size(), 5);

    args->input_relation =
        ToBuilder(std::move(argument_list[kInputRelationArgIndex]))
            .release_scan();
    args->window_size_expr =
        ToBuilder(std::move(argument_list[kWindowSizeArgIndex])).release_expr();
    args->step_size_expr =
        ToBuilder(std::move(argument_list[kStepSizeArgIndex])).release_expr();
    args->origin_expr =
        ToBuilder(std::move(argument_list[kOriginArgIndex])).release_expr();

    return args;
  }

  // Builds a Common Table Expression (CTE) named '_hop_params' to
  // pre-calculate and validate the HOP function arguments (window_size,
  // step_size, origin). This ensures that the expressions for these parameters
  // are evaluated only once. It also pre-calculates step_size in nanoseconds
  // for use in GENERATE_TIMESTAMP_ARRAY.
  //
  // The generated SQL structure for the CTE is:
  //
  // WITH _hop_params AS (
  //   SELECT
  //     CASE
  //       WHEN precomputed_raw.window_size IS NULL
  //         THEN ERROR("Window size cannot be null.")
  //       WHEN precomputed_raw.window_size = INTERVAL 0 DAY
  //         THEN ERROR("Window size cannot be zero.")
  //       WHEN precomputed_raw.window_size < INTERVAL 0 DAY
  //         THEN ERROR("Window size cannot be negative.")
  //       WHEN EXTRACT(MONTH FROM precomputed_raw.window_size) != 0 OR
  //            EXTRACT(YEAR FROM precomputed_raw.window_size) != 0
  //         THEN ERROR("Window size cannot contain MONTH or YEAR parts.")
  //       ELSE precomputed_raw.window_size
  //     END AS window_size,
  //     CASE
  //       WHEN precomputed_raw.step_size IS NULL
  //         THEN ERROR("Step size cannot be null.")
  //       WHEN precomputed_raw.step_size = INTERVAL 0 DAY
  //         THEN ERROR("Step size cannot be zero.")
  //       WHEN precomputed_raw.step_size < INTERVAL 0 DAY
  //         THEN ERROR("Step size cannot be negative.")
  //       WHEN EXTRACT(MONTH FROM precomputed_raw.step_size) != 0 OR
  //            EXTRACT(YEAR FROM precomputed_raw.step_size) != 0
  //         THEN ERROR("Step size cannot contain MONTH or YEAR parts.")
  //       WHEN precomputed_raw.window_size IS NOT NULL AND
  //            precomputed_raw.window_size > INTERVAL 0 DAY AND
  //            precomputed_raw.step_size > precomputed_raw.window_size
  //         THEN ERROR("Window size must be greater than or equal to step
  //         size.")
  //       ELSE precomputed_raw.step_size
  //     END AS step_size,
  //     IntervalToNanos(precomputed_raw.step_size) AS step_nanos,
  //     CASE
  //       WHEN precomputed_raw.origin IS NULL
  //         THEN ERROR("Origin cannot be null.")
  //       ELSE precomputed_raw.origin
  //     END AS origin
  //   FROM (
  //     SELECT
  //       <window_size_expr> AS window_size,
  //       <step_size_expr> AS step_size,
  //       <origin_expr> AS origin
  //   ) AS precomputed_raw
  // )
  //
  // See (broken link) for more details.
  absl::StatusOr<std::unique_ptr<const ResolvedWithEntry>>
  BuildHopParametersCTE(std::unique_ptr<const ResolvedExpr> window_size_expr,
                        std::unique_ptr<const ResolvedExpr> step_size_expr,
                        std::unique_ptr<const ResolvedExpr> origin_expr) {
    // -- Part 1: Construct the raw parameters:
    // SELECT
    //   <window_size_expr> AS window_size,
    //   <step_size_expr> AS step_size,
    //   <origin_expr> AS origin
    //   ) AS precomputed_raw.
    std::vector<std::unique_ptr<const ResolvedComputedColumn>> compute_exprs;
    ResolvedColumnList compute_cols;

    const Type* window_size_type = window_size_expr->type();
    ResolvedColumn raw_window_col = column_factory_.MakeCol(
        "$precomputed_raw", "window_size", window_size_type);
    compute_cols.push_back(raw_window_col);
    compute_exprs.push_back(MakeResolvedComputedColumn(
        raw_window_col, std::move(window_size_expr)));

    const Type* step_size_type = step_size_expr->type();
    ResolvedColumn raw_step_col = column_factory_.MakeCol(
        "$precomputed_raw", "step_size", step_size_type);
    compute_cols.push_back(raw_step_col);
    compute_exprs.push_back(
        MakeResolvedComputedColumn(raw_step_col, std::move(step_size_expr)));

    const Type* origin_type = origin_expr->type();
    ResolvedColumn raw_origin_col =
        column_factory_.MakeCol("$precomputed_raw", "origin", origin_type);
    compute_cols.push_back(raw_origin_col);
    compute_exprs.push_back(
        MakeResolvedComputedColumn(raw_origin_col, std::move(origin_expr)));

    // -- Part 2: Validate arguments and project final output.
    std::string cte_name = cte_name_generator_.MakeUniqueCteName("_hop_params");
    std::vector<std::unique_ptr<const ResolvedComputedColumn>> final_exprs;
    ResolvedColumnList final_cols;

    // window_size (Validation).
    ResolvedColumn hop_window_col =
        column_factory_.MakeCol(cte_name, "window_size", raw_window_col.type());
    final_cols.push_back(hop_window_col);
    {
      auto raw_ws_ref =
          MakeResolvedColumnRef(raw_window_col.type(), raw_window_col, false);
      GOOGLESQL_ASSIGN_OR_RETURN(
          auto validated_expr,
          AnalyzeSubstitute(analyzer_options_, catalog_, type_factory_,
                            R"sql(
              CASE
                WHEN raw_ws IS NULL THEN ERROR("Window size cannot be null.")
                WHEN raw_ws = INTERVAL 0 DAY THEN ERROR("Window size cannot be zero.")
                WHEN raw_ws < INTERVAL 0 DAY THEN ERROR("Window size cannot be negative.")
                WHEN EXTRACT(MONTH FROM raw_ws) != 0 OR EXTRACT(YEAR FROM raw_ws) != 0
                  THEN ERROR("Window size cannot contain MONTH or YEAR parts.")
                ELSE raw_ws
              END
              )sql",
                            {{"raw_ws", raw_ws_ref.get()}}),
          _.With(ExpectAnalyzeSubstituteSuccess));
      final_exprs.push_back(MakeResolvedComputedColumn(
          hop_window_col, std::move(validated_expr)));
    }

    // step_size (Validation chain).
    ResolvedColumn hop_step_col =
        column_factory_.MakeCol(cte_name, "step_size", raw_step_col.type());
    final_cols.push_back(hop_step_col);
    {
      auto raw_ss_ref =
          MakeResolvedColumnRef(raw_step_col.type(), raw_step_col, false);
      auto raw_ws_ref =
          MakeResolvedColumnRef(raw_window_col.type(), raw_window_col, false);

      GOOGLESQL_ASSIGN_OR_RETURN(
          auto final_val,
          AnalyzeSubstitute(
              analyzer_options_, catalog_, type_factory_,
              R"sql(
              CASE
                WHEN raw_ss IS NULL THEN ERROR("Step size cannot be null.")
                WHEN raw_ss = INTERVAL 0 DAY THEN ERROR("Step size cannot be zero.")
                WHEN raw_ss < INTERVAL 0 DAY THEN ERROR("Step size cannot be negative.")
                WHEN EXTRACT(MONTH FROM raw_ss) != 0 OR EXTRACT(YEAR FROM raw_ss) != 0
                  THEN ERROR("Step size cannot contain MONTH or YEAR parts.")
                WHEN raw_ws IS NOT NULL AND raw_ws > INTERVAL 0 DAY AND raw_ss > raw_ws
                  THEN ERROR("Window size must be greater than or equal to step size.")
                ELSE raw_ss
              END
              )sql",
              {{"raw_ss", raw_ss_ref.get()}, {"raw_ws", raw_ws_ref.get()}}),
          _.With(ExpectAnalyzeSubstituteSuccess));

      final_exprs.push_back(
          MakeResolvedComputedColumn(hop_step_col, std::move(final_val)));
    }

    // Calculate step_nanos unconditionally.
    ResolvedColumn hop_step_nanos_col =
        column_factory_.MakeCol(cte_name, "step_nanos", types::Int64Type());
    final_cols.push_back(hop_step_nanos_col);
    {
      auto step_col_ref_for_nanos =
          MakeResolvedColumnRef(raw_step_col.type(), raw_step_col, false);
      GOOGLESQL_ASSIGN_OR_RETURN(auto total_nanos_expr,
                       fn_builder_.IntervalToNanos(
                           std::move(step_col_ref_for_nanos), "HOP"));
      final_exprs.push_back(MakeResolvedComputedColumn(
          hop_step_nanos_col, std::move(total_nanos_expr)));
    }

    // origin (Validation).
    ResolvedColumn hop_origin_col =
        column_factory_.MakeCol(cte_name, "origin", raw_origin_col.type());
    final_cols.push_back(hop_origin_col);
    {
      auto raw_origin_ref =
          MakeResolvedColumnRef(raw_origin_col.type(), raw_origin_col, false);
      GOOGLESQL_ASSIGN_OR_RETURN(
          auto validated_expr,
          AnalyzeSubstitute(analyzer_options_, catalog_, type_factory_,
                            R"sql(
              CASE
                WHEN raw_origin IS NULL THEN ERROR("Origin cannot be null.")
                ELSE raw_origin
              END
              )sql",
                            {{"raw_origin", raw_origin_ref.get()}}),
          _.With(ExpectAnalyzeSubstituteSuccess));
      final_exprs.push_back(MakeResolvedComputedColumn(
          hop_origin_col, std::move(validated_expr)));
    }

    return ResolvedWithEntryBuilder()
        .set_with_query_name(cte_name)
        .set_with_subquery(
            ResolvedProjectScanBuilder()
                .set_column_list(final_cols)
                .set_expr_list(std::move(final_exprs))
                .set_input_scan(
                    ResolvedProjectScanBuilder()
                        .set_column_list(compute_cols)
                        .set_expr_list(std::move(compute_exprs))
                        .set_input_scan(MakeResolvedSingleRowScan())))
        .Build();
  }

  // Builds the final projection for the HOP rewrite. This SELECT statement
  // uses the validated parameters from the '_hop_params' CTE. For each row
  // in the input table, it generates an array of possible window start times
  // and then unnests this array, effectively left outer joining the input row
  // with each window it belongs to. If the timestamp is NULL, the row is
  // preserved but the window columns will be NULL.
  //
  // The generated Resolved AST structure is something like:
  //
  // SELECT
  //   t.*,
  //   generated_windows AS window_start,
  //   TIMESTAMP_ADD(
  //     generated_windows,
  //     p.window_size
  //   ) AS window_end
  // FROM
  //   <InputTable> AS t
  //   CROSS JOIN _hop_params AS p
  //   LEFT JOIN UNNEST(
  //       -- Calculate window_from using GENERATE_TIMESTAMP_ARRAY
  //       GENERATE_TIMESTAMP_ARRAY(
  //         -- starting point
  //         TIMESTAMP_ADD(
  //           TIMESTAMP_BUCKET(
  //             TIMESTAMP_SUB(t.ts, p.window_size),
  //             p.step_size,
  //             p.origin
  //           ),
  //           p.step_size
  //         ),
  //         -- end point
  //         TIMESTAMP_BUCKET(
  //           t.ts,
  //           p.step_size,
  //           p.origin
  //         ),
  //         -- step expression (pre-calculated integer)
  //         INTERVAL p.step_nanos NANOSECOND
  //       )
  //     )
  //   ) AS generated_windows
  //
  absl::StatusOr<std::unique_ptr<const ResolvedScan>> BuildHopFinalProjection(
      const HopFunctionArguments& args, const ResolvedColumnList& cte_cols,
      absl::string_view cte_name,
      std::unique_ptr<const ResolvedScan> input_relation) {
    // Create new CTE reference columns for _hop_params CTE.
    // We cross join input_relation with this CTE scan to make parameters
    // directly available as column references.
    std::vector<ResolvedColumn> cte_ref_cols;
    cte_ref_cols.reserve(cte_cols.size());
    for (const auto& col : cte_cols) {
      cte_ref_cols.push_back(
          column_factory_.MakeCol(cte_name, col.name(), col.type()));
    }
    auto cte_scan = MakeResolvedWithRefScan(cte_ref_cols, cte_name);

    auto get_window_interval_expr =
        [&]() -> absl::StatusOr<std::unique_ptr<const ResolvedExpr>> {
      return MakeResolvedColumnRef(cte_ref_cols[0].type(), cte_ref_cols[0],
                                   /*is_correlated=*/false);
    };
    auto get_step_interval_expr =
        [&]() -> absl::StatusOr<std::unique_ptr<const ResolvedExpr>> {
      return MakeResolvedColumnRef(cte_ref_cols[1].type(), cte_ref_cols[1],
                                   /*is_correlated=*/false);
    };
    auto get_step_nanos_expr =
        [&]() -> absl::StatusOr<std::unique_ptr<const ResolvedExpr>> {
      return MakeResolvedColumnRef(cte_ref_cols[2].type(), cte_ref_cols[2],
                                   /*is_correlated=*/false);
    };
    auto get_origin_expr =
        [&]() -> absl::StatusOr<std::unique_ptr<const ResolvedExpr>> {
      return MakeResolvedColumnRef(cte_ref_cols[3].type(), cte_ref_cols[3],
                                   /*is_correlated=*/false);
    };

    // Creates the starting point expression for GENERATE_TIMESTAMP_ARRAY:
    // TIMESTAMP_ADD(
    //   TIMESTAMP_BUCKET(
    //     TIMESTAMP_SUB(t.ts, <window_size>),
    //     <step_size>,
    //     <origin>
    //   ),
    //   <step_size>
    // )
    auto make_start_gen_array_expr =
        [&]() -> absl::StatusOr<std::unique_ptr<const ResolvedExpr>> {
      GOOGLESQL_ASSIGN_OR_RETURN(
          auto ts_col_ref,
          BuildTimestampColumnExpression(
              args.timestamp_column,
              args.argument_column_list[args.timestamp_column.column_index],
              fn_builder_));
      GOOGLESQL_ASSIGN_OR_RETURN(auto window_interval_expr, get_window_interval_expr());
      GOOGLESQL_ASSIGN_OR_RETURN(auto ts_minus_window,
                       fn_builder_.Subtract(std::move(ts_col_ref),
                                            std::move(window_interval_expr)));
      std::vector<std::unique_ptr<const ResolvedExpr>> bucket_args;
      bucket_args.push_back(std::move(ts_minus_window));
      GOOGLESQL_ASSIGN_OR_RETURN(auto step_interval_expr, get_step_interval_expr());
      bucket_args.push_back(std::move(step_interval_expr));
      GOOGLESQL_ASSIGN_OR_RETURN(auto origin_expr, get_origin_expr());
      bucket_args.push_back(std::move(origin_expr));
      GOOGLESQL_ASSIGN_OR_RETURN(auto bucket,
                       fn_builder_.TimestampBucket(std::move(bucket_args)));

      GOOGLESQL_ASSIGN_OR_RETURN(auto step_interval_expr_2, get_step_interval_expr());
      return fn_builder_.Add(std::move(bucket),
                             std::move(step_interval_expr_2));
    };

    // Creates the end point expression for GENERATE_TIMESTAMP_ARRAY:
    // TIMESTAMP_BUCKET(
    //   t.ts,
    //   <step_size>,
    //   <origin>
    // )
    auto make_end_gen_array_expr =
        [&]() -> absl::StatusOr<std::unique_ptr<const ResolvedExpr>> {
      GOOGLESQL_ASSIGN_OR_RETURN(
          auto ts_col_ref,
          BuildTimestampColumnExpression(
              args.timestamp_column,
              args.argument_column_list[args.timestamp_column.column_index],
              fn_builder_));
      std::vector<std::unique_ptr<const ResolvedExpr>> bucket_args;
      bucket_args.push_back(std::move(ts_col_ref));
      GOOGLESQL_ASSIGN_OR_RETURN(auto step_interval_expr, get_step_interval_expr());
      bucket_args.push_back(std::move(step_interval_expr));
      GOOGLESQL_ASSIGN_OR_RETURN(auto origin_expr, get_origin_expr());
      bucket_args.push_back(std::move(origin_expr));
      return fn_builder_.TimestampBucket(std::move(bucket_args));
    };

    // Generate the array of window starts:
    // GENERATE_TIMESTAMP_ARRAY(start, end, INTERVAL p.step_nanos NANOSECOND)
    GOOGLESQL_ASSIGN_OR_RETURN(auto start_for_gen, make_start_gen_array_expr());
    GOOGLESQL_ASSIGN_OR_RETURN(auto end_for_gen, make_end_gen_array_expr());

    auto date_part_literal = MakeResolvedLiteral(
        types::DatePartEnumType(),
        Value::Enum(types::DatePartEnumType(), functions::NANOSECOND));

    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> step_nanos_expr,
                     get_step_nanos_expr());
    GOOGLESQL_ASSIGN_OR_RETURN(
        auto generate_array_expr,
        fn_builder_.GenerateTimestampArrayWithDatePart(
            std::move(start_for_gen), std::move(end_for_gen),
            std::move(step_nanos_expr), std::move(date_part_literal)));

    ResolvedColumn window_start_col =
        column_factory_.MakeCol("$hop", "window_start", types::TimestampType());

    // Save input columns before moving input_relation.
    const ResolvedColumnList input_columns = input_relation->column_list();

    ResolvedColumnList cross_join_output_cols = input_columns;
    for (const auto& col : cte_ref_cols) {
      cross_join_output_cols.push_back(col);
    }

    auto cross_join_scan =
        MakeResolvedJoinScan(cross_join_output_cols, ResolvedJoinScan::INNER,
                             std::move(input_relation), std::move(cte_scan),
                             /*join_expr=*/nullptr);

    ResolvedColumnList array_scan_output_cols = cross_join_output_cols;
    array_scan_output_cols.push_back(window_start_col);
    // LEFT OUTER JOIN UNNEST(...) AS generated_windows
    // This preserves the input row even if array generation yields a NULL
    // array (if the timestamp is NULL), in which case window columns will
    // be NULL.
    std::vector<std::unique_ptr<const ResolvedExpr>> array_expr_list;
    array_expr_list.push_back(std::move(generate_array_expr));
    std::vector<ResolvedColumn> element_column_list;
    element_column_list.push_back(window_start_col);
    auto array_scan = MakeResolvedArrayScan(
        array_scan_output_cols, std::move(cross_join_scan),
        std::move(array_expr_list), std::move(element_column_list),
        /*array_offset_column=*/nullptr,
        /*join_expr=*/nullptr,
        /*is_outer=*/true,
        /*array_zip_mode=*/nullptr);

    const ResolvedColumnList& output_column_list = args.tvf_column_list;
    std::vector<std::unique_ptr<const ResolvedComputedColumn>> output_expr_list;
    const std::vector<ResolvedColumn>& argument_column_list =
        args.argument_column_list;
    const size_t num_input_columns = argument_column_list.size();

    GOOGLESQL_RET_CHECK_EQ(output_column_list.size(), args.column_index_list.size());

    // Map each output column by looking up its index in the TVF's
    // argument_column_list. Since we are looping over output_column_list,
    // columns pruned due to column pruning (i.e., not referenced in the
    // query) are already excluded from the output_column_list and will be
    // excluded from the rewritten output.
    // Indices greater than num_input_columns correspond to new columns created
    // by the TVF - WINDOW_START and WINDOW_END.
    for (int i = 0; i < output_column_list.size(); ++i) {
      const int idx = args.column_index_list[i];
      if (idx < num_input_columns) {
        // Passing input columns through to the output.
        const ResolvedColumn& input_col = argument_column_list[idx];
        output_expr_list.push_back(MakeResolvedComputedColumn(
            output_column_list[i],
            MakeResolvedColumnRef(input_col.type(), input_col,
                                  /*is_correlated=*/false)));
      } else if (idx == num_input_columns) {
        // WINDOW_START column.
        const ResolvedColumn& final_ws_col = output_column_list[i];
        GOOGLESQL_RET_CHECK(googlesql_base::CaseEqual(final_ws_col.name(), "WINDOW_START"));
        output_expr_list.push_back(MakeResolvedComputedColumn(
            final_ws_col, MakeResolvedColumnRef(window_start_col.type(),
                                                window_start_col, false)));
      } else if (idx == num_input_columns + 1) {
        // WINDOW_END column.
        const ResolvedColumn& final_we_col = output_column_list[i];
        GOOGLESQL_RET_CHECK(googlesql_base::CaseEqual(final_we_col.name(), "WINDOW_END"));
        auto window_start_ref = MakeResolvedColumnRef(window_start_col.type(),
                                                      window_start_col, false);
        GOOGLESQL_ASSIGN_OR_RETURN(auto window_interval_expr, get_window_interval_expr());
        GOOGLESQL_ASSIGN_OR_RETURN(auto window_end_expr,
                         fn_builder_.Add(std::move(window_start_ref),
                                         std::move(window_interval_expr)));
        output_expr_list.push_back(MakeResolvedComputedColumn(
            final_we_col, std::move(window_end_expr)));
      } else {
        GOOGLESQL_RET_CHECK_FAIL() << "Invalid column index: " << idx;
      }
    }

    if (output_column_list.empty()) {
      // The query uses none of HOP's output columns, e.g. COUNT(*), they
      // might be pruned. Return the scan directly instead of constructing an
      // invalid ProjectScan with empty output columns.
      GOOGLESQL_RET_CHECK(output_expr_list.empty());
      return std::move(array_scan);
    }

    return ResolvedProjectScanBuilder()
        .set_column_list(output_column_list)
        .set_expr_list(std::move(output_expr_list))
        .set_input_scan(std::move(array_scan))
        .Build();
  }

  // ==========================================================================
  // SHARED HELPERS
  // ==========================================================================

  const AnalyzerOptions& analyzer_options_;
  Catalog& catalog_;
  TypeFactory& type_factory_;
  ColumnFactory& column_factory_;
  CteQueryNameGenerator& cte_name_generator_;
  FunctionCallBuilder fn_builder_;
  ProductMode product_mode_;
};

}  // namespace

class HopFunctionRewriter : public Rewriter {
 public:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, std::unique_ptr<const ResolvedNode> input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    GOOGLESQL_RET_CHECK(options.id_string_pool() != nullptr);
    GOOGLESQL_RET_CHECK(options.column_id_sequence_number() != nullptr);
    ColumnFactory column_factory(0, options.id_string_pool().get(),
                                 options.column_id_sequence_number());

    CteQueryNameGenerator cte_name_generator;
    if (input != nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(input->Accept(&cte_name_generator));
    }

    BuiltinOnlyCatalog builtin_catalog("builtin_catalog", catalog);
    HopRewriteVisitor rewriter(options, builtin_catalog, type_factory,
                               column_factory, cte_name_generator);

    return rewriter.VisitAll(std::move(input));
  }

  std::string Name() const override { return "HopFunctionRewriter"; }

  bool ProvideUnfilteredCatalogToBuiltinRewriter() const override {
    return true;
  }
};

const Rewriter* GetHopFunctionRewriter() {
  static const auto* kRewriter = new HopFunctionRewriter;
  return kRewriter;
}

}  // namespace googlesql
