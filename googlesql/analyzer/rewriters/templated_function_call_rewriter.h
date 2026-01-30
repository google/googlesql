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

#ifndef GOOGLESQL_ANALYZER_REWRITERS_TEMPLATED_FUNCTION_CALL_REWRITER_H_
#define GOOGLESQL_ANALYZER_REWRITERS_TEMPLATED_FUNCTION_CALL_REWRITER_H_

#include <functional>
#include <memory>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "absl/status/statusor.h"

namespace googlesql {

// Applies `rewriters_func` to the SQL bodies of templated function calls inside
// of `input` so that rewriter based query features and functions can be used
// inside the body of templated functions (e.g. FLATTEN, PIVOT, TYPEOF).
absl::StatusOr<std::unique_ptr<const ResolvedNode>>
RewriteTemplatedFunctionCalls(
    const AnalyzerOptions& analyzer_options,
    std::function<absl::StatusOr<std::unique_ptr<const ResolvedNode>>(
        const AnalyzerOptions& analyzer_options,
        std::unique_ptr<const ResolvedNode>)>
        rewriters_func,
    std::unique_ptr<const ResolvedNode> input);

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_REWRITERS_TEMPLATED_FUNCTION_CALL_REWRITER_H_
