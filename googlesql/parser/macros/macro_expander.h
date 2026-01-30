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

#ifndef GOOGLESQL_PARSER_MACROS_MACRO_EXPANDER_H_
#define GOOGLESQL_PARSER_MACROS_MACRO_EXPANDER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/arena.h"
#include "googlesql/parser/macros/diagnostic.h"
#include "googlesql/parser/macros/macro_catalog.h"
#include "googlesql/parser/macros/token_provider_base.h"
#include "googlesql/parser/token_with_location.h"
#include "googlesql/public/error_location.pb.h"
#include "googlesql/public/parse_location.h"
#include "absl/base/nullability.h"
#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace parser {
namespace macros {

// Interface for the macro expander
class MacroExpanderBase {
 public:
  virtual ~MacroExpanderBase() = default;

  virtual absl::StatusOr<TokenWithLocation> GetNextToken() = 0;
  virtual int num_unexpanded_tokens_consumed() const = 0;
};

// Options to configure the macro expander.
struct MacroExpanderOptions {
  // If true, the expander will return errors instead of warnings.
  bool is_strict = false;
  // Options for diagnostics, such as warnings and error messages.
  DiagnosticOptions diagnostic_options = {};

  // The maximum number of macro invocations per macro expansion call.
  // This is a safeguard against exponential macro expansions,
  int64_t max_macro_invocations = 10000;

  // The maximum number of allowed stack frames.
  // This is a safeguard for OOMs, which can occur if exponential stack frames
  // are created.
  // The default value is set to 1GB.
  int64_t max_number_of_stack_frames =
      (1024 * 1024 * 1024) / sizeof(StackFrame);
};

// A struct that holds the expansion details for a macro.
// TODO: fix the names, e.g. full_match is actually "invocation"
struct Expansion {
  // The macro that is being expanded.
  std::string macro_name;
  // The string that is fully matched by the macro including parameters.
  std::string full_match;
  // The new string that is going to replace full_match in the expanded
  // string.
  std::string expansion;
};

// Encapsulates outputs for the non-streaming API
struct ExpansionOutput {
  std::vector<TokenWithLocation> expanded_tokens;
  std::vector<absl::Status> warnings;
  std::unique_ptr<googlesql_base::UnsafeArena> arena;
  absl::btree_map<size_t, Expansion> location_map;
  // This is used to maintain the ownership of the allocated stack frames.
  // All newly allocated stack frames owned by this vector thereby avoiding
  // memory leaks.
  // std::unique_ptr is needed to avoid copying of this vector.
  std::vector<std::unique_ptr<StackFrame>> stack_frames;
};

// GoogleSQL's implementation of the macro expander.
class MacroExpander final : public MacroExpanderBase {
 public:
  MacroExpander(std::unique_ptr<TokenProviderBase> token_provider,
                const MacroCatalog& macro_catalog, googlesql_base::UnsafeArena* arena,
                StackFrame::StackFrameFactory& stack_frame_factory,
                MacroExpanderOptions macro_expander_options,
                StackFrame* /*absl_nullable*/ parent_location);

  MacroExpander(const MacroExpander&) = delete;
  MacroExpander& operator=(const MacroExpander&) = delete;

  absl::StatusOr<TokenWithLocation> GetNextToken() override;

  int num_unexpanded_tokens_consumed() const override;

  // Convenient non-streaming API to return all expanded tokens.
  static absl::StatusOr<ExpansionOutput> ExpandMacros(
      std::unique_ptr<TokenProviderBase> token_provider,
      const MacroCatalog& macro_catalog, MacroExpanderOptions options = {});

 private:
  // Collects warnings from the current expansion across all levels, hiding the
  // logic to cap the number of warnings.
  class WarningCollector {
   public:
    explicit WarningCollector(int max_warning_count)
        : max_warning_count_(max_warning_count) {}

    // Adds the given status as a warning if the max warning count has not been
    // reached. If the cap is hit, adds a sentinel warning indicating that
    // further warnings were truncated.
    absl::Status AddWarning(absl::Status status);

    // Releases all warnings collected so far and resets the list to empty.
    std::vector<absl::Status> ReleaseWarnings();

   private:
    const int max_warning_count_;
    std::vector<absl::Status> warnings_;
  };

  // Tracks the global state of macro expansion to detect recursion.
  class ExpansionState {
   public:
    ExpansionState() = default;
    // ExpansionState is neither copyable nor movable.
    ExpansionState(const ExpansionState&) = delete;
    ExpansionState& operator=(const ExpansionState&) = delete;

    bool MarkAsVisited(absl::string_view macro_invocation_name) {
      auto [it, inserted] = visited_macros_.insert(macro_invocation_name);
      return inserted;
    }

    void UnmarkAsVisited(absl::string_view macro_invocation_name) {
      visited_macros_.erase(macro_invocation_name);
    }

    void IncrementInvocationCount() { ++total_number_of_macro_invocations_; }

    int64_t GetTotalNumberOfMacroInvocations() const {
      return total_number_of_macro_invocations_;
    }

   private:
    // Track visited macros in current expansion chain. If a new macro is pushed
    // which has already been visited, then it indicates a cycle.
    absl::flat_hash_set<absl::string_view> visited_macros_;

    // Track the total number of macro invocations. This also includes argument
    // expansions. This is a total number of count for `ExpandMacrosInternal`.
    int64_t total_number_of_macro_invocations_ = 0;
  };

  MacroExpander(
      std::unique_ptr<TokenProviderBase> token_provider,
      const MacroCatalog& macro_catalog, googlesql_base::UnsafeArena* arena,
      StackFrame::StackFrameFactory& stack_frame_factory,
      ExpansionState& expansion_state,
      const std::vector<std::vector<TokenWithLocation>> call_arguments,
      MacroExpanderOptions macro_expander_options,
      WarningCollector* override_warning_collector,
      StackFrame* /*absl_nullable*/ parent_location)
      : token_provider_(std::move(token_provider)),
        macro_catalog_(macro_catalog),
        arena_(arena),
        stack_frame_factory_(stack_frame_factory),
        call_arguments_(std::move(call_arguments)),
        macro_expander_options_(macro_expander_options),
        owned_warning_collector_(
            macro_expander_options.diagnostic_options.max_warning_count),
        warning_collector_(override_warning_collector == nullptr
                               ? owned_warning_collector_
                               : *override_warning_collector),
        parent_location_(parent_location),
        expansion_state_(expansion_state) {}

  // Because this function may be called internally (e.g. when expanding
  // a nested macro), it appends to `out_warnings`, instead of replacing it.
  static absl::Status ExpandMacrosInternal(
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
      bool drop_comments);

  class TokenBuffer {
   public:
    void Push(TokenWithLocation token) { tokens_.push(std::move(token)); }

    bool empty() const { return tokens_.empty(); }

    // Consumes the next token from the buffer.
    // REQUIRES: the buffer must not be empty.
    TokenWithLocation ConsumeToken() {
      ABSL_DCHECK(!tokens_.empty());
      TokenWithLocation token = std::move(tokens_.front());
      tokens_.pop();
      return token;
    }

   private:
    std::queue<TokenWithLocation> tokens_;
  };

  // Loads the next chunk of tokens that might be needed to splice the next
  // token, until we hit EOF or a token that we know will absolutely never
  // contribute to the current token. The candidates are loaded into
  // `splicing_buffer_`.
  // REQUIRES: `splicing_buffer_` must be empty.
  absl::Status LoadPotentiallySplicingTokens();

  // If we have an argument list, read it to be part of the splicing buffer.
  absl::Status LoadArgsIfAny();

  // We have already consumed the opening parenthesis. Keep reading until
  // they are balanced back.
  absl::Status LoadUntilParenthesesBalance();

  // Expands everything in 'splicing_buffer_' and puts the resulting finalized
  // tokens into 'output_token_buffer_'
  absl::Status ExpandPotentiallySplicingTokens();

  // Pushes `incoming_token` to `pending_token`.
  // 1. If `pending_token` already has a token and is not just pending
  //    whitespaces, it is first flushed to the output buffer.
  // 2. Otherwise, any pending whitespaces on `pending_token` are prepended to
  //    the preceding whitespaces of `incoming_token`.
  absl::StatusOr<TokenWithLocation> AdvancePendingToken(
      TokenWithLocation pending_token, TokenWithLocation incoming_token);

  // Parses the invocation arguments (each argument must have balanced
  // parentheses) and expands the arguments.
  absl::Status ParseAndExpandArgs(
      const TokenWithLocation& unexpanded_macro_invocation_token,
      std::vector<std::vector<TokenWithLocation>>& expanded_args,
      bool& has_explicit_unexpanded_arg, int& out_invocation_end_offset,
      StackFrame& macro_invocation_stack_frame);

  // Expands the given macro invocation or argument reference and handles any
  // splicing needed with the tokens around the invocation/argument reference.
  // Returns the updated pending_token to reflect the state needed for deciding
  // unexpanded_macro_token with the next token after the macro item.
  //
  // REQUIRES: For an invocation, the full argument list must have already
  // been loaded into the splicing buffer.
  //
  // The method simply expands the given macro item, and handles any necessary
  // splicing as follows:
  //   1. If the expansion is empty, preserve the space before the invocation.
  //      This is reflected in the pending_token.
  //   2. Otherwise, splice the first token with the pending token if needed.
  //   3. The function returns with the last token set as the pending token.(It
  //      could the first if there is only one, can be already splicing with the
  //       previous pending token)
  absl::StatusOr<TokenWithLocation> ExpandAndMaybeSpliceMacroItem(
      TokenWithLocation unexpanded_macro_token,
      TokenWithLocation pending_token);

  absl::Status ExpandMacroArgumentReference(
      const TokenWithLocation& token,
      std::vector<TokenWithLocation>& expanded_tokens);

  // Expands the macro invocation starting at the given token.
  // REQUIRES: The macro definition must have already been loaded from the
  //           macro catalog.
  absl::Status ExpandMacroInvocation(
      const TokenWithLocation& token, const MacroInfo& macro_info,
      std::vector<TokenWithLocation>& expanded_tokens);

  // Expands a string literal or a quoted identifier.
  absl::StatusOr<TokenWithLocation> ExpandLiteral(
      TokenWithLocation pending_token, TokenWithLocation literal_token);

  // Creates a new token by appending the new text.
  // Location is passed separately because the spliced tokens in can be from
  // different expansions. We need to report at the common level of expansion
  // to get the line & column number translation correct.
  // REQUIRES: neither `pending_token` nor `incoming_token_text` can be empty.
  absl::StatusOr<TokenWithLocation> Splice(
      TokenWithLocation pending_token, const TokenWithLocation& incoming_token,
      const ParseLocationPoint& location);

  // Returns the given status as error if expanding in strict mode, or adds it
  // as a warning otherwise.
  // Note that not all problematic conditions can be relegated to warnings.
  // For example, a macro invocation with unbalanced parens is always an error
  // even in lenient mode.
  absl::Status RaiseErrorOrAddWarning(absl::Status status);

  // Returns a string_view over the concatenation of the 2 input strings.
  // If both inputs are non-empty, the concatenation is stored on `arena_`.
  // Otherwise, the returned string_view points to the non-empty input.
  // If both are empty, can return either.
  absl::string_view MaybeAllocateConcatenation(absl::string_view a,
                                               absl::string_view b);

  // Returns true if this expander is strict, and false if it is lenient.
  bool IsStrict() const;

  // Consumes the next token from the input buffer, raising a warning on invalid
  // tokens when in lenient mode.
  absl::StatusOr<TokenWithLocation> ConsumeInputToken();

  // Creates an INVALID_ARGUMENT status with the given message at the given
  // location, based on this expander's `error_message_options_`.
  absl::Status MakeSqlErrorAt(const ParseLocationPoint& location,
                              absl::string_view message);

  std::unique_ptr<TokenProviderBase> token_provider_;

  // The macro catalog which contains current definitions.
  // Never changes during the expansion of a statement.
  const MacroCatalog& macro_catalog_;

  // Used to allocate strings for spliced tokens. Must stay valid as long as
  // the tokens referring to the spliced strings are still alive.
  // IMPORTANT: The strings in the arena should never be modified, because they
  // store their buffers in the arena as well. AllocateString() returns a
  // string_view to enforce this.
  googlesql_base::UnsafeArena* arena_ = nullptr;

  // Used to create new stack frames. This is a factory class which will help
  // in creating new stack frames and maintaining their ownership.
  // Outside of the factory, no new stack frames should be created.
  StackFrame::StackFrameFactory& stack_frame_factory_;

  // Used when we are expanding potentially splicing tokens, for example:
  //     $prefix(arg1)some_id$suffix1($somearg(a))$suffix2
  // When it is not empty, it means that we need to expand these tokens
  // before we are sure that we are ready to output a token.
  std::queue<TokenWithLocation> splicing_buffer_;

  // Contains finalized tokens. Anything here will never splice with something
  // coming after.
  TokenBuffer output_token_buffer_;

  // If we are in a macro invocation, contains the expanded arguments of the
  // call. This list is never empty, except at the top level, outside of any
  // invocations. In an invocation, $0 is the macro name, and even if no args
  // are passed, the 0th argument is the macro name. Every argument, including
  // the 0th one, end in YYEOF.
  const std::vector<std::vector<TokenWithLocation>> call_arguments_;

  // Controls the behavior of the macro expander.
  const MacroExpanderOptions macro_expander_options_;

  // Holds whitespaces that will prepend whatever comes next. For example, when
  // expanding `   $empty $empty$empty 123`, we do not assume the first $empty
  // will splice with anything, so when it turns out it has an empty expansion,
  // the spaces before need to be held somewhere, and so on with all $empty
  // expansions until we hit the first token (or EOF) that we can emit.
  //
  // Always backed by a string in the arena, except when it is reset, where it
  // takes an empty literal, just like the initialization here.
  absl::string_view pending_whitespaces_ = "";

  // Owned by the top-level expander, which is created by the public ctor.
  // Null for child expanders. It should not be accessed directly.
  // Instead, all expanders should use the borrowed `warning_collector_`
  // pointer.
  WarningCollector owned_warning_collector_;

  // The active warning collector. Not owned, except at the top-level.
  WarningCollector& warning_collector_ = owned_warning_collector_;

  // Holds the highest index seen for a macro argument reference, e.g. $1, $2,
  // etc. Useful when expanding an invocation, to report back the highest index
  // seen so that the invocation can give a warning or error on unused
  // arguments.
  int max_arg_ref_index_ = 0;

  // True only at the beginning, or after a semicolon. Useful when detecting
  // top-level DEFINE MACRO statements.
  // IMPORTANT: that it is relevant only at the top-level (i.e., call_arguments_
  // is empty)
  bool at_statement_start_ = true;

  // This is a mini-parser to detect when we are in the body of a macro
  // definition (), in which case nothing is expanded until we exit, either at
  // EOI or semicolon.
  // IMPORTANT: that it is relevant only at the top-level (i.e., call_arguments_
  // is empty)
  bool inside_macro_definition_ = false;

  // Tracks the current stack of macro expansions up to the parent.
  StackFrame* /*absl_nullable*/ parent_location_ = nullptr;

  // Used only for the non-streaming API.
  absl::btree_map<size_t, Expansion>* location_map_ = nullptr;

  // ExpansionState to detect cycles. When the public constructor for
  // MacroExpander is called, the `expansion_state_` will be a pointer to the
  // `owned_expansion_state_` (empty). When MacroExpander recursively creates a
  // new MacroExpander, the private constructor sets `expansion_state_` to the
  // expansion state from the parent.
  ExpansionState owned_expansion_state_;
  ExpansionState& expansion_state_;
};

}  // namespace macros
}  // namespace parser
}  // namespace googlesql

#endif  // GOOGLESQL_PARSER_MACROS_MACRO_EXPANDER_H_
