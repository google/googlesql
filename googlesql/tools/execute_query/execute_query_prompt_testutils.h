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

#ifndef GOOGLESQL_TOOLS_EXECUTE_QUERY_EXECUTE_QUERY_PROMPT_TESTUTILS_H_
#define GOOGLESQL_TOOLS_EXECUTE_QUERY_EXECUTE_QUERY_PROMPT_TESTUTILS_H_

#include "googlesql/tools/execute_query/execute_query_prompt.h"
#include "gmock/gmock.h"
#include "absl/strings/string_view.h"

namespace googlesql {

// Identify boundaries of word under cursor using `absl::ascii_isalnum` and
// populate a completion request.
ExecuteQueryCompletionRequest PrepareCompletionReq(absl::string_view body,
                                                   size_t cursor_position);

// Create a gMock matcher for a completion response.
template <typename M1, typename M2>
auto CompletionResponseMatcher(M1 prefix_start_matcher, M2 items_matcher) {
  return ::testing::AllOf(
      ::testing::Field("prefix_start",
                       &ExecuteQueryCompletionResult::prefix_start,
                       prefix_start_matcher),
      ::testing::Field("items", &ExecuteQueryCompletionResult::items,
                       items_matcher));
}

}  // namespace googlesql

#endif  // GOOGLESQL_TOOLS_EXECUTE_QUERY_EXECUTE_QUERY_PROMPT_TESTUTILS_H_
