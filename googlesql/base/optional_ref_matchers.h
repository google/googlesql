//
// Copyright 2024 Google LLC
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

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_OPTIONAL_REF_MATCHERS_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_OPTIONAL_REF_MATCHERS_H_

#include "gmock/gmock.h"

namespace googlesql_base {
namespace testing {

// We define a custom matcher instead of reusing testing::Optional because
// testing::Optional depends on operator bool() being defined. We avoid
// defining operator bool() because it is error prone for optional_ref<bool>.
MATCHER_P(HasValue, matcher, "") {
  if (!arg.has_value()) {
    *result_listener << "which is not engaged";
    return false;
  }
  return ExplainMatchResult(matcher, *arg, result_listener);
}

}  // namespace testing
}  // namespace googlesql_base

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_OPTIONAL_REF_MATCHERS_H_
