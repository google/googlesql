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

#include "googlesql/parser/parser_internal.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/errors.h"
#include "googlesql/common/timer_util.h"
#include "googlesql/common/utf_util.h"
#include "googlesql/common/warning_sink.h"
#include "googlesql/parser/ast_node.h"
#include "googlesql/parser/ast_node_factory.h"
#include "googlesql/parser/keywords.h"
#include "googlesql/parser/lookahead_transformer.h"
#include "googlesql/parser/macros/macro_catalog.h"
#include "googlesql/parser/macros/macro_expander.h"
#include "googlesql/parser/macros/token_provider.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser_mode.h"
#include "googlesql/parser/parser_runtime_info.h"
#include "googlesql/parser/statement_properties.h"
#include "googlesql/parser/textmapper_lexer_adapter.h"
#include "googlesql/parser/tm_parser.h"
#include "googlesql/parser/tm_token.h"
#include "googlesql/parser/token_stream.h"
#include "googlesql/parser/token_with_location.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/proto/logging.pb.h"
#include "absl/container/flat_hash_map.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "googlesql/base/status_macros.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/arena.h"
#include "googlesql/base/status_builder.h"  
#include "re2/re2.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_builder.h"



ABSL_FLAG(
    bool, googlesql_parser_strip_errors, false,
    "Simplify GoogleSQL parser error messages to just \"Syntax Error\". Using "
    "this flag in tests ensures that tests will not be written assuming an "
    "exact parser error message. This allows GoogleSQL changes to improve "
    "error messages later without breaking user tests.");

namespace googlesql {
namespace parser {

static absl::StatusOr<std::string> GetParserModeName(ParserMode mode) {
  switch (mode) {
    case ParserMode::kExpression:
      return "expression";
    case ParserMode::kType:
      return "type";
    case ParserMode::kStatement:
    case ParserMode::kNextStatement:
    case ParserMode::kNextScriptStatement:
    case ParserMode::kNextStatementKind:
      return "statement";
    case ParserMode::kScript:
      return "script";
    case ParserMode::kTokenizer:
    case ParserMode::kTokenizerPreserveComments:
      GOOGLESQL_RET_CHECK_FAIL() << "CleanUpBisonError called in tokenizer mode";
  }
}

// The longest literal value that we're willing to echo in an error message.
// This value is in bytes, not characters. It includes the quotes. The value was
// chosen so that "Unexpected string literal" messages typically still fit on
// one line. (Not an 80-column line, but a realistic line in a query UI.)
//
// When you change this limit, also adjust the test literals in
// parser_errors.test.
static constexpr int kMaxErrorLiteralLength = 50;

static std::string ShortenStringLiteralForError(absl::string_view literal) {
  if (literal.size() > kMaxErrorLiteralLength) {
    // Shorten by inserting "..." before the final quotes.
    int num_end_quotes = 1;
    if (absl::StartsWith(literal, "\"\"\"") ||
        absl::StartsWith(literal, "\'\'\'") ||
        absl::StartsWithIgnoreCase(literal, "r\"\"\"") ||
        absl::StartsWithIgnoreCase(literal, "r\'\'\'")) {
      num_end_quotes = 3;
    }
    int excerpt_size = kMaxErrorLiteralLength - num_end_quotes;
    // If we can't remove at least four bytes in addition to the quotes, then
    // we're not narrowing the output at all. If that happens, just keep the
    // original. The net effect is that all narrowed literals have the same
    // length in bytes, but we'll let some literals through that are just over
    // the limit. (Note that the constant size is in bytes, not characters. This
    // is not that important.)
    if (excerpt_size >
        static_cast<int64_t>(literal.size()) - num_end_quotes - 4) {
      return std::string(literal);
    }
    // Don't cut the string off in the middle of a multibyte character.
    while (!IsWellFormedUTF8(literal.substr(0, excerpt_size))) {
      --excerpt_size;
    }
    return absl::StrCat(
        literal.substr(0, excerpt_size), "...",
        absl::ClippedSubstr(literal, literal.size() - num_end_quotes,
                            num_end_quotes));
  }
  return std::string(literal);
}

static std::string ShortenBytesLiteralForError(absl::string_view literal) {
  if (literal.size() < kMaxErrorLiteralLength) {
    return std::string(literal);
  }
  // Shorten by inserting "..." before the final quotes.
  int num_end_quotes = 1;
  if (absl::StartsWithIgnoreCase(literal, "b\"\"\"") ||
      absl::StartsWithIgnoreCase(literal, "rb\"\"\"") ||
      absl::StartsWithIgnoreCase(literal, "br\"\"\"") ||
      absl::StartsWithIgnoreCase(literal, "b\'\'\'") ||
      absl::StartsWithIgnoreCase(literal, "rb\'\'\'") ||
      absl::StartsWithIgnoreCase(literal, "br\'\'\'")) {
    num_end_quotes = 3;
  }
  const int excerpt_size = kMaxErrorLiteralLength - num_end_quotes;
  // If we can't remove at least four bytes in addition to the quotes, then
  // we're not narrowing the output at all. If that happens, just keep the
  // original. The net effect is that all narrowed literals have the same
  // length in bytes, but we'll let some literals through that are just over
  // the limit.
  if (excerpt_size >
      static_cast<int64_t>(literal.size()) - num_end_quotes - 4) {
    return std::string(literal);
  }
  return absl::StrCat(
      literal.substr(0, excerpt_size), "...",
      absl::ClippedSubstr(literal, literal.size() - num_end_quotes,
                          num_end_quotes));
}

// Generates an error message based on the followsets reported by the parser.
// The arguments should match those passed into `ParseInternal()`.
static absl::Status GenerateImprovedBisonSyntaxError(
    absl::Span<const std::set<Token>> expected_followsets,
    const LanguageOptions& language_options, ParserMode mode,
    const LookaheadTransformer& lexer, absl::string_view input,
    int start_offset) {
  std::set<Token> expected_tokens;
  if (!expected_followsets.empty()) {
    // We only look at the first followset for now. Technically correct error
    // messages must consider all the followsets, but for now we stay consistent
    // with the existing wrong error messages. Considering all the followsets
    // pushes most of the error messages over the kMaxExpectedTokens limit, so
    // the technically correct error messages are subjectively worse. We need
    // a better strategy for dealing with large followsets to be able to produce
    // technically correct error messages that are also helpful.
    // TODO: b/490131898 - Implement a better strategy for dealing with large
    //       followsets.
    for (Token token : expected_followsets.front()) {
      expected_tokens.insert(token);
    }
  }
  // Remove utility tokens that are not relevant to the user.
  expected_tokens.erase(Token::FORCE_LA1_TOKEN);

  // If there are more than kMaxExpectedTokens then we don't think a super long
  // list of tokens is very helpful, so just don't show any of them.
  static constexpr int kMaxExpectedTokens = 4;
  if (expected_tokens.size() > kMaxExpectedTokens) {
    expected_tokens.clear();
  }

  const auto& user_facing_kw_images =
      GetUserFacingImagesForSpecialKeywordsMap();

  // Convert the set of expected tokens to a set of strings. Legacy logic in
  // this function works on strigs, but muchy of it could be migrated to work
  // with tokens directly.
  std::vector<std::string> expectations;
  for (Token token : expected_tokens) {
    if (token == Token::EOI) {
      expectations.emplace_back("end of input");
    } else if (token >= Token::SENTINEL_GUIDANCE_TOKENS_START &&
               token <= Token::SENTINEL_GUIDANCE_TOKENS_END) {
      // Ignore guidance tokens for now. We could potentially do something
      // smarter here like inserting a list of potential tokens from a
      // generated token set or giving a custom error message. For now, we
      // mostly just want to avoid seeing these in user facing error messages.
      continue;
    } else {
      absl::string_view token_str = tokenName[static_cast<size_t>(token)];
      expectations.emplace_back(
          googlesql_base::FindWithDefault(user_facing_kw_images, token_str, token_str));
    }
  }

  for (std::string& expectation : expectations) {
    // Wrap the single-character operators, "+=" and "-=" in quotes.
    if ((expectation.size() == 1 && !isalpha(expectation[0])) ||
        expectation == "+=" || expectation == "-=") {
      expectation = absl::StrCat("\"", expectation, "\"");
    }

    // The only time we ever see MACRO_BODY_TOKEN in the expected set is when
    // the macro name is missing in a definition. For a better error message,
    // just replace it with "macro name".
    if (expectation == "MACRO_BODY_TOKEN") {
      expectation = "macro name";
    }

    // We use some special tokens for lexical disambiguation. The labels we
    // give those in the parser are not necessarily user friendly or what we
    // want to show in error messages. Here we r-map those labels back to what
    // the user will find most understandable.
    if (const auto found = user_facing_kw_images.find(expectation);
        found != user_facing_kw_images.end()) {
      expectation = found->second;
    }

    // These are a single token in the Bison tokenizer but we treat them as two
    // tokens externally.
    if (expectation == "\".*\"") {
      expectation = "\".\"";
    } else if (expectation == "\"@{\"") {
      expectation = "\"@\"";
    } else if (expectation == "@@") {
      expectation = "\"@@\"";
    }
    // If it looks like an uppercased keyword, say it's a keyword. All other
    // things such as string literals are described in lower case in the Bison
    // parser's token names.
    static LazyRE2 re_keyword = {"[A-Z]+"};
    if (RE2::FullMatch(expectation, *re_keyword)) {
      expectation = absl::StrCat("keyword ", expectation);
    }
  }
  // Deduplicate expectations. Transformations may have introduced duplicates.
  std::set<std::string> expectations_set(expectations.begin(),
                                         expectations.end());

  // Remove an expectation that is sure to raise question marks with most users.
  // Positional parameters ("?") are only there for some specific client
  // libraries outside Google.
  expectations_set.erase("\"?\"");
  // When the user has entered another token, they don't intend to end the
  // statement, so giving that as an alternative option makes little sense.
  // Bison also tends to give it as the only option when there is an unexpected
  // token in the middle of an expression, even though there are other possible
  // continuations. This may be triggered by the fact that we use operator
  // precedence parsing.
  expectations_set.erase("$end");

  // Avoid suggesting pipe characters since the syntax might not be enabled,
  // and to avoid adding this on every error that looks for a ")" after a query.
  expectations_set.erase("|>");

  // Avoid suggesting FROM every place a query might show up.
  // This is usually a bad suggestion anyway.
  expectations_set.erase("keyword FROM");

  // Avoid suggesting OR everywhere an expression might show up.
  // OR is generally the lowest precedence operator and it appears separate in
  // the highest rule for `expression` in the grammar, whereas other operators
  // are grouped in a more specific rule, such as
  // `expression_with_prec_higher_than_and`. Consequently, when an unexpected
  // token is encountered after an `expression`, the expected set is a lot
  // smaller because all other operators are in more specific rules. In several
  // instances, the expected set is small enough so bison displays it, and OR
  // shows up (since an `expression` can continue with OR to form a larger
  // `expression`). However, there is nothing special about it to single it out
  // and suggest the user continue their expression with an OR any time there is
  // some syntax error.
  expectations_set.erase("keyword OR");

  // Removes the "+=" and "-=" from the expectations set if the language feature
  // is not enabled.
  if (!language_options.LanguageFeatureEnabled(
          FEATURE_ENABLE_ALTER_ARRAY_OPTIONS)) {
    expectations_set.erase("\"+=\"");
    expectations_set.erase("\"-=\"");
  }

  ParseLocationPoint error_location = lexer.LastTokenLocation().start();

  // Normalize the special forms of the keywords so that they don't require
  // special handling in error message generation.
  auto normalize_token = [](Token token) {
    static const auto* const kNormalized =
        new absl::flat_hash_map<Token, Token>{
            {Token::KW_WITH_STARTING_WITH_EXPRESSION, Token::KW_WITH},
            {Token::KW_EXCEPT_IN_SET_OP, Token::KW_EXCEPT},
            {Token::KW_FOR_BEFORE_LOCK_MODE, Token::KW_FOR},
            {Token::KW_FULL_IN_SET_OP, Token::KW_FULL},
            {Token::KW_INNER_IN_SET_OP, Token::KW_INNER},
            {Token::KW_LEFT_IN_SET_OP, Token::KW_LEFT},
            {Token::KW_TABLE_FOR_TABLE_CLAUSE, Token::KW_TABLE},
            {Token::KW_REPLACE_AFTER_INSERT, Token::KW_REPLACE},
            {Token::KW_UPDATE_AFTER_INSERT, Token::KW_UPDATE},
            {Token::KW_NOT_SPECIAL, Token::KW_NOT},
            {Token::KW_TIMESTAMP_IN_WITH_TIMESTAMP_AS_ALIAS,
             Token::KW_TIMESTAMP},
        };
    return kNormalized->contains(token) ? kNormalized->at(token) : token;
  };
  Token token = normalize_token(lexer.LastToken());
  absl::string_view token_text = lexer.LastTokenText();
  std::string actual_token_description;

  if (token == Token::EOI) {
    // The error location was at end-of-input, so this is an
    // unexpected-end-of error. Format with a better string, and move its
    // location to the end of the last token.
    GOOGLESQL_ASSIGN_OR_RETURN(std::string mode_name, GetParserModeName(mode));
    actual_token_description = absl::StrCat("end of ", mode_name);
    ParseLocationRange last_non_eoi_location = lexer.LastLastTokenLocation();
    int error_offset =
        last_non_eoi_location.IsValid()
            // This is the case were we saw at least one non-EOI, non-comment
            // token.
            ? last_non_eoi_location.end().GetByteOffset()
            // This is the case that the input is all whitespace or comments.
            : start_offset;
    // For statements that end in comments, its nicer if the error points to the
    // true end of input after the comment. This code is not perfect because it
    // doesn't perfectly match the lexer's definition of whitespace, but it's
    // good enough for this case of adjusting a corner case error message.
    absl::string_view tag =
        absl::StripLeadingAsciiWhitespace(input.substr(error_offset));
    if (absl::StartsWith(tag, "--") || absl::StartsWith(tag, "#") ||
        absl::StartsWith(tag, "/*")) {
      // Reset to the true end of the input.
      error_offset = error_location.GetByteOffset();
      // If the last comment is /* ... */ then we can put the error immediately
      // after the "*/", otherwise leave it at the end of input token.
      absl::string_view stripped_tag = absl::StripTrailingAsciiWhitespace(tag);
      if (absl::EndsWith(stripped_tag, "*/")) {
        error_offset -= (tag.size() - stripped_tag.size());
      }
    }
    error_location.SetByteOffset(error_offset);
  } else if (token == Token::KW_OVER) {
    // When the OVER keyword is used in the wrong place, we tell the user
    // exactly where it can be used.
    return MakeSqlErrorAtPoint(error_location)
           << "Syntax error: OVER keyword must follow a function call";
  } else if (token == Token::ERROR_EXCEPT_IN_UNEXPECTED_CONTEXT) {
    return MakeSqlErrorAtPoint(error_location)
           << R"(EXCEPT must be followed by ALL, DISTINCT, or "(")";
  } else if (token == Token::ERROR_SCRIPT_LABEL_IS_RESERVED_KEYWORD) {
    return MakeSqlErrorAtPoint(error_location)
           << "Reserved keyword '" << token_text
           << "' may not be used as a label name without backticks";
  } else if (const KeywordInfo* keyword_info = GetKeywordInfoForToken(token)) {
    actual_token_description =
        absl::StrCat("keyword ", keyword_info->keyword());
  } else if (token == Token::STRING_LITERAL) {
    // Escape physical newlines, to avoid multi-line error messages. (Note
    // that this is technically incorrect for raw string literals.)
    std::string escaped_token_text = std::string(token_text);
    absl::StrReplaceAll({{"\r", "\\r"}}, &escaped_token_text);
    absl::StrReplaceAll({{"\n", "\\n"}}, &escaped_token_text);
    actual_token_description = absl::StrCat(
        "string literal ", ShortenStringLiteralForError(escaped_token_text));
  } else if (token == Token::BYTES_LITERAL) {
    // Escape physical newlines, to avoid multi-line error messages. (Note
    // that this is technically incorrect for raw bytes literals.)
    std::string escaped_token_text = std::string(token_text);
    absl::StrReplaceAll({{"\r", "\\r"}}, &escaped_token_text);
    absl::StrReplaceAll({{"\n", "\\n"}}, &escaped_token_text);
    actual_token_description = absl::StrCat(
        "bytes literal ", ShortenBytesLiteralForError(escaped_token_text));
  } else if (token == Token::INTEGER_LITERAL) {
    actual_token_description =
        absl::StrCat("integer literal \"", token_text, "\"");
  } else if (token == Token::FLOATING_POINT_LITERAL) {
    actual_token_description =
        absl::StrCat("floating point literal \"", token_text, "\"");
  } else if (token == Token::IDENTIFIER) {
    if (token_text[0] == '`') {
      // Don't put extra quotes around an already-backquoted identifier.
      actual_token_description = absl::StrCat("identifier ", token_text);
    } else {
      actual_token_description =
          absl::StrCat("identifier \"", token_text, "\"");
    }
  } else if (token == Token::SEMICOLON) {
    // The ";" token includes trailing whitespace, and we don't want to
    // echo that back.
    actual_token_description = "\";\"";
  } else if (token == Token::KW_OPEN_HINT ||
             token == Token::KW_OPEN_INTEGER_HINT ||
             token == Token::OPEN_INTEGER_PREFIX_HINT) {
    // This is a single token for "@{", but we want to expose this as "@"
    // externally.
    actual_token_description = "\"@\"";
  } else if (token == Token::KW_DOUBLE_AT) {
    actual_token_description = "\"@@\"";
  } else {
    actual_token_description = absl::StrCat("\"", token_text, "\"");
  }
  if (!expectations_set.empty()) {
    return MakeSqlErrorAtPoint(error_location)
           << "Syntax error: Expected "
           << absl::StrJoin(expectations_set, " or ") << " but got "
           << actual_token_description;
  }
  return MakeSqlErrorAtPoint(error_location)
         << "Syntax error: Unexpected " << actual_token_description;
}

// Dispatch to the appropriate parser input method based on the mode.
static absl::Status ParseByMode(ParserMode mode, TextMapperLexerAdapter& lexer,
                                Parser& parser) {
  switch (mode) {
    case ParserMode::kStatement:
      return parser.ParseSqlStatement(lexer);
    case ParserMode::kScript:
      return parser.ParseScript(lexer);
    case ParserMode::kNextStatement:
      return parser.ParseNextStatement(lexer);
    case ParserMode::kNextScriptStatement:
      return parser.ParseNextScriptStatement(lexer);
    case ParserMode::kNextStatementKind:
      return parser.ParseNextStatementKind(lexer);
    case ParserMode::kExpression:
      return parser.ParseStandaloneExpression(lexer);
    case ParserMode::kType:
      return parser.ParseStandaloneType(lexer);
    case ParserMode::kTokenizerPreserveComments:
    case ParserMode::kTokenizer:
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected mode: " << static_cast<int>(mode);
  }
}

// Initializes fields on the nodes out of 'allocated_ast_nodes' and moves them
// out into 'other_allocated_ast_nodes', separating 'output_node' which goes
// into 'output'.
static absl::Status InitNodes(
    absl::string_view input, const ASTNode* output_node,
    std::vector<std::unique_ptr<ASTNode>>& allocated_ast_nodes,
    std::unique_ptr<ASTNode>* output,
    std::vector<std::unique_ptr<ASTNode>>& other_allocated_ast_nodes) {
  // Make sure InitFields() is called for all ASTNodes that were created.
  // We don't use the result of InitFields() in the grammar itself, so we
  // don't need to do this during parsing.
  for (const auto& ast_node : allocated_ast_nodes) {
    if (ast_node->Is<ASTMacroBody>()) {
      ASTMacroBody* macro_body = ast_node->GetAsOrNull<ASTMacroBody>();
      GOOGLESQL_RET_CHECK(macro_body != nullptr);
      GOOGLESQL_RET_CHECK(macro_body->location().IsValid());
      macro_body->set_image(
          std::string{macro_body->location().GetTextFrom(input)});
    }
    GOOGLESQL_RETURN_IF_ERROR(ast_node->InitFields());
  }

  if (output != nullptr && output_node != nullptr) {
    *output = nullptr;
    // Move 'output_node' out of 'allocated_ast_nodes' and into 'output'.
    for (int64_t i = allocated_ast_nodes.size() - 1; i >= 0; --i) {
      if ((allocated_ast_nodes)[i].get() == output_node) {
        *output = std::move(allocated_ast_nodes[i]);
        // There's no need to erase the entry in allocated_ast_nodes_.
        break;
      }
    }
    GOOGLESQL_RET_CHECK_EQ(output->get(), output_node);
  }
  other_allocated_ast_nodes = std::move(allocated_ast_nodes);
  return absl::OkStatus();
}

static absl::Status MaybeStripParserError(absl::Status status) {
  if (absl::IsInvalidArgument(status) &&
      absl::GetFlag(FLAGS_googlesql_parser_strip_errors)) {
    return absl::InvalidArgumentError("Syntax error");
  }
  return status;
}

// Returns true if the parsed statement (or input) contains a macro invocation.
//
// Because LookaheadTransformer buffers up to 3 tokens ahead, MacroExpander may
// load a macro from the next statement before the parser finishes the current
// statement (e.g., `SELECT 1; $m()`). To ignore lookahead macros in the next
// statement, we check that the first macro offset is at or before where the
// parser stopped:
//   1. kNone mode (`macro_expander == nullptr`): Macro tokens are never valid
//      SQL in kNone mode, so the parser fails immediately on the first macro
//      token it reaches (`LastToken()`).
//   2. Tokenizer/expander error (`!GetOverrideError().ok()`): The token stream
//      stops at its first error, and `GetOverrideError()` is only set when the
//      parser reaches that token, so any loaded macro came at or before it.
//   3. Parse success (`parse_status.ok()`): The macro must start before
//      `*statement_end_byte_offset` (if set).
//   4. Syntax error (`!parse_status.ok()`): Either the error token itself came
//      from a macro expansion, or the macro started at or before the error
//      token (`LastTokenLocation().start()`).
static bool ParseHasMacroInvocation(const absl::Status& parse_status,
                                    const LookaheadTransformer& transformer,
                                    const macros::MacroExpander* macro_expander,
                                    const int* statement_end_byte_offset) {
  if (macro_expander == nullptr) {
    const Token last_token = transformer.LastToken();
    return !parse_status.ok() &&
           (last_token == Token::MACRO_INVOCATION ||
            last_token == Token::MACRO_BUILTIN_INVOCATION ||
            last_token == Token::MACRO_ARGUMENT_REFERENCE);
  }
  if (!macro_expander->first_macro_invocation_offset().has_value()) {
    return false;
  }
  if (!transformer.GetOverrideError().ok()) {
    return true;
  }
  const int first_macro_offset =
      *macro_expander->first_macro_invocation_offset();
  if (parse_status.ok()) {
    return statement_end_byte_offset == nullptr ||
           *statement_end_byte_offset < 0 ||
           first_macro_offset < *statement_end_byte_offset;
  }
  return transformer.LastTokenIsFromMacroExpansion() ||
         first_macro_offset <=
             transformer.LastTokenLocation().start().GetByteOffset();
}

absl::Status ParseInternal(
    ParserMode mode, absl::string_view filename, absl::string_view input,
    int start_byte_offset, IdStringPool* id_string_pool, googlesql_base::UnsafeArena* arena,
    const LanguageOptions& language_options,
    MacroExpansionMode macro_expansion_mode, Catalog* catalog,
    std::unique_ptr<ASTNode>* output, ParserRuntimeInfo& runtime_info,
    WarningSink& warning_sink,
    std::vector<std::unique_ptr<ASTNode>>* other_allocated_ast_nodes,
    ASTStatementProperties* ast_statement_properties,
    int* statement_end_byte_offset) {
  // The primary logic of the function is wrapped in a lambda to ensure all
  // return paths are handled by MaybeStripParserError while allowing natural
  // control flow within the function body.
  return MaybeStripParserError([&]() -> absl::Status {
      auto parser_timer =
          MakeScopedTimerStarted(&runtime_info.parser_timed_value());

      auto token_provider = std::make_unique<macros::TokenProvider>(
          filename, input, start_byte_offset, /*end_offset=*/std::nullopt,
          /*offset_in_original_input=*/0);
      TokenStream* token_stream = token_provider.get();

      std::unique_ptr<macros::MacroExpander> macro_expander;
      if (macro_expansion_mode != MacroExpansionMode::kNone) {
        bool is_strict = macro_expansion_mode == MacroExpansionMode::kStrict;
        GOOGLESQL_ASSIGN_OR_RETURN(
            macro_expander,
            macros::MacroExpander::Create(
                token_stream, catalog, arena,
                /*macro_expander_options=*/{.is_strict = is_strict},
                /*parent_location=*/nullptr));
        token_stream = macro_expander.get();
      }

      GOOGLESQL_ASSIGN_OR_RETURN(
          std::unique_ptr<LookaheadTransformer> transformer,
          LookaheadTransformer::Create(mode, language_options, token_stream));
      auto lexer = std::make_unique<TextMapperLexerAdapter>(transformer.get());

      ASTNode* output_node = nullptr;
      ASTNodeFactory node_factory(arena, id_string_pool);
      Parser parser(*transformer, language_options, node_factory, warning_sink,
                    macro_expansion_mode, &output_node,
                    ast_statement_properties, statement_end_byte_offset);
      // Register the parser with the lookahead transformer to enable token
      // transformations to consider parser state.
      transformer->SetParser(&parser);

      absl::Status parse_status = ParseByMode(mode, *lexer, parser);
      if (ParseHasMacroInvocation(parse_status, *transformer,
                                  macro_expander.get(),
                                  statement_end_byte_offset)) {
        runtime_info.set_has_macro_invocation(true);
      }
      // We want to continue if there was a parsing error or a successful parse.
      // Anything else, such as kInternal or kResourceExhausted, should be
      // returned.
      if (!(parse_status.ok() || absl::IsInvalidArgument(parse_status))) {
        return parse_status;
      }

      // The tokenizer's error overrides the parser's error.
      GOOGLESQL_RETURN_IF_ERROR(transformer->GetOverrideError());

      runtime_info.add_lexical_tokens(transformer->num_consumed_tokens());
      // When a multi-statement input ends with a semicolon, ParseNext.* will
      // return `statement_end_byte_offset` at the semi-colon. What we want is
      // the end of the input so that we don't try to parse any trailing
      // whitespace as another statement.
      if (statement_end_byte_offset != nullptr && transformer->IsAtEoi()) {
        *statement_end_byte_offset = static_cast<int>(input.size());
      }

      std::vector<std::unique_ptr<ASTNode>> allocated_ast_nodes =
          std::move(node_factory).ReleaseAllocatedASTNodes();
      if (parse_status.ok()) {
        return InitNodes(input, output_node, allocated_ast_nodes, output,
                         *other_allocated_ast_nodes);
      }

      // googlesql::Parser uses an error message starting with a special prefix
      // to signal a state-transition failure.
      if (absl::IsInvalidArgument(parse_status) &&
          parse_status.message().starts_with("syntax error, ")) {
        // This was a Textmapper-generated syntax error. Generate a message that
        // is to our own liking.
        return GenerateImprovedBisonSyntaxError(
            parser.expected_tokens(), language_options, mode, *transformer,
            input, start_byte_offset);
      }
      return parse_status;
  }());
}

}  // namespace parser
}  // namespace googlesql
