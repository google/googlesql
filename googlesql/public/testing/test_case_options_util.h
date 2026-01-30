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

#ifndef GOOGLESQL_PUBLIC_TESTING_TEST_CASE_OPTIONS_UTIL_H_
#define GOOGLESQL_PUBLIC_TESTING_TEST_CASE_OPTIONS_UTIL_H_

#include "googlesql/public/language_options.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "file_based_test_driver/test_case_options.h"

namespace googlesql {

// TODO: b/439682125 - Everything in this file should be moved to internal, it's
// only used by internal tests.
inline constexpr absl::string_view kLanguageFeatures = "language_features";

// Returns a collection of LanguageFeatures that must be enabled for the
// provided 'test_case_options'.
absl::StatusOr<LanguageOptions::LanguageFeatureSet> GetRequiredLanguageFeatures(
    const file_based_test_driver::TestCaseOptions& test_case_options);

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_TESTING_TEST_CASE_OPTIONS_UTIL_H_
