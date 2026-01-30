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

#include "googlesql/tools/execute_query/execute_query_web.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "googlesql/tools/execute_query/execute_query_tool.h"
#include "googlesql/tools/execute_query/execute_query_web_handler.h"
#include "googlesql/tools/execute_query/execute_query_web_server.h"
#include "googlesql/tools/execute_query/web/embedded_resources.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "CivetServer.h"
#include "civetweb.h"

namespace googlesql {

namespace {

constexpr absl::string_view kHttpHeaders =
    "HTTP/1.1 200 OK\r\nContent-Type: "
    "text/html\r\nConnection: close\r\n\r\n";

// HTTP handler for the Web UI.
class RootHandler : public CivetHandler {
 public:
  bool handleGet(CivetServer *server, struct mg_connection *conn) override {
    return handleAll("GET", server, conn);
  }
  bool handlePost(CivetServer *server, struct mg_connection *conn) override {
    return handleAll("POST", server, conn);
  }

 private:
  bool handleAll(const char *method, CivetServer *server,
                 struct mg_connection *conn) {
    absl::string_view request_uri = mg_get_request_info(conn)->request_uri;
    if (request_uri != "/") {
      ABSL_LOG(WARNING) << "Request " << request_uri << " not handled.";
      return false;
    }

    std::unique_ptr<ExecuteQueryWebRequest> request = ParseRequest(conn);
    if (!request) {
      ABSL_LOG(WARNING) << "Failed to parse request.";
      return false;
    }

    ExecuteQueryWebHandler::Writer writer =
        [conn](absl::string_view rendered_html) {
          mg_write(conn, rendered_html.data(), rendered_html.size());
          return rendered_html.size();
        };

    if (!writer(kHttpHeaders)) {
      ABSL_LOG(WARNING) << "Error writing HTTP headers.";
      return false;
    }

    ExecuteQueryWebHandler handler(QueryWebTemplates::Default());
    return handler.HandleRequest(*request, writer);
  }

  std::unique_ptr<ExecuteQueryWebRequest> ParseRequest(
      struct mg_connection *conn) {
    std::string query;
    CivetServer::getParam(conn, "query", query);

    std::string sql_mode;
    CivetServer::getParam(conn, "sql_mode", sql_mode);

    std::string target_syntax_mode;
    CivetServer::getParam(conn, "target_syntax_mode", target_syntax_mode);

    std::string catalog;
    CivetServer::getParam(conn, "catalog", catalog);

    std::string enabled_language_features;
    CivetServer::getParam(conn, "language-features", enabled_language_features);

    std::string enabled_ast_rewrites;
    CivetServer::getParam(conn, "ast-rewrites", enabled_ast_rewrites);

    return std::make_unique<ExecuteQueryWebRequest>(
        GetModesParams(conn), ExecuteQueryConfig::parse_sql_mode(sql_mode),
        ExecuteQueryConfig::parse_target_syntax_mode(target_syntax_mode), query,
        catalog, enabled_language_features, enabled_ast_rewrites);
  }

  // Gets all the modes currently checked in the form.
  std::vector<std::string> GetModesParams(struct mg_connection *conn) {
    std::vector<std::string> modes;
    for (int i = 0; i < 10; i++) {
      std::string mode;
      if (!CivetServer::getParam(conn, "mode", mode, i)) {
        break;
      }
      modes.push_back(mode);
    }
    return modes;
  }
};

// Used as a callback for the CivetServer..
int CivetLogMessage(const struct mg_connection *, const char *message) {
  ABSL_LOG(INFO) << "CivetWeb: " << message;
  return 0;
}

constexpr char kBanner[] = R"txt(
  ╔══════════════════════════════════════════╗
  ║ ┌─┐─┐ ┬┌─┐┌─┐┬ ┬┌┬┐┌─┐  ┌─┐ ┬ ┬┌─┐┬─┐┬ ┬ ║
  ║ ├┤ ┌┴┬┘├┤ │  │ │ │ ├┤   │─┼┐│ │├┤ ├┬┘└┬┘ ║
  ║ └─┘┴ └─└─┘└─┘└─┘ ┴ └─┘  └─┘└└─┘└─┘┴└─ ┴  ║
  ╚══════════════════════════════════════════╝

  Ready!
  http://%s:%i

)txt";

class WebServer : public ExecuteQueryWebServerInterface {
 public:
  WebServer() = default;
  ~WebServer() override = default;

  bool Start(int32_t port) override {
    mg_init_library(0);
    // Using +<port> as the listening port allows it to bind to both IPV4 and
    // IPV6 addresses.
    std::vector<std::string> options = {"listening_ports",
                                        absl::StrCat("+", port)};
    CivetCallbacks callbacks;
    callbacks.log_message = CivetLogMessage;
    civet_server_ = std::make_unique<CivetServer>(options, &callbacks);

    civet_server_->addHandler("/", &query_handler_);
    return true;
  }

  void Wait() override {
    absl::Notification notification;
    notification.WaitForNotification();
  }

  void Stop() override {
    civet_server_->close();
    mg_exit_library();
  }

 private:
  std::unique_ptr<CivetServer> civet_server_;
  RootHandler query_handler_;
};

std::unique_ptr<ExecuteQueryWebServerInterface> CreateWebServer() {

  return std::make_unique<WebServer>();
}

std::string GetHostname() {
  char hostname[1024];
  int ret = gethostname(hostname, sizeof(hostname));
  return std::string((ret == 0) ? hostname : "localhost");
}

}  // namespace

absl::Status RunExecuteQueryWebServer(int32_t port) {
  std::unique_ptr<ExecuteQueryWebServerInterface> server = CreateWebServer();
  if (!server->Start(port)) {
    return absl::InternalError("Failed to start web server.");
  }

  std::string hostname = GetHostname();
  std::printf(kBanner, hostname.c_str(), port);
  server->Wait();
  server->Stop();
  return absl::OkStatus();
}

}  // namespace googlesql
