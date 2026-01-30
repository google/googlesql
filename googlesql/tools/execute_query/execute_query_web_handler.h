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

#ifndef GOOGLESQL_TOOLS_EXECUTE_QUERY_EXECUTE_QUERY_WEB_HANDLER_H_
#define GOOGLESQL_TOOLS_EXECUTE_QUERY_EXECUTE_QUERY_WEB_HANDLER_H_

#include <optional>
#include <string>

#include "googlesql/resolved_ast/sql_builder.h"
#include "googlesql/tools/execute_query/execute_query_tool.h"
#include "googlesql/tools/execute_query/execute_query_writer.h"
#include "googlesql/tools/execute_query/web/embedded_resources.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql {

using ModeSet = absl::flat_hash_set<ExecuteQueryConfig::ToolMode>;

// Encapsulates request parameters.
class ExecuteQueryWebRequest {
 public:
  ExecuteQueryWebRequest(
      absl::Span<const std::string> str_modes,
      std::optional<ExecuteQueryConfig::SqlMode> sql_mode,
      std::optional<SQLBuilder::TargetSyntaxMode> target_syntax_mode,
      std::string query, std::string catalog,
      std::string enabled_language_features, std::string enabled_ast_rewrites);

  const std::string& query() const { return query_; }
  const ModeSet& modes() const { return modes_; }
  std::optional<ExecuteQueryConfig::SqlMode> sql_mode() const {
    return sql_mode_;
  }
  SQLBuilder::TargetSyntaxMode target_syntax_mode() const {
    return target_syntax_mode_;
  }
  const std::string& catalog() const { return catalog_; }

  const std::string& GetEnabledLanguageFeaturesOptionsStr() const {
    return enabled_language_features_;
  }

  const std::string& GetEnabledAstRewritesOptionsStr() const {
    return enabled_ast_rewrites_;
  }

  std::string DebugString() const;

 private:
  ModeSet modes_;
  std::optional<ExecuteQueryConfig::SqlMode> sql_mode_;
  SQLBuilder::TargetSyntaxMode target_syntax_mode_;
  std::string query_;
  std::string catalog_;
  std::string enabled_language_features_;
  std::string enabled_ast_rewrites_;
};

// Handler for a web request. This class takes an incoming request, executes
// the query, and writes the results in HTML.
class ExecuteQueryWebHandler {
 public:
  explicit ExecuteQueryWebHandler(const QueryWebTemplates& templates)
      : templates_(templates) {}
  ~ExecuteQueryWebHandler() = default;

  using Writer = absl::AnyInvocable<bool(const absl::string_view) const>;

  bool HandleRequest(const ExecuteQueryWebRequest& request,
                     const Writer& writer);

 private:
  absl::Status ExecuteQueryImpl(const ExecuteQueryWebRequest& request,
                                ExecuteQueryWriter& exec_query_writer);
  bool ExecuteQuery(const ExecuteQueryWebRequest& request,
                    std::string& error_msg,
                    ExecuteQueryWriter& exec_query_writer);

  const QueryWebTemplates& templates_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_TOOLS_EXECUTE_QUERY_EXECUTE_QUERY_WEB_HANDLER_H_
