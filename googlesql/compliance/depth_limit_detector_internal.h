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

#ifndef GOOGLESQL_COMPLIANCE_DEPTH_LIMIT_DETECTOR_INTERNAL_H_
#define GOOGLESQL_COMPLIANCE_DEPTH_LIMIT_DETECTOR_INTERNAL_H_

#include <cstdint>
#include <limits>
#include <set>
#include <variant>
#include <vector>

#include "googlesql/public/options.pb.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace depth_limit_detector_internal {

// Templates for the testcases can either be strings, numbers for creating
// unique names, or repeated templates.
class DepthLimitDetectorDepthNumber {};
class DepthLimitDetectorRepeatedTemplate;
using DepthLimitDetectorTemplatePart =
    std::variant<absl::string_view, DepthLimitDetectorDepthNumber,
                 DepthLimitDetectorRepeatedTemplate>;

class DepthLimitDetectorRepeatedTemplate
    : public std::vector<DepthLimitDetectorTemplatePart> {
 public:
  using std::vector<DepthLimitDetectorTemplatePart>::vector;
};

}  // namespace depth_limit_detector_internal
class DepthLimitDetectorTemplate
    : public std::vector<
          depth_limit_detector_internal::DepthLimitDetectorTemplatePart> {
 public:
  using std::vector<
      depth_limit_detector_internal::DepthLimitDetectorTemplatePart>::vector;
};

struct DepthLimitDetectorTestCase {
  absl::string_view depth_limit_test_case_name;
  DepthLimitDetectorTemplate depth_limit_template;
  std::vector<LanguageFeature> depth_limit_required_features = {};
  int64_t depth_limit_max_depth = std::numeric_limits<int64_t>::max();
};

}  // namespace googlesql

#endif  // GOOGLESQL_COMPLIANCE_DEPTH_LIMIT_DETECTOR_INTERNAL_H_
