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

#ifndef GOOGLESQL_PUBLIC_ERROR_HELPERS_H_
#define GOOGLESQL_PUBLIC_ERROR_HELPERS_H_

#include <string>

#include "googlesql/public/options.pb.h"
#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace googlesql {

// NOTE: Inside ErrorLocation, <line> is computed assuming lines can be
// split with \n, \r or \r\n, and <column> is computed assuming tabs expand to
// eight characters.
class ErrorLocation;
class ErrorSource;

// Format an ErrorLocation as "[file:]line:column".
// ErrorSource information is ignored (if present).
std::string FormatErrorLocation(const ErrorLocation& location);

// Fully format an ErrorLocation based on <mode>.  If <mode> is
// ERROR_MESSAGE_MODE_MULTI_LINE_WITH_CARET then <input_text> is used as the
// source text.  If ErrorLocation contains ErrorSource, then the ErrorSource
// information is appended based on <mode> (with ErrorSources separated by
// a newline for WITH_CARET mode, or '; ' for ONE_LINE mode).
std::string FormatErrorLocation(const ErrorLocation& location,
                                absl::string_view input_text,
                                ErrorMessageMode mode);

// Format an ErrorSource payload.
// If the <mode> is ERROR_MESSAGE_WITH_PAYLOAD then returns an empty string.
// Otherwise formats the ErrorSource based on <mode>.
std::string FormatErrorSource(const ErrorSource& error_source,
                              ErrorMessageMode mode);

// Format an error message.  If this looks like a googlesql error, format it as
// "message [at <ErrorLocation>] [<ErrorSource>, ...]" (effectively
// ERROR_MESSAGE_MODE_ONE_LINE), omitting the ErrorLocation and ErrorSource
// information if they do not exist.
// Otherwise, just return status.ToString().
std::string FormatError(const absl::Status& status);

// Return true if <status> has a googlesql::ErrorLocation payload.
bool HasErrorLocation(const absl::Status& status);

// Copy a googlesql::ErrorLocation payload out of <status> into <*location>.
// Return true if <status> had an ErrorLocation.
bool GetErrorLocation(const absl::Status& status, ErrorLocation* location);

// Mutate <*status> by removing any attached googlesql::ErrorLocation payload.
void ClearErrorLocation(absl::Status* status);

// Returns a two-line string pointing at the error location in <input>.
// The first line will be substring of up to <max_width> characters from the
// line of <input> with the error, using "..." to indicate truncation.
// The second line will have spaces and a caret ("^") pointing at the error.
// <location> must point to a character inside the <input>.
// Tabs are expanded using spaces, assuming a tab width of eight.
std::string GetErrorStringWithCaret(absl::string_view input,
                                    const ErrorLocation& location,
                                    int max_width_in = 80);

// Encapsulates options controlling the error message, including location and
// payload.
struct ErrorMessageOptions {
  ErrorMessageMode mode = ERROR_MESSAGE_WITH_PAYLOAD;
  bool attach_error_location_payload = false;
  ErrorMessageStability stability = ERROR_MESSAGE_STABILITY_UNSPECIFIED;

  // The original line & column of the input text.
  // Used when the input is from a larger, unavailable source.
  // Note that these are the actual 1-based line and column, *NOT* offsets.
  int input_original_start_line = 1;
  int input_original_start_column = 1;
};

// Possibly updates the <status> error string based on <input_text> and <mode>.
//
// For OK status or <mode> ERROR_MESSAGE_WITH_PAYLOAD, simply returns <status>
// (the call is a no-op).
//
// Otherwise, if <status> has an ErrorLocation and/or ErrorSource payload,
// removes those payloads and updates the error message to include location
// and source info. 'keep_error_location_payload' leaves the payload after it
// updates the Status, instead of clearing it.
//
// For the ErrorLocation payload, the updated message will include
// "[at <line>:<column>]", and if <mode> is ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
// will include additional lines with a substring of <input_text> and a
// location pointer, as in GetErrorStringWithCaret().
//
// An ErrorLocation can have a list of ErrorSource payloads, and the error
// message for an ErrorSource is also generated based on ErrorMessageOptions.
// If there are multiple ErrorSource payloads, then the messages are in order
// based on their dependencies - if an error has a source error then the source
// error message will appear immediately after the original error message.
absl::Status MaybeUpdateErrorFromPayload(ErrorMessageOptions options,
                                         absl::string_view input_text,
                                         const absl::Status& status);

ABSL_DEPRECATED("Please use the overload using ErrorMessageOptions")
absl::Status MaybeUpdateErrorFromPayload(ErrorMessageMode mode,
                                         bool keep_error_location_payload,
                                         absl::string_view input_text,
                                         const absl::Status& status);

// If <status> contains an (external) ErrorLocation payload, and if that
// ErrorLocation does not have a filename, then updates the ErrorLocation
// payload to set the <filename> and returns an updated Status with the
// updated ErrorLocation.  Otherwise, just returns <status>.
absl::Status UpdateErrorLocationPayloadWithFilenameIfNotPresent(
    const absl::Status& status, absl::string_view filename);

// Replaces internal error payloads with external payloads.
// `sql` should be the sql that was parsed; it is used to convert the error
// location from line/column to byte offset or vice versa. This function is
// called on all errors before returning them to the client.
//
// `input_start_line_offset` and `input_start_column_offset` are used when
// `sql` isn't the full source, but rather starts at those offsets. The
// offsets are passed separately because location is relative to `input`, and
// for the caret substring to work correctly, `input_start_line_offset` and
// `input_start_column_offset` are added only after the caret string has been
// extracted.
// `input_start_byte_offset` is the number of bytes in the original input before
// the input `sql`. As the given internal location would have offsets relative
// to that larger original input, the `input_start_byte_offset` needs to be
// subtracted in order to calculate the offset in the input `sql` (e.g. for
// the caret substring).
absl::Status ConvertInternalErrorPayloadsToExternal(
    absl::Status status, absl::string_view sql, int input_start_line_offset,
    int input_start_column_offset, int input_start_byte_offset);

// Replaces internal error payloads with external payloads, assuming that the
// input has no start offsets. `sql` should be the sql that was parsed; it is
// used to convert the error location from line/column to byte offset or vice
// versa. This function is called on all errors before returning them to the
// client.
absl::Status ConvertInternalErrorPayloadsToExternal(absl::Status status,
                                                    absl::string_view sql);

// If <status> is OK or if it does not have a InternalErrorLocation payload,
// returns <status>. Otherwise, replaces the InternalErrorLocation payload by an
// ErrorLocation payload. 'query' should be the query that was parsed; it is
// used to convert the error location from line/column to byte offset or vice
// versa. This function is called on all errors before returning them to
// the client.
//
// An InternalErrorLocation contained in 'status' must be valid for
// 'query'. If it is not, then this function returns an internal error.
//
// `input_start_line_offset` and `input_start_column_offset` are used when
// `query` isn't the full source, but rather starts at those offsets. The
// offsets are passed separately because location is relative to `input`, and
// for the caret substring to work correctly, `input_start_line_offset` and
// `input_start_column_offset` are added only after the caret string has been
// extracted.
// `input_start_byte_offset` is the number of bytes in the original input before
// the input `query`. As the given internal location would have offsets relative
// to that larger original input, the `input_start_byte_offset` needs to be
// subtracted in order to calculate the offset in the input `query` (e.g. for
// the caret substring).
ABSL_DEPRECATED("Inline me!")
inline absl::Status ConvertInternalErrorLocationToExternal(
    absl::Status status, absl::string_view query, int input_start_line_offset,
    int input_start_column_offset, int input_start_byte_offset) {
  return ConvertInternalErrorPayloadsToExternal(
      status, query, input_start_line_offset, input_start_column_offset,
      input_start_byte_offset);
}

// If <status> is OK or if it does not have a InternalErrorLocation payload,
// returns <status>. Otherwise, replaces the InternalErrorLocation payload by an
// ErrorLocation payload. 'query' should be the query that was parsed; it is
// used to convert the error location from line/column to byte offset or vice
// versa. This function is called on all errors before returning them to
// the client.
//
// An InternalErrorLocation contained in 'status' must be valid for
// 'query'. If it is not, then this function returns an internal error.
ABSL_DEPRECATED("Inline me!")
inline absl::Status ConvertInternalErrorLocationToExternal(
    absl::Status status, absl::string_view query) {
  return ConvertInternalErrorPayloadsToExternal(status, query);
}

// The type url for the ErrorMessageMode payload. Used to indicate what mode
// was applied to a given error message (e.g. caret on same line or multiline).
inline static constexpr absl::string_view kErrorMessageModeUrl =
    "type.googleapis.com/googlesql.ErrorMessageModeForPayload";

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_ERROR_HELPERS_H_
