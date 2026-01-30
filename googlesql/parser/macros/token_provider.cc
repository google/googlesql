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

#include "googlesql/parser/macros/token_provider.h"

#include <cstddef>
#include <memory>
#include <optional>

#include "googlesql/parser/macros/token_provider_base.h"
#include "googlesql/parser/tm_token.h"
#include "googlesql/parser/token_with_location.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/base/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace parser {
namespace macros {

static absl::string_view GetTextBetween(absl::string_view input, size_t start,
                                        size_t end) {
  ABSL_DCHECK_LE(start, end);
  ABSL_DCHECK_LE(start, input.length());
  size_t len = end - start;
  ABSL_DCHECK_LE(len, input.length());
  return absl::ClippedSubstr(input, start, len);
}

static ParseLocationRange WithOffset(const ParseLocationRange& location,
                                     int offset) {
  return ParseLocationRange(
      ParseLocationPoint::FromByteOffset(
          location.start().filename(),
          location.start().GetByteOffset() + offset),
      ParseLocationPoint::FromByteOffset(
          location.end().filename(), location.end().GetByteOffset() + offset));
}

TokenProvider::TokenProvider(absl::string_view filename,
                             absl::string_view input, int start_offset,
                             std::optional<int> end_offset,
                             int offset_in_original_input)
    : TokenProviderBase(filename, input, start_offset, end_offset,
                        offset_in_original_input),
      tokenizer_(std::make_unique<GoogleSqlTokenizer>(
          filename, input.substr(0, this->end_offset()), start_offset)),
      location_(ParseLocationPoint::FromByteOffset(filename, -1),
                ParseLocationPoint::FromByteOffset(filename, -1)) {}

std::unique_ptr<TokenProviderBase> TokenProvider::CreateNewInstance(
    absl::string_view filename, absl::string_view input, int start_offset,
    std::optional<int> end_offset, int offset_in_original_input) const {
  return std::make_unique<TokenProvider>(filename, input, start_offset,
                                         end_offset, offset_in_original_input);
}

absl::StatusOr<TokenWithLocation> TokenProvider::ConsumeNextTokenImpl() {
  if (!input_token_buffer_.empty()) {
    // Check for any unused tokens first, before we pull any more
    const TokenWithLocation front_token = input_token_buffer_.front();
    input_token_buffer_.pop();
    return front_token;
  }

  return GetToken();
}

absl::StatusOr<TokenWithLocation> TokenProvider::GetToken() {
  int last_token_end_offset = location_.end().GetByteOffset();
  if (last_token_end_offset == -1) {
    last_token_end_offset = start_offset();
  }

  GOOGLESQL_ASSIGN_OR_RETURN(Token token_kind, tokenizer_->GetNextToken(&location_));

  absl::string_view prev_whitespaces;
  prev_whitespaces = GetTextBetween(input(), last_token_end_offset,
                                    location_.start().GetByteOffset());

  return {{token_kind, WithOffset(location_, offset_in_original_input()),
           location_.GetTextFrom(input()), prev_whitespaces}};
}

}  // namespace macros
}  // namespace parser
}  // namespace googlesql
