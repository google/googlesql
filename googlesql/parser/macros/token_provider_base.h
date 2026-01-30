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

#ifndef GOOGLESQL_PARSER_MACROS_TOKEN_PROVIDER_BASE_H_
#define GOOGLESQL_PARSER_MACROS_TOKEN_PROVIDER_BASE_H_

#include <memory>
#include <optional>

#include "googlesql/parser/token_with_location.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace parser {
namespace macros {

// This interface defines the contract for token providers that feed into the
// macro expander.
class TokenProviderBase {
 public:
  TokenProviderBase(absl::string_view filename, absl::string_view input,
                    int start_offset, std::optional<int> end_offset,
                    int offset_in_original_input)
      : filename_(filename),
        input_(input),
        start_offset_(start_offset),
        end_offset_(end_offset.value_or(input.length())),
        offset_in_original_input_(offset_in_original_input) {}

  virtual ~TokenProviderBase() = default;

  // Peeks the next token, but does not consume it.
  virtual absl::StatusOr<TokenWithLocation> PeekNextToken() = 0;

  // Consumes the next token, and increments num_consumed_tokens.
  absl::StatusOr<TokenWithLocation> ConsumeNextToken() {
    GOOGLESQL_ASSIGN_OR_RETURN(TokenWithLocation next_token, ConsumeNextTokenImpl());
    ++num_consumed_tokens_;
    return next_token;
  }

  // Returns the number of tokens consumed so far.
  int num_consumed_tokens() const { return num_consumed_tokens_; }

  // Returns the filename for this token provider.
  absl::string_view filename() const { return filename_; }

  // Returns the input for this token provider.
  absl::string_view input() const { return input_; }

  // The offset where to start tokenizing in the input. Used for accurate
  // location on tokens and errors.
  int start_offset() const { return start_offset_; }

  // The offset where to stop tokenizing in the input.
  int end_offset() const { return end_offset_; }

  // The start offset of `input()` in the original file.
  int offset_in_original_input() const { return offset_in_original_input_; }

  // Creates a new instance of this token provider, with the same settings but
  // different inputs.
  virtual std::unique_ptr<TokenProviderBase> CreateNewInstance(
      absl::string_view filename, absl::string_view input, int start_offset,
      std::optional<int> end_offset, int offset_in_original_input) const = 0;

  // Convenience overload which uses the same input but simply tokenizes
  // different offsets in `input`.
  std::unique_ptr<TokenProviderBase> CreateNewInstance(
      int start_offset, std::optional<int> end_offset) const {
    return CreateNewInstance(filename_, input_, start_offset, end_offset,
                             offset_in_original_input_);
  }

 protected:
  // Implements the logic for consuming the next token.
  virtual absl::StatusOr<TokenWithLocation> ConsumeNextTokenImpl() = 0;

 private:
  // The number of tokens consumed so far.
  int num_consumed_tokens_ = 0;

  // The filename of the input that will be attached to locations.
  absl::string_view filename_;

  // The input to the tokenizer.
  absl::string_view input_;

  // The offset where to start tokenizing in the input. Used for accurate
  // location on tokens and errors.
  int start_offset_;

  // The offset where to stop tokenizing in the input.
  int end_offset_;

  // If the input text itself is still only a part of a larger file, this is
  // the start offset of `input_` in that larger content.
  // The token provider adds this offset to all output tokens.
  // Note that `start_offset_` is only relative to the start of `input_`.
  int offset_in_original_input_ = 0;
};

}  // namespace macros
}  //  namespace parser
}  // namespace googlesql

#endif  // GOOGLESQL_PARSER_MACROS_TOKEN_PROVIDER_BASE_H_
