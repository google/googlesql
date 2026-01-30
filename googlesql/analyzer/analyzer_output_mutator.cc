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


#include "googlesql/analyzer/analyzer_output_mutator.h"

#include <memory>
#include <utility>

#include "googlesql/common/status_payload_utils.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/proto/logging.pb.h"
#include "absl/flags/flag.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"

ABSL_FLAG(
    double, googlesql_stack_usage_proportion_warning, 0.8,
    "Warn if a GoogleSQL query uses more than this proportion of the stack");
namespace googlesql {

// We may call this function multiple times for the same AnalyzerOutput, e.g.
// if we are rewriting the tree. So it must not add the same warnings twice.
absl::StatusOr<std::unique_ptr<const AnalyzerOutput>>
AnalyzerOutputMutator::FinalizeAnalyzerOutput(
    std::unique_ptr<AnalyzerOutput> output) {
  ExecutionStats stats =
      output->runtime_info().overall_timed_value().ToExecutionStatsProto();

  // TODO: high complexity queries should fit into the more general
  // kinds of warning that will be available soon.
#ifndef __EMSCRIPTEN__
  ABSL_DCHECK_GT(stats.stack_available_bytes(), 0);
#endif  // __EMSCRIPTEN__
  if (stats.stack_peak_used_bytes() >
      stats.stack_available_bytes() *
          absl::GetFlag(FLAGS_googlesql_stack_usage_proportion_warning)) {
    absl::Status complexity_status =
        absl::ResourceExhaustedError("GoogleSQL query has high complexity.");
    bool existing = false;
    for (auto& warning : output->deprecation_warnings_) {
      if (warning == complexity_status) {
        existing = true;
        break;
      }
    }
    if (!existing) {
      DeprecationWarning warning_proto;
      warning_proto.set_kind(DeprecationWarning::QUERY_TOO_COMPLEX);

      googlesql::internal::AttachPayload(&complexity_status, warning_proto);

      output->deprecation_warnings_.push_back(complexity_status);
    }
  }
  return std::move(output);
}
}  // namespace googlesql
