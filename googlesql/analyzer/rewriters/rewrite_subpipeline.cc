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

#include "googlesql/analyzer/rewriters/rewrite_subpipeline.h"

#include <memory>
#include <utility>

#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_rewrite_visitor.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {

class SubpipelineRewriteVisitor : public ResolvedASTRewriteVisitor {
 public:
  explicit SubpipelineRewriteVisitor(
      std::unique_ptr<const ResolvedScan> input_scan)
      : input_scan_(std::move(input_scan)) {}

  absl::Status PreVisitResolvedSubpipeline(
      const ResolvedSubpipeline& node) override {
    ++subpipeline_depth_;
    return absl::OkStatus();
  }

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedSubpipeline(
      std::unique_ptr<const ResolvedSubpipeline> node) override {
    GOOGLESQL_RET_CHECK_GE(subpipeline_depth_, 1);
    --subpipeline_depth_;
    return std::move(node);
  }

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedSubpipelineInputScan(
      std::unique_ptr<const ResolvedSubpipelineInputScan> node) override {
    // If this scan belongs to a nested ResolvedSubpipeline, leave it.
    if (subpipeline_depth_ > 0) {
      return std::move(node);
    }

    GOOGLESQL_RET_CHECK(input_scan_ != nullptr)
        << "Found multiple ResolvedSubpipelineInputScans to replace";

    return std::move(input_scan_);
  }

  absl::Status CheckFinalStatus() {
    GOOGLESQL_RET_CHECK_EQ(subpipeline_depth_, 0);
    GOOGLESQL_RET_CHECK(input_scan_ == nullptr)
        << "Didn't find a ResolvedSubpipelineInputScan to replace";
    return absl::OkStatus();
  }

 private:
  std::unique_ptr<const ResolvedScan> input_scan_;
  int subpipeline_depth_ = 0;
};

}  // namespace

// TODO This rewrite implementation is potentially O(n^2) when
// there are nested subpipelines because it just rewrites one subpipeline at a
// time, and makes a visitor that traverses everything inside it, to find the
// ResolvedSubpipelineInputScan to replace.
//
// This isn't likely to be too big a problem because deeply nested subpipelines
// are unlikely.
//
// This could be improved a few ways.  The rewrite visitor could have a feature
// where PreVisit can make it stop visiting, to avoid traversing into
// subtreees (like nested ResolvedSubpipelines) unnecessarily.
// It might also be possible to combine multiple subpipeline visits into one
// rewrite, building a map of ResolvedSubpipelineInputScans to replace later.
absl::StatusOr<std::unique_ptr<const ResolvedScan>> RewriteSubpipelineToScan(
    std::unique_ptr<const ResolvedSubpipeline> subpipeline,
    std::unique_ptr<const ResolvedScan> input_scan) {
  SubpipelineRewriteVisitor rewriter(std::move(input_scan));

  auto subpipeline_builder = ToBuilder(std::move(subpipeline));

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedNode> node,
                   rewriter.VisitAll(subpipeline_builder.release_scan()));

  GOOGLESQL_RETURN_IF_ERROR(rewriter.CheckFinalStatus());

  GOOGLESQL_RET_CHECK(node->IsScan());
  return GetAsResolvedNode<ResolvedScan>(std::move(node));
}

}  // namespace googlesql
