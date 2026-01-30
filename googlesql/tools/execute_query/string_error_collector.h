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

// Contains a google::protobuf::io::ErrorCollector implementation that appends errors and
// warnings to an unowned string pointer.  Errors are separated by newline '\n'
// characters.
#ifndef GOOGLESQL_TOOLS_EXECUTE_QUERY_STRING_ERROR_COLLECTOR_H_
#define GOOGLESQL_TOOLS_EXECUTE_QUERY_STRING_ERROR_COLLECTOR_H_

#include <string>

#include "google/protobuf/io/tokenizer.h"

namespace googlesql {

// Sample usage:
//
// google::protobuf::TextFormat::Parser parser;
// string all_errors;
// StringErrorCollector collector(&all_errors);
// parser.RecordErrorsTo(&collector);
// MyProto* mutable_proto;
// if (parser.Parse(GetProtoStream(), mutable_proto)) {
//   // All is well.
// } else {
//   ABSL_LOG(FATAL) << "Could not parse MyProto: " << std::endl << all_errors;
// }
//
class StringErrorCollector : public google::protobuf::io::ErrorCollector {
 public:
  StringErrorCollector(const StringErrorCollector&) = delete;
  StringErrorCollector& operator=(const StringErrorCollector&) = delete;

  // String error_text is unowned and must remain valid during the use of
  // StringErrorCollector.
  explicit StringErrorCollector(std::string* error_text);
  // If one_indexing is set to true, all line and column numbers will be
  // increased by one for cases when provided indices are 0-indexed and
  // 1-indexed error messages are desired
  StringErrorCollector(std::string* error_text, bool one_indexing);

  // Implementation of google::protobuf::io::ErrorCollector::AddError.
  void RecordError(int line, int column, absl::string_view message) override;

  // Implementation of google::protobuf::io::ErrorCollector::AddWarning.
  void RecordWarning(int line, int column, absl::string_view message) override;

 private:
  std::string* const error_text_;
  const int index_offset_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_TOOLS_EXECUTE_QUERY_STRING_ERROR_COLLECTOR_H_
