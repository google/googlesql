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

#ifndef GOOGLESQL_PUBLIC_SIMPLE_TOKEN_LIST_H_
#define GOOGLESQL_PUBLIC_SIMPLE_TOKEN_LIST_H_

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "googlesql/public/simple_token_list.pb.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql::tokens {

class IndexAttribute {
 public:
  explicit IndexAttribute(uint64_t value = 0) : value_(value) {}

  uint64_t value() const { return value_; }

 private:
  uint64_t value_;
};

class TextAttribute {
 public:
  explicit TextAttribute(uint64_t value = 0) : value_(value) {}

  uint64_t value() const { return value_; }

 private:
  uint64_t value_;
};

// Each Token consists of the token `text` and a scoring `attribute`.
class Token {
 public:
  Token() = default;
  explicit Token(std::string text, uint64_t attribute = 0)
      : text_(std::move(text)), attribute_(attribute) {}

  absl::string_view text() const { return text_; }
  void set_text(std::string text) { text_ = std::move(text); }

  uint64_t attribute() const { return attribute_; }
  void set_attribute(uint64_t attribute) { attribute_ = attribute; }

  std::string DebugString() const;
  friend std::ostream& operator<<(std::ostream& s, const Token& t);
  friend bool operator==(const Token& a, const Token& b);
  friend bool operator!=(const Token& a, const Token& b) { return !(a == b); }
  friend bool operator<(const Token& a, const Token& b);
  template <typename H>
  friend H AbslHashValue(H h, const Token& t) {
    return H::combine(std::move(h), t.text_, t.attribute_);
  }

 private:
  friend class TextToken;

  std::string text_;
  uint64_t attribute_ = 0;
};

// TextToken stores token data for indexing, snippeting and scoring. TextTokens
// usually come in a list.
class TextToken {
 public:
  static TextToken MakeIndex(
      std::string text, TextAttribute attribute = TextAttribute(0),
      IndexAttribute index_attribute = IndexAttribute(0));
  static TextToken MakeIndex(std::string text, IndexAttribute index_attribute) {
    return MakeIndex(std::move(text), TextAttribute(0), index_attribute);
  }

  static TextToken Make(std::string text, uint64_t attribute = 0,
                        std::vector<Token>&& index_tokens = {});

  static TextToken Make(std::string text, uint64_t attribute,
                        const std::vector<Token>& index_tokens) {
    return Make(std::move(text), attribute, std::vector<Token>(index_tokens));
  }

  TextToken() = default;
  TextToken(const TextToken& other) = default;
  TextToken(TextToken&& other) = default;
  TextToken& operator=(const TextToken& other) = default;
  TextToken& operator=(TextToken&& other) = default;

  void set_text(std::string text) { text_ = std::move(text); }
  absl::string_view text() const { return text_; }

  bool text_is_indexed() const { return index_token_set_.contains(text_); }

  uint64_t attribute() const { return attribute_; }
  void set_attribute(uint64_t attribute) { attribute_ = attribute; }

  absl::Span<const Token> index_tokens() const;
  void ReserveIndexTokens(size_t num);
  bool AddIndexToken(Token token);
  bool AddIndexToken(std::string text, uint64_t attribute = 0) {
    return AddIndexToken(Token(std::move(text), attribute));
  }
  bool AddIndexTokens(std::vector<Token>&& tokens);
  bool AddIndexTokens(const std::vector<Token>& tokens) {
    return AddIndexTokens(std::vector<Token>(tokens));
  }

  std::vector<Token> ReleaseIndexTokens();
  bool SetIndexTokens(std::vector<Token>&& tokens);
  bool SetIndexTokens(const std::vector<Token>& tokens) {
    return SetIndexTokens(std::vector<Token>(tokens));
  }

  std::string DebugString() const;

  friend bool operator==(const TextToken& a, const TextToken& b) {
    return a.text() == b.text() && a.attribute() == b.attribute() &&
           a.index_tokens() == b.index_tokens();
  }

  friend bool operator!=(const TextToken& a, const TextToken& b) {
    return !(a == b);
  }

  template <typename H>
  friend H AbslHashValue(H h, const TextToken& t) {
    return H::combine(std::move(h), t.attribute_, t.text_, t.index_tokens());
  }

 private:
  std::string text_;
  uint64_t attribute_ = 0;
  std::vector<Token> index_tokens_;
  absl::flat_hash_set<std::string> index_token_set_;
};

// A TokenList is an ordered list of tokens (used for snippeting, etc).
class TokenList {
 public:
  TokenList() = default;
  TokenList(TokenList&& other) = default;
  TokenList& operator=(TokenList&& other) = default;

  std::string ToBytes() && { return std::move(data_); }
  const std::string& GetBytes() const { return data_; };
  // Constructs a TokenList from serialized bytes.
  static TokenList FromBytesUnvalidated(std::string serialized) {
    return TokenList(std::move(serialized));
  }

  // Allows iteration over this TokenList.
  class Iterator {
   public:
    // Returns true if there is no more data to be iterated over.
    bool done() const { return cur_ == data_.text_token_size(); }
    // Retrieves the next TextToken from the TokenList being iterated over.
    absl::Status Next(TextToken& token) {
      TextTokenProto& text_token_proto = *data_.mutable_text_token(cur_++);
      token.set_text(*std::move(text_token_proto.mutable_text()));
      token.set_attribute(text_token_proto.attribute());
      token.ReleaseIndexTokens();
      token.ReserveIndexTokens(text_token_proto.index_token_size());
      for (TokenProto& index_token : *text_token_proto.mutable_index_token()) {
        token.AddIndexToken(*std::move(index_token.mutable_text()),
                            index_token.attribute());
      }
      return absl::OkStatus();
    }

   private:
    friend class TokenList;
    explicit Iterator(SimpleTokenListProto data) : data_(std::move(data)) {}

    SimpleTokenListProto data_;
    int cur_ = 0;
  };

  absl::StatusOr<Iterator> GetIterator() const {
    if (data_.empty()) {
      return Iterator(SimpleTokenListProto());
    }
    SimpleTokenListProto proto;
    if (!proto.ParseFromString(data_)) {
      return absl::InvalidArgumentError("Deserializing TokenList failed");
    }
    return Iterator(std::move(proto));
  }

  // Logical equality comparison for TokenList objects.
  bool EquivalentTo(const TokenList& other) const {
    return data_ == other.data_;
  }

  // Hash method for the TokenList objects. Not meant to be efficient.
  template <typename H>
  friend H AbslHashValue(H h, const TokenList& token_list) {
    auto iter = token_list.GetIterator();
    if (!iter.ok()) {
      return H::combine(std::move(h), 0);
    }
    TextToken t;
    while (!iter->done()) {
      if (!iter->Next(t).ok()) {
        return H::combine(std::move(h), 0);
      }
      h = H::combine(std::move(h), t);
    }
    return h;
  }

  // Convenience: checks if the TokenList is well-formed, by attempting to
  // fully decode. An empty TokenList is considered valid.
  bool IsValid() const { return GetIterator().ok(); }

  // Estimated in-memory byte size.
  size_t SpaceUsed() const {
    return sizeof(TokenList) +
           (data_.capacity() > std::string().capacity() ? data_.capacity() : 0);
  }

 private:
  explicit TokenList(std::string data) : data_(std::move(data)) {}

  std::string data_;
};

// TokenListBuilder allows constructing a TokenList by adding a sequence
// of token data.
class TokenListBuilder {
 public:
  TokenListBuilder() = default;

  // Adds a text token to the TokenListBuilder.
  void Add(TextToken text_token) { Add({&text_token, 1}); }
  // As above, but allows adding more than one token at a time.
  void Add(absl::Span<const TextToken> text_tokens) {
    for (const TextToken& text_token : text_tokens) {
      TextTokenProto& text_token_proto = *proto_.add_text_token();
      text_token_proto.set_text(text_token.text());
      text_token_proto.set_attribute(text_token.attribute());
      for (const Token& index_token : text_token.index_tokens()) {
        TokenProto& index_token_proto = *text_token_proto.add_index_token();
        index_token_proto.set_text(index_token.text());
        index_token_proto.set_attribute(index_token.attribute());
      }
    }
  }

  // Builds the TokenList and resets this builder.
  TokenList Build() {
    std::string serialized = proto_.SerializeAsString();
    proto_.Clear();
    return TokenList::FromBytesUnvalidated(std::move(serialized));
  }

  size_t max_byte_size() const { return proto_.ByteSizeLong(); }

 private:
  SimpleTokenListProto proto_;
};

}  // namespace googlesql::tokens

#endif  // GOOGLESQL_PUBLIC_SIMPLE_TOKEN_LIST_H_
