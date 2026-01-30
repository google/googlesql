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

#include "googlesql/analyzer/rewriters/pipe_if_rewriter.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/analyzer/rewriters/rewrite_subpipeline.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_ast_rewrite_visitor.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/rewrite_utils.h"
#include "absl/status/statusor.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {

// Rewriter for ResolvedPipeIfScan.
//
// Pipe IF is rewritten to just remove the IF and insert the chosen subpipeline,
// if any.  If no case is selected and there's no ELSE, IF is a no-op and
// the rewriter just removes it.
class PipeIfRewriteVisitor : public ResolvedASTRewriteVisitor {
 public:
  PipeIfRewriteVisitor(const AnalyzerOptions& analyzer_options,
                       Catalog& catalog, TypeFactory& type_factory) {}

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedPipeIfScan(
      std::unique_ptr<const ResolvedPipeIfScan> node) override {
    ResolvedColumnList column_list = node->column_list();

    if (!node->hint_list().empty()) {
      return MakeUnimplementedErrorAtNode(node.get())
             << "Pipe IF does not support hints";
    }

    ResolvedPipeIfScanBuilder builder = ToBuilder(std::move(node));
    auto scan = builder.release_input_scan();

    if (builder.selected_case() != -1) {
      auto subpipeline =
          const_cast<ResolvedPipeIfCase*>(
              builder.release_if_case_list()[builder.selected_case()].get())
              ->release_subpipeline();

      GOOGLESQL_ASSIGN_OR_RETURN(scan, RewriteSubpipelineToScan(std::move(subpipeline),
                                                      std::move(scan)));
    }

    // If the IfScan had a different column_list than the contained subpipeline
    // (e.g. because some columns were pruned in the IfScan's output), add a
    // ProjectScan to apply that column_list pruning.  This seems safer than
    // directly mutating the scan's column_list in case it was a ResolvedScan
    // with a meaningful column_list.
    if (builder.column_list() != scan->column_list()) {
      scan = MakeResolvedProjectScan(builder.column_list(),
                                     /*expr_list=*/{}, std::move(scan));
    }

    return scan;
  }
};

}  // namespace

class PipeIfRewriter : public Rewriter {
 public:
  std::string Name() const override { return "PipeIfRewriter"; }

  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, std::unique_ptr<const ResolvedNode> input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    PipeIfRewriteVisitor rewriter(options, catalog, type_factory);
    return rewriter.VisitAll(std::move(input));
  };
};

const Rewriter* GetPipeIfRewriter() {
  static const auto* const kRewriter = new PipeIfRewriter();
  return kRewriter;
}

}  // namespace googlesql
