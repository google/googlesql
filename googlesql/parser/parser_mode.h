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

#ifndef GOOGLESQL_PARSER_PARSER_MODE_H_
#define GOOGLESQL_PARSER_PARSER_MODE_H_

namespace googlesql {
namespace parser {

// Indicates what sort of GoogleSQL incantation we are parsing. This roughly
// corresponds to the start states declared in the Textmapper grammar.
enum class ParserMode {
  // Parse one statement until the end of the input.
  // Script statements are disallowed.
  kStatement,

  // Parse an entire script until the end of the input.
  kScript,

  // Parse one statement off the start of the input.
  // Script statements are disallowed.
  kNextStatement,

  // Parse one statement off the start of the input.
  // Script statements are allowed.
  kNextScriptStatement,

  // Quickly determine the statement kind of the first statement and return
  // only that.
  //
  // Script statements are disallowed.
  kNextStatementKind,

  // Parse one expression. No support for parsing multiple expressions.
  kExpression,

  // Parse one type. No support for parsing multiple types.
  kType,

  // No mode token is returned. This is used for raw tokenization.
  kTokenizer,

  // No mode token is returned. Comments are preserved. This is used for
  // formatting.
  kTokenizerPreserveComments,

  // Similar to kTokenizer mode in that its used for raw tokenization of
  // everything between the macro name and closing semi-colon. This disables
  // some token disambiguation and error production that occurs in the lexer
  // that might be wrong to apply to macros before they are expanded.
  kMacroBody,
};

// Controls whether and how macros are expanded during parsing.
enum class MacroExpansionMode {
  kNone = 0,
  kLenient = 1,
  kStrict = 2,
};

}  // namespace parser
}  // namespace googlesql

#endif  // GOOGLESQL_PARSER_PARSER_MODE_H_
