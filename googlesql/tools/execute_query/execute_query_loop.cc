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

#include "googlesql/tools/execute_query/execute_query_loop.h"

#include <optional>
#include <string>

#include "googlesql/common/status_payload_utils.h"
#include "googlesql/tools/execute_query/execute_query.pb.h"
#include "googlesql/tools/execute_query/execute_query_prompt.h"
#include "googlesql/tools/execute_query/execute_query_tool.h"
#include "googlesql/tools/execute_query/execute_query_writer.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

using execute_query::ParserErrorContext;

absl::Status ExecuteQueryLoopNoOpStatusHandler(absl::Status status) {
  return status;
}

absl::Status ExecuteQueryLoop(
    ExecuteQueryPrompt& prompt, ExecuteQueryConfig& config,
    ExecuteQueryWriter& writer,
    const ExecuteQueryLoopStatusHandler status_handler) {
  for (;;) {
    const absl::StatusOr<std::optional<std::string>> input = prompt.Read();

    if (!input.ok()) {
      GOOGLESQL_RETURN_IF_ERROR(status_handler(input.status()));
      continue;
    }

    if (!input->has_value()) {
      // Reached end of input
      return absl::OkStatus();
    }

    absl::Status status = ExecuteQuery(**input, config, writer);

    if (!status.ok()) {
      ParserErrorContext ctx;
      ctx.set_text(absl::StripAsciiWhitespace(**input));
      internal::AttachPayload(&status, ctx);
    }

    GOOGLESQL_RETURN_IF_ERROR(status_handler(status));
  }
}

}  // namespace googlesql
