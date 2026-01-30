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

#include "googlesql/public/simple_token_list.h"

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/simple_token_list.pb.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/types/span.h"

namespace googlesql::tokens {

std::string Token::DebugString() const {
  return absl::StrFormat("'%s':%d", absl::Utf8SafeCEscape(text_), attribute_);
}

std::ostream& operator<<(std::ostream& s, const Token& t) {
  return s << t.DebugString();
}

bool operator==(const Token& a, const Token& b) {
  return a.attribute_ == b.attribute_ && a.text_ == b.text_;
}

bool operator<(const Token& a, const Token& b) {
  if (a.text_ == b.text_) {
    return a.attribute_ < b.attribute_;
  }
  return a.text_ < b.text_;
}

TextToken TextToken::MakeIndex(std::string text, TextAttribute attribute,
                               IndexAttribute index_attribute) {
  TextToken text_token;
  text_token.text_ = text;
  text_token.attribute_ = attribute.value();
  text_token.index_tokens_.push_back(Token(text, index_attribute.value()));
  text_token.index_token_set_.insert(std::move(text));
  return text_token;
}

TextToken TextToken::Make(std::string text, uint64_t attribute,
                          std::vector<Token>&& index_tokens) {
  TextToken text_token;
  text_token.text_ = std::move(text);
  text_token.attribute_ = attribute;
  text_token.AddIndexTokens(std::move(index_tokens));
  index_tokens.clear();
  return text_token;
}

std::string TextToken::DebugString() const {
  std::string out;
  absl::StrAppend(&out, "{");
  absl::StrAppendFormat(&out, "text: '%s'", absl::Utf8SafeCEscape(text()));
  absl::StrAppendFormat(&out, ", attribute: %d", attribute_);
  absl::StrAppendFormat(
      &out, ", index_tokens: [%s]",
      absl::StrJoin(index_tokens(), ", ", [](std::string* out, const auto& t) {
        absl::StrAppendFormat(out, "'%s':%d", absl::Utf8SafeCEscape(t.text()),
                              t.attribute());
      }));
  absl::StrAppend(&out, "}");
  return out;
}

absl::Span<const Token> TextToken::index_tokens() const {
  return absl::Span<const Token>(index_tokens_);
}

void TextToken::ReserveIndexTokens(size_t num) {
  index_tokens_.reserve(index_tokens_.size() + num);
}

bool TextToken::AddIndexToken(Token token) {
  if (!index_token_set_.contains(token.text_)) {
    index_token_set_.insert(token.text_);
    index_tokens_.push_back(std::move(token));
    return true;
  }
  return false;
}

bool TextToken::AddIndexTokens(std::vector<Token>&& tokens) {
  bool all_added = true;
  ReserveIndexTokens(tokens.size());
  for (Token& token : tokens) {
    all_added &= AddIndexToken(std::move(token));
  }
  return all_added;
}

std::vector<Token> TextToken::ReleaseIndexTokens() {
  std::vector<Token> result = std::move(index_tokens_);
  index_tokens_.clear();
  index_token_set_.clear();
  return result;
}

bool TextToken::SetIndexTokens(std::vector<Token>&& tokens) {
  bool all_tokens_added = true;
  index_tokens_.clear();
  index_token_set_.clear();
  for (Token& token : tokens) {
    all_tokens_added &= AddIndexToken(std::move(token));
  }
  return all_tokens_added;
}

}  // namespace googlesql::tokens
