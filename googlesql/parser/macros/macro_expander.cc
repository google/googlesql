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

#include "googlesql/base/arena_allocator.h"
#include "googlesql/common/thread_stack.h"
#include "googlesql/parser/macros/diagnostic.h"
#include "googlesql/parser/macros/macro_catalog.h"
#include "googlesql/parser/macros/quoting.h"
#include "googlesql/parser/macros/standalone_macro_expansion.h"
#include "googlesql/parser/macros/token_provider_base.h"
#include "googlesql/parser/macros/token_splicing_utils.h"
#include "googlesql/parser/tm_token.h"
#include "googlesql/parser/token_stream.h"
#include "googlesql/parser/token_with_location.h"
#include "googlesql/proto/internal_error_location.pb.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/strings.h"
#include "absl/base/nullability.h"
#include "absl/container/btree_map.h"
#include "googlesql/base/check.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "googlesql/base/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "googlesql/base/arena.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {
namespace parser {
namespace macros {

constexpr absl::string_view kBuiltinMacroIdentifier = "IDENTIFIER";
constexpr absl::string_view kBuiltinMacroString = "STRING";

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

absl::StatusOr<int> ParseMacroArgIndex(absl::string_view text) {
  GOOGLESQL_RET_CHECK_GE(text.length(), 1);
  GOOGLESQL_RET_CHECK_EQ(text.front(), '$');
  int arg_index;
  GOOGLESQL_RET_CHECK(absl::SimpleAtoi(text.substr(1), &arg_index));
  return arg_index;
}

// Returns true if `kind` is a macro invocation (user or built-in).
static bool IsMacroInvocation(Token kind) {
  return kind == Token::MACRO_INVOCATION ||
         kind == Token::MACRO_BUILTIN_INVOCATION;
}

// Returns true if `kind` is a macro argument reference.
static bool IsMacroArgumentReference(Token kind) {
  return kind == Token::MACRO_ARGUMENT_REFERENCE;
}

// Returns true if `kind` is any macro item (invocation or argument reference).
static bool IsMacroItem(Token kind) {
  return IsMacroInvocation(kind) || IsMacroArgumentReference(kind);
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

// Looks up the location of the unexpanded argument that expanded to the
// `expanded_arg_token` for a given macro invocation (represented by
// `invocation_frame`).
static absl::StatusOr<ParseLocationRange> GetUnexpandedArgumentLocation(
    const StackFrame& invocation_frame,
    const TokenWithLocation& expanded_arg_token) {
  GOOGLESQL_RET_CHECK(expanded_arg_token.stack_frame != nullptr)
      << "Expanded argument tokens must have a stack frame";
  // The macro argument stack frame (kMacroArg), whose `invocation_frame`
  // corresponds to the macro invocation of interest, is the lowest common
  // ancestor in the tree representing the stack trace of expanded tokens
  // spanning a single argument in an invocation.
  //
  // This macro argument stack frame's children represents unexpanded argument
  // tokens. The following implementation traverses the stack trace up from the
  // `expanded_arg` till it encounters this child of the macro argument stack
  // frame, whose location is used for error reporting.

  // When an argument token does not require further expansions (i.e. when it is
  // not a macro invocation or argument reference token) then the
  // `expanded_arg_token` is the same as the unexpanded argument.
  if (expanded_arg_token.stack_frame->invocation_frame == &invocation_frame) {
    return expanded_arg_token.location;
  }
  StackFrame& unexpanded_arg_frame = *expanded_arg_token.stack_frame;
  while (unexpanded_arg_frame.parent != nullptr &&
         unexpanded_arg_frame.parent->invocation_frame != &invocation_frame) {
    unexpanded_arg_frame = *unexpanded_arg_frame.parent;
  }
  GOOGLESQL_RET_CHECK(unexpanded_arg_frame.parent != nullptr)
      << "Unexpanded argument token must have a parent stack frame";
  GOOGLESQL_RET_CHECK(unexpanded_arg_frame.parent->invocation_frame == &invocation_frame)
      << "Invalid stack trace: Missing link to invocation frame";
  return unexpanded_arg_frame.location;
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

absl::StatusOr<std::unique_ptr<MacroExpander>> MacroExpander::Create(
    TokenStream* token_provider, Catalog* /*absl_nullable*/ catalog,
    googlesql_base::UnsafeArena* /*absl_nonnull*/ arena,
    MacroExpanderOptions macro_expander_options,
    StackFrame* /*absl_nullable*/ parent_location) {
  TokenProviderBase* token_provider_base =
      dynamic_cast<TokenProviderBase*>(token_provider);
  GOOGLESQL_RET_CHECK(token_provider_base != nullptr);
  GOOGLESQL_RET_CHECK(arena != nullptr);
  auto stack_frame_factory = std::make_shared<StackFrame::StackFrameFactory>(
      arena, macro_expander_options.max_number_of_stack_frames);
  return absl::WrapUnique(new MacroExpander(
      token_provider_base, catalog, std::move(stack_frame_factory),
      macro_expander_options, parent_location));
}

MacroExpander::MacroExpander(
    TokenProviderBase* token_provider, Catalog* /*absl_nullable*/ catalog,
    std::shared_ptr<StackFrame::StackFrameFactory> stack_frame_factory,
    MacroExpanderOptions macro_expander_options,
    StackFrame* /*absl_nullable*/ parent_location)
    : MacroExpander(token_provider, catalog, std::move(stack_frame_factory),
                    // Public constructor, expansion state uses the owned
                    // (empty) expansion state from this MacroExpander.
                    owned_expansion_state_,
                    /*call_arguments=*/{}, macro_expander_options,
                    /*override_warning_collector=*/nullptr, parent_location) {}

absl::StatusOr<ExpansionOutput> MacroExpander::ExpandMacros(
    std::unique_ptr<TokenStream> token_provider, Catalog* /*absl_nullable*/ catalog,
    MacroExpanderOptions macro_expander_options) {
  ExpansionOutput expansion_output;
  ExpansionState expansion_state;
  expansion_output.arena = CreateUnsafeArena();
  auto stack_frame_factory = std::make_shared<StackFrame::StackFrameFactory>(
      expansion_output.arena.get(),
      macro_expander_options.max_number_of_stack_frames);
  WarningCollector warning_collector(
      macro_expander_options.diagnostic_options.max_warning_count);
  GOOGLESQL_RETURN_IF_ERROR(ExpandMacrosInternal(
      std::move(token_provider), catalog, std::move(stack_frame_factory),
      expansion_state,
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

absl::Status MacroExpander::MakeInvalidBuiltinArgumentError(
    const StackFrame& invocation_frame,
    const TokenWithLocation& expanded_arg_token, absl::string_view message) {
  GOOGLESQL_ASSIGN_OR_RETURN(
      const ParseLocationRange& unexpanded_arg_location,
      GetUnexpandedArgumentLocation(invocation_frame, expanded_arg_token));
  return MakeSqlErrorWithStackFrame(unexpanded_arg_location.start(), message,
                                    token_provider_->input(),
                                    expanded_arg_token.stack_frame,
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

int MacroExpander::num_consumed_tokens() const {
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

  if (IsMacroItem(token.kind)) {
    // Only load a macro invocation or argument reference into the current chunk
    // if the token on the left can actually splice with its expansion (see
    // `CanSplice`).
    return last_was_macro_invocation ||
           IsMacroArgumentReference(last_token.kind) ||
           TokenCanBeKeywordOrUnquotedIdentifier(last_token);
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
  return last_was_macro_invocation || IsMacroArgumentReference(last_token.kind);
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

  return AllocateString(absl::StrCat(a, b), stack_frame_factory_->arena());
}

// TODO : Update token provider to have the options or state of macro expander.
// So that this can be moved into the token provider.
absl::StatusOr<TokenWithLocation> MacroExpander::ConsumeInputToken() {
  GOOGLESQL_ASSIGN_OR_RETURN(TokenWithLocation token, token_provider_->GetNextToken());
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
      GOOGLESQL_ASSIGN_OR_RETURN(TokenWithLocation next_token,
                       token_provider_->PeekLookahead2());
      bool has_visibility = (next_token.kind == Token::KW_PUBLIC ||
                             next_token.kind == Token::KW_PRIVATE);

      GOOGLESQL_ASSIGN_OR_RETURN(TokenWithLocation macro_token,
                       has_visibility ? token_provider_->PeekLookahead3()
                                      : token_provider_->PeekLookahead2());

      if (macro_token.kind == Token::KW_MACRO) {
        GOOGLESQL_ASSIGN_OR_RETURN(TokenWithLocation define_token, ConsumeInputToken());
        // Mark the leading DEFINE keyword as the special one marking a DEFINE
        // MACRO statement.
        define_token.kind = Token::KW_DEFINE_FOR_MACROS;
        inside_macro_definition_ = true;
        at_statement_start_ = false;
        splicing_buffer_.push(define_token);
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

    // Record when loading, not when expanding: loading reads one token past
    // the chunk, so a tokenizer error there (e.g. `$m() 'abc`) can come out
    // before this chunk is expanded. Use the start of the chunk, since a token
    // glued to the invocation (`x$m()`) comes out of the same expansion.
    if (!inside_macro_definition_ && IsMacroItem(token.kind)) {
      RecordMacroInvocation(splicing_buffer_.front().start_offset());
    }

    if (IsMacroInvocation(token.kind)) {
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
  GOOGLESQL_RET_CHECK(IsMacroInvocation(splicing_buffer_.back().kind))
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
  if (IsMacroArgumentReference(unexpanded_macro_token.kind)) {
    GOOGLESQL_RETURN_IF_ERROR(
        ExpandMacroArgumentReference(unexpanded_macro_token, expanded_tokens));
  } else {
    GOOGLESQL_RET_CHECK(IsMacroInvocation(unexpanded_macro_token.kind));
    GOOGLESQL_ASSIGN_OR_RETURN(absl::string_view macro_name,
                     GetMacroName(unexpanded_macro_token));
    GOOGLESQL_ASSIGN_OR_RETURN(StackFrame * invocation_stack_frame,
                     MakeInvocationStackFrame(unexpanded_macro_token));
    std::vector<TokenWithLocation> unexpanded_args;
    std::vector<std::vector<TokenWithLocation>> expanded_args;
    const Macro* macro = nullptr;

    // The following check exists to prevent arguments from being parsed
    // and expanded if the invoked macro does not exist. This applies only to
    // user macro invocations in LENIENT mode.
    if (unexpanded_macro_token.kind == Token::MACRO_INVOCATION) {
      if (catalog_ != nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(catalog_->GetMacro(std::string(macro_name), &macro));
      }
      if (macro == nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(RaiseErrorOrAddWarning(MakeSqlErrorAt(
            unexpanded_macro_token.location.start(),
            absl::StrFormat("Macro '%s' not found.", macro_name))));
        // In lenient mode, just return this token as is, and let the argument
        // list expand as if it were just normal text.
        return AdvancePendingToken(std::move(pending_token),
                                   std::move(unexpanded_macro_token));
      }
    }

    GOOGLESQL_RETURN_IF_ERROR(ParseAndExpandArgs(unexpanded_macro_token, unexpanded_args,
                                       expanded_args, *invocation_stack_frame));

    if (unexpanded_macro_token.kind == Token::MACRO_BUILTIN_INVOCATION) {
      GOOGLESQL_RETURN_IF_ERROR(ExpandBuiltinMacroInvocation(
          macro_name, *invocation_stack_frame, unexpanded_args, expanded_args,
          expanded_tokens));
    } else {
      GOOGLESQL_RET_CHECK_NE(macro, nullptr);
      GOOGLESQL_RETURN_IF_ERROR(ExpandUserMacroInvocation(*macro, *invocation_stack_frame,
                                                unexpanded_args, expanded_args,
                                                expanded_tokens));
    }
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
    } else if (IsMacroItem(token.kind)) {
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
    pending_whitespaces_ = AllocateString(pending_token.preceding_whitespaces,
                                          stack_frame_factory_->arena());
  }
  return absl::OkStatus();
}

absl::Status MacroExpander::ParseAndExpandArgs(
    const TokenWithLocation& unexpanded_macro_invocation_token,
    std::vector<TokenWithLocation>& unexpanded_args,
    std::vector<std::vector<TokenWithLocation>>& expanded_args,
    StackFrame& macro_invocation_stack_frame) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  GOOGLESQL_RET_CHECK(expanded_args.empty() && unexpanded_args.empty());
  int out_invocation_end_offset =
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
  std::vector<ParseRange> unexpanded_arg_ranges;
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
    } else if (token.kind == Token::COMMA && num_open_parens == 1) {
      // Top-level comma means the end of the current argument
      unexpanded_arg_ranges.push_back(
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
    } else if (token.kind == Token::RPAREN) {
      num_open_parens--;
      if (num_open_parens == 0) {
        // This was the last argument.
        out_invocation_end_offset = token.end_offset();
        unexpanded_arg_ranges.push_back(
            {.start_offset = arg_start_offset,
             .end_offset =
                 token.start_offset() -
                 token_provider_->offset_in_original_input() -
                 static_cast<int>(token.preceding_whitespaces.length())});
      }
    }

    // `unexpanded_args` excludes comments and the outermost parentheses
    // representing the argument list.
    if (token.kind != Token::COMMENT && num_open_parens > 0) {
      unexpanded_args.push_back(std::move(token));
    }
  }
  // Update the end location of the macro invocation to reflect the end of the
  // last argument.
  macro_invocation_stack_frame.location.set_end(
      ParseLocationPoint::FromByteOffset(
          unexpanded_macro_invocation_token.location.start().filename(),
          out_invocation_end_offset));
  int arg_index = 0;
  for (const auto& [arg_start_offset, arg_end_offset] : unexpanded_arg_ranges) {
    arg_index++;

    // Create a new stack frame representing the argument expansion.
    // NOTE : Argument is a child of the macro invocation. This will be helpful
    // in generating error during the expansion of the argument.
    // we will be delinking macro invocation from the argument expansion once
    // argument is expanded.
    GOOGLESQL_ASSIGN_OR_RETURN(
        StackFrame * child_stack_frame,
        stack_frame_factory_->MakeStackFrame(
            AllocateString(std::string("arg:$" + std::to_string(arg_index)),
                           stack_frame_factory_->arena()),
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
        catalog_, stack_frame_factory_, expansion_state_, call_arguments_,
        macro_expander_options_, child_stack_frame, location_map_, expanded_arg,
        warning_collector_, &max_arg_ref_index_in_current_arg,
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
                     stack_frame_factory_->AllocateStackFrame());
    expanded_token.stack_frame = copy_stack_frame;
    while (stack_frame != nullptr) {
      *copy_stack_frame = *stack_frame;
      if (stack_frame->parent != nullptr) {
        GOOGLESQL_ASSIGN_OR_RETURN(copy_stack_frame->parent,
                         stack_frame_factory_->AllocateStackFrame());
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
                     stack_frame_factory_->MakeStackFrame(
                         AllocateString(absl::StrCat("$", arg_index),
                                        stack_frame_factory_->arena()),
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

absl::Status MacroExpander::ExpandBuiltinMacroInvocation(
    absl::string_view builtin_macro_name, StackFrame& builtin_invocation_frame,
    const std::vector<TokenWithLocation>& unexpanded_args,
    const std::vector<std::vector<TokenWithLocation>>& expanded_args,
    std::vector<TokenWithLocation>& expanded_tokens) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  GOOGLESQL_RET_CHECK(!expanded_args.empty()) << "Implicit arg $0 must always be present";
  GOOGLESQL_RET_CHECK(!expanded_args[0].empty()) << "Implicit arg $0 cannot be empty";
  GOOGLESQL_RET_CHECK_EQ(expanded_args[0][0].kind, Token::IDENTIFIER);
  GOOGLESQL_RET_CHECK_EQ(expanded_args[0][0].text, builtin_macro_name);

  if (builtin_macro_name == kBuiltinMacroIdentifier) {
    GOOGLESQL_RETURN_IF_ERROR(ExpandIdentifierBuiltin(builtin_invocation_frame,
                                            expanded_args, expanded_tokens));
  } else if (builtin_macro_name == kBuiltinMacroString) {
    GOOGLESQL_RETURN_IF_ERROR(ExpandStringBuiltin(builtin_invocation_frame,
                                        unexpanded_args, expanded_args,
                                        expanded_tokens));
  } else {
    return MakeSqlErrorAt(
        builtin_invocation_frame.location.start(),
        absl::StrFormat("Builtin macro '%s' not found", builtin_macro_name));
  }

  expanded_tokens.push_back({
      .kind = Token::EOI,
      .location = builtin_invocation_frame.location,
      .text = "",
      .stack_frame = &builtin_invocation_frame,
  });
  return absl::OkStatus();
}

absl::Status MacroExpander::ExpandIdentifierBuiltin(
    StackFrame& builtin_invocation_frame,
    const std::vector<std::vector<TokenWithLocation>>& expanded_args,
    std::vector<TokenWithLocation>& expanded_tokens) {
  GOOGLESQL_RET_CHECK_GE(expanded_args.size(), 1);
  GOOGLESQL_RET_CHECK_GE(expanded_args.front().size(), 1);
  GOOGLESQL_RET_CHECK_EQ(expanded_args.front()[0].kind, Token::IDENTIFIER);
  GOOGLESQL_RET_CHECK_EQ(expanded_args.front()[0].text, kBuiltinMacroIdentifier);
  absl::Span<const std::vector<TokenWithLocation>> builtin_args =
      absl::MakeSpan(expanded_args).subspan(1);

  std::string spliced_identifier_name;
  for (const std::vector<TokenWithLocation>& expanded_arg : builtin_args) {
    // All argument expansions, including empty ones, must be EOI terminated.
    GOOGLESQL_RET_CHECK(!expanded_arg.empty())
        << "Malformed argument expansion: missing EOI terminator";
    GOOGLESQL_RET_CHECK_EQ(expanded_arg.back().kind, Token::EOI)
        << "Malformed argument expansion: missing EOI terminator";

    // A single argument must be at most a single token (excluding EOI)
    if (expanded_arg.size() > 2) {
      return MakeInvalidBuiltinArgumentError(
          builtin_invocation_frame, expanded_arg[1],
          absl::StrCat("Argument must resolve to at most a single token, got: ",
                       expanded_arg.size() - 1));
    }

    // Empty arguments are allowed.
    if (expanded_arg.front().kind == Token::EOI) {
      continue;
    }

    // Argument token must be one of the following:
    // 1. IDENTIFIER (quoted or unquoted)
    // 2. KEYWORD
    // 3. DECIMAL_INTEGER_LITERAL
    const TokenWithLocation& arg_token = expanded_arg.front();
    if (arg_token.kind == Token::IDENTIFIER || IsKeyword(arg_token.text)) {
      std::string identifier_name;
      GOOGLESQL_RETURN_IF_ERROR(ParseGeneralizedIdentifier(expanded_arg.front().text,
                                                 &identifier_name,
                                                 /*error_string=*/nullptr,
                                                 /*error_offset=*/nullptr));
      absl::StrAppend(&spliced_identifier_name, identifier_name);
    } else if (expanded_arg.front().kind == Token::DECIMAL_INTEGER_LITERAL) {
      absl::StrAppend(&spliced_identifier_name, expanded_arg[0].text);
    } else {
      return MakeInvalidBuiltinArgumentError(
          builtin_invocation_frame, expanded_arg.front(),
          "Invalid argument: Only identifiers, keywords and "
          "integer literals are allowed.");
    }
  }

  if (spliced_identifier_name.empty()) {
    return MakeSqlErrorAt(
        builtin_invocation_frame.location.start(),
        "Invalid invocation: At least one non-empty argument is required.");
  }

  expanded_tokens.push_back(TokenWithLocation{
      .kind = Token::IDENTIFIER,
      .location = builtin_invocation_frame.location,
      .text = AllocateString(ToIdentifierLiteral(spliced_identifier_name),
                             stack_frame_factory_->arena()),
      .stack_frame = &builtin_invocation_frame});
  return absl::OkStatus();
}

absl::Status MacroExpander::ExpandStringBuiltin(
    StackFrame& builtin_invocation_frame,
    const std::vector<TokenWithLocation>& unexpanded_args,
    const std::vector<std::vector<TokenWithLocation>>& expanded_args,
    std::vector<TokenWithLocation>& expanded_tokens) {
  GOOGLESQL_RETURN_IF_ERROR(ValidateStringBuiltinInvocation(
      builtin_invocation_frame, unexpanded_args, expanded_args));

  std::string result = "";

  std::vector<TokenWithLocation> tokens_to_stringify = expanded_args[1];
  GOOGLESQL_RET_CHECK_EQ(tokens_to_stringify.back().kind, Token::EOI);
  tokens_to_stringify.pop_back();

  for (int i = 0; i < tokens_to_stringify.size(); ++i) {
    const TokenWithLocation& token = tokens_to_stringify[i];
    if (i > 0 && !tokens_to_stringify[i - 1].AdjacentlyPrecedes(token)) {
      absl::StrAppend(&result, " ");
    }
    absl::StrAppend(&result, token.text);
  }

  // If there is exactly one token and it is a string literal, then we pass it
  // through as is. Otherwise, (single) quote the expansion.
  if (tokens_to_stringify.empty() ||
      tokens_to_stringify[0].kind != Token::STRING_LITERAL) {
    result = ToSingleQuotedStringLiteral(result);
  }

  expanded_tokens.push_back(TokenWithLocation{
      .kind = Token::STRING_LITERAL,
      .location = builtin_invocation_frame.location,
      .text = AllocateString(result, stack_frame_factory_->arena()),
      .stack_frame = &builtin_invocation_frame});
  return absl::OkStatus();
}

absl::Status MacroExpander::ValidateStringBuiltinInvocation(
    const StackFrame& builtin_invocation_frame,
    const std::vector<TokenWithLocation>& unexpanded_args,
    const std::vector<std::vector<TokenWithLocation>>& expanded_args) {
  absl::string_view arg_spec =
      "Invocation requires exactly one argument that is a macro invocation "
      "or a macro argument reference";

  // Empty arguments in the unexpanded form is invalid. Note that, it is ok for
  // the expansion to be empty (i.e. only EOI), in which case we return an empty
  // string literal token.
  if (unexpanded_args.empty()) {
    return MakeSqlErrorAt(builtin_invocation_frame.location.start(), arg_spec);
  }

  // Argument must be exactly a single macro invocation or macro argument.
  const TokenWithLocation& first_arg = unexpanded_args.front();
  if (!IsMacroItem(first_arg.kind)) {
    return MakeSqlErrorAt(first_arg.location.start(), arg_spec);
  }

  // We know by now the first argument is a macro invocation or a macro argument
  // reference.
  //
  // If in addition to that, it is the only argument, then it is valid. Note
  // that, while macro invocations with an empty argument list still requires
  // parentheses (i.e. `()`) to be present in STRICT mode, that check happens
  // before this validation.
  if (unexpanded_args.size() == 1) {
    return absl::OkStatus();
  }

  // When the argument is a macro argument reference, it must be the only token
  if (IsMacroArgumentReference(first_arg.kind) && unexpanded_args.size() > 1) {
    return MakeSqlErrorAt(unexpanded_args[1].location.start(), arg_spec);
  }

  // We now know for a fact that the first argument is a macro invocation
  // followed by additional tokens.
  GOOGLESQL_RET_CHECK(IsMacroInvocation(first_arg.kind));

  // If the macro invocation is followed by additional tokens, they should
  // strictly constitute an argument list to be considered a valid single macro
  // invocation.
  //
  // A valid non-empty argument list must start with a left parenthesis, which
  // in addition must be adjacent to the macro invocation token.
  if (unexpanded_args[1].kind != Token::LPAREN ||
      !first_arg.AdjacentlyPrecedes(unexpanded_args[1])) {
    return MakeSqlErrorAt(first_arg.location.start(), arg_spec);
  }

  // Find the right parenthesis token that balances the opening parenthesis. For
  // a single macro invocation, closing parenthesis must be the last token in
  // the argument.
  //
  // `2` is safe below because we now know for a fact that this is a macro
  // invocation with a non-empty argument list, and the `ParseAndExpandArgs`
  // guarantees that the parenthesis in the argument list are balanced.
  int token_index = 2;
  int num_open_parens = 1;
  while (num_open_parens > 0) {
    GOOGLESQL_RET_CHECK(token_index < unexpanded_args.size())
        << "Parenthesis must be balanced in the argument list";
    if (unexpanded_args[token_index].kind == Token::LPAREN) {
      num_open_parens++;
    } else if (unexpanded_args[token_index].kind == Token::RPAREN) {
      num_open_parens--;
    }
    token_index++;
  }

  // If there are any tokens beyond the macro invocation and the argument
  // list, return an error at the first such token.
  if (token_index < unexpanded_args.size()) {
    return MakeSqlErrorAt(unexpanded_args[token_index].location.start(),
                          arg_spec);
  }
  return absl::OkStatus();
}

// Expands the user macro invocation.
// REQUIRES: The macro definition must have already been loaded from the
//           macro catalog.
absl::Status MacroExpander::ExpandUserMacroInvocation(
    const Macro& macro, StackFrame& macro_invocation_stack_frame,
    const std::vector<TokenWithLocation>& unexpanded_args,
    const std::vector<std::vector<TokenWithLocation>>& expanded_args,
    std::vector<TokenWithLocation>& expanded_tokens) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  const ParseLocationRange& invocation_location =
      macro_invocation_stack_frame.location;

  MacroExpanderOptions child_macro_expander_options = macro_expander_options_;
  // The line & column in source when expanding the invocation reads from those
  // of the macro in question, not the top-level.
  ErrorMessageOptions& child_error_options =
      child_macro_expander_options.diagnostic_options.error_message_options;
  child_error_options.input_original_start_line = macro.definition_start_line();
  child_error_options.input_original_start_column =
      macro.definition_start_column();

  // The macro definition can contain anything, not necessarily a statement or
  // a script. Expanding a definition for an invocation always occurs in raw
  // tokenization, without carrying over comments.
  auto child_token_provider = token_provider_->CreateNewInstance(
      macro.location().start().filename(), macro.source_text(),
      /*start_offset=*/macro.body_location().start().GetByteOffset(),
      /*end_offset=*/macro.body_location().end().GetByteOffset(),
      macro.definition_start_offset());

  // The number of explicit arguments is the number of arguments written by the
  // user, without the implicit $0 argument, which is the macro name.
  int num_explicit_args = static_cast<int>(expanded_args.size()) - 1;

  int max_arg_ref_in_definition;
  // Before expanding, mark the macro as visited and check for cycles.
  if (!expansion_state_.MarkAsVisited(macro_invocation_stack_frame.name)) {
    return MakeSqlErrorAt(macro_invocation_stack_frame.location.start(),
                          "Cycle detected in macro expansion");
  }
  GOOGLESQL_RETURN_IF_ERROR(ExpandMacrosInternal(
      std::move(child_token_provider), catalog_, stack_frame_factory_,
      expansion_state_, std::move(expanded_args), child_macro_expander_options,
      &macro_invocation_stack_frame, location_map_, expanded_tokens,
      warning_collector_, &max_arg_ref_in_definition,
      /*drop_comments=*/true));
  // Track that we are no longer in a cycle including this macro.
  expansion_state_.UnmarkAsVisited(macro_invocation_stack_frame.name);
  if (!unexpanded_args.empty() &&
      num_explicit_args > max_arg_ref_in_definition) {
    GOOGLESQL_RETURN_IF_ERROR(RaiseErrorOrAddWarning(MakeSqlErrorAt(
        invocation_location.start(),
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
        invocation_location.start().GetByteOffset(),
        Expansion{.macro_name = std::string(macro.Name()),
                  .full_match = std::string(absl::ClippedSubstr(
                      token_provider_->input(),
                      invocation_location.start().GetByteOffset(),
                      invocation_location.end().GetByteOffset() -
                          invocation_location.start().GetByteOffset())),
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

    if (!macro_expander_options_.is_strict) {
      // Lenient mode expands this below. Record it before any check that can
      // fail, at the start of the literal, which is where the parser sees it.
      RecordMacroInvocation(original_literal_token.start_offset());
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
        std::move(child_token_provider), catalog_, stack_frame_factory_,
        expansion_state_, call_arguments_, macro_expander_options_,
        parent_location_, location_map_, expanded_tokens, warning_collector_,
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
          absl::StrAppend(&content, extracted_token_content);
        } else {
          // We need to break the literal
          TokenWithLocation current_literal = literal_token;
          current_literal.text = AllocateString(QuoteText(content, quoting),
                                                stack_frame_factory_->arena());
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

  literal_token.text = AllocateString(QuoteText(content, quoting),
                                      stack_frame_factory_->arena());
  return literal_token;
}

absl::Status MacroExpander::ExpandMacrosInternal(
    std::unique_ptr<TokenStream> token_provider, Catalog* /*absl_nullable*/ catalog,
    std::shared_ptr<StackFrame::StackFrameFactory> stack_frame_factory,
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

  auto token_provider_base =
      dynamic_cast<TokenProviderBase*>(token_provider.get());
  GOOGLESQL_RET_CHECK(token_provider_base != nullptr);
  auto expander = absl::WrapUnique(new MacroExpander(
      token_provider_base, catalog, std::move(stack_frame_factory),
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

absl::StatusOr<StackFrame*> MacroExpander::MakeInvocationStackFrame(
    const TokenWithLocation& invocation_token) {
  GOOGLESQL_RET_CHECK(IsMacroInvocation(invocation_token.kind));

  absl::string_view name_suffix =
      invocation_token.kind == Token::MACRO_INVOCATION ? "macro:"
                                                       : "builtin macro:";
  GOOGLESQL_ASSIGN_OR_RETURN(absl::string_view macro_name,
                   GetMacroName(invocation_token));

  return stack_frame_factory_->MakeStackFrame(
      AllocateString(absl::StrCat(name_suffix, macro_name),
                     stack_frame_factory_->arena()),
      StackFrame::FrameType::kMacroInvocation, invocation_token.location,
      token_provider_->input(), token_provider_->offset_in_original_input(),
      macro_expander_options_.diagnostic_options.error_message_options
          .input_original_start_line,
      macro_expander_options_.diagnostic_options.error_message_options
          .input_original_start_column,
      parent_location_);
}

}  // namespace macros
}  // namespace parser
}  // namespace googlesql
