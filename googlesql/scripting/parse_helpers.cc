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

#include "googlesql/scripting/parse_helpers.h"

#include <memory>

#include "googlesql/parser/parser.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/scripting/parsed_script.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
absl::StatusOr<std::unique_ptr<ParserOutput>> ParseAndValidateScript(
    absl::string_view script_string, const ParserOptions& parser_options,
    ErrorMessageOptions error_message_options,
    const ParsedScriptOptions& parsed_script_options) {
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_RETURN_IF_ERROR(ParseScript(script_string, parser_options,
                              error_message_options, &parser_output));

  // Verify that we can obtain a ParsedScript from the AST. This performs
  // various checks, such as that BREAK and CONTINUE statements have an
  // enclosing loop.
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ParsedScript> parsed_script,
      ParsedScript::Create(script_string, parser_output->script(),
                           error_message_options, parsed_script_options));
  return parser_output;
}
}  // namespace googlesql
