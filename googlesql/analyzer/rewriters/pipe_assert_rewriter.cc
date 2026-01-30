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

#include "googlesql/analyzer/rewriters/pipe_assert_rewriter.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_ast_rewrite_visitor.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/rewrite_utils.h"
#include "absl/status/statusor.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {

// Rewriter for AssertScan.
//
// Pipe ASSERT operators are rewritten using FilterScans and BarrierScans. For
// example, the following AssertScan:
//
// FROM Table
// |> ASSERT <condition>, <payload>
//
// is rewritten as:
//
// FROM Table
// |> Barrier
// |> WHERE IF(condition, TRUE, ERROR(error_message))
// |> Barrier
//
// where "Barrier" is a fake syntax representing a BarrierScan.
class PipeAssertRewriteVisitor : public ResolvedASTRewriteVisitor {
 public:
  PipeAssertRewriteVisitor(const AnalyzerOptions& analyzer_options,
                           Catalog& catalog, TypeFactory& type_factory)
      : fn_builder_(analyzer_options, catalog, type_factory) {}

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedAssertScan(
      std::unique_ptr<const ResolvedAssertScan> node) override {
    ResolvedColumnList column_list = node->column_list();
    ResolvedAssertScanBuilder assert_scan_builder = ToBuilder(std::move(node));

    // Construct the error message corresponding to the SQL:
    // CONCAT("Assert failed: ", <message>)
    //
    // The resolver guarantees the <message> is not NULL.
    std::vector<std::unique_ptr<const ResolvedExpr>> concat_args;
    concat_args.push_back(
        MakeResolvedLiteral(values::String("Assert failed: ")));
    concat_args.push_back(assert_scan_builder.release_message());
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedFunctionCall> concat_expr,
                     fn_builder_.Concat(std::move(concat_args)));

    // ERROR(error_message)
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedFunctionCall> error_expr,
        fn_builder_.Error(std::move(concat_expr), types::BoolType()));

    // IF(condition, TRUE, ERROR(error_message))
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedFunctionCall> if_expr,
                     fn_builder_.If(assert_scan_builder.release_condition(),
                                    MakeResolvedLiteral(values::True()),
                                    std::move(error_expr)));

    // FROM Table
    // |> Barrier
    // |> WHERE IF(condition, TRUE, ERROR(error_message))
    // |> Barrier
    return ResolvedBarrierScanBuilder()
        .set_column_list(column_list)
        .set_input_scan(
            ResolvedFilterScanBuilder()
                .set_column_list(column_list)
                .set_input_scan(
                    ResolvedBarrierScanBuilder()
                        .set_column_list(column_list)
                        .set_input_scan(
                            assert_scan_builder.release_input_scan()))
                .set_filter_expr(std::move(if_expr)))
        .Build();
  }

 private:
  FunctionCallBuilder fn_builder_;
};

}  // namespace

class PipeAssertRewriter : public Rewriter {
 public:
  std::string Name() const override { return "PipeAssertRewriter"; }

  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, std::unique_ptr<const ResolvedNode> input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    GOOGLESQL_RET_CHECK(options.id_string_pool() != nullptr);
    GOOGLESQL_RET_CHECK(options.column_id_sequence_number() != nullptr);
    PipeAssertRewriteVisitor rewriter(options, catalog, type_factory);
    return rewriter.VisitAll(std::move(input));
  };
};

const Rewriter* GetPipeAssertRewriter() {
  static const auto* const kRewriter = new PipeAssertRewriter();
  return kRewriter;
}

}  // namespace googlesql
