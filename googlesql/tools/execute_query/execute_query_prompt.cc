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

#include "googlesql/tools/execute_query/execute_query_prompt.h"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/status_payload_utils.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/public/parse_tokens.h"
#include "googlesql/tools/execute_query/execute_query.pb.h"
#include "googlesql/tools/execute_query/execute_query_tool.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

using execute_query::ParserErrorContext;

namespace {
bool IsUnclosedTripleQuotedLiteralError(absl::Status status) {
  // Unfortunately there's no better way to recognize triple-quoted strings
  return status.code() == absl::StatusCode::kInvalidArgument &&
         absl::StrContains(
             status.message(),
             "Syntax error: Unclosed triple-quoted string literal");
}

// Pluck next statement terminated by a semiclon from input string. nullopt is
// returned if more input is necessary, e.g. because a statement is incomplete.
absl::StatusOr<std::optional<absl::string_view>> NextStatement(
    absl::string_view input, const LanguageOptions& language_options) {
  ABSL_DCHECK(!input.empty());

  ParseTokenOptions options;
  options.stop_at_end_of_statement = true;
  options.language_options = language_options;

  ParseResumeLocation resume_loc{ParseResumeLocation::FromStringView(input)};

  std::vector<ParseToken> tokens;

  if (const absl::Status status = GetParseTokens(options, &resume_loc, &tokens);
      !status.ok()) {
    if (IsUnclosedTripleQuotedLiteralError(status)) {
      return std::nullopt;
    }

    return status;
  }

  for (auto it = tokens.cbegin(); it != tokens.cend(); ++it) {
    if (it->GetKeyword() == ";") {
      // Found a cleanly terminated statement
      const size_t stmt_end = it->GetLocationRange().end().GetByteOffset();

      return resume_loc.input().substr(0, stmt_end);
    }
  }

  return std::nullopt;
}
}  // namespace

std::string ExecuteQueryCompletionRequest::DebugString() const {
  return absl::Substitute(
      "body:\"$0\" cursor_pos:$1 word_start:$2 word_end:$3 word:\"$4\"",
      absl::CEscape(body), cursor_position, word_start, word_end,
      absl::CEscape(word));
}

absl::Status ExecuteQueryCompletionRequest::Validate() const {
  // There are too many offsets to leave them unchecked.
  GOOGLESQL_RET_CHECK_LE(cursor_position, body.length());
  GOOGLESQL_RET_CHECK_LE(word_start, body.length());
  GOOGLESQL_RET_CHECK_LE(word_end, body.length());
  GOOGLESQL_RET_CHECK_LE(word_start, word_end);
  GOOGLESQL_RET_CHECK_LE(word.size(), word_end - word_start);

  return absl::OkStatus();
}

ExecuteQueryStatementPrompt::ExecuteQueryStatementPrompt(
    const ExecuteQueryConfig& config,
    std::function<absl::StatusOr<std::optional<std::string>>(bool)>
        read_next_func)
    : config_(config), read_next_func_{read_next_func} {
  ABSL_CHECK(read_next_func_);
}

absl::StatusOr<ExecuteQueryCompletionResult>
ExecuteQueryStatementPrompt::Autocomplete(
    const ExecuteQueryCompletionRequest &req) {
  GOOGLESQL_RET_CHECK_OK(req.Validate());

  if (autocomplete_func_ == nullptr) {
    return ExecuteQueryCompletionResult{};
  }

  // Storage for buffered input and new input before cursor position; the rest
  // uses views into this data.
  const std::string complete_body = absl::StrCat(buf_.Flatten(), req.body);

  // Prepend previously read text
  const ExecuteQueryCompletionRequest inner_req{
      .body = complete_body,
      .cursor_position = buf_.size() + req.cursor_position,
      .word_start = buf_.size() + req.word_start,
      .word_end = buf_.size() + req.word_end,
      .word = req.word,
  };

  GOOGLESQL_ASSIGN_OR_RETURN(ExecuteQueryCompletionResult result,
                   autocomplete_func_(inner_req));

  result.prefix_start -= buf_.size();

  // Append a space for better usability in case the completion is unambiguous.
  if (result.items.size() == 1) {
    result.items[0].append(" ");
  }

  return result;
}

absl::StatusOr<std::optional<std::string>> ExecuteQueryStatementPrompt::Read() {
  while (!(eof_ && buf_.empty() && queue_.empty())) {
    if (!queue_.empty()) {
      const absl::StatusOr<std::optional<std::string>> front{
          std::move(queue_.front())};
      queue_.pop_front();
      return front;
    }

    if (!eof_) {
      ReadInput(continuation_);
    }

    ProcessBuffer();
  }

  ABSL_DCHECK(eof_);

  return std::nullopt;
}

void ExecuteQueryStatementPrompt::ReadInput(bool continuation) {
  ABSL_DCHECK(queue_.empty()) << "Queue must be drained before reading again";
  ABSL_DCHECK(!eof_) << "Can't read after EOF";

  absl::StatusOr<std::optional<std::string>> input{
      read_next_func_(continuation)};

  if (!input.ok()) {
    queue_.emplace_back(std::move(input).status());
    return;
  }

  if (!input->has_value()) {
    eof_ = true;
    return;
  }

  // Skip space at beginning of statements
  if (const absl::string_view input_text{input->value()};
      continuation_ || !absl::StripLeadingAsciiWhitespace(input_text).empty()) {
    // The current implementation always tokenizes from the beginning of the
    // buffer. If that were changed it may be possible to handle very large
    // inputs.
    if ((buf_.size() + input_text.length()) >= max_length_) {
      queue_.emplace_back(absl::ResourceExhaustedError(absl::StrFormat(
          "Reached maximum statement length of %d KiB", max_length_ / 1024)));
      continuation_ = false;
      buf_.Clear();
      return;
    }

    buf_.Append(input_text);
  }
}

void ExecuteQueryStatementPrompt::ProcessBuffer() {
  while (!buf_.empty()) {
    absl::StatusOr<std::optional<absl::string_view>> stmt =
        NextStatement(buf_.Flatten(), config_.analyzer_options().language());

    if (!stmt.ok()) {
      absl::Status status = std::move(stmt).status();

      {
        ParserErrorContext ctx;
        ctx.set_text(absl::StripAsciiWhitespace(buf_.Flatten()));
        internal::AttachPayload(&status, ctx);
      }

      queue_.emplace_back(status);
      buf_.Clear();
      continuation_ = false;

      break;
    }

    if (!stmt->has_value()) {
      // Incomplete statement in buffer
      std::string stripped{absl::StripAsciiWhitespace(buf_.Flatten())};

      if (eof_) {
        continuation_ = false;

        // Use whatever is left after all full statements have been used
        if (!stripped.empty()) {
          queue_.emplace_back(stripped);
        }

        buf_.Clear();
      } else {
        continuation_ = !stripped.empty();
      }

      break;
    }

    // Got a full statement
    continuation_ = false;

    absl::string_view stripped{
        absl::StripLeadingAsciiWhitespace(stmt->value())};
    ABSL_DCHECK(!stripped.empty());

    queue_.emplace_back(std::move(stripped));

    // Modify the buffer last as it serves as the string storage
    buf_.RemovePrefix(stmt->value().length());
  }
}

}  // namespace googlesql
