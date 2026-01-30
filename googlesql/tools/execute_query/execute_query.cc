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

// Tool for running a query against a Catalog constructed from various input
// sources. Also serves as a demo of the PreparedQuery API.

#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/tools/execute_query/execute_query_tool.h"
#include "googlesql/tools/execute_query/execute_query_web.h"
#include "googlesql/tools/execute_query/execute_query_writer.h"
#include "absl/flags/flag.h"
#include "absl/functional/bind_front.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/flags/usage_config.h"
#include "absl/log/initialize.h"
#include "absl/strings/match.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "googlesql/base/status_macros.h"

namespace {
constexpr absl::string_view kHistoryFileName{
    ".googlesql_execute_query_history"};
}

ABSL_FLAG(bool, web, false, "Run a local webserver to execute queries.");
ABSL_FLAG(int32_t, port, 8080, "Port to run the local webserver on.");

namespace googlesql {
namespace {

absl::Status RunTool(const std::vector<std::string>& args) {
  ExecuteQueryConfig config;

  GOOGLESQL_RETURN_IF_ERROR(InitializeExecuteQueryConfig(config));

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ExecuteQueryWriter> writer,
                   MakeWriterFromFlags(config, std::cout));

  if (absl::GetFlag(FLAGS_web)) {
    return RunExecuteQueryWebServer(absl::GetFlag(FLAGS_port));
  }

  const std::string sql = absl::StrJoin(args, " ");
  return ExecuteQuery(sql, config, *writer);
}
}  // namespace
}  // namespace googlesql

// Make --help show these flags.
static bool HelpFilter(absl::string_view module) {
  return absl::StrContains(module, "/execute_query.cc") ||
         absl::StrContains(module, "/execute_query_tool.cc");
}

// Make --helpshort show these flags.
static bool HelpShortFilter(absl::string_view module) {
  return absl::StrContains(module, "/execute_query.cc");
}

int main(int argc, char* argv[]) {
  const char kUsage[] =
      "Usage: execute_query "
      "{ \"<sql>\" | {--web [--port=<port>] } }\n";

  std::vector<std::string> args;

  absl::FlagsUsageConfig flag_config;
  flag_config.contains_help_flags = &HelpFilter;
  flag_config.contains_helpshort_flags = &HelpShortFilter;
  absl::SetFlagsUsageConfig(flag_config);

  absl::SetProgramUsageMessage(kUsage);
  {
    std::vector<char*> remaining_args = absl::ParseCommandLine(argc, argv);
    args.assign(remaining_args.cbegin() + 1, remaining_args.cend());
  }
  absl::InitializeLog();

  bool args_needed = !absl::GetFlag(FLAGS_web);

  if (args_needed && args.empty()) {
    std::cerr << kUsage << "Pass --help for a full list of flags.\n";
    return 1;
  }

  if (const absl::Status status = googlesql::RunTool(args); status.ok()) {
    return 0;
  } else {
    std::cerr << status.message() << '\n';
    return 1;
  }
}
