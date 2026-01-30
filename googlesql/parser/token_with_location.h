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

#ifndef GOOGLESQL_PARSER_TOKEN_WITH_LOCATION_H_
#define GOOGLESQL_PARSER_TOKEN_WITH_LOCATION_H_

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "googlesql/parser/tm_token.h"
#include "googlesql/public/error_location.pb.h"
#include "googlesql/public/parse_location.h"
#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace parser {

// Represents a stack trace for the macro expansions.
// The stack is used to provide more context for errors.
// `name` is the name of the invocation. It can be a macro/argument invocation
// or an argument expansion. `location` is the location of the macro invocation.
//
// NOTE : All the string_view defined in this struct are allocated in the arena
// and will be valid for the lifetime of the arena.
// The arena should outlive the stack frame.

// NOTE: This struct is intentionally an aggregate type (i.e., has no
// user-declared constructor) to allow for easy initialization in tests. In
// production code, all instances of StackFrame should be created via the
// StackFrame::StackFrameFactory to enforce resource limits.
//
// Keep this cheap to copy.
struct StackFrame {
  enum class FrameType {
    kMacroInvocation,  // The macro invocation. For example, "$m" in $m(abc).
    kMacroArg,         // The macro argument. For example, "abc" in $m("abc").
    kArgRef,  // The reference of the macro argument. For example, $1 in
              // DEFINE MACRO m $1, pqr;
  };
  absl::string_view name;
  FrameType frame_type;
  ParseLocationRange location;

  // The input text and offsets in the original input from which this frame is
  // created.
  absl::string_view input_text;
  int offset_in_original_input;   // Byte offset in the original input.
  int input_start_line_offset;    // 1-based line number for the original input.
  int input_start_column_offset;  // 1-based column number for the original
                                  // input.

  StackFrame* /*absl_nullable*/ parent;

  // The invocation frame where this frame is produced from.
  // This is only set for kMacroArg. For example, $m(abc) arg:$1 is kMacroArg,
  // and $m is the invocation frame. Parent frame can be different from the
  // invocation frame.
  // For example,
  //
  // DEFINE MACRO m1 $1,
  // DEFINE MACRO m2 $m1($1);
  // $m2(abc)
  //
  // For above example, expansion for ABC will look like this:
  // ABC         :   Representing token definition
  // |
  // |
  // Arg:$1(m2)  :   Argument is created for first time in m2.
  // |
  // |
  // $1(ArgRef)  :   Above chain is getting used in DEFINE MACRO m2 $m1($1)
  // |
  // |
  // Arg:$1(m2)  :   Argument is created for time in m1.
  // |
  // |
  // $1(ArgRef)  :   Above chain is getting used in DEFINE MACRO m1 $1
  // |
  // |
  // $m1()
  // |
  // |
  // $m2()
  //
  // Here, In above example, For Arg:$1(m2), its parent will be $1(ArgRef)
  // and invocation frame will be $m2().

  StackFrame* /*absl_nullable*/ invocation_frame;

  // Returns the location range without the offset.
  ParseLocationRange LocationRangeWithoutStartOffset() const {
    return ParseLocationRange(
        ParseLocationPoint::FromByteOffset(
            location.start().filename(),
            location.start().GetByteOffset() - offset_in_original_input),
        ParseLocationPoint::FromByteOffset(
            location.end().filename(),
            location.end().GetByteOffset() - offset_in_original_input));
  }

  // Returns the invocation string for this stack from the original input.
  absl::string_view GetInvocationString() const {
    return this->LocationRangeWithoutStartOffset().GetTextFrom(input_text);
  }

  // A factory for creating stack frames.
  // All stack frames allocation will be done through this factory.
  // This will help in controlling the maximum number of stack frames that can
  // be created.
  class StackFrameFactory {
   public:
    StackFrameFactory() = default;
    explicit StackFrameFactory(
        int64_t max_number_of_stack_frames,
        std::vector<std::unique_ptr<StackFrame>>& stack_frames)
        : max_number_of_stack_frames_(max_number_of_stack_frames),
          stack_frames_(stack_frames) {}

    // StackFrameFactory is neither copyable nor movable.
    StackFrameFactory(const StackFrameFactory&) = delete;
    StackFrameFactory& operator=(const StackFrameFactory&) = delete;

    absl::StatusOr<StackFrame* /*absl_nonnull*/> AllocateStackFrame() {
      if (stack_frames_.size() >= max_number_of_stack_frames_) {
        return absl::InternalError(absl::StrCat(
            "Too many stack frames are created, probably due to an ",
            "exponential macro definition. Current size of stack frames in ",
            "bytes: ", stack_frames_.size() * sizeof(StackFrame)));
      }
      return stack_frames_.emplace_back(std::make_unique<StackFrame>()).get();
    }

    // Creates and initializes a new `StackFrame` for given parameters on the
    // stack_frames_ vector. Returns error if the maximum number of stack frames
    // has been reached.
    absl::StatusOr<StackFrame* /*absl_nonnull*/> MakeStackFrame(
        absl::string_view frame_name, StackFrame::FrameType frame_type,
        ParseLocationRange location, absl::string_view input_text,
        int offset_in_original_input, int input_start_line_offset,
        int input_start_column_offset,
        StackFrame* /*absl_nullable*/ parent_location,
        StackFrame* /*absl_nullable*/ invocation_frame = nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(StackFrame * stack_frame, AllocateStackFrame());
      stack_frame->frame_type = frame_type;
      stack_frame->name = frame_name;
      stack_frame->location = std::move(location);
      stack_frame->input_text = input_text;
      stack_frame->offset_in_original_input = offset_in_original_input;
      stack_frame->input_start_line_offset = input_start_line_offset;
      stack_frame->input_start_column_offset = input_start_column_offset;
      stack_frame->parent = parent_location;
      stack_frame->invocation_frame = invocation_frame;
      return stack_frame;
    }

   private:
    // The maximum number of stack frames allowed.
    // This is a safeguard for OOMs, which can occur if exponential stack
    // frames are created.
    int64_t max_number_of_stack_frames_ =
        (1024 * 1024 * 1024) / sizeof(StackFrame);

    // Used to maintain the ownership of the allocated stack frames.
    // All newly allocated stack frames owned by this vector. This will help to
    // avoid memory leaks.
    std::vector<std::unique_ptr<StackFrame>> owned_stack_frames_;
    std::vector<std::unique_ptr<StackFrame>>& stack_frames_ =
        owned_stack_frames_;
  };
};

// Represents one token in the unexpanded input stream. 'Kind' is the lexical
// token kind, e.g. STRING_LITERAL, IDENTIFIER, KW_SELECT.
// Offsets and 'text' refer to the token *without* any whitespaces.
// Preceding whitespaces are stored in their own string_view; they
// are used when serializing the end result to preserve the original user
// spacing.
struct TokenWithLocation {
  Token kind;
  ParseLocationRange location;
  absl::string_view text;
  absl::string_view preceding_whitespaces;

  // TEMPORARY FIELD: Please do not use as it will be removed shortly.
  //
  // Location offsets must be valid for the source they refer to.
  // Currently, the parser & analyzer only have the unexpanded source, so we
  // use the unexpanded offset.
  // In the future, the resolver should show the expanded location and where
  // it was expanded from. The expander would have the full location map and
  // the sources of macro definitions as well, so we would not need this
  // adjustment nor the `topmost_invocation_location` at all, since the
  // expander will be able to provide the stack. All layers, however, will need
  // to ask for that mapping.
  ParseLocationRange topmost_invocation_location;

  int start_offset() const { return location.start().GetByteOffset(); }
  int end_offset() const { return location.end().GetByteOffset(); }

  // Returns whether `other` and `this` are adjacent tokens (no spaces in
  // between) and `this` precedes `other`.
  //
  // If the location of either token is invalid, i.e. with one end smaller than
  // 0, the function call returns false.
  bool AdjacentlyPrecedes(const TokenWithLocation& other) const;

  // The stack frame from which this token was expanded.
  // This field will be null for tokens that were not expanded from a macro or
  // macro args.
  // StackFrame has its own `location`, which is the location of the macro
  // invocation. which is different from the `location` of the token.
  StackFrame* stack_frame = nullptr;
};

}  // namespace parser
}  // namespace googlesql

#endif  // GOOGLESQL_PARSER_TOKEN_WITH_LOCATION_H_
