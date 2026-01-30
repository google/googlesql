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

#ifndef GOOGLESQL_PARSER_MACROS_DIAGNOSTIC_H_
#define GOOGLESQL_PARSER_MACROS_DIAGNOSTIC_H_

#include "googlesql/parser/token_with_location.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/parse_location.h"
#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace parser {
namespace macros {

// This struct controls the behavior of the macro expander.
// Sadly we do not yet have structured errors with IDs, so we have to control
// warnings with options, to avoid brittle matching from the callers.
struct DiagnosticOptions {
  ErrorMessageOptions error_message_options = {};
  bool warn_on_literal_expansion = true;
  bool warn_on_identifier_splicing = true;
  bool warn_on_macro_invocation_with_no_parens = true;
  int max_warning_count = 5;
};

// Creates an INVALID_ARGUMENT status with the given message at the given
// location, based on given error_message_options.
// `input_text` is the original input statement, which was used to calculate
// line/column numbers for given `location`.
// `stack_frame` is the stack frame that the error is associated with. It can be
// nullptr if the error is coming from a token which is not associated with any
// macro expansion.
absl::Status MakeSqlErrorWithStackFrame(
    const ParseLocationPoint& location, absl::string_view message,
    absl::string_view input_text, const StackFrame* /*absl_nullable*/ stack_frame,
    int offset_in_original_input,
    const parser::macros::DiagnosticOptions& diagnostic_options);

}  // namespace macros
}  // namespace parser
}  // namespace googlesql

#endif  // GOOGLESQL_PARSER_MACROS_DIAGNOSTIC_H_
