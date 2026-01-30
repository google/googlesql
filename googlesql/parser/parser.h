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

#ifndef GOOGLESQL_PARSER_PARSER_H_
#define GOOGLESQL_PARSER_PARSER_H_

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/base/arena.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/warning_sink.h"
#include "googlesql/parser/ast_node_kind.h"  
#include "googlesql/parser/macros/macro_catalog.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser_mode.h"
#include "googlesql/parser/parser_runtime_info.h"
#include "googlesql/parser/statement_properties.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "absl/types/variant.h"

namespace googlesql {

class ASTExpression;
class ASTNode;
class ASTScript;
class ASTStatement;
class ASTType;
class IdStringPool;
class ParseResumeLocation;

// ParserOptions contains options that affect parser behavior.
class ParserOptions {
 public:
  ABSL_DEPRECATED("Use the constructor overload that accepts LanguageOptions.")
  ParserOptions();

  ParserOptions(
      std::shared_ptr<IdStringPool> id_string_pool,
      std::shared_ptr<googlesql_base::UnsafeArena> arena, LanguageOptions language_options,
      ErrorMessageOptions error_message_options,
      parser::MacroExpansionMode macro_expansion_mode,
      const parser::macros::MacroCatalog* /*absl_nullable*/ macro_catalog);

  explicit ParserOptions(
      LanguageOptions language_options,
      parser::MacroExpansionMode macro_expansion_mode =
          parser::MacroExpansionMode::kNone,
      const parser::macros::MacroCatalog* macro_catalog = nullptr);

  // This will make a _copy_ of language_options. It is not referenced after
  // construction.
  ABSL_DEPRECATED("Inline me!")
  ParserOptions(std::shared_ptr<IdStringPool> id_string_pool,
                std::shared_ptr<googlesql_base::UnsafeArena> arena,
                const LanguageOptions* language_options)
      : ParserOptions(
            std::move(id_string_pool), std::move(arena),
            language_options ? *language_options : LanguageOptions()) {}

  ParserOptions(std::shared_ptr<IdStringPool> id_string_pool,
                std::shared_ptr<googlesql_base::UnsafeArena> arena,
                LanguageOptions language_options = {},
                parser::MacroExpansionMode macro_expansion_mode =
                    parser::MacroExpansionMode::kNone,
                const parser::macros::MacroCatalog* macro_catalog = nullptr);

  ~ParserOptions();

  // Sets an IdStringPool for storing strings used in parsing. If it is not set,
  // then the parser APIs will create a new IdStringPool for every query that is
  // parsed. WARNING: If this is set, calling Parse functions concurrently with
  // the same ParserOptions is not allowed.
  std::shared_ptr<IdStringPool> id_string_pool() const {
    return id_string_pool_;
  }

  // Sets an googlesql_base::UnsafeArena for storing objects created during parsing.  If it is
  // not set, then the parser APIs will create a new googlesql_base::UnsafeArena for every query
  // that is parsed. WARNING: If this is set, calling Parse functions
  // concurrently with the same ParserOptions is not allowed.
  void set_arena(std::shared_ptr<googlesql_base::UnsafeArena> arena) {
    arena_ = std::move(arena);
  }
  std::shared_ptr<googlesql_base::UnsafeArena> arena() const { return arena_; }

  // Creates a default-sized id_string_pool() and arena().
  // WARNING: After calling this, calling Parse functions concurrently with
  // the same ParserOptions is no longer allowed.
  void CreateDefaultArenasIfNotSet();

  // Returns true if arena() and id_string_pool() are both non-NULL.
  bool AllArenasAreInitialized() const {
    return arena_ != nullptr && id_string_pool_ != nullptr;
  }

  void set_language_options(LanguageOptions language_options) {
    language_options_ = std::move(language_options);
  }

  const LanguageOptions& language_options() const { return language_options_; }

  const ErrorMessageOptions& error_message_options() const {
    return error_message_options_;
  }

  ErrorMessageOptions& mutable_error_message_options() {
    return error_message_options_;
  }

  const parser::macros::MacroCatalog* macro_catalog() const {
    return macro_catalog_;
  }

  parser::MacroExpansionMode macro_expansion_mode() const {
    return macro_expansion_mode_;
  }

 private:
  // Allocate all AST nodes in this arena.
  // The arena will also be referenced in ParserOutput to keep it alive.
  std::shared_ptr<googlesql_base::UnsafeArena> arena_;

  // Allocate all IdStrings in the parse tree in this pool.
  // The pool will also be referenced in ParserOutput to keep it alive.
  std::shared_ptr<IdStringPool> id_string_pool_;

  LanguageOptions language_options_;
  parser::MacroExpansionMode macro_expansion_mode_;
  const parser::macros::MacroCatalog* macro_catalog_ = nullptr;
  ErrorMessageOptions error_message_options_;
};

// Output of a parse operation. The output parse tree can be accessed via
// statement(), expression(), or type(), depending on the parse function that
// was called.
class ParserOutput {
 public:
  ParserOutput(
      std::shared_ptr<IdStringPool> id_string_pool,
      std::shared_ptr<googlesql_base::UnsafeArena> arena,
      std::vector<std::unique_ptr<ASTNode>> other_allocated_ast_nodes,
      std::variant<std::unique_ptr<ASTStatement>, std::unique_ptr<ASTScript>,
                   std::unique_ptr<ASTType>, std::unique_ptr<ASTExpression>>
          node,
      WarningSink warnings, std::unique_ptr<ParserRuntimeInfo> info = nullptr);
  ParserOutput(const ParserOutput&) = delete;
  ParserOutput& operator=(const ParserOutput&) = delete;
  ~ParserOutput();

  // Getters for parse trees of different types corresponding to the different
  // parse statements.
  const ASTStatement* statement() const { return GetNodeAs<ASTStatement>(); }
  const ASTScript* script() const { return GetNodeAs<ASTScript>(); }
  const ASTType* type() const { return GetNodeAs<ASTType>(); }
  const ASTExpression* expression() const { return GetNodeAs<ASTExpression>(); }

  const ASTNode* node() const {
    if (std::holds_alternative<std::unique_ptr<ASTStatement>>(node_)) {
      return statement();
    }
    if (std::holds_alternative<std::unique_ptr<ASTScript>>(node_)) {
      return script();
    }
    if (std::holds_alternative<std::unique_ptr<ASTType>>(node_)) {
      return type();
    }
    if (std::holds_alternative<std::unique_ptr<ASTExpression>>(node_)) {
      return expression();
    }
    return nullptr;
  }

  // Returns the IdStringPool that stores IdStrings allocated for the parse
  // tree.  This was propagated from ParserOptions.
  const std::shared_ptr<IdStringPool>& id_string_pool() const {
    return id_string_pool_;
  }

  // Returns the arena that stores the parse tree.  This was propagated from
  // ParserOptions.
  const std::shared_ptr<googlesql_base::UnsafeArena>& arena() const { return arena_; }

  absl::Span<absl::Status const> warnings() const {
    return warnings_.warnings();
  }

  const ParserRuntimeInfo& runtime_info() const {
    ABSL_DCHECK(runtime_info_ != nullptr);
    return *runtime_info_;
  }

 private:
  template <class T>
  T* GetNodeAs() const {
    return std::get<std::unique_ptr<T>>(node_).get();
  }

  // This IdStringPool and arena must be kept alive for the parse trees below to
  // be valid. Careful: do not reorder these members to go after the ASTNodes
  // below, because the destruction order is relevant!
  std::shared_ptr<IdStringPool> id_string_pool_;
  std::shared_ptr<googlesql_base::UnsafeArena> arena_;

  // This vector owns the non-root nodes in the AST.
  std::vector<std::unique_ptr<ASTNode>> other_allocated_ast_nodes_;

  std::variant<std::unique_ptr<ASTStatement>, std::unique_ptr<ASTScript>,
               std::unique_ptr<ASTType>, std::unique_ptr<ASTExpression>>
      node_;

  WarningSink warnings_;

  std::unique_ptr<ParserRuntimeInfo> runtime_info_;
};

// Parses <statement_string> and returns the parser output in <output> upon
// success. The AST can be retrieved from output->statement().
//
// A semi-colon following the statement is optional.
//
// Script statements are not supported.
//
// This can return errors annotated with an ErrorLocation payload that indicates
// the input location of an error.
absl::Status ParseStatement(absl::string_view statement_string,
                            const ParserOptions& parser_options_in,
                            std::unique_ptr<ParserOutput>* output);

// Parses <script_string> and returns the parser output in <output> upon
// success.
//
// A terminating semicolon is optional for the last statement in the script,
// and mandatory for all other statements.
absl::Status ParseScript(absl::string_view script_string,
                         const ParserOptions& parser_options_in,
                         ErrorMessageOptions error_message_options,
                         std::unique_ptr<ParserOutput>* output);

ABSL_DEPRECATED("Inline me!")
inline absl::Status ParseScript(absl::string_view script_string,
                                const ParserOptions& parser_options_in,
                                ErrorMessageMode error_message_mode,
                                bool keep_error_location_payload,
                                std::unique_ptr<ParserOutput>* output) {
  return ParseScript(
      script_string, parser_options_in,
      {.mode = error_message_mode,
       .attach_error_location_payload = keep_error_location_payload,
       .stability = GetDefaultErrorMessageStability()},
      output);
}

ABSL_DEPRECATED("Inline me!")
inline absl::Status ParseScript(absl::string_view script_string,
                                const ParserOptions& parser_options_in,
                                ErrorMessageMode error_message_mode,
                                std::unique_ptr<ParserOutput>* output) {
  return ParseScript(script_string, parser_options_in,
                     {.mode = error_message_mode,
                      .attach_error_location_payload =
                          (error_message_mode == ERROR_MESSAGE_WITH_PAYLOAD),
                      .stability = GetDefaultErrorMessageStability()},
                     output);
}

// Parses one statement from a string that may contain multiple statements.
// This can be called in a loop with the same <resume_location> to parse
// all statements from a string.
//
// Returns the parser output in <output> upon success. The AST can be retrieved
// from output->statement(). <*at_end_of_input> will be true if parsing reached
// the end of the string.
//
// Statements are separated by semicolons.  A final semicolon is not required
// on the last statement.  If only whitespace and comments follow the
// semicolon, <*at_end_of_input> will be set to true.  Otherwise, it will be set
// to false.  Script statements are not supported.
//
// After a parse error, <resume_location> is not updated and parsing further
// statements is not supported.
//
// This can return errors annotated with an ErrorLocation payload that indicates
// the input location of an error.
absl::Status ParseNextStatement(ParseResumeLocation* resume_location,
                                const ParserOptions& parser_options_in,
                                std::unique_ptr<ParserOutput>* output,
                                bool* at_end_of_input);

// Similar to the above function, but allows statements specific to scripting,
// in addition to SQL statements.  Entire constructs such as IF...END IF,
// WHILE...END WHILE, and BEGIN...END are returned as a single statement, and
// may contain inner statements, which can be examined through the returned
// parse tree.
absl::Status ParseNextScriptStatement(ParseResumeLocation* resume_location,
                                      const ParserOptions& parser_options_in,
                                      std::unique_ptr<ParserOutput>* output,
                                      bool* at_end_of_input);

// Parses <type_string> as a type name and returns the parser output in <output>
// upon success. The AST can be retrieved from output->type().
//
// This can return errors annotated with an ErrorLocation payload that indicates
// the input location of an error.
absl::Status ParseType(absl::string_view type_string,
                       const ParserOptions& parser_options_in,
                       std::unique_ptr<ParserOutput>* output);

// Parses <expression_string> as an expression and returns the parser output in
// <output> upon success. The AST can be retrieved from output->expression().
//
// This can return errors annotated with an ErrorLocation payload that indicates
// the input location of an error.
absl::Status ParseExpression(absl::string_view expression_string,
                             const ParserOptions& parser_options_in,
                             std::unique_ptr<ParserOutput>* output);
// Similar to the previous function, but takes a ParseResumeLocation that
// indicates the source string that contains the expression, and the offset
// into that string where the expression begins.
absl::Status ParseExpression(const ParseResumeLocation& resume_location,
                             const ParserOptions& parser_options_in,
                             std::unique_ptr<ParserOutput>* output);

// Unparse a given AST back to a canonical SQL string and return it.
// Works for any AST node.
std::string Unparse(const ASTNode* root);

// Parse the first few keywords from <input> (ignoring whitespace, comments and
// hints) to determine what kind of statement it is (if it is valid).
//
// If <input> cannot be any known statement type, or is a script statement,
// returns -1.
// <*statement_is_ctas> will be set to true iff the query is CREATE
// TABLE AS SELECT, and false otherwise.
ASTNodeKind ParseStatementKind(
    absl::string_view input, const LanguageOptions& language_options,
    parser::MacroExpansionMode macro_expansion_mode,
    const parser::macros::MacroCatalog* macro_catalog, bool* statement_is_ctas);

inline ASTNodeKind ParseStatementKind(absl::string_view input,
                                      const LanguageOptions& language_options,
                                      bool* statement_is_ctas) {
  return ParseStatementKind(input, language_options,
                            parser::MacroExpansionMode::kNone,
                            /*macro_catalog=*/nullptr, statement_is_ctas);
}

// Similar to ParseStatementKind, but determines the statement kind for the next
// statement starting from <resume_location>.
//
// <language_options> are used for parsing.
//
// <statement_is_ctas> cannot null; its content will be set to true iff
// the query is CREATE TABLE AS SELECT.
ASTNodeKind ParseNextStatementKind(
    const ParseResumeLocation& resume_location,
    const LanguageOptions& language_options,
    parser::MacroExpansionMode macro_expansion_mode,
    const parser::macros::MacroCatalog* macro_catalog,
    bool* next_statement_is_ctas);

inline ASTNodeKind ParseNextStatementKind(
    const ParseResumeLocation& resume_location,
    const LanguageOptions& language_options, bool* next_statement_is_ctas) {
  return ParseNextStatementKind(
      resume_location, language_options, parser::MacroExpansionMode::kNone,
      /*macro_catalog=*/nullptr, next_statement_is_ctas);
}

// Parse the first few keywords from <resume_location> (ignoring whitespace
// and comments), to determine basic statement properties.
//
// Requires that <resume_location> is valid and <parser_options> includes
// initialized arenas, or an error is returned.
//
// Requires that <parser_options> has appropriate LanguageOptions set.
// Also requires that all arenas are initialized, since the returned
// <ast_statement_properties> might point at hints that are allocated in
// those arenas (so the <parser_options> arenas must outlive the returned
// <ast_statement_properties>).
//
// The returned <ast_statement_properties> currently includes the ASTNodeKind,
// whether the statement is CTAS, the CREATE statement scope (TEMP, etc.) if
// relevant, and statement level hints.
absl::Status ParseNextStatementProperties(
    const ParseResumeLocation& resume_location,
    const ParserOptions& parser_options,
    std::vector<std::unique_ptr<ASTNode>>* allocated_ast_nodes,
    parser::ASTStatementProperties* ast_statement_properties);

// Process the statement level hints in `ast_hints` and copy their key value
// pairs into `hints_map`.
absl::Status ProcessStatementLevelHintsToMap(
    const ASTHint* ast_hints, absl::string_view sql_input,
    absl::flat_hash_map<std::string, std::string>& hints_map);

}  // namespace googlesql

#endif  // GOOGLESQL_PARSER_PARSER_H_
