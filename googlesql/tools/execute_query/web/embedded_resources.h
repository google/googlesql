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

#ifndef GOOGLESQL_TOOLS_EXECUTE_QUERY_WEB_EMBEDDED_RESOURCES_H_
#define GOOGLESQL_TOOLS_EXECUTE_QUERY_WEB_EMBEDDED_RESOURCES_H_

#include <string>

namespace googlesql {

// Interface for accessing the templates used in the web server.
class QueryWebTemplates {
 public:
  QueryWebTemplates();
  virtual ~QueryWebTemplates() = default;

  static const QueryWebTemplates& Default();

  virtual const std::string& GetWebPageContents() const;
  virtual const std::string& GetWebPageCSS() const;
  virtual const std::string& GetWebPageBody() const;
  virtual const std::string& GetTable() const;

  std::string page_template_, style_css_, page_body_, table_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_TOOLS_EXECUTE_QUERY_WEB_EMBEDDED_RESOURCES_H_
