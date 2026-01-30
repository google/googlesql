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

#ifndef GOOGLESQL_PARSER_MACROS_TOKEN_PROVIDER_H_
#define GOOGLESQL_PARSER_MACROS_TOKEN_PROVIDER_H_

#include <memory>
#include <optional>
#include <queue>

#include "googlesql/parser/macros/token_provider_base.h"
#include "googlesql/parser/token_with_location.h"
#include "googlesql/parser/tokenizer.h"
#include "googlesql/public/parse_location.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace parser {
namespace macros {

// Provides the next token from a tokenizer without any macro expansion.
// This is the normal case, where we only have the text and we need to
// tokenize it from the start.
class TokenProvider : public TokenProviderBase {
 public:
  TokenProvider(absl::string_view filename, absl::string_view input,
                int start_offset, std::optional<int> end_offset,
                int offset_in_original_input);

  TokenProvider(const TokenProvider&) = delete;
  TokenProvider& operator=(const TokenProvider&) = delete;

  std::unique_ptr<TokenProviderBase> CreateNewInstance(
      absl::string_view filename, absl::string_view input, int start_offset,
      std::optional<int> end_offset,
      int offset_in_original_input) const override;

  // Peeks the next token, but does not consume it.
  absl::StatusOr<TokenWithLocation> PeekNextToken() override {
    if (input_token_buffer_.empty()) {
      GOOGLESQL_ASSIGN_OR_RETURN(TokenWithLocation next_token, GetToken());
      input_token_buffer_.push(next_token);
      return next_token;
    }
    return input_token_buffer_.front();
  }

 protected:
  // Consumes the next token from the buffer, or pull one from Flex if the
  // buffer is empty.
  absl::StatusOr<TokenWithLocation> ConsumeNextTokenImpl() override;

 private:
  // Pulls the next token from the lexer.
  absl::StatusOr<TokenWithLocation> GetToken();

  // The GoogleSQL tokenizer which gives us all the tokens.
  std::unique_ptr<GoogleSqlTokenizer> tokenizer_;

  // Used as a buffer when we need a lookahead from the tokenizer.
  // Any tokens here are still unprocessed by the expander.
  std::queue<TokenWithLocation> input_token_buffer_;

  // Location into the current input, used by the tokenizer.
  ParseLocationRange location_;
};

}  // namespace macros
}  // namespace parser
}  // namespace googlesql

#endif  // GOOGLESQL_PARSER_MACROS_TOKEN_PROVIDER_H_
