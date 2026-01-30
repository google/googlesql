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

#include "googlesql/analyzer/rewriters/pipe_describe_rewriter.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/rewriter_interface.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/column_factory.h"
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

// Rewriter for ResolvedDescribeScan.
//
// The rewrite for
//   <query>
//   |> DESCRIBE
// looks like
//   <query>
//   |> WHERE false
//   |> SELECT NULL AS Describe
//   |> UNION ALL (
//        SELECT <describe value> AS Describe
//      )
//
// This keeps the original query, so engines can still do ACL checks or other
// validation, but applies `WHERE false` on it to tell engines its output is
// not needed.  The desired single-row output comes from a separate branch
// of a UNION.
//
// The original ResolvedColumn produced by DESCRIBE becomes the ResolvedColumn
// output from UNION, so any later scans work as before.
class PipeDescribeRewriteVisitor : public ResolvedASTRewriteVisitor {
 public:
  explicit PipeDescribeRewriteVisitor(ColumnFactory* column_factory)
      : column_factory_(column_factory) {}

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedDescribeScan(
      std::unique_ptr<const ResolvedDescribeScan> node) override {
    if (!node->hint_list().empty()) {
      return MakeUnimplementedErrorAtNode(node.get())
             << "Pipe DESCRIBE does not support hints";
    }

    ResolvedColumn final_column = node->describe_expr()->column();
    const Type* column_type = final_column.type();

    ResolvedColumn input_branch_column =
        column_factory_->MakeCol("$rewrite_describe", "$null", column_type);

    ResolvedColumn describe_branch_column =
        column_factory_->MakeCol("$rewrite_describe", "$describe", column_type);

    ResolvedDescribeScanBuilder describe_scan = ToBuilder(std::move(node));

    // Build the UNION branch holding the original input scan.
    auto input_branch_scan = describe_scan.release_input_scan();

    // Add `|> WHERE false`.  This doesn't need to preserve any columns.
    input_branch_scan = MakeResolvedFilterScan(
        ResolvedColumnList(), std::move(input_branch_scan),
        MakeResolvedLiteral(Value::Bool(false)));

    // Add `|> SELECT NULL`.  This always has zero rows after the WHERE above,
    // but its columns still need to match for the UNION ALL.
    std::vector<std::unique_ptr<const ResolvedComputedColumn>>
        input_computed_column_list;
    input_computed_column_list.push_back(MakeResolvedComputedColumn(
        input_branch_column, MakeResolvedLiteral(Value::Null(column_type))));

    input_branch_scan = MakeResolvedProjectScan(
        ResolvedColumnList({input_branch_column}),
        std::move(input_computed_column_list), std::move(input_branch_scan));

    // Build the UNION branch for the DESCRIBE output.
    // It will just do a single-row table plus a SELECT to project the
    // literal expression from the original DescribeScan.

    // Change its computed column to `describe_branch_column`, since the
    // original column will become the UNION output column instead.
    ResolvedComputedColumnBuilder computed_column =
        ToBuilder(describe_scan.release_describe_expr());
    computed_column.set_column(describe_branch_column);
    GOOGLESQL_ASSIGN_OR_RETURN(auto new_computed_column,
                     std::move(computed_column).Build());

    std::vector<std::unique_ptr<const ResolvedComputedColumn>>
        describe_computed_column_list;
    describe_computed_column_list.push_back(std::move(new_computed_column));

    auto describe_branch_scan = MakeResolvedProjectScan(
        ResolvedColumnList({describe_branch_column}),
        std::move(describe_computed_column_list),
        MakeResolvedSingleRowScan(ResolvedColumnList()));

    // Make the UNION that combines the two branches.
    std::vector<std::unique_ptr<ResolvedSetOperationItem>> union_items;
    union_items.push_back(MakeResolvedSetOperationItem(
        std::move(input_branch_scan),
        ResolvedColumnList({input_branch_column})));
    union_items.push_back(MakeResolvedSetOperationItem(
        std::move(describe_branch_scan),
        ResolvedColumnList({describe_branch_column})));

    auto union_scan = MakeResolvedSetOperationScan(
        ResolvedColumnList({final_column}), ResolvedSetOperationScan::UNION_ALL,
        std::move(union_items));

    return std::move(union_scan);
  }

  ColumnFactory* column_factory_;
};

}  // namespace

class PipeDescribeRewriter : public Rewriter {
 public:
  std::string Name() const override { return "PipeDescribeRewriter"; }

  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, std::unique_ptr<const ResolvedNode> input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    GOOGLESQL_RET_CHECK(options.column_id_sequence_number() != nullptr);
    ColumnFactory column_factory(0, options.id_string_pool().get(),
                                 options.column_id_sequence_number());

    PipeDescribeRewriteVisitor rewriter(&column_factory);
    return rewriter.VisitAll(std::move(input));
  };
};

const Rewriter* GetPipeDescribeRewriter() {
  static const auto* const kRewriter = new PipeDescribeRewriter();
  return kRewriter;
}

}  // namespace googlesql
