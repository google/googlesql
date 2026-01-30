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

#include "googlesql/parser/macros/diagnostic.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "googlesql/common/errors.h"
#include "googlesql/parser/token_with_location.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/parse_location.h"
#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace parser {
namespace macros {

// Creates an `ErrorSource` proto from a given `StackFrame`. This involves
// setting an appropriate error message based on the frame type, translating
// the location within the stack frame's input text to line and column numbers,
// and generating a caret string to highlight the error location.
absl::StatusOr<ErrorSource> CreateErrorSource(
    const StackFrame* /*absl_nonnull*/ stack_frame) {
  ErrorSource error_source;
  if (stack_frame->frame_type == StackFrame::FrameType::kArgRef) {
    error_source.set_error_message(
        absl::StrCat(stack_frame->name, " is getting used at"));
  } else if (stack_frame->frame_type == StackFrame::FrameType::kMacroArg) {
    error_source.set_error_message(
        absl::StrCat("Which is ", stack_frame->name, ", and got created at"));
  } else {
    error_source.set_error_message(
        absl::StrCat("Expanded from ", stack_frame->name));
  }

  ParseLocationTranslator location_translator(stack_frame->input_text);
  std::pair<int, int> line_and_column;

  // Relative to `input()`
  ParseLocationPoint start_relative_to_input = stack_frame->location.start();
  start_relative_to_input.SetByteOffset(
      start_relative_to_input.GetByteOffset() -
      stack_frame->offset_in_original_input);

  GOOGLESQL_ASSIGN_OR_RETURN(line_and_column,
                   location_translator.GetLineAndColumnAfterTabExpansion(
                       start_relative_to_input),
                   _ << "Location " << stack_frame->location.start().GetString()
                     << " not found in:\n"
                     << stack_frame->input_text);

  ErrorLocation* err_loc = error_source.mutable_error_location();
  if (!stack_frame->location.start().filename().empty()) {
    err_loc->set_filename(stack_frame->location.start().filename());
  }
  err_loc->set_line(line_and_column.first);
  err_loc->set_column(line_and_column.second);

  // Relative to `input()`
  err_loc->set_input_start_line_offset(stack_frame->input_start_line_offset -
                                       1);
  err_loc->set_input_start_column_offset(
      stack_frame->input_start_column_offset - 1);

  error_source.set_error_message_caret_string(
      GetErrorStringWithCaret(stack_frame->input_text, *err_loc));
  return error_source;
}

absl::Status MakeSqlErrorWithStackFrame(
    const ParseLocationPoint& location, absl::string_view message,
    absl::string_view input_text, const StackFrame* /*absl_nullable*/ stack_frame,
    int offset_in_original_input,
    const parser::macros::DiagnosticOptions& diagnostic_options) {
  googlesql_base::StatusBuilder status_builder = MakeSqlError() << message;
  InternalErrorLocation internal_location = location.ToInternalErrorLocation();

  const StackFrame* next_ancestor = stack_frame;
  std::vector<ErrorSource> error_sources;
  while (next_ancestor != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(ErrorSource error_source,
                     CreateErrorSource(next_ancestor));
    error_sources.push_back(std::move(error_source));
    next_ancestor = next_ancestor->parent;
  }
  // ErrorSources are supposed to be supplied in the reverse order of display.
  // See the ErrorSource proto definition.
  std::reverse(error_sources.begin(), error_sources.end());
  for (auto& error_source : error_sources) {
    *internal_location.add_error_source() = std::move(error_source);
  }

  status_builder.AttachPayload(std::move(internal_location));
  const ErrorMessageOptions& error_options =
      diagnostic_options.error_message_options;
  absl::Status status = ConvertInternalErrorPayloadsToExternal(
      status_builder, input_text, error_options.input_original_start_line - 1,
      error_options.input_original_start_column - 1, offset_in_original_input);
  return MaybeUpdateErrorFromPayload(error_options, input_text, status);
}

}  // namespace macros
}  // namespace parser
}  // namespace googlesql
