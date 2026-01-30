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

#ifndef GOOGLESQL_COMMON_PARSED_TEMPLATED_SQL_FUNCTION_H_
#define GOOGLESQL_COMMON_PARSED_TEMPLATED_SQL_FUNCTION_H_

#include <string>
#include <vector>

#include "googlesql/parser/parser.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/templated_sql_function.h"

namespace googlesql {

// A `TemplatedSQLFunction` that includes the parsed output of its creation
// statement. This can be used in places that need the parsed AST such as when
// analyzing expressions calling this function.
//
// <parser_output> is not owned by this class and must outlive it.
class ParsedTemplatedSQLFunction : public TemplatedSQLFunction {
 public:
  ParsedTemplatedSQLFunction(const std::vector<std::string>& function_name_path,
                             const FunctionSignature& signature,
                             const std::vector<std::string>& argument_names,
                             const ParseResumeLocation& parse_resume_location,
                             Mode mode, const FunctionOptions& options,
                             const ParserOutput* parser_output)
      : TemplatedSQLFunction(function_name_path, signature, argument_names,
                             parse_resume_location, mode, options),
        parser_output_(parser_output) {}

  // Returns this function's CREATE statement parser output.
  const ParserOutput* parser_output() const { return parser_output_; }

 private:
  // The parser output of this functions CREATE statement. Not owned.
  const ParserOutput* const parser_output_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_PARSED_TEMPLATED_SQL_FUNCTION_H_
