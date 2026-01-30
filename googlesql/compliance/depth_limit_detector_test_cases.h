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

#ifndef GOOGLESQL_COMPLIANCE_DEPTH_LIMIT_DETECTOR_TEST_CASES_H_
#define GOOGLESQL_COMPLIANCE_DEPTH_LIMIT_DETECTOR_TEST_CASES_H_

#include <cstddef>
#include <functional>
#include <ostream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "googlesql/public/language_options.h"
#include "absl/base/attributes.h"
#include "absl/functional/any_invocable.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/time/time.h"

namespace googlesql {
class DepthLimitDetectorTemplate;

struct DepthLimitDetectorTestCase;
struct DepthLimitDetectorTestResult;

LanguageOptions DepthLimitDetectorTestCaseLanguageOptions(
    const DepthLimitDetectorTestCase& depth_case);

struct DepthLimitDetectorRuntimeControl {
  // Attempt to control amount of time spent in the initial phase of searching
  // for a crash, before bisecting to find the exact depth a status changes at.
  absl::Duration max_probing_duration = absl::InfiniteDuration();
  // Do not search for cases which expand to larger queries than this parameter.
  size_t max_sql_bytes = 1000000;
};

// Find all return statuses of the test_driver_function for all
// instantiations of a query template and return them as ordered ranges.
// While the query may get very complex, the test_driver_function should never
// crash.
DepthLimitDetectorTestResult RunDepthLimitDetectorTestCase(
    DepthLimitDetectorTestCase const& depth_limit_case,
    absl::FunctionRef<absl::Status(absl::string_view)> test_driver_function,
    const DepthLimitDetectorRuntimeControl& runtime_control);

ABSL_DEPRECATED("Explicitly set the runtime limits via `runtime_control`")
DepthLimitDetectorTestResult RunDepthLimitDetectorTestCase(
    DepthLimitDetectorTestCase const& depth_limit_case,
    absl::FunctionRef<absl::Status(absl::string_view)> test_driver_function);

// SQL test cases.
absl::Span<const std::reference_wrapper<const DepthLimitDetectorTestCase>>
AllDepthLimitDetectorTestCases();

// JSON test cases.
absl::Span<const std::reference_wrapper<const DepthLimitDetectorTestCase>>
JSONDepthLimitDetectorTestCases();

std::string DepthLimitDetectorTemplateToString(
    const DepthLimitDetectorTemplate& depth_limit_template, int depth);
std::string DepthLimitDetectorTemplateToString(
    const DepthLimitDetectorTestCase& depth_case, int depth);

struct DepthLimitDetectorReturnCondition {
  int starting_depth;
  int ending_depth;
  absl::Status return_status;
};
std::ostream& operator<<(std::ostream& os,
                         const DepthLimitDetectorReturnCondition& condition);

struct DepthLimitDetectorTestResult {
  absl::string_view depth_limit_test_case_name;
  std::vector<DepthLimitDetectorReturnCondition>
      depth_limit_detector_return_conditions;
};
std::ostream& operator<<(std::ostream& os,
                         const DepthLimitDetectorTestResult& test_result);

std::ostream& operator<<(std::ostream& os,
                         const DepthLimitDetectorTestCase& test_case);

absl::AnyInvocable<std::vector<std::tuple<std::string>>() const>
DepthLimitDetectorSeeds(
    absl::AnyInvocable<absl::Status(absl::string_view) const>
        test_driver_function);

}  // namespace googlesql

#endif  // GOOGLESQL_COMPLIANCE_DEPTH_LIMIT_DETECTOR_TEST_CASES_H_
