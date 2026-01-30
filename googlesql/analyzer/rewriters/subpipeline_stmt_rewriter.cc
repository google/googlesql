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

#include "googlesql/analyzer/rewriters/subpipeline_stmt_rewriter.h"

// This rewrites ResolvedSubpipelineStmt to a ResolvedGeneralizedSubqueryStmt
// by stitching the ResolvedTableScan into the ResolvedSubpipeline.

#include <memory>
#include <string>
#include <utility>

#include "googlesql/analyzer/rewriters/rewrite_subpipeline.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/base/check.h"
#include "absl/status/statusor.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

class SubpipelineStmtRewriter : public Rewriter {
 public:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, std::unique_ptr<const ResolvedNode> input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    if (input->node_kind() == RESOLVED_SUBPIPELINE_STMT) {
      ResolvedSubpipelineStmtBuilder builder = ToBuilder(
          GetAsResolvedNode<ResolvedSubpipelineStmt>(std::move(input)));

      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedScan> scan,
                       RewriteSubpipelineToScan(builder.release_subpipeline(),
                                                builder.release_table_scan()));

      auto result = MakeResolvedGeneralizedQueryStmt(
          builder.release_output_schema(), std::move(scan));
      result->set_hint_list(builder.release_hint_list());
      return result;
    }

    return std::move(input);
  }

  std::string Name() const override { return "SubpipelineStmtRewriter"; }
};

const Rewriter* GetSubpipelineStmtRewriter() {
  static const auto* const kRewriter = new SubpipelineStmtRewriter();
  return kRewriter;
}

}  // namespace googlesql
