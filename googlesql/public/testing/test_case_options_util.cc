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

#include "googlesql/public/testing/test_case_options_util.h"

#include "googlesql/common/options_utils.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "file_based_test_driver/test_case_options.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

absl::StatusOr<LanguageOptions::LanguageFeatureSet> GetRequiredLanguageFeatures(
    const file_based_test_driver::TestCaseOptions& test_case_options) {
  GOOGLESQL_ASSIGN_OR_RETURN(auto parsed_features,
                   internal::ParseEnabledLanguageFeatures(
                       test_case_options.GetString(kLanguageFeatures)));
  return LanguageOptions::LanguageFeatureSet(parsed_features.options.begin(),
                                             parsed_features.options.end());
}

}  // namespace googlesql
