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

#include "googlesql/tools/execute_query/execute_query_web_handler.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/options_utils.h"
#include "googlesql/resolved_ast/sql_builder.h"
#include "googlesql/tools/execute_query/execute_query_tool.h"
#include "googlesql/tools/execute_query/execute_query_web_writer.h"
#include "googlesql/tools/execute_query/execute_query_writer.h"
#include "googlesql/tools/execute_query/selectable_catalog.h"
#include "googlesql/tools/execute_query/web/embedded_resources.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/span.h"
#include "mstch/mstch.hpp"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {

ModeSet ModeSetFromStrings(absl::Span<const std::string> mode_strings) {
  ModeSet modes;
  for (const auto &mode : mode_strings) {
    std::optional<ExecuteQueryConfig::ToolMode> tool_mode =
        ExecuteQueryConfig::parse_tool_mode(mode);
    if (tool_mode.has_value()) {
      modes.insert(*tool_mode);
    }
  }

  if (modes.empty()) {
    modes.insert(ExecuteQueryConfig::ToolMode::kExecute);
  }

  return modes;
}

}  // namespace

ExecuteQueryWebRequest::ExecuteQueryWebRequest(
    absl::Span<const std::string> str_modes,
    std::optional<ExecuteQueryConfig::SqlMode> sql_mode,
    std::optional<SQLBuilder::TargetSyntaxMode> target_syntax_mode,
    std::string query, std::string catalog,
    std::string enabled_language_features, std::string enabled_ast_rewrites)
    : modes_(ModeSetFromStrings(str_modes)),
      query_(std::move(query)),
      catalog_(std::move(catalog)),
      enabled_language_features_(std::move(enabled_language_features)),
      enabled_ast_rewrites_(std::move(enabled_ast_rewrites)) {
  if (sql_mode.has_value()) {
    sql_mode_ = sql_mode;
  } else {
    sql_mode_ =
        ExecuteQueryConfig::parse_sql_mode(absl::GetFlag(FLAGS_sql_mode))
            .value_or(ExecuteQueryConfig::SqlMode::kQuery);
  }

  if (target_syntax_mode.has_value()) {
    target_syntax_mode_ = *target_syntax_mode;
  } else {
    target_syntax_mode_ =
        ExecuteQueryConfig::parse_target_syntax_mode(
            absl::GetFlag(FLAGS_target_syntax))
            .value_or(SQLBuilder::TargetSyntaxMode::kStandard);
  }

  // TODO: Also default modes_ to flags if not supplied.

  if (catalog_.empty()) {
    // Get the value from the flag on the initial page load.
    catalog_ = absl::GetFlag(FLAGS_catalog);
  }

  // The query retrieved from the POST body has newlines as \r\n as per the
  // encoding rules for the form-urlencoded content-type. Replace these with
  // proper newlines so that our javascript code for mapping byte offsets to
  // line/column numbers works correctly.
  absl::StrReplaceAll({{"\r\n", "\n"}}, &query_);

  // The query input sometimes contains non-breaking spaces (or the HTML entity
  // &nbsp;). This is 0xA0 (encoded in UTF-8 as \xc2\xa0). A bare 0xa0 is
  // sometimes seen when a query is copied from an HTML editor.
  // We explicitly replace these sequences with a normal space character.
  absl::StrReplaceAll({{"\xc2\xa0", " "}, {"\xa0", " "}}, &query_);
}

std::string ExecuteQueryWebRequest::DebugString() const {
  std::vector<absl::string_view> modes;
  for (const auto &mode : modes_) {
    modes.push_back(ExecuteQueryConfig::tool_mode_name(mode));
  }
  std::sort(modes.begin(), modes.end());

  absl::string_view sql_mode_name =
      sql_mode_.has_value() ? ExecuteQueryConfig::sql_mode_name(*sql_mode_)
                            : "none";

  absl::string_view target_syntax_mode_name =
      ExecuteQueryConfig::target_syntax_mode_name(target_syntax_mode_);

  return absl::StrCat(
      "modes: [", absl::StrJoin(modes, ","), "], catalog: ", catalog_,
      ", sql_mode: ", sql_mode_name, ", syntax: ", target_syntax_mode_name,
      ", query_size: ", query_.size(),
      ", lang_features: ", GetEnabledLanguageFeaturesOptionsStr(),
      ", ast_rewrites: ", GetEnabledAstRewritesOptionsStr());
}

bool ExecuteQueryWebHandler::HandleRequest(
    const ExecuteQueryWebRequest &request, const Writer &writer) {
  mstch::map template_params = {{"query", request.query()},
                                {"css", templates_.GetWebPageCSS()}};

  mstch::array catalogs;
  for (const SelectableCatalogInfo &catalog_info :
       GetSelectableCatalogsInfo()) {
    mstch::map entry = {{"name", std::string(catalog_info.name)},
                        {"label", absl::StrCat(catalog_info.name, " - ",
                                               catalog_info.description)}};
    if (catalog_info.name == request.catalog()) {
      entry["selected"] = std::string("selected");
    }
    catalogs.push_back(entry);
  }
  template_params.insert(std::pair("catalogs", catalogs));

  std::string selected_feature = request.GetEnabledLanguageFeaturesOptionsStr();
  if (selected_feature.empty()) {
    selected_feature = "MAXIMUM";
  }
  mstch::array language_features;
  for (const absl::string_view options_str : {"NONE", "MAXIMUM", "DEV"}) {
    mstch::map entry = {{"name", std::string(options_str)}};
    if (options_str == selected_feature) {
      entry["selected"] = std::string("selected");
    }
    language_features.push_back(entry);
  }
  template_params.insert(std::pair("language_features", language_features));

  std::string selected_ast_rewrites = request.GetEnabledAstRewritesOptionsStr();
  if (selected_ast_rewrites.empty()) {
    selected_ast_rewrites = "ALL_MINUS_DEV";
  }
  mstch::array ast_rewrites;
  for (const absl::string_view options_str :
       {"NONE", "ALL", "ALL_MINUS_DEV", "DEFAULTS", "DEFAULTS_MINUS_DEV"}) {
    mstch::map entry = {{"name", std::string(options_str)}};
    if (options_str == selected_ast_rewrites) {
      entry["selected"] = std::string("selected");
    }
    ast_rewrites.push_back(entry);
  }
  template_params.insert(std::pair("ast_rewrites", ast_rewrites));

  if (!request.query().empty()) {
    std::string error_msg;
    ExecuteQueryWebWriter params_writer(template_params);
    ExecuteQuery(request, error_msg, params_writer);
    params_writer.FlushStatement(/*at_end=*/true, error_msg);
  }

  // Add the selected modes back into the template so that the same checkboxes
  // are checked when the page is rendered again.
  for (const auto &mode : request.modes()) {
    template_params[absl::StrCat(
        "mode_", ExecuteQueryConfig::tool_mode_name(mode))] = true;
  }
  if (request.sql_mode().has_value()) {
    template_params[absl::StrCat(
        "sql_mode_", ExecuteQueryConfig::sql_mode_name(*request.sql_mode()))] =
        true;
  }
  template_params[absl::StrCat("target_syntax_mode_",
                               ExecuteQueryConfig::target_syntax_mode_name(
                                   request.target_syntax_mode()))] = true;

  // Render the page.
  std::string rendered =
      mstch::render(templates_.GetWebPageContents(), template_params,
                    {{"body", templates_.GetWebPageBody()},
                     {"table", templates_.GetTable()}});

  if (writer(rendered) <= 0) {
    ABSL_LOG(WARNING) << "Error writing rendered HTML.";
    return false;
  }

  return true;
}

absl::Status ExecuteQueryWebHandler::ExecuteQueryImpl(
    const ExecuteQueryWebRequest &request,
    ExecuteQueryWriter &exec_query_writer) {
  // TODO: Try to avoid creating a new config each time
  ExecuteQueryConfig config;
  GOOGLESQL_RETURN_IF_ERROR(googlesql::InitializeExecuteQueryConfig(config));

  config.set_tool_modes(request.modes());
  if (request.sql_mode().has_value()) {
    config.set_sql_mode(*request.sql_mode());
  }
  config.set_target_syntax_mode(request.target_syntax_mode());

  config.mutable_analyzer_options().set_error_message_mode(
      ERROR_MESSAGE_MULTI_LINE_WITH_CARET);

  const std::string &enabled_language_features =
      request.GetEnabledLanguageFeaturesOptionsStr();
  if (!enabled_language_features.empty()) {
    auto language_features = googlesql::internal::ParseEnabledLanguageFeatures(
        enabled_language_features);
    if (!language_features.ok()) {
      return language_features.status();
    }
    GOOGLESQL_VLOG(1) << "Enabled language features: " << enabled_language_features;
    config.mutable_analyzer_options()
        .mutable_language()
        ->SetEnabledLanguageFeatures({language_features->options.begin(),
                                      language_features->options.end()});

    config.SetBuiltinsCatalogFromLanguageOptions(
        config.analyzer_options().language());
  }

  const std::string &enabled_ast_rewrites =
      request.GetEnabledAstRewritesOptionsStr();
  if (!enabled_ast_rewrites.empty()) {
    auto ast_rewrites =
        googlesql::internal::ParseEnabledAstRewrites(enabled_ast_rewrites);
    if (!ast_rewrites.ok()) {
      return ast_rewrites.status();
    }
    GOOGLESQL_VLOG(1) << "Enabled AST rewrites: " << enabled_ast_rewrites;
    config.mutable_analyzer_options().set_enabled_rewrites(
        {ast_rewrites->options.begin(), ast_rewrites->options.end()});
  }

  GOOGLESQL_RETURN_IF_ERROR(config.SetCatalogFromString(request.catalog()));

  GOOGLESQL_RETURN_IF_ERROR(
      googlesql::ExecuteQuery(request.query(), config, exec_query_writer));
  return absl::OkStatus();
}

bool ExecuteQueryWebHandler::ExecuteQuery(
    const ExecuteQueryWebRequest &request, std::string &error_msg,
    ExecuteQueryWriter &exec_query_writer) {
  absl::Status st = ExecuteQueryImpl(request, exec_query_writer);
  if (!st.ok()) {
    error_msg = st.message();
  }
  return st.ok();
}

}  // namespace googlesql
