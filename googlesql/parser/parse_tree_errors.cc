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

#include "googlesql/parser/parse_tree_errors.h"

#include <string>
#include <string_view>

#include "googlesql/base/logging.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/status_payload_utils.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parse_tree_generated.h"
#include "googlesql/proto/internal_error_location.pb.h"
#include "googlesql/proto/internal_fix_suggestion.pb.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status.h"
#include "googlesql/base/status_builder.h"

namespace googlesql {

// Sometimes we make an error pointing at an expression like 1+2 and since
// the Plus is the root AST node, the error points at the '+' character.
// This traverses left-most children of a node and updates <location> with
// the earliest position it finds.
static void FindMinimalErrorLocation(const ASTNode* parent_node,
                                     bool use_end_location,
                                     ParseLocationPoint* error_location) {
  // TODO This still won't find the real start character for
  // a parenthesized expression like (1+2).  It will point at the '1'.
  const ASTNode* node = parent_node;
  ABSL_DCHECK(node != nullptr);
  while (node->num_children() > 0) {
    node = node->child(0);
    ABSL_DCHECK(node != nullptr);
    if (node->location().start() < *error_location) {
      if (use_end_location) {
        *error_location = node->location().end();
      } else {
        *error_location = node->location().start();
      }
    }
  }
}

ParseLocationPoint GetErrorLocationPoint(const ASTNode* ast_node,
                                         bool include_leftmost_child,
                                         bool use_end_location) {
  // For extra safety, try not to crash while constructing errors.
  ABSL_DCHECK(ast_node != nullptr);
  if (ast_node != nullptr) {
    ParseLocationPoint error_location;
    if (use_end_location) {
      error_location = ast_node->location().end();
    } else {
      error_location = ast_node->location().start();
    }
    if (include_leftmost_child) {
      FindMinimalErrorLocation(ast_node, use_end_location, &error_location);
    } else if (!use_end_location) {
      const ASTFunctionCall* function_call =
          ast_node->GetAsOrNull<ASTFunctionCall>();
      if (const ASTAnalyticFunctionCall* analytic_function_call =
              ast_node->GetAsOrNull<ASTAnalyticFunctionCall>();
          analytic_function_call != nullptr) {
        function_call = analytic_function_call->function();  // May be NULL.
      }
      if (function_call != nullptr && function_call->is_chained_call()) {
        error_location = function_call->function()->location().start();
      }
      // TODO: If we had a location for the operator in
      // ASTBinaryExpression, we could use it here so the error would
      // point at the operator when the operator call is bad.
    }
    return error_location;
  }
  return {};
}

absl::Status StatusWithInternalErrorLocation(const absl::Status& status,
                                             const ASTNode* ast_node,
                                             bool include_leftmost_child) {
  if (status.ok()) return status;

  return StatusWithInternalErrorLocation(
      status, GetErrorLocationPoint(ast_node, include_leftmost_child));
}

absl::Status MakeStatusWithErrorLocation(absl::StatusCode code,
                                         absl::string_view message,
                                         absl::string_view filename,
                                         absl::string_view query,
                                         const ASTNode* ast_node,
                                         bool include_leftmost_child) {
  const absl::Status status =
      googlesql_base::StatusBuilder(code).AttachPayload(
          MakeInternalErrorLocation(ast_node, filename, include_leftmost_child))
      << message;
  return ConvertInternalErrorPayloadsToExternal(status, query);
}

InternalErrorLocation MakeInternalErrorLocation(
    const ASTNode* ast_node, absl::string_view filename,
    bool include_leftmost_child) {
  InternalErrorLocation internal_error_location =
      GetErrorLocationPoint(
          ast_node, include_leftmost_child).ToInternalErrorLocation();
  if (!filename.empty()) {
    internal_error_location.set_filename(filename);
  }
  return internal_error_location;
}

absl::Status WrapNestedErrorStatus(const ASTNode* ast_location,
                                   absl::string_view error_message,
                                   const absl::Status& input_status,
                                   ErrorMessageMode error_source_mode) {
  GOOGLESQL_RET_CHECK(!input_status.ok());
  if (ShouldPropagateError(input_status)) {
    return input_status;
  }
  googlesql_base::StatusBuilder error_status_builder = MakeSqlError();
  absl::Status error_status =
      error_status_builder.AttachPayload(
          SetErrorSourcesFromStatus(MakeInternalErrorLocation(ast_location),
                                    input_status, error_source_mode))
      << error_message;
  if (internal::HasPayloadWithType<InternalErrorFixSuggestions>(input_status)) {
    internal::AttachPayload(
        &error_status,
        internal::GetPayload<InternalErrorFixSuggestions>(input_status));
  }

  return error_status;
}

::absl::Status MakeSqlErrorIfPresent(const ASTNode* ast_node) {
  if (ast_node != nullptr) {
    return MakeSqlErrorAt(ast_node);
  } else {
    return absl::OkStatus();
  }
}

absl::Status& AddFixSuggestionToStatus(absl::Status& status,
                                       absl::string_view title,
                                       const ParseLocationPoint& start_location,
                                       const ParseLocationPoint& end_location,
                                       absl::string_view new_text) {
  InternalFix fix;
  fix.set_title(title);
  InternalTextEdit* edit = fix.mutable_edits()->add_text_edits();
  edit->mutable_range()->mutable_start()->set_byte_offset(
      start_location.GetByteOffset());
  edit->mutable_range()->mutable_start()->set_filename(
      start_location.filename());
  edit->mutable_range()->set_length(end_location.GetByteOffset() -
                                    start_location.GetByteOffset());
  edit->set_new_text(new_text);
  if (internal::HasPayloadWithType<InternalErrorFixSuggestions>(status)) {
    auto fix_copy = internal::GetPayload<InternalErrorFixSuggestions>(status);
    *fix_copy.add_fix_suggestions() = fix;
    internal::ErasePayloadTyped<InternalErrorFixSuggestions>(&status);
    internal::AttachPayload(&status, fix_copy);
    return status;
  }
  InternalErrorFixSuggestions fixes;
  *fixes.add_fix_suggestions() = fix;
  internal::AttachPayload(&status, fixes);
  return status;
}

}  // namespace googlesql
