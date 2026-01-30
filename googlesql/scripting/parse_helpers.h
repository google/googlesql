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

#ifndef GOOGLESQL_SCRIPTING_PARSE_HELPERS_H_
#define GOOGLESQL_SCRIPTING_PARSE_HELPERS_H_

#include <memory>

#include "googlesql/parser/parser.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/scripting/parsed_script.h"
#include "absl/base/macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace googlesql {
// Parses a script and verifies that it passes some basic checks:
// - The nesting depth of scripting constructs is within allowable limits
// - All BREAK and CONTINUE statements happen within a loop
// - Variable declarations do not have the same name as another variable in
//   the same block or any enclosing block.
absl::StatusOr<std::unique_ptr<ParserOutput>> ParseAndValidateScript(
    absl::string_view script_string, const ParserOptions& parser_options,
    ErrorMessageOptions error_message_options,
    const ParsedScriptOptions& parsed_script_options = {});

ABSL_DEPRECATED("Inline me!")
inline absl::StatusOr<std::unique_ptr<ParserOutput>> ParseAndValidateScript(
    absl::string_view script_string, const ParserOptions& parser_options,
    ErrorMessageMode error_message_mode,
    const ParsedScriptOptions& parsed_script_options = {}) {
  return ParseAndValidateScript(script_string, parser_options,
                                {.mode = error_message_mode},
                                parsed_script_options);
}
}  // namespace googlesql

#endif  // GOOGLESQL_SCRIPTING_PARSE_HELPERS_H_
