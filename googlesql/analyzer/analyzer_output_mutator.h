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

#ifndef GOOGLESQL_ANALYZER_ANALYZER_OUTPUT_MUTATOR_H_
#define GOOGLESQL_ANALYZER_ANALYZER_OUTPUT_MUTATOR_H_

#include <memory>
#include <utility>

#include "googlesql/common/timer_util.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/analyzer_output_properties.h"
#include "googlesql/public/proto/logging.pb.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {
// Helper to allow mutating AnalyzerOutput.
class AnalyzerOutputMutator {
 public:
  // 'column_factory' and 'output' must outlive AnalyzerOutputMutator.
  explicit AnalyzerOutputMutator(AnalyzerOutput* output) : output_(*output) {}
  explicit AnalyzerOutputMutator(const std::unique_ptr<AnalyzerOutput>& output)
      : output_(*output) {}

  // Updates the output with the new ResolvedNode (and new max column id).
  absl::Status Update(std::unique_ptr<const ResolvedNode> node,
                      int max_column_id) {
    GOOGLESQL_RET_CHECK(node != nullptr);
    output_.max_column_id_ = max_column_id;
    if (node->IsStatement()) {
      output_.statement_.reset(node.release()->GetAs<ResolvedStatement>());
      return absl::OkStatus();
    }
    if (node->IsExpression()) {
      output_.expr_.reset(node.release()->GetAs<ResolvedExpr>());
      return absl::OkStatus();
    }
    return absl::Status(
        absl::StatusCode::kInternal,
        absl::StrCat("Unexpected new node type: ", node->node_kind_string()));
  }

  AnalyzerOutputProperties& mutable_output_properties() {
    return output_.analyzer_output_properties_;
  }

  std::unique_ptr<const ResolvedNode> release_output_node() {
    if (output_.statement_ != nullptr) {
      return std::move(output_.statement_);
    } else {
      return std::move(output_.expr_);
    }
  }

  const ResolvedNode* resolved_node() {
    if (output_.statement_ != nullptr) {
      return output_.statement_.get();
    } else {
      return output_.expr_.get();
    }
  }

  AnalyzerRuntimeInfo& mutable_runtime_info() { return output_.runtime_info_; }

  internal::TimedValue& overall_timed_value() {
    return mutable_runtime_info().impl_->overall_timed_value;
  }

  static absl::StatusOr<std::unique_ptr<const AnalyzerOutput>>
  FinalizeAnalyzerOutput(std::unique_ptr<AnalyzerOutput> output);

 private:
  AnalyzerOutput& output_;
};
}  // namespace googlesql
#endif  // GOOGLESQL_ANALYZER_ANALYZER_OUTPUT_MUTATOR_H_
