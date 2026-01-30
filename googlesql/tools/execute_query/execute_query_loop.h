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

#ifndef GOOGLESQL_TOOLS_EXECUTE_QUERY_EXECUTE_QUERY_LOOP_H_
#define GOOGLESQL_TOOLS_EXECUTE_QUERY_EXECUTE_QUERY_LOOP_H_

#include "googlesql/tools/execute_query/execute_query_prompt.h"
#include "googlesql/tools/execute_query/execute_query_tool.h"
#include "googlesql/tools/execute_query/execute_query_writer.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"

namespace googlesql {

using ExecuteQueryLoopStatusHandler =
    absl::FunctionRef<absl::Status(absl::Status status)>;

// Returns status without modification.
absl::Status ExecuteQueryLoopNoOpStatusHandler(absl::Status status);

// Read queries from prompt and execute them until either the end of input is
// reached (see ExecuteQueryPrompt::Read) or an error occurs.
//
// The status handler is invoked for all status codes returned for queries,
// regardless of the actual status code (OK, invalid argument, etc.). If
// a non-OK status is returned from the handler the loop terminates and returns
// the status from the handler. A payload of type
// execute_query::ParserErrorContext contains the failed SQL statement. When
// used by an interactive shell the handler can log errors along with the
// failing query before returning an OK status to proceed with reading the next
// query. ExecuteQueryLoopNoOpStatusHandler is a status handler returning all
// values unmodified.
absl::Status ExecuteQueryLoop(ExecuteQueryPrompt& prompt,
                              ExecuteQueryConfig& config,
                              ExecuteQueryWriter& writer,
                              ExecuteQueryLoopStatusHandler status_handler =
                                  &ExecuteQueryLoopNoOpStatusHandler);

}  // namespace googlesql

#endif  // GOOGLESQL_TOOLS_EXECUTE_QUERY_EXECUTE_QUERY_LOOP_H_
