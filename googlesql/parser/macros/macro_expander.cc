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

#include "googlesql/parser/macros/macro_expander.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/arena.h"
#include "googlesql/base/arena_allocator.h"
#include "googlesql/common/thread_stack.h"
#include "googlesql/parser/macros/diagnostic.h"
#include "googlesql/parser/macros/macro_catalog.h"
#include "googlesql/parser/macros/quoting.h"
#include "googlesql/parser/macros/standalone_macro_expansion.h"
#include "googlesql/parser/macros/token_provider_base.h"
#include "googlesql/parser/macros/token_splicing_utils.h"
#include "googlesql/parser/tm_token.h"
#include "googlesql/parser/token_with_location.h"
#include "googlesql/proto/internal_error_location.pb.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/base/case.h"
#include "absl/base/nullability.h"
#include "absl/container/btree_map.h"
#include "googlesql/base/check.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace parser {
namespace macros {

#define RETURN_ERROR_IF_OUT_OF_STACK_SPACE() \
  GOOGLESQL_RETURN_IF_NOT_ENOUGH_STACK(      \
      "Out of stack space due to deeply nested macro calls.")

static absl::StatusOr<absl::string_view> GetMacroName(
    const TokenWithLocation& macro_invocation_token) {
  if (macro_invocation_token.kind == Token::MACRO_INVOCATION) {
    return macro_invocation_token.text.substr(1);
  } else {
    GOOGLESQL_RET_CHECK_EQ(macro_invocation_token.kind, Token::MACRO_BUILTIN_INVOCATION);
    return macro_invocation_token.text.substr(2);
  }
}

// Note: end_offset is exclusive
static absl::string_view GetTextBetween(absl::string_view input, size_t start,
                                        size_t end) {
  ABSL_DCHECK_LE(start, end);
  ABSL_DCHECK_LE(start, input.length());
  size_t len = end - start;
  ABSL_DCHECK_LE(len, input.length());
  return absl::ClippedSubstr(input, start, len);
}

absl::StatusOr<int> ParseMacroArgIndex(absl::string_view text) {
  GOOGLESQL_RET_CHECK_GE(text.length(), 1);
  GOOGLESQL_RET_CHECK_EQ(text.front(), '$');
  int arg_index;
  GOOGLESQL_RET_CHECK(absl::SimpleAtoi(text.substr(1), &arg_index));
  return arg_index;
}

// Similar to IsKeywordOrUnquotedIdentifier, but also returns true for
// EXP_IN_FLOAT_NO_SIGN and STANDALONE_EXPONENT_SIGN because they can also be
// identifiers.
//
// Because a float can end with the token kind EXP_IN_FLOAT_NO_SIGN, such a
// float can be spliced with a following identifier or integer, for example
// "1.E10" splicing with a following "a" and becomes "1.E10a". We allow this
// extra splicing to happen in lenient mode.
static bool TokenCanBeKeywordOrUnquotedIdentifier(
    const TokenWithLocation& token) {
  switch (token.kind) {
    case Token::EXP_IN_FLOAT_NO_SIGN:
    case Token::STANDALONE_EXPONENT_SIGN:
      return true;
    default:
      return IsKeywordOrUnquotedIdentifier(token);
  }
}

// Returns true if the two tokens can be spliced into one
static bool CanSplice(const TokenWithLocation& previous_token,
                      const TokenWithLocation& current_token) {
  if (!TokenCanBeKeywordOrUnquotedIdentifier(previous_token)) {
    return false;
  }
  switch (current_token.kind) {
    case Token::DECIMAL_INTEGER_LITERAL:
    case Token::HEX_INTEGER_LITERAL:
      return true;
    default:
      return TokenCanBeKeywordOrUnquotedIdentifier(current_token);
  }
}

static bool AreSame(const QuotingSpec& q1, const QuotingSpec& q2) {
  ABSL_DCHECK(q1.literal_kind() == q2.literal_kind());

  if (q1.quote_kind() != q2.quote_kind()) {
    return false;
  }

  if (q1.prefix().length() != q2.prefix().length()) {
    return false;
  }

  return q1.prefix().length() == 2 ||
         googlesql_base::CaseEqual(q1.prefix(), q2.prefix());
}

static std::unique_ptr<googlesql_base::UnsafeArena> CreateUnsafeArena() {
  return std::make_unique<googlesql_base::UnsafeArena>(/*block_size=*/4096);
}

absl::Status MacroExpander::WarningCollector::AddWarning(absl::Status status) {
  GOOGLESQL_RET_CHECK(!status.ok());
  if (warnings_.size() < max_warning_count_) {
    warnings_.push_back(std::move(status));
  } else if (warnings_.size() == max_warning_count_) {
    // Add a "sentinel" warning indicating there were more.
    warnings_.push_back(absl::InvalidArgumentError(
        "Warning count limit reached. Truncating further warnings"));
  }
  return absl::OkStatus();
}

std::vector<absl::Status> MacroExpander::WarningCollector::ReleaseWarnings() {
  std::vector<absl::Status> tmp;
  std::swap(tmp, warnings_);
  return tmp;
}

MacroExpander::MacroExpander(std::unique_ptr<TokenProviderBase> token_provider,
                             const MacroCatalog& macro_catalog,
                             googlesql_base::UnsafeArena* arena,
                             StackFrame::StackFrameFactory& stack_frame_factory,
                             MacroExpanderOptions macro_expander_options,
                             StackFrame* /*absl_nullable*/ parent_location)
    : MacroExpander(std::move(token_provider), macro_catalog, arena,
                    stack_frame_factory,
                    // Public constructor, expansion state uses the owned
                    // (empty) expansion state from this MacroExpander.
                    owned_expansion_state_,
                    /*call_arguments=*/{}, macro_expander_options,
                    /*override_warning_collector=*/nullptr, parent_location) {}

absl::StatusOr<ExpansionOutput> MacroExpander::ExpandMacros(
    std::unique_ptr<TokenProviderBase> token_provider,
    const MacroCatalog& macro_catalog,
    MacroExpanderOptions macro_expander_options) {
  ExpansionOutput expansion_output;
  ExpansionState expansion_state;
  StackFrame::StackFrameFactory stack_frame_factory(
      macro_expander_options.max_number_of_stack_frames,
      expansion_output.stack_frames);
  expansion_output.arena = CreateUnsafeArena();
  WarningCollector warning_collector(
      macro_expander_options.diagnostic_options.max_warning_count);
  GOOGLESQL_RETURN_IF_ERROR(ExpandMacrosInternal(
      std::move(token_provider), macro_catalog, expansion_output.arena.get(),
      stack_frame_factory, expansion_state,
      /*call_arguments=*/{}, macro_expander_options,
      /*parent_location=*/nullptr, &expansion_output.location_map,
      expansion_output.expanded_tokens, warning_collector,
      // Only the top-level comments should be preserved.
      /*out_max_arg_ref_index=*/nullptr, /*drop_comments=*/false));
  expansion_output.warnings = warning_collector.ReleaseWarnings();
  return expansion_output;
}

absl::Status MacroExpander::MakeSqlErrorAt(const ParseLocationPoint& location,
                                           absl::string_view message) {
  return MakeSqlErrorWithStackFrame(location, message, token_provider_->input(),
                                    parent_location_,
                                    token_provider_->offset_in_original_input(),
                                    macro_expander_options_.diagnostic_options);
}

absl::StatusOr<TokenWithLocation> MacroExpander::GetNextToken() {
  // The loop is needed to skip chunks that all expand into empty. For example:
  // $empty   $empty  $empty $empty  (...etc)
  // Eventually we will hit a nonempty expansion, or YYEOF in the output buffer.
  while (output_token_buffer_.empty()) {
    GOOGLESQL_RETURN_IF_ERROR(LoadPotentiallySplicingTokens());
    GOOGLESQL_RETURN_IF_ERROR(ExpandPotentiallySplicingTokens());
  }

  TokenWithLocation token = output_token_buffer_.ConsumeToken();
  if (token.kind == Token::SEMICOLON || token.kind == Token::EOI) {
    at_statement_start_ = true;
    inside_macro_definition_ = false;
    GOOGLESQL_RET_CHECK(output_token_buffer_.empty());
  }

  if (parent_location_ != nullptr) {
    token.topmost_invocation_location = parent_location_->location;
  }
  // Adding the parent stack frame to the token. This will only happen at the
  // leaf level. When this token will be passed to its upstream caller, the
  // caller won't update the stack frame.
  if (token.stack_frame == nullptr) {
    token.stack_frame = parent_location_;
  }
  return token;
}

int MacroExpander::num_unexpanded_tokens_consumed() const {
  return token_provider_->num_consumed_tokens();
}

// Returns true if `token` may splice from the right of `last_token`.
// `last_was_macro_invocation` is passed separately because a macro invocation
// has multiple tokens when it has arguments, so last_token would only reflect
// the closing parenthesis in that case.
static bool CanUnexpandedTokensSplice(bool last_was_macro_invocation,
                                      const TokenWithLocation& last_token,
                                      const TokenWithLocation& token) {
  if (token.start_offset() > last_token.end_offset()) {
    // Forced space: we will never splice!
    return false;
  }

  if (token.kind == Token::MACRO_INVOCATION ||
      token.kind == Token::MACRO_ARGUMENT_REFERENCE) {
    // Invocations and macro args can splice with pretty much anything.
    // Safer to always load them.
    return true;
  }

  if (token.kind == Token::DECIMAL_INTEGER_LITERAL ||
      token.kind == Token::HEX_INTEGER_LITERAL) {
    // Those will never splice with a previous non-macro token.
    // Otherwise, it'd have already lexed with it.
    return last_was_macro_invocation;
  }

  // Everything else: we have 2 categories:
  // 1. Unquoted identifiers and keywords: those can splice with macro
  //    invocations or argument references.
  // 2. Everything else, e.g. symbols, quoted IDs, string literals, etc, does
  //    not splice.
  if (!TokenCanBeKeywordOrUnquotedIdentifier(token)) {
    return false;
  }
  return last_was_macro_invocation ||
         last_token.kind == Token::MACRO_ARGUMENT_REFERENCE;
}

static absl::string_view AllocateString(absl::string_view str,
                                        googlesql_base::UnsafeArena* arena) {
  return *::googlesql_base::NewInArena<std::basic_string<
      char, std::char_traits<char>, googlesql_base::ArenaAllocator<char, googlesql_base::UnsafeArena>>>(
      arena, str, arena);
}

absl::string_view MacroExpander::MaybeAllocateConcatenation(
    absl::string_view a, absl::string_view b) {
  if (a.empty()) {
    return b;
  }
  if (b.empty()) {
    return a;
  }

  return AllocateString(absl::StrCat(a, b), arena_);
}

// TODO : Update token provider to have the options or state of macro expander.
// So that this can be moved into the token provider.
absl::StatusOr<TokenWithLocation> MacroExpander::ConsumeInputToken() {
  GOOGLESQL_ASSIGN_OR_RETURN(TokenWithLocation token,
                   token_provider_->ConsumeNextToken());
  if (macro_expander_options_.is_strict) {
    return token;
  }

  // TODO: jmorcos - Handle '$' here as well since it is also a lenient token.
  // Add a warning if this is a lenient token (Backslash or a generalized
  // identifier that starts with a number, e.g. 30d or 1ab23cd).
  if (token.kind == Token::BACKSLASH ||
      (token.kind == Token::IDENTIFIER &&
       std::isdigit(token.text.front() && !std::isdigit(token.text.back())))) {
    GOOGLESQL_RETURN_IF_ERROR(RaiseErrorOrAddWarning(MakeSqlErrorAt(
        token.location.start(),
        absl::StrFormat(
            "Invalid token (%s). Did you mean to use it in a literal?",
            token.text))));
  }
  return token;
}

absl::Status MacroExpander::LoadPotentiallySplicingTokens() {
  GOOGLESQL_RET_CHECK(splicing_buffer_.empty());
  GOOGLESQL_RET_CHECK(output_token_buffer_.empty());

  // Skip the leading comment tokens, if any.
  while (true) {
    GOOGLESQL_ASSIGN_OR_RETURN(TokenWithLocation token, token_provider_->PeekNextToken());
    if (token.kind != Token::COMMENT) {
      break;
    }
    GOOGLESQL_ASSIGN_OR_RETURN(token, ConsumeInputToken());
    splicing_buffer_.push(token);
  }

  // Special logic to find top-level DEFINE MACRO statement, which do not get
  // expanded.
  if (at_statement_start_ && call_arguments_.empty()) {
    GOOGLESQL_ASSIGN_OR_RETURN(TokenWithLocation token, token_provider_->PeekNextToken());
    if (token.kind == Token::KW_DEFINE) {
      GOOGLESQL_ASSIGN_OR_RETURN(token, ConsumeInputToken());
      splicing_buffer_.push(token);

      GOOGLESQL_ASSIGN_OR_RETURN(token, token_provider_->PeekNextToken());
      if (token.kind == Token::KW_MACRO) {
        // Mark the leading DEFINE keyword as the special one marking a DEFINE
        // MACRO statement.
        splicing_buffer_.back().kind = Token::KW_DEFINE_FOR_MACROS;
        inside_macro_definition_ = true;
        at_statement_start_ = false;
        return absl::OkStatus();
      }
    }
  }

  at_statement_start_ = false;
  bool last_was_macro_invocation = false;

  // Unquoted identifiers and INT_LITERALs may only splice to the left if what
  // came before them was a macro invocation. Unquoted identifiers may further
  // splice with an argument reference to their left.
  while (true) {
    GOOGLESQL_ASSIGN_OR_RETURN(TokenWithLocation token, token_provider_->PeekNextToken());
    if (!splicing_buffer_.empty() &&
        !CanUnexpandedTokensSplice(last_was_macro_invocation,
                                   splicing_buffer_.back(), token)) {
      return absl::OkStatus();
    }
    GOOGLESQL_ASSIGN_OR_RETURN(token, ConsumeInputToken());
    splicing_buffer_.push(token);
    if (token.kind == Token::EOI) {
      return absl::OkStatus();
    }

    if (token.kind == Token::MACRO_INVOCATION ||
        token.kind == Token::MACRO_BUILTIN_INVOCATION) {
      GOOGLESQL_RETURN_IF_ERROR(LoadArgsIfAny());
      last_was_macro_invocation = true;
    } else {
      last_was_macro_invocation = false;
    }
  }

  GOOGLESQL_RET_CHECK_FAIL() << "We should never hit this path. We always return from "
                      "inside the loop";
}

absl::Status MacroExpander::LoadArgsIfAny() {
  GOOGLESQL_RET_CHECK(!splicing_buffer_.empty())
      << "Splicing buffer cannot be empty. This method should not be "
         "called except after a macro invocation has been loaded";
  GOOGLESQL_RET_CHECK(splicing_buffer_.back().kind == Token::MACRO_INVOCATION ||
            splicing_buffer_.back().kind == Token::MACRO_BUILTIN_INVOCATION)
      << "This method should not be called except after a macro invocation "
         "has been loaded";
  GOOGLESQL_ASSIGN_OR_RETURN(TokenWithLocation token, token_provider_->PeekNextToken());
  if (token.kind != Token::LPAREN ||
      token.start_offset() > splicing_buffer_.back().end_offset()) {
    // The next token is not an opening parenthesis, or is an open parenthesis
    // that is separated by some whitespace. This means that the current
    // invocation does not have an argument list.
    return absl::OkStatus();
  }

  GOOGLESQL_ASSIGN_OR_RETURN(token, ConsumeInputToken());
  splicing_buffer_.push(token);
  return LoadUntilParenthesesBalance();
}

absl::Status MacroExpander::LoadUntilParenthesesBalance() {
  GOOGLESQL_RET_CHECK(!splicing_buffer_.empty());
  GOOGLESQL_RET_CHECK_EQ(splicing_buffer_.back().kind, Token::LPAREN);
  int num_open_parens = 1;
  while (num_open_parens > 0) {
    GOOGLESQL_ASSIGN_OR_RETURN(TokenWithLocation token, ConsumeInputToken());
    switch (token.kind) {
      case Token::LPAREN:
        num_open_parens++;
        break;
      case Token::RPAREN:
        num_open_parens--;
        break;
      case Token::SEMICOLON:
      case Token::EOI:
        // Always an error, even when not in strict mode.
        return MakeSqlErrorAt(
            token.location.start(),
            "Unbalanced parentheses in macro argument list. Make sure that "
            "parentheses are balanced even inside macro arguments.");
      default:
        break;
    }
    splicing_buffer_.push(token);
  }

  return absl::OkStatus();
}

// Returns the given status as error if expanding in strict mode, or adds it
// as a warning otherwise.
// Note that not all problematic conditions can be relegated to warnings.
// For example, a macro invocation with unbalanced parens is always an error.
absl::Status MacroExpander::RaiseErrorOrAddWarning(absl::Status status) {
  GOOGLESQL_RET_CHECK(!status.ok());
  if (macro_expander_options_.is_strict) {
    return status;
  }
  GOOGLESQL_RET_CHECK_OK(warning_collector_.AddWarning(std::move(status)));
  return absl::OkStatus();
}

absl::StatusOr<TokenWithLocation> MacroExpander::Splice(
    TokenWithLocation pending_token, const TokenWithLocation& incoming_token,
    const ParseLocationPoint& location) {
  GOOGLESQL_RET_CHECK(!incoming_token.text.empty());
  GOOGLESQL_RET_CHECK(!pending_token.text.empty());
  GOOGLESQL_RET_CHECK_NE(pending_token.kind, Token::UNAVAILABLE);

  if (macro_expander_options_.diagnostic_options.warn_on_identifier_splicing ||
      macro_expander_options_.is_strict) {
    GOOGLESQL_RETURN_IF_ERROR(RaiseErrorOrAddWarning(MakeSqlErrorAt(
        location, absl::StrFormat("Splicing tokens (%s) and (%s)",
                                  pending_token.text, incoming_token.text))));
  }

  pending_token.text =
      MaybeAllocateConcatenation(pending_token.text, incoming_token.text);

  // The splicing token could be a keyword, e.g. FR+OM becoming FROM. But for
  // expansion purposes, these are all identifiers, and likely will splice with
  // more characters and not be a keyword. Re-lexing happens at the very end.
  // TODO: strict mode should produce an error when forming keywords through
  // splicing.
  pending_token.kind = Token::IDENTIFIER;

  // If two tokens which are going to be spliced have different parent stack
  // frames, we will always choose the parent of the first token.
  // If parent of first token is null, then only we will choose the parent of
  // the second token.
  if (pending_token.stack_frame == nullptr) {
    pending_token.stack_frame = incoming_token.stack_frame;
    pending_token.location = incoming_token.location;
  }
  return pending_token;
}

absl::StatusOr<TokenWithLocation> MacroExpander::ExpandAndMaybeSpliceMacroItem(
    TokenWithLocation unexpanded_macro_token, TokenWithLocation pending_token) {
  std::vector<TokenWithLocation> expanded_tokens;
  if (unexpanded_macro_token.kind == Token::MACRO_ARGUMENT_REFERENCE) {
    GOOGLESQL_RETURN_IF_ERROR(
        ExpandMacroArgumentReference(unexpanded_macro_token, expanded_tokens));
  } else if (unexpanded_macro_token.kind == Token::MACRO_BUILTIN_INVOCATION) {
    GOOGLESQL_ASSIGN_OR_RETURN(absl::string_view macro_name,
                     GetMacroName(unexpanded_macro_token));
    // Since we do not have any builtin macros defined (yet), a builtin macro
    // reference always results in an error. So do not call
    // `RaiseErrorOrAddWarning`, instead directly throw an error.
    return MakeSqlErrorAt(
        unexpanded_macro_token.location.start(),
        absl::StrFormat("Builtin macro '%s' not found.", macro_name));
  } else {
    GOOGLESQL_RET_CHECK_EQ(unexpanded_macro_token.kind, Token::MACRO_INVOCATION);
    GOOGLESQL_ASSIGN_OR_RETURN(absl::string_view macro_name,
                     GetMacroName(unexpanded_macro_token));
    std::optional<MacroInfo> macro_info = macro_catalog_.Find(macro_name);
    if (!macro_info.has_value()) {
      GOOGLESQL_RETURN_IF_ERROR(RaiseErrorOrAddWarning(MakeSqlErrorAt(
          unexpanded_macro_token.location.start(),
          absl::StrFormat("Macro '%s' not found.", macro_name))));
      // In lenient mode, just return this token as is, and let the argument
      // list expand as if it were just normal text.
      return AdvancePendingToken(std::move(pending_token),
                                 std::move(unexpanded_macro_token));
    }
    GOOGLESQL_RETURN_IF_ERROR(ExpandMacroInvocation(unexpanded_macro_token, *macro_info,
                                          expanded_tokens));
  }

  GOOGLESQL_RET_CHECK(!expanded_tokens.empty())
      << "A proper expansion should have at least the YYEOF token at "
         "the end. Failure was when expanding "
      << unexpanded_macro_token.text;
  GOOGLESQL_RET_CHECK(expanded_tokens.back().kind == Token::EOI);
  // Pop the trailing space at the end of the expansion, which is tacked
  // on to the YYEOF.
  expanded_tokens.pop_back();

  // Empty expansions are tricky. There are 2 cases, the 3rd is impossible:
  // 1. pending_token == YYUNDEF: only has whitespace, potentially carried from
  //    previous chunks as in: $empty $empty \t $empty
  // 2. pending_token may splice, e.g. part of an identifier (which means there
  //    can never be whitespace separation, or we wouldn't have chunked them
  //    together).
  // 3. (impossible) pending_token never splices, e.g. '+' (in which case we
  //     wouldn't be in the same chunk anyway)
  if (expanded_tokens.empty()) {
    if (!unexpanded_macro_token.preceding_whitespaces.empty()) {
      GOOGLESQL_RET_CHECK(pending_token.kind == Token::UNAVAILABLE);
      pending_token.preceding_whitespaces = MaybeAllocateConcatenation(
          pending_token.preceding_whitespaces,
          unexpanded_macro_token.preceding_whitespaces);
    }
    return pending_token;
  }

  if (CanSplice(pending_token, expanded_tokens.front())) {
    GOOGLESQL_RET_CHECK(unexpanded_macro_token.preceding_whitespaces.empty());
    // TODO: warning on implicit splicing here and everywhere else
    // The first token will splice with what we currently have.
    // TODO: if we already have another token, do we
    // allow splicing identifiers into a keywod? e.g. FR + OM = FROM.
    //   1. Each on its own is an identifier. Together they're a
    //      keyword. This is token splicing. We probably should make
    //      this an error.
    // However:
    //   2. For all we know, more identifiers may come up. So the
    //      final token is FROMother.
    //   3. This is an open question, probably to be done at the final
    //      result like a validation pass. But even that likely won't
    //      get it, because we can't tell the difference between a
    //      quoted and an unquoted identifier.
    //   4. Long term, we'd like to stop splicing, and introduce an
    //      explicit splice operator like C's ##.
    //   5. Furthermore, a keyword like FROM may still be understood
    //      by the parser to be an identifier anyway, such as when it
    //      is preceded by a dot, e.g. 'a.FROM'.
    GOOGLESQL_ASSIGN_OR_RETURN(pending_token,
                     Splice(std::move(pending_token), expanded_tokens.front(),
                            unexpanded_macro_token.location.start()));
  } else {
    // The first expanded token becomes the pending token, and takes on the
    // previous whitespace (for example from a series of $empty), as well as
    // the whitespace before the original macro token (but drops the leading
    // whitespaces from the definition.)
    expanded_tokens.front().preceding_whitespaces =
        unexpanded_macro_token.preceding_whitespaces;
    GOOGLESQL_ASSIGN_OR_RETURN(pending_token,
                     AdvancePendingToken(std::move(pending_token),
                                         std::move(expanded_tokens.front())));
  }

  if (expanded_tokens.size() > 1) {
    // Leading and trailing spaces around the expansion results are
    // discarded, but not the whitespaces within. Consequently, the
    // first and last tokens in an expansion may splice with the
    // calling context. Everything in the middle is kept as is, with
    // no splicing.
    output_token_buffer_.Push(std::move(pending_token));

    // Everything in the middle (all except the first and last) will
    // not splice.
    for (int i = 1; i < expanded_tokens.size() - 1; i++) {
      // Use the unexpanded token's location
      output_token_buffer_.Push(std::move(expanded_tokens[i]));
    }
    pending_token = expanded_tokens.back();
  }
  return pending_token;
}

absl::StatusOr<TokenWithLocation> MacroExpander::AdvancePendingToken(
    TokenWithLocation pending_token, TokenWithLocation incoming_token) {
  if (pending_token.kind != Token::UNAVAILABLE) {
    output_token_buffer_.Push(std::move(pending_token));
  } else {
    GOOGLESQL_RET_CHECK(pending_token.text.empty());
    // Prepend any pending whitespaces. If there's none, we skip allocating
    // a copy of incoming_token.preceding_whitespaces.
    incoming_token.preceding_whitespaces =
        MaybeAllocateConcatenation(pending_token.preceding_whitespaces,
                                   incoming_token.preceding_whitespaces);
  }
  return incoming_token;
}

static bool IsQuotedLiteral(const TokenWithLocation& token) {
  return token.kind == Token::STRING_LITERAL ||
         token.kind == Token::BYTES_LITERAL || IsQuotedIdentifier(token);
}

absl::Status MacroExpander::ExpandPotentiallySplicingTokens() {
  GOOGLESQL_RET_CHECK(!splicing_buffer_.empty());
  TokenWithLocation pending_token{
      .kind = Token::UNAVAILABLE,
      .location = ParseLocationRange{},
      .text = "",
      .preceding_whitespaces = pending_whitespaces_};
  // Do not forget to reset pending_whitespaces_
  pending_whitespaces_ = "";
  while (!splicing_buffer_.empty()) {
    TokenWithLocation token = splicing_buffer_.front();
    splicing_buffer_.pop();

    if (inside_macro_definition_) {
      // Check the pending token in case there were some pending spaces from the
      // previous statement. But there shouldn't be any splicing.
      GOOGLESQL_ASSIGN_OR_RETURN(
          pending_token,
          AdvancePendingToken(std::move(pending_token), std::move(token)));
    } else if (token.kind == Token::MACRO_ARGUMENT_REFERENCE ||
               token.kind == Token::MACRO_INVOCATION ||
               token.kind == Token::MACRO_BUILTIN_INVOCATION) {
      GOOGLESQL_ASSIGN_OR_RETURN(pending_token,
                       ExpandAndMaybeSpliceMacroItem(std::move(token),
                                                     std::move(pending_token)));
    } else if (IsQuotedLiteral(token)) {
      GOOGLESQL_ASSIGN_OR_RETURN(pending_token, ExpandLiteral(std::move(pending_token),
                                                    std::move(token)));
    } else if (CanSplice(pending_token, token)) {
      GOOGLESQL_ASSIGN_OR_RETURN(pending_token, Splice(std::move(pending_token), token,
                                             token.location.start()));
    } else {
      GOOGLESQL_ASSIGN_OR_RETURN(
          pending_token,
          AdvancePendingToken(std::move(pending_token), std::move(token)));
    }
  }

  GOOGLESQL_RET_CHECK(pending_whitespaces_.empty());
  if (pending_token.kind != Token::UNAVAILABLE) {
    output_token_buffer_.Push(std::move(pending_token));
    pending_whitespaces_ = "";
  } else if (!pending_token.preceding_whitespaces.empty()) {
    // This is the case where the last part of our chunk has all been empty
    // expansions. We need to hold onto these whitespaces for the next chunk.
    pending_whitespaces_ =
        AllocateString(pending_token.preceding_whitespaces, arena_);
  }
  return absl::OkStatus();
}

absl::Status MacroExpander::ParseAndExpandArgs(
    const TokenWithLocation& unexpanded_macro_invocation_token,
    std::vector<std::vector<TokenWithLocation>>& expanded_args,
    bool& has_explicit_unexpanded_arg, int& out_invocation_end_offset,
    StackFrame& macro_invocation_stack_frame) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  GOOGLESQL_RET_CHECK(expanded_args.empty());

  has_explicit_unexpanded_arg = false;

  out_invocation_end_offset =
      unexpanded_macro_invocation_token.location.end().GetByteOffset();

  // The first argument is the macro name
  GOOGLESQL_ASSIGN_OR_RETURN(absl::string_view macro_name,
                   GetMacroName(unexpanded_macro_invocation_token));
  expanded_args.push_back(std::vector<TokenWithLocation>{
      {.kind = Token::IDENTIFIER,
       .location = unexpanded_macro_invocation_token.location,
       .text = macro_name,
       .preceding_whitespaces = ""},
      {.kind = Token::EOI,
       .location = unexpanded_macro_invocation_token.location,
       .text = "",
       .preceding_whitespaces = ""}});

  if (splicing_buffer_.empty() ||
      splicing_buffer_.front().kind != Token::LPAREN) {
    // No arguments for this invocation. Generate any necessary warnings or
    // errors before returning.
    if (macro_expander_options_.diagnostic_options
            .warn_on_macro_invocation_with_no_parens ||
        macro_expander_options_.is_strict) {
      GOOGLESQL_RETURN_IF_ERROR(RaiseErrorOrAddWarning(MakeSqlErrorAt(
          unexpanded_macro_invocation_token.location.end(),
          absl::StrFormat("Invocation of macro '%s' missing argument list.",
                          macro_name))));
    }
    return absl::OkStatus();
  }

  const TokenWithLocation opening_paren = splicing_buffer_.front();
  splicing_buffer_.pop();
  GOOGLESQL_RET_CHECK(opening_paren.kind == Token::LPAREN);
  int arg_start_offset =
      opening_paren.end_offset() - token_provider_->offset_in_original_input();

  // Parse arguments, each being a sequence of tokens. Arg offsets here are
  // relative to the available input, not the start of the file.
  struct ParseRange {
    int start_offset = -1;
    int end_offset = -1;
  };
  std::vector<ParseRange> unexpanded_args;
  GOOGLESQL_RET_CHECK(!splicing_buffer_.empty());
  int num_open_parens = 1;
  while (num_open_parens > 0) {
    GOOGLESQL_RET_CHECK(!splicing_buffer_.empty());
    TokenWithLocation token = splicing_buffer_.front();
    splicing_buffer_.pop();

    // The current argument ends at the next top-level comma, or the closing
    // parenthesis. Note that an argument may itself have parentheses and
    // commas, for example:
    //     $m( x(  a  ,  b  ),  y  )
    // The arguments to the invocation of $m are `x(a,b)` and y.
    // The comma between `a` and `b` is internal, not top-level.
    if (token.kind == Token::LPAREN) {
      num_open_parens++;
      has_explicit_unexpanded_arg = true;
    } else if (token.kind == Token::COMMA && num_open_parens == 1) {
      // Top-level comma means the end of the current argument
      unexpanded_args.push_back(
          {.start_offset = arg_start_offset,
           .end_offset =
               token.start_offset() -
               token_provider_->offset_in_original_input() -
               static_cast<int>(token.preceding_whitespaces.length())});
      arg_start_offset =
          token.end_offset() - token_provider_->offset_in_original_input();
      // Even if the first arg is completely empty, when there's a comma
      // separating it from the second, it's an explicit signal that there is
      // an intended first argument that just happens to be empty.
      has_explicit_unexpanded_arg = true;
    } else if (token.kind == Token::RPAREN) {
      num_open_parens--;
      if (num_open_parens == 0) {
        // This was the last argument.
        out_invocation_end_offset = token.end_offset();
        unexpanded_args.push_back(
            {.start_offset = arg_start_offset,
             .end_offset =
                 token.start_offset() -
                 token_provider_->offset_in_original_input() -
                 static_cast<int>(token.preceding_whitespaces.length())});
        break;
      } else {
        has_explicit_unexpanded_arg = true;
      }
    } else if (token.kind != Token::COMMENT) {
      has_explicit_unexpanded_arg = true;
    }
  }
  // Update the end location of the macro invocation to reflect the end of the
  // last argument.
  macro_invocation_stack_frame.location.set_end(
      ParseLocationPoint::FromByteOffset(
          unexpanded_macro_invocation_token.location.start().filename(),
          out_invocation_end_offset));
  int arg_index = 0;
  for (const auto& [arg_start_offset, arg_end_offset] : unexpanded_args) {
    arg_index++;

    // Create a new stack frame representing the argument expansion.
    // NOTE : Argument is a child of the macro invocation. This will be helpful
    // in generating error during the expansion of the argument.
    // we will be delinking macro invocation from the argument expansion once
    // argument is expanded.
    GOOGLESQL_ASSIGN_OR_RETURN(
        StackFrame * child_stack_frame,
        stack_frame_factory_.MakeStackFrame(
            AllocateString(std::string("arg:$" + std::to_string(arg_index)),
                           arena_),
            StackFrame::FrameType::kMacroArg,
            ParseLocationRange(
                ParseLocationPoint::FromByteOffset(
                    token_provider_->filename(),
                    arg_start_offset +
                        token_provider_->offset_in_original_input()),
                ParseLocationPoint::FromByteOffset(
                    token_provider_->filename(),
                    arg_end_offset +
                        token_provider_->offset_in_original_input())),
            token_provider_->input(),
            token_provider_->offset_in_original_input(),
            macro_expander_options_.diagnostic_options.error_message_options
                .input_original_start_line,
            macro_expander_options_.diagnostic_options.error_message_options
                .input_original_start_column,
            &macro_invocation_stack_frame,
            /*invocation_frame=*/&macro_invocation_stack_frame));
    std::vector<TokenWithLocation> expanded_arg;
    int max_arg_ref_index_in_current_arg;
    GOOGLESQL_RETURN_IF_ERROR(ExpandMacrosInternal(
        // Expand this arg, but note that we expand in raw tokenization mode:
        // We are not necessarily starting a statement or a script.
        // Pass the input from the beginning of the file, up to the end offset.
        // Note that the start offset is passed separately, in order to reflect
        // the correct location of each token.
        // If we only pass the arg as the input, the location offsets will start
        // from 0.
        token_provider_->CreateNewInstance(arg_start_offset, arg_end_offset),
        macro_catalog_, arena_, stack_frame_factory_, expansion_state_,
        call_arguments_, macro_expander_options_, child_stack_frame,
        location_map_, expanded_arg, warning_collector_,
        &max_arg_ref_index_in_current_arg,
        /*drop_comments=*/true));

    // Delink the macro invocation from the argument expansion.
    child_stack_frame->parent = nullptr;

    max_arg_ref_index_ =
        std::max(max_arg_ref_index_, max_arg_ref_index_in_current_arg);
    GOOGLESQL_RET_CHECK(!expanded_arg.empty()) << "A proper expansion should have at "
                                        "least the YYEOF token at the end";
    GOOGLESQL_RET_CHECK(expanded_arg.back().kind == Token::EOI);
    expanded_args.push_back(std::move(expanded_arg));
  }

  return absl::OkStatus();
}

absl::Status MacroExpander::ExpandMacroArgumentReference(
    const TokenWithLocation& token,
    std::vector<TokenWithLocation>& expanded_tokens) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  GOOGLESQL_RET_CHECK(expanded_tokens.empty());
  if (call_arguments_.empty()) {
    // This is the top-level, not the body of a macro (otherwise, we'd have at
    // least the macro name as arg #0). This means we just leave the arg ref
    // unexpanded, as opposed to assuming it was passed as if empty.
    expanded_tokens = {token,
                       {.kind = Token::EOI,
                        .location = token.location,
                        .text = "",
                        .preceding_whitespaces = ""}};
    return absl::OkStatus();
  }

  GOOGLESQL_ASSIGN_OR_RETURN(int arg_index, ParseMacroArgIndex(token.text));
  max_arg_ref_index_ = std::max(arg_index, max_arg_ref_index_);

  if (arg_index >= call_arguments_.size()) {
    GOOGLESQL_RETURN_IF_ERROR(RaiseErrorOrAddWarning(MakeSqlErrorAt(
        token.location.start(),
        absl::StrFormat(
            "Argument index %s out of range. Invocation was provided "
            "only %d arguments.",
            // call_args.size() - 1 because $0 is the added arg for macro name.
            token.text, call_arguments_.size() - 1))));
    expanded_tokens = {TokenWithLocation{.kind = Token::EOI,
                                         .location = token.location,
                                         .text = "",
                                         .preceding_whitespaces = ""}};
    return absl::OkStatus();
  }

  // Copy the list, since the same argument may be referenced
  // elsewhere.
  expanded_tokens = call_arguments_[arg_index];

  // Each Argument will have its expansion tree and arguments can be referenced
  // in multiple places. Argument reference will be represented as a separate
  // stack frame and that will be a parent of the Argument expansion tree.
  // Having a argument reference node a parent of the argument expansion tree
  // will help in finding correct stack trace.
  // eg. DEFINE MACRO m2 ABC;
  //     DEFINE MACRO m $1 $1; $m($m2())
  // In above example, $1 = $m2() which have its own
  // expansion tree and root of the tree will be arg:$1. $1 is referenced at two
  // places, so we will have two reference nodes as a parent of the argument
  // expansion tree.
  //                    $m2()
  //                   /    \
  //                  /      \
  //                 /        \
  //                /          \
  //               /            \
  //              /              \
  //        $1 (ArgRef 1)   $1 (ArgRef 2)
  //              \              /
  //               \            /
  //                \          /
  //                 \        /
  //                  \      /
  //                   arg:$1
  //                      |
  //                      m2
  //                      |
  //                     ABC
  //
  // Here we will have two parent nodes for the single node. Our data structure
  // doesn't allow us to have multiple parents for a node. To avoid this, we
  // will create a deep copy till argument reference node.
  //                    $m2()
  //                   /     \
  //                  /       \
  //                 /         \
  //                /           \
  //               /             \
  //              /               \
  //        $1 (ArgRef 1)   $1 (ArgRef 2)
  //              |                |
  //            arg:$1            arg:$1
  //              |                |
  //              m2               m2
  //              |                |
  //             ABC              ABC
  //
  // This will help in finding correct stack trace.
  for (TokenWithLocation& expanded_token : expanded_tokens) {
    StackFrame* stack_frame = expanded_token.stack_frame;
    if (stack_frame == nullptr) {
      continue;
    }
    GOOGLESQL_ASSIGN_OR_RETURN(StackFrame * copy_stack_frame,
                     stack_frame_factory_.AllocateStackFrame());
    expanded_token.stack_frame = copy_stack_frame;
    while (stack_frame != nullptr) {
      *copy_stack_frame = *stack_frame;
      if (stack_frame->parent != nullptr) {
        GOOGLESQL_ASSIGN_OR_RETURN(copy_stack_frame->parent,
                         stack_frame_factory_.AllocateStackFrame());
        copy_stack_frame = copy_stack_frame->parent;
      }
      stack_frame = stack_frame->parent;
    }
    // After copying the stack frame, we will
    // create a new stack frame for the argument reference and will add it as a
    // parent of the argument expansion tree.
    // NOTE : This newly added reference node will be attached to macro
    // invocation node.
    GOOGLESQL_ASSIGN_OR_RETURN(copy_stack_frame->parent,
                     stack_frame_factory_.MakeStackFrame(
                         AllocateString(absl::StrCat("$", arg_index), arena_),
                         StackFrame::FrameType::kArgRef, token.location,
                         token_provider_->input(),
                         token_provider_->offset_in_original_input(),
                         macro_expander_options_.diagnostic_options
                             .error_message_options.input_original_start_line,
                         macro_expander_options_.diagnostic_options
                             .error_message_options.input_original_start_column,
                         parent_location_));
  }
  return absl::OkStatus();
}

// Expands the macro invocation starting at the given token.
// REQUIRES: Any arguments must have already been loaded into the splicing
//           buffer.
absl::Status MacroExpander::ExpandMacroInvocation(
    const TokenWithLocation& token, const MacroInfo& macro_info,
    std::vector<TokenWithLocation>& expanded_tokens) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  GOOGLESQL_RET_CHECK(!token.text.empty());
  GOOGLESQL_RET_CHECK_EQ(token.text.front(), '$');
  GOOGLESQL_RET_CHECK(token.kind == Token::MACRO_INVOCATION);

  GOOGLESQL_ASSIGN_OR_RETURN(std::string_view macro_name, GetMacroName(token));

  // We expand arguments regardless, even if the macro being invoked does not
  // exist.
  std::vector<std::vector<TokenWithLocation>> expanded_args;
  GOOGLESQL_ASSIGN_OR_RETURN(
      StackFrame * macro_invocation_stack_frame,
      stack_frame_factory_.MakeStackFrame(
          AllocateString(absl::StrCat("macro:", macro_name), arena_),
          StackFrame::FrameType::kMacroInvocation,
          ParseLocationRange(token.location.start(),
                             ParseLocationPoint::FromByteOffset(
                                 token.location.end().filename(),
                                 token.location.end().GetByteOffset())),
          token_provider_->input(), token_provider_->offset_in_original_input(),
          macro_expander_options_.diagnostic_options.error_message_options
              .input_original_start_line,
          macro_expander_options_.diagnostic_options.error_message_options
              .input_original_start_column,
          parent_location_));

  bool has_explicit_unexpanded_arg;
  int invocation_end_offset;
  GOOGLESQL_RETURN_IF_ERROR(
      ParseAndExpandArgs(token, expanded_args, has_explicit_unexpanded_arg,
                         invocation_end_offset, *macro_invocation_stack_frame));

  MacroExpanderOptions child_macro_expander_options = macro_expander_options_;
  // The line & column in source when expanding the invocation reads from those
  // of the macro in question, not the top-level.
  ErrorMessageOptions& child_error_options =
      child_macro_expander_options.diagnostic_options.error_message_options;
  child_error_options.input_original_start_line =
      macro_info.definition_start_line;
  child_error_options.input_original_start_column =
      macro_info.definition_start_column;

  // The macro definition can contain anything, not necessarily a statement or
  // a script. Expanding a definition for an invocation always occurs in raw
  // tokenization, without carrying over comments.
  auto child_token_provider = token_provider_->CreateNewInstance(
      macro_info.location.start().filename(), macro_info.source_text,
      /*start_offset=*/macro_info.body_location.start().GetByteOffset(),
      /*end_offset=*/macro_info.body_location.end().GetByteOffset(),
      macro_info.definition_start_offset);

  // The number of explicit arguments is the number of arguments written by the
  // user, without the implicit $0 argument, which is the macro name.
  int num_explicit_args = static_cast<int>(expanded_args.size()) - 1;

  int max_arg_ref_in_definition;
  // Before expanding, mark the macro as visited and check for cycles.
  if (!expansion_state_.MarkAsVisited(macro_invocation_stack_frame->name)) {
    return MakeSqlErrorAt(macro_invocation_stack_frame->location.start(),
                          "Cycle detected in macro expansion");
  }
  GOOGLESQL_RETURN_IF_ERROR(ExpandMacrosInternal(
      std::move(child_token_provider), macro_catalog_, arena_,
      stack_frame_factory_, expansion_state_, std::move(expanded_args),
      child_macro_expander_options, macro_invocation_stack_frame, location_map_,
      expanded_tokens, warning_collector_, &max_arg_ref_in_definition,
      /*drop_comments=*/true));
  // Track that we are no longer in a cycle including this macro.
  expansion_state_.UnmarkAsVisited(macro_invocation_stack_frame->name);
  if (has_explicit_unexpanded_arg &&
      num_explicit_args > max_arg_ref_in_definition) {
    GOOGLESQL_RETURN_IF_ERROR(RaiseErrorOrAddWarning(MakeSqlErrorAt(
        token.location.start(),
        absl::StrFormat("Macro invocation has too many arguments (%d) while "
                        "the definition only references up to %d arguments",
                        num_explicit_args, max_arg_ref_in_definition))));
  }

  // The location map, if needed, only cares about the top level.
  if (location_map_ != nullptr && call_arguments_.empty()) {
    // Leading and trailing whitespace is dropped when replacing the invocation
    // with the expansion, but we have to do it a bit early here for the
    // location map.
    expanded_tokens.front().preceding_whitespaces = "";
    expanded_tokens.back().preceding_whitespaces = "";
    location_map_->insert_or_assign(
        token.start_offset(),
        Expansion{.macro_name = std::string(macro_name),
                  .full_match = std::string(absl::ClippedSubstr(
                      token_provider_->input(), token.start_offset(),
                      invocation_end_offset - token.start_offset())),
                  .expansion = TokensToString(expanded_tokens)});
  }
  return absl::OkStatus();
}

absl::StatusOr<TokenWithLocation> MacroExpander::ExpandLiteral(
    TokenWithLocation pending_token, TokenWithLocation literal_token) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  if (macro_expander_options_.is_strict &&
      !macro_expander_options_.diagnostic_options.warn_on_literal_expansion) {
    // Strict mode does not expand literals, and we are not producing the
    // warning on macros in literals.
    return AdvancePendingToken(std::move(pending_token),
                               std::move(literal_token));
  }

  // This function has the only cases where we add diagnostics directly as
  // warnings, instead of calling RaiseOrAddWarning().
  // In all other cases, a warning in lenient mode is an error that fails
  // expansion in strict mode. However, strict mode does not expand literals,
  // so such diagnostics are never an error that fails expansion.
  const TokenWithLocation original_literal_token = literal_token;

  absl::string_view literal_contents;
  GOOGLESQL_ASSIGN_OR_RETURN(
      QuotingSpec quoting,
      QuotingSpec::FindQuotingKind(literal_token.text, literal_contents));

  // Relative to the start of `token_provider_->input()`
  int literal_content_start_offset =
      literal_token.start_offset() -
      token_provider_->offset_in_original_input() +
      static_cast<int>(quoting.prefix().length() +
                       QuoteStr(quoting.quote_kind()).length());

  // We cannot expand like normal, because literals can contain anything,
  // which means we cannot count on our parser. For example, 3m is not a
  // SQL token. Furthermore, consider expanding the literal "#$a('#)", what is
  // a comment and what is just actual text? For tractability, we do not allow
  // arguments. We just have our own miniparser here.
  std::string content;
  for (int num_chars_read = 0; num_chars_read < literal_contents.length();
       num_chars_read++) {
    char cur_char = literal_contents[num_chars_read];

    // If this is not a macro invocation or a macro argument, or if this is
    // a dollar sign at the end, then no expansion will occur. Append
    // directly.
    if (cur_char != '$' || num_chars_read == literal_contents.length() - 1 ||
        !IsIdentifierCharacter(literal_contents[num_chars_read + 1])) {
      absl::StrAppend(&content, std::string(1, cur_char));
      continue;
    }

    // This is a macro token: either an invocation (including builtins), a macro
    // argument, or a standalone dollar sign with other stuff behind it.
    int end_index = num_chars_read + 1;
    GOOGLESQL_RET_CHECK(end_index < literal_contents.length());
    if (CanCharStartAnIdentifier(literal_contents[end_index])) {
      // A `$macro` invocation preceded by a dollar sign resembles a builtin
      // macro invocation (`$$macro`), which are not expanded inside literals.
      // We retain the preceding dollar sign in the final literal, emit a
      // warning, and try and expand the current macro invocation.
      if (num_chars_read > 0 && literal_contents[num_chars_read - 1] == '$') {
        ParseLocationPoint builtin_macro_location =
            literal_token.location.start();
        builtin_macro_location.IncrementByteOffset(num_chars_read);
        GOOGLESQL_RETURN_IF_ERROR(warning_collector_.AddWarning(
            MakeSqlErrorAt(builtin_macro_location,
                           "Builtin macros are not expanded inside literals")));
      }

      do {
        end_index++;
      } while (end_index < literal_contents.size() &&
               IsIdentifierCharacter(literal_contents[end_index]));

      if (end_index < literal_contents.size() &&
          literal_contents[end_index] == '(') {
        ParseLocationPoint paren_location = literal_token.location.start();
        paren_location.SetByteOffset(
            literal_content_start_offset + end_index +
            token_provider_->offset_in_original_input());

        if (macro_expander_options_.diagnostic_options
                .warn_on_literal_expansion) {
          GOOGLESQL_RETURN_IF_ERROR(warning_collector_.AddWarning(MakeSqlErrorAt(
              paren_location,
              "Argument lists are not allowed inside literals")));
        }

        // Best-effort expansion of this invocation, if we can compose an
        // argument list. No quotes or other parens inside.
        int token_end = end_index + 1;
        const auto disallowed_in_expansion_in_literal = [](const char c) {
          return c == '(' || c == ')' || c == '\'' || c == '"';
        };
        while (
            token_end < literal_contents.size() &&
            !disallowed_in_expansion_in_literal(literal_contents[token_end])) {
          token_end++;
        }
        if (token_end == literal_contents.size() ||
            literal_contents[token_end] != ')') {
          // This is an error, even in lenient mode, because the args are not
          // parseable and can be anything.
          return MakeSqlErrorAt(
              paren_location,
              "Nested macro argument lists inside literals are not allowed");
        }
        end_index = token_end + 1;
      }
    } else {
      // If this is an argument reference, pick up the argument position.
      while (end_index < literal_contents.size() &&
             std::isdigit(literal_contents[end_index])) {
        end_index++;
      }
      if (end_index > num_chars_read) {
        GOOGLESQL_ASSIGN_OR_RETURN(int arg_index,
                         ParseMacroArgIndex(literal_contents.substr(
                             num_chars_read, end_index - num_chars_read)));
        max_arg_ref_index_ = std::max(arg_index, max_arg_ref_index_);
      }
    }

    std::vector<TokenWithLocation> expanded_tokens;
    // relative to the start of `token_provider_->input()`
    int invocation_start_offset = literal_content_start_offset + num_chars_read;

    auto child_token_provider = token_provider_->CreateNewInstance(
        /*start_offset=*/invocation_start_offset,
        /*end_offset=*/literal_content_start_offset + end_index);

    // For MakeSqlErrorAt, we need the location relative to the start of the
    // original file, not just `token_provider_->input()`.
    ParseLocationPoint unexpanded_macro_start_point =
        literal_token.location.start();
    unexpanded_macro_start_point.SetByteOffset(
        invocation_start_offset + token_provider_->offset_in_original_input());

    if (macro_expander_options_.diagnostic_options.warn_on_literal_expansion) {
      GOOGLESQL_RETURN_IF_ERROR(warning_collector_.AddWarning(
          MakeSqlErrorAt(unexpanded_macro_start_point,
                         "Macro expansion in literals is deprecated. Strict "
                         "mode does not expand literals")));
    }

    num_chars_read = end_index - 1;

    if (macro_expander_options_.is_strict) {
      // Do not expand. Simply continue checking for other warnings.
      continue;
    }

    GOOGLESQL_RETURN_IF_ERROR(ExpandMacrosInternal(
        std::move(child_token_provider), macro_catalog_, arena_,
        stack_frame_factory_, expansion_state_, call_arguments_,
        macro_expander_options_, parent_location_, location_map_,
        expanded_tokens, warning_collector_,
        /*out_max_arg_ref_index=*/nullptr, /*drop_comments=*/true));

    GOOGLESQL_RET_CHECK(!expanded_tokens.empty())
        << "A proper expansion should have at least the YYEOF token at "
           "the end. Failure was when expanding "
        << literal_token.text;
    GOOGLESQL_RET_CHECK_EQ(expanded_tokens.back().kind, Token::EOI);
    // Pop the trailing space, which is stored on the YYEOF's
    // preceding_whitespaces.
    expanded_tokens.pop_back();

    for (TokenWithLocation& expanded_token : expanded_tokens) {
      // we always choose the first stack frame for the literal expansion.
      if (literal_token.stack_frame == nullptr) {
        literal_token.stack_frame = expanded_token.stack_frame;
        literal_token.location = expanded_token.location;
      }

      // Intermediate spaces are preserved in the outer literal.
      absl::StrAppend(&content, expanded_token.preceding_whitespaces);

      // Ensure the incoming text can be safely placed in the current literal
      if (!IsQuotedLiteral(expanded_token)) {
        absl::StrAppend(&content, expanded_token.text);
      } else {
        absl::string_view extracted_token_content;
        GOOGLESQL_ASSIGN_OR_RETURN(QuotingSpec inner_spec,
                         QuotingSpec::FindQuotingKind(expanded_token.text,
                                                      extracted_token_content));
        if (quoting.literal_kind() != inner_spec.literal_kind()) {
          return MakeSqlErrorAt(
              unexpanded_macro_start_point,
              absl::StrFormat("Cannot expand a %s into a %s.",
                              inner_spec.Description(), quoting.Description()));
        }

        if (AreSame(quoting, inner_spec)) {
          // Expand in the same literal since it's identical, but remove the
          // quotes.
          GOOGLESQL_RET_CHECK(expanded_token.preceding_whitespaces.empty());
          absl::StrAppend(&content, extracted_token_content);
        } else {
          // We need to break the literal
          TokenWithLocation current_literal = literal_token;
          current_literal.text =
              AllocateString(QuoteText(content, quoting), arena_);
          GOOGLESQL_ASSIGN_OR_RETURN(current_literal,
                           AdvancePendingToken(std::move(pending_token),
                                               std::move(current_literal)));
          output_token_buffer_.Push(std::move(current_literal));
          if (expanded_token.preceding_whitespaces.empty()) {
            // The literal components need some whitespace separation.
            // see (broken link).
            expanded_token.preceding_whitespaces = " ";
          }
          output_token_buffer_.Push(std::move(expanded_token));

          pending_token = {.kind = Token::UNAVAILABLE,
                           .location = literal_token.location,
                           .text = "",
                           .preceding_whitespaces = ""};

          content.clear();
          literal_token.preceding_whitespaces = " ";
        }
      }
    }
  }

  if (macro_expander_options_.is_strict) {
    return original_literal_token;
  }

  literal_token.text = AllocateString(QuoteText(content, quoting), arena_);
  return literal_token;
}

absl::Status MacroExpander::ExpandMacrosInternal(
    std::unique_ptr<TokenProviderBase> token_provider,
    const MacroCatalog& macro_catalog, googlesql_base::UnsafeArena* arena,
    StackFrame::StackFrameFactory& stack_frame_factory,
    ExpansionState& expansion_state,
    const std::vector<std::vector<TokenWithLocation>>& call_arguments,
    MacroExpanderOptions macro_expander_options,
    StackFrame* /*absl_nullable*/ parent_location,
    absl::btree_map<size_t, Expansion>* location_map,
    std::vector<TokenWithLocation>& output_token_list,
    WarningCollector& warning_collector, int* out_max_arg_ref_index,
    bool drop_comments) {
  // Increase the invocation count before any expansion.
  expansion_state.IncrementInvocationCount();

  // Check if the total number of macro invocations is within the limit.
  int64_t total_number_of_macro_invocations =
      expansion_state.GetTotalNumberOfMacroInvocations();
  if (total_number_of_macro_invocations >
      macro_expander_options.max_macro_invocations) {
    return absl::InternalError(absl::Substitute(
        "Too many macro invocations: $0", total_number_of_macro_invocations));
  }

  auto expander = absl::WrapUnique(new MacroExpander(
      std::move(token_provider), macro_catalog, arena, stack_frame_factory,
      expansion_state, call_arguments, macro_expander_options,
      &warning_collector, parent_location));
  expander->location_map_ = location_map;
  while (true) {
    GOOGLESQL_ASSIGN_OR_RETURN(TokenWithLocation token, expander->GetNextToken());
    if (drop_comments && token.kind == Token::COMMENT) {
      // We only preserve top level comments.
      continue;
    }
    output_token_list.push_back(std::move(token));
    if (output_token_list.back().kind == Token::EOI) {
      break;
    }
  }
  if (out_max_arg_ref_index != nullptr) {
    *out_max_arg_ref_index = expander->max_arg_ref_index_;
  }
  return absl::OkStatus();
}

}  // namespace macros
}  // namespace parser
}  // namespace googlesql
