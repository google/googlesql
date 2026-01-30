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

#ifndef GOOGLESQL_ANALYZER_RUN_ANALYZER_TEST_H_
#define GOOGLESQL_ANALYZER_RUN_ANALYZER_TEST_H_

#include <functional>
#include <memory>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "file_based_test_driver/run_test_case_result.h"
#include "file_based_test_driver/test_case_options.h"

namespace googlesql {

using TestDumperCallback = std::function<void(
    absl::string_view input,
    const file_based_test_driver::TestCaseOptions& test_options,
    const AnalyzerOptions& options,
    const absl::StatusOr<const AnalyzerOutput*>& output,
    const file_based_test_driver::RunTestCaseResult* result)>;

bool RunAllTests(TestDumperCallback callback);

// Returns a copy of <node> with parse locations stripped on all subtrees.
std::unique_ptr<ResolvedNode> StripParseLocations(const ResolvedNode* node);

}  // namespace googlesql


#endif  // GOOGLESQL_ANALYZER_RUN_ANALYZER_TEST_H_
