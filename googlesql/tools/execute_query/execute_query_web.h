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

#ifndef GOOGLESQL_TOOLS_EXECUTE_QUERY_EXECUTE_QUERY_WEB_H_
#define GOOGLESQL_TOOLS_EXECUTE_QUERY_EXECUTE_QUERY_WEB_H_

#include <cstdint>

#include "absl/status/status.h"

namespace googlesql {

// Runs a local Web server that can be used to execute queries.
absl::Status RunExecuteQueryWebServer(int32_t port);

}  // namespace googlesql

#endif  // GOOGLESQL_TOOLS_EXECUTE_QUERY_EXECUTE_QUERY_WEB_H_
