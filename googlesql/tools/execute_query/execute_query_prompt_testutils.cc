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

#include "googlesql/tools/execute_query/execute_query_prompt_testutils.h"

#include <algorithm>
#include <iterator>

#include "googlesql/base/logging.h"
#include "googlesql/tools/execute_query/execute_query_prompt.h"
#include "absl/strings/ascii.h"

namespace googlesql {

ExecuteQueryCompletionRequest PrepareCompletionReq(absl::string_view body,
                                                   size_t cursor_position) {
  ExecuteQueryCompletionRequest req{
      .body = body,
      .cursor_position = cursor_position,
      .word_start = cursor_position,
      .word_end = cursor_position,
  };

  // A few tests deliberately use a cursor behind the body
  if (cursor_position > body.size()) {
    return req;
  }

  // Find word under cursor
  const auto cursor = std::next(body.cbegin(), cursor_position),
             start = std::find_if_not(std::make_reverse_iterator(cursor),
                                      std::make_reverse_iterator(body.cbegin()),
                                      &absl::ascii_isalnum)
                         .base(),
             end = std::find_if_not(cursor,
                                    std::next(body.cbegin(), cursor_position),
                                    &absl::ascii_isalnum);

  if (std::distance(start, end) > 0) {
    req.word_start = std::distance(body.cbegin(), start);
    req.word_end = std::distance(body.cbegin(), end);

    ABSL_CHECK_LE(req.word_start, req.word_end);

    req.word = body.substr(req.word_start, req.word_end - req.word_start);
  }

  return req;
}

}  // namespace googlesql
