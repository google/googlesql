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

#include "googlesql/scripting/error_helpers.h"

#include <functional>
#include <utility>

#include "googlesql/common/status_payload_utils.h"
#include "absl/status/statusor.h"
#include "googlesql/base/status_builder.h"

namespace googlesql {

// Converts an ErrorLocation relative to a particular statement/expression,
// <segment>, into an ErrorLocation relative to an entire script.
//
// If the error location contains one or more sources, the source locations are
// converted as well.  As we do not support updating
// ErrorSource::error_message_caret_string, all errors passed here must be
// obtained using ERROR_MESSAGE_WITH_PAYLOAD.
//
// Returns an error if <error_location_in> contains line or column numbers
// outside the bounds of the input segment.
absl::StatusOr<ErrorLocation> ConvertLocalErrorToScriptError(
    const ScriptSegment& segment, const ErrorLocation& error_location_in) {
  // Convert the error location's line and column back to a byte offset
  // relative to the statement/expression which generated the error.
  ParseLocationTranslator stmt_or_expr_translator(segment.GetSegmentText());
  ParseLocationTranslator script_translator(segment.script());
  int byte_offset_relative_to_stmt_or_expr;

  GOOGLESQL_ASSIGN_OR_RETURN(byte_offset_relative_to_stmt_or_expr,
                   stmt_or_expr_translator.GetByteOffsetFromLineAndColumn(
                       error_location_in.line(), error_location_in.column()));

  // Now, convert the byte offset into a line and column relative to the entire
  // input.
  ParseLocationPoint error_location_relative_to_script =
      ParseLocationPoint::FromByteOffset(
          byte_offset_relative_to_stmt_or_expr +
          segment.range().start().GetByteOffset());
  ErrorLocation error_location_out;
  if (error_location_in.has_filename()) {
    error_location_out.set_filename(error_location_in.filename());
  }
  std::pair<int, int> converted_line_and_column;
  GOOGLESQL_ASSIGN_OR_RETURN(converted_line_and_column,
                   script_translator.GetLineAndColumnAfterTabExpansion(
                       error_location_relative_to_script));
  error_location_out.set_line(converted_line_and_column.first);
  error_location_out.set_column(converted_line_and_column.second);

  for (const ErrorSource& source : error_location_in.error_source()) {
    ErrorSource error_source_out(source);
    GOOGLESQL_ASSIGN_OR_RETURN(
        *error_source_out.mutable_error_location(),
        ConvertLocalErrorToScriptError(segment, source.error_location()));
    *error_location_out.add_error_source() = error_source_out;
  }
  return error_location_out;
}

std::function<googlesql_base::StatusBuilder(googlesql_base::StatusBuilder)>
ConvertLocalErrorToScriptError(const ScriptSegment& segment) {
  return [=](googlesql_base::StatusBuilder b) -> googlesql_base::StatusBuilder {
    if (!internal::HasPayloadWithType<ErrorLocation>(b)) {
      return b;
    }
    ErrorLocation old_error_loc = internal::GetPayload<ErrorLocation>(b);
    absl::StatusOr<ErrorLocation> status_or_new_error_loc =
        ConvertLocalErrorToScriptError(segment, old_error_loc);
    GOOGLESQL_RET_CHECK_OK(status_or_new_error_loc.status())
        << "Unable to adjust error location: "
        << status_or_new_error_loc.status();
    b.AttachPayload(status_or_new_error_loc.value());
    return b;
  };
}

}  // namespace googlesql
