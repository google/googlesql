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

#include "googlesql/tools/execute_query/string_error_collector.h"

#include <string>

#include "googlesql/base/logging.h"
#include "absl/strings/substitute.h"

namespace googlesql {

StringErrorCollector::StringErrorCollector(std::string* error_text)
    : StringErrorCollector(error_text, false) {}

StringErrorCollector::StringErrorCollector(std::string* error_text,
                                           bool one_indexing)
    : error_text_(error_text), index_offset_(one_indexing ? 1 : 0) {
  ABSL_CHECK_NE(error_text, nullptr);
}

void StringErrorCollector::RecordError(int line, int column,
                                       absl::string_view message) {
  absl::SubstituteAndAppend(error_text_, "$0($1): $2\n", line + index_offset_,
                            column + index_offset_, message);
}

void StringErrorCollector::RecordWarning(int line, int column,
                                         absl::string_view message) {
  RecordError(line, column, message);
}

}  // namespace googlesql
