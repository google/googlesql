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

#include "googlesql/analyzer/rewriters/tumble_function_rewriter.h"

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

struct TumbleFunctionArguments {
  std::vector<ResolvedColumn> tvf_column_list;
  std::vector<int> column_index_list;
  std::vector<ResolvedColumn> argument_column_list;
  std::unique_ptr<const ResolvedScan> input_relation;
  ResolvedTimestampColumnPath timestamp_column;
  std::unique_ptr<const ResolvedExpr> window_size_expr;
  std::unique_ptr<const ResolvedExpr> origin_expr;
};

class TumbleRewriteVisitor : public ResolvedASTRewriteVisitor {
 public:
  TumbleRewriteVisitor(const AnalyzerOptions& analyzer_options,
                       Catalog& catalog, TypeFactory& type_factory,
                       ColumnFactory& column_factory,
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
    for (const auto& signature : node->tvf()->signatures()) {
      if (signature.context_id() == googlesql::FN_TUMBLE) {
        return RewriteTumble(std::move(node));
      }
    }
    return std::move(node);
  }

  // ==========================================================================
  // TUMBLE HELPERS
  // ==========================================================================

  absl::StatusOr<std::unique_ptr<const ResolvedNode>> RewriteTumble(
      std::unique_ptr<const ResolvedTVFScan> node) {
    // Step 1: Validate input.
    GOOGLESQL_RETURN_IF_ERROR(ValidateTvfIsBuiltinAndSignatureMatches(
        node.get(), "TUMBLE", FN_TUMBLE));

    // Step 2: Extract and validate arguments.
    GOOGLESQL_ASSIGN_OR_RETURN(auto args, ExtractTumbleArguments(std::move(node)));

    // Step 3: Build the parameters CTE.
    GOOGLESQL_ASSIGN_OR_RETURN(auto params_cte,
                     BuildTumbleParametersCTE(std::move(args->window_size_expr),
                                              std::move(args->origin_expr)));

    // Step 4: Build the final projection.
    absl::string_view cte_name = params_cte->with_query_name();
    const ResolvedColumnList& cte_cols =
        params_cte->with_subquery()->column_list();
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedScan> main_scan,
        BuildTumbleFinalProjection(*args, cte_cols, cte_name,
                                   std::move(args->input_relation)));

    return ResolvedWithScanBuilder()
        .set_column_list(args->tvf_column_list)
        .add_with_entry_list(std::move(params_cte))
        .set_query(std::move(main_scan))
        .set_recursive(false)
        .Build();
  }

  absl::StatusOr<std::unique_ptr<TumbleFunctionArguments>>
  ExtractTumbleArguments(std::unique_ptr<const ResolvedTVFScan> node) {
    const int arg_count = node->argument_list_size();
    GOOGLESQL_RET_CHECK_EQ(arg_count, 4);

    // Argument 0: Input relation.
    const ResolvedTVFArgument* input_arg = node->argument_list(0);
    GOOGLESQL_RET_CHECK(input_arg != nullptr);
    const ResolvedScan* input_scan = input_arg->scan();
    GOOGLESQL_RET_CHECK(input_scan != nullptr);

    // Argument 1: Timestamp Column.
    const ResolvedTVFArgument* ts_arg = node->argument_list(1);
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
    const ResolvedTVFArgument* window_size_arg = node->argument_list(2);
    GOOGLESQL_RET_CHECK(window_size_arg != nullptr);
    const ResolvedExpr* window_size_expr = window_size_arg->expr();
    GOOGLESQL_RET_CHECK(window_size_expr != nullptr);

    // Argument 3: Origin.
    const ResolvedTVFArgument* origin_arg = node->argument_list(3);
    GOOGLESQL_RET_CHECK(origin_arg != nullptr);
    const ResolvedExpr* origin_expr = origin_arg->expr();
    GOOGLESQL_RET_CHECK(origin_expr != nullptr);

    // Ensure arguments don't contain correlations to outer scopes. The rewriter
    // puts the argument expressions into a CTE, and GoogleSQL currently does
    // not support correlated references in a CTE. See b/244184304 and
    // (broken link).
    GOOGLESQL_RETURN_IF_ERROR(ValidateArgumentsDoNotContainCorrelation(
        node.get(), "TUMBLE", {window_size_expr, origin_expr}));

    std::vector<ResolvedColumn> argument_column_list =
        input_arg->argument_column_list();
    std::vector<int> column_index_list = node->column_index_list();
    ResolvedTVFScanBuilder builder = ToBuilder(std::move(node));
    auto args = std::make_unique<TumbleFunctionArguments>();
    args->tvf_column_list = builder.release_column_list();
    args->column_index_list = std::move(column_index_list);
    args->argument_column_list = std::move(argument_column_list);
    args->timestamp_column = std::move(*timestamp_column);

    std::vector<std::unique_ptr<const ResolvedTVFArgument>> argument_list =
        builder.release_argument_list();
    GOOGLESQL_RET_CHECK_EQ(argument_list.size(), 4);

    args->input_relation =
        ToBuilder(std::move(argument_list[0])).release_scan();
    args->window_size_expr =
        ToBuilder(std::move(argument_list[2])).release_expr();
    args->origin_expr = ToBuilder(std::move(argument_list[3])).release_expr();

    return args;
  }

  // Builds a Common Table Expression (CTE) named '_tumble_params' to
  // pre-calculate and validate the TUMBLE function arguments (window_size,
  // origin). This ensures that the expressions for these parameters are
  // evaluated only once.
  //
  // The generated SQL structure for the CTE is:
  //
  // WITH _tumble_params AS (
  //   SELECT *
  //   FROM (
  //     SELECT
  //       <window_size_expr> AS window_size,
  //       <origin_expr> AS origin
  //   )
  //   WHERE
  //     CASE
  //       WHEN window_size IS NULL
  //         THEN ERROR("Window size cannot be null.")
  //       WHEN window_size = INTERVAL 0 DAY
  //         THEN ERROR("Window size cannot be zero.")
  //       WHEN window_size < INTERVAL 0 DAY
  //         THEN ERROR("Window size cannot be negative.")
  //       WHEN EXTRACT(MONTH FROM window_size) != 0 OR
  //            EXTRACT(YEAR FROM window_size) != 0
  //         THEN ERROR("Window size cannot contain MONTH or YEAR parts.")
  //       WHEN origin IS NULL
  //         THEN ERROR("Origin cannot be null.")
  //       ELSE TRUE
  //     END
  // )
  //
  // See (broken link) for more details.
  absl::StatusOr<std::unique_ptr<const ResolvedWithEntry>>
  BuildTumbleParametersCTE(std::unique_ptr<const ResolvedExpr> window_size_expr,
                           std::unique_ptr<const ResolvedExpr> origin_expr) {
    std::string cte_name =
        cte_name_generator_.MakeUniqueCteName("_tumble_params");

    // SELECT
    //   <window_size_expr> AS window_size,
    //   <origin_expr> AS origin
    std::vector<std::unique_ptr<const ResolvedComputedColumn>> compute_exprs;
    ResolvedColumnList compute_cols;

    ResolvedColumn window_size_col = column_factory_.MakeCol(
        cte_name, "window_size", window_size_expr->type());
    compute_cols.push_back(window_size_col);
    compute_exprs.push_back(MakeResolvedComputedColumn(
        window_size_col, std::move(window_size_expr)));

    ResolvedColumn origin_col =
        column_factory_.MakeCol(cte_name, "origin", origin_expr->type());
    compute_cols.push_back(origin_col);
    compute_exprs.push_back(
        MakeResolvedComputedColumn(origin_col, std::move(origin_expr)));

    // Check the arguments in a WHERE clause rather than in the SELECT.
    // Engines may skip computing SELECT columns that nothing uses. This ensures
    // that the invalid arguments errors are not skipped.
    auto window_size_ref = MakeResolvedColumnRef(
        window_size_col.type(), window_size_col, /*is_correlated=*/false);
    auto origin_ref = MakeResolvedColumnRef(origin_col.type(), origin_col,
                                            /*is_correlated=*/false);
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedExpr> check_expr,
        AnalyzeSubstitute(analyzer_options_, catalog_, type_factory_,
                          R"sql(
            CASE
              WHEN window_size IS NULL THEN ERROR("Window size cannot be null.")
              WHEN window_size = INTERVAL 0 DAY THEN ERROR("Window size cannot be zero.")
              WHEN window_size < INTERVAL 0 DAY THEN ERROR("Window size cannot be negative.")
              WHEN EXTRACT(MONTH FROM window_size) != 0 OR EXTRACT(YEAR FROM window_size) != 0
                THEN ERROR("Window size cannot contain MONTH or YEAR parts.")
              WHEN origin IS NULL THEN ERROR("Origin cannot be null.")
              ELSE TRUE
            END
            )sql",
                          {{"window_size", window_size_ref.get()},
                           {"origin", origin_ref.get()}}),
        _.With(ExpectAnalyzeSubstituteSuccess));

    return ResolvedWithEntryBuilder()
        .set_with_query_name(cte_name)
        .set_with_subquery(
            ResolvedFilterScanBuilder()
                .set_column_list(compute_cols)
                .set_input_scan(
                    ResolvedProjectScanBuilder()
                        .set_column_list(compute_cols)
                        .set_expr_list(std::move(compute_exprs))
                        .set_input_scan(MakeResolvedSingleRowScan()))
                .set_filter_expr(std::move(check_expr)))
        .Build();
  }

  // Builds the final projection for the TUMBLE rewrite. This SELECT statement
  // joins the original input table with the validated parameters from the
  // '_tumble_params' CTE. It calculates the WINDOW_START and WINDOW_END
  // columns for each row based on the timestamp column and window parameters.
  //
  // The generated SQL structure for the main query is:
  //
  // SELECT
  //   t.*,
  //   -- Calculate window_start using TIMESTAMP_BUCKET
  //   TIMESTAMP_BUCKET(
  //     t.<timestamp_column>,
  //     p.window_size,
  //     p.origin
  //   ) AS window_start,
  //   -- Calculate window_end
  //   TIMESTAMP_ADD(
  //     TIMESTAMP_BUCKET(
  //       t.<timestamp_column>,
  //       p.window_size,
  //       p.origin
  //     ),
  //     p.window_size
  //   ) AS window_end
  // FROM
  //   <InputTable> AS t
  //   CROSS JOIN _tumble_params AS p
  //
  // See (broken link) for more details.
  absl::StatusOr<std::unique_ptr<const ResolvedScan>>
  BuildTumbleFinalProjection(
      const TumbleFunctionArguments& args, const ResolvedColumnList& cte_cols,
      absl::string_view cte_name,
      std::unique_ptr<const ResolvedScan> input_relation) {
    // Create new CTE reference columns for the _tumble_params CTE.
    // We cross join input_relation with this CTE scan to make parameters
    // directly available as column references.
    // Columns are window_size, origin.
    GOOGLESQL_RET_CHECK_EQ(cte_cols.size(), 2);
    std::vector<ResolvedColumn> cte_ref_cols;
    cte_ref_cols.reserve(cte_cols.size());
    for (const auto& col : cte_cols) {
      cte_ref_cols.push_back(
          column_factory_.MakeCol(cte_name, col.name(), col.type()));
    }
    auto cte_scan = MakeResolvedWithRefScan(cte_ref_cols, cte_name);
    const ResolvedColumn& window_size_ref_col = cte_ref_cols[0];
    const ResolvedColumn& origin_ref_col = cte_ref_cols[1];

    // p.window_size
    auto get_window_size_expr =
        [&]() -> absl::StatusOr<std::unique_ptr<const ResolvedExpr>> {
      return MakeResolvedColumnRef(window_size_ref_col.type(),
                                   window_size_ref_col,
                                   /*is_correlated=*/false);
    };
    // p.origin
    auto get_origin_expr =
        [&]() -> absl::StatusOr<std::unique_ptr<const ResolvedExpr>> {
      return MakeResolvedColumnRef(origin_ref_col.type(), origin_ref_col,
                                   /*is_correlated=*/false);
    };
    // Creates a TIMESTAMP_BUCKET(ts_col, window_size, origin) expression.
    auto make_window_start_expr =
        [&]() -> absl::StatusOr<std::unique_ptr<const ResolvedExpr>> {
      // BuildTimestampColumnExpression assumes that the path is valid (which is
      // true since we resolved and validated it in ExtractTumbleArguments).
      GOOGLESQL_ASSIGN_OR_RETURN(
          auto ts_col_ref,
          BuildTimestampColumnExpression(
              args.timestamp_column,
              args.argument_column_list[args.timestamp_column.column_index],
              fn_builder_));
      std::vector<std::unique_ptr<const ResolvedExpr>> bucket_args;
      bucket_args.push_back(std::move(ts_col_ref));
      GOOGLESQL_ASSIGN_OR_RETURN(auto window_size_expr, get_window_size_expr());
      bucket_args.push_back(std::move(window_size_expr));
      GOOGLESQL_ASSIGN_OR_RETURN(auto origin_expr, get_origin_expr());
      bucket_args.push_back(std::move(origin_expr));
      return fn_builder_.TimestampBucket(std::move(bucket_args));
    };
    std::vector<std::unique_ptr<const ResolvedComputedColumn>> output_expr_list;
    const ResolvedColumnList& output_column_list = args.tvf_column_list;
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
        const ResolvedColumn& window_start_col = output_column_list[i];
        GOOGLESQL_RET_CHECK(
            googlesql_base::CaseEqual(window_start_col.name(), "WINDOW_START"));
        GOOGLESQL_ASSIGN_OR_RETURN(auto window_start_expr, make_window_start_expr());
        output_expr_list.push_back(MakeResolvedComputedColumn(
            window_start_col, std::move(window_start_expr)));
      } else if (idx == num_input_columns + 1) {
        // WINDOW_END column.
        const ResolvedColumn& window_end_col = output_column_list[i];
        GOOGLESQL_RET_CHECK(googlesql_base::CaseEqual(window_end_col.name(), "WINDOW_END"));
        GOOGLESQL_ASSIGN_OR_RETURN(auto window_start_expr_for_end,
                         make_window_start_expr());
        GOOGLESQL_ASSIGN_OR_RETURN(auto window_size_expr, get_window_size_expr());
        GOOGLESQL_ASSIGN_OR_RETURN(auto window_end_expr,
                         fn_builder_.Add(std::move(window_start_expr_for_end),
                                         std::move(window_size_expr)));
        output_expr_list.push_back(MakeResolvedComputedColumn(
            window_end_col, std::move(window_end_expr)));
      } else {
        GOOGLESQL_RET_CHECK_FAIL() << "Invalid column index: " << idx;
      }
    }

    // <InputTable> AS t CROSS JOIN _tumble_params AS p
    ResolvedColumnList cross_join_output_cols = input_relation->column_list();
    cross_join_output_cols.insert(cross_join_output_cols.end(),
                                  cte_ref_cols.begin(), cte_ref_cols.end());
    std::unique_ptr<const ResolvedScan> cross_join_scan =
        MakeResolvedJoinScan(cross_join_output_cols, ResolvedJoinScan::INNER,
                             std::move(input_relation), std::move(cte_scan),
                             /*join_expr=*/nullptr);

    if (output_column_list.empty()) {
      // The query uses none of TUMBLE's output columns, e.g. COUNT(*), they
      // might be pruned. Return the scan directly instead of constructing an
      // invalid ProjectScan with empty output columns.
      GOOGLESQL_RET_CHECK(output_expr_list.empty());
      return std::move(cross_join_scan);
    }

    return ResolvedProjectScanBuilder()
        .set_column_list(output_column_list)
        .set_expr_list(std::move(output_expr_list))
        .set_input_scan(std::move(cross_join_scan))
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

class TumbleFunctionRewriter : public Rewriter {
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
    TumbleRewriteVisitor rewriter(options, builtin_catalog, type_factory,
                                  column_factory, cte_name_generator);
    return rewriter.VisitAll(std::move(input));
  }

  std::string Name() const override { return "TumbleFunctionRewriter"; }

  bool ProvideUnfilteredCatalogToBuiltinRewriter() const override {
    return true;
  }
};

const Rewriter* GetTumbleFunctionRewriter() {
  static const auto* kRewriter = new TumbleFunctionRewriter;
  return kRewriter;
}

}  // namespace googlesql
