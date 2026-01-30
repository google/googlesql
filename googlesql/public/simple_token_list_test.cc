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

#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {
namespace tokens {
namespace {

using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::IsEmpty;
using ::absl_testing::StatusIs;

TEST(TokenTest, Comparison) {
  EXPECT_EQ(Token("foo", 6), Token("foo", 6));
  EXPECT_NE(Token("foo", 6), Token("foo", 8));
  EXPECT_NE(Token("bar", 6), Token("foo", 6));
  EXPECT_LT(Token("foo", 6), Token("foo", 8));
  EXPECT_LT(Token("a", 6), Token("b", 6));
}

TEST(TokenTest, DebugString) {
  EXPECT_EQ(Token("foo", 6).DebugString(), "'foo':6");
}

TEST(TextTokenTest, MakeIndex) {
  {
    TextToken token = TextToken::MakeIndex("foo");
    EXPECT_EQ(token.text(), "foo");
    EXPECT_EQ(token.attribute(), 0);
    EXPECT_THAT(token.index_tokens(), ElementsAre(Token("foo", 0)));
  }
  {
    TextToken token =
        TextToken::MakeIndex("foo", TextAttribute(6), IndexAttribute(8));
    EXPECT_EQ(token.text(), "foo");
    EXPECT_EQ(token.attribute(), 6);
    EXPECT_THAT(token.index_tokens(), ElementsAre(Token("foo", 8)));
  }
  {
    TextToken token = TextToken::MakeIndex("foo", TextAttribute(6));
    EXPECT_EQ(token.text(), "foo");
    EXPECT_EQ(token.attribute(), 6);
    EXPECT_THAT(token.index_tokens(), ElementsAre(Token("foo", 0)));
  }
  {
    TextToken token = TextToken::MakeIndex("foo", IndexAttribute(8));
    EXPECT_EQ(token.text(), "foo");
    EXPECT_EQ(token.attribute(), 0);
    EXPECT_THAT(token.index_tokens(), ElementsAre(Token("foo", 8)));
  }
}

TEST(TextTokenTest, Make) {
  {
    TextToken token = TextToken::Make("bar");
    EXPECT_EQ(token.text(), "bar");
    EXPECT_EQ(token.attribute(), 0);
    EXPECT_THAT(token.index_tokens(), IsEmpty());
  }
  {
    TextToken token = TextToken::Make("bar", 6, {Token("foo", 8)});
    EXPECT_EQ(token.text(), "bar");
    EXPECT_EQ(token.attribute(), 6);
    EXPECT_THAT(token.index_tokens(), ElementsAre(Token("foo", 8)));
  }
  // Index tokens cannot be duplicate.
  {
    TextToken token =
        TextToken::Make("bar", 6, {Token("foo", 8), Token("foo", 2)});
    EXPECT_EQ(token.text(), "bar");
    EXPECT_EQ(token.attribute(), 6);
    EXPECT_THAT(token.index_tokens(), ElementsAre(Token("foo", 8)));
  }
}

TEST(TextTokenTest, Setters) {
  TextToken token = TextToken::Make("text");
  EXPECT_EQ(token.attribute(), 0);
  token.set_attribute(10);
  EXPECT_EQ(token.attribute(), 10);

  EXPECT_EQ(token.text(), "text");
  token.set_text("foo");
  EXPECT_EQ(token.text(), "foo");
}

TEST(TextTokenTest, TextIsIndexed) {
  TextToken token = TextToken::Make("text");
  EXPECT_FALSE(token.text_is_indexed());
  token.AddIndexToken("foo");
  EXPECT_FALSE(token.text_is_indexed());
  token.AddIndexToken("text");
  EXPECT_TRUE(token.text_is_indexed());
}

TEST(TextTokenTest, AddIndexToken) {
  TextToken token;
  ASSERT_TRUE(token.AddIndexToken("foo", 30));
  EXPECT_EQ(token.text(), "");
  EXPECT_EQ(token.attribute(), 0);
  EXPECT_THAT(token.index_tokens(), ElementsAre(Token("foo", 30)));

  ASSERT_TRUE(token.AddIndexToken("bar", 60));
  EXPECT_THAT(token.index_tokens(),
              ElementsAre(Token("foo", 30), Token("bar", 60)));

  // Index tokens cannot be duplicate.
  ASSERT_FALSE(token.AddIndexToken("bar"));
  EXPECT_THAT(token.index_tokens(),
              ElementsAre(Token("foo", 30), Token("bar", 60)));
}

TEST(TextTokenTest, AddIndexTokens) {
  TextToken token = TextToken::Make("foo");
  ASSERT_TRUE(token.AddIndexTokens({Token("bar", 30), Token("baz")}));
  EXPECT_EQ(token.text(), "foo");
  EXPECT_EQ(token.attribute(), 0);
  EXPECT_THAT(token.index_tokens(),
              ElementsAre(Token("bar", 30), Token("baz", 0)));

  const std::vector<Token> index_tokens = {Token("bar", 80), Token("foo")};
  ASSERT_FALSE(token.AddIndexTokens(index_tokens));
  EXPECT_EQ(token.text(), "foo");
  EXPECT_EQ(token.attribute(), 0);
  EXPECT_THAT(token.index_tokens(),
              ElementsAre(Token("bar", 30), Token("baz", 0), Token("foo", 0)));
}

TEST(TextTokenTest, ReleaseIndexTokens) {
  {
    TextToken token = TextToken::MakeIndex("foo");
    const std::vector<Token> index_tokens = token.ReleaseIndexTokens();
    EXPECT_THAT(index_tokens, ElementsAre(Token("foo", 0)));
    EXPECT_THAT(token.index_tokens(), IsEmpty());
  }
  {
    TextToken token =
        TextToken::Make("bar", 6, {Token("bar", 8), Token("foo", 6)});
    const std::vector<Token> index_tokens = token.ReleaseIndexTokens();
    EXPECT_THAT(index_tokens, ElementsAre(Token("bar", 8), Token("foo", 6)));
    EXPECT_THAT(token.index_tokens(), IsEmpty());
  }
}

TEST(TextTokenTest, SetIndexTokens) {
  TextToken token = TextToken::Make("foo");
  token.SetIndexTokens({Token("foo", 6), Token("bar", 8)});
  EXPECT_EQ(token.text(), "foo");
  EXPECT_EQ(token.attribute(), 0);
  EXPECT_THAT(token.index_tokens(),
              ElementsAre(Token("foo", 6), Token("bar", 8)));

  const std::vector<Token> index_tokens = {Token("foo", 30), Token("text", 20)};
  token.SetIndexTokens(index_tokens);
  EXPECT_THAT(token.index_tokens(), ElementsAreArray(index_tokens));
}

TEST(TextTokenTest, DebugString) {
  TextToken token =
      TextToken::MakeIndex("foo", TextAttribute(6), IndexAttribute(8));
  EXPECT_EQ(token.DebugString(),
            "{text: 'foo', attribute: 6, index_tokens: ['foo':8]}");
}

TEST(TextTokenTest, Equivalence) {
  EXPECT_EQ(TextToken::Make("foo"), TextToken::Make("foo"));
  EXPECT_EQ(TextToken::Make("foo", 6, {Token("foo", 8)}),
            TextToken::Make("foo", 6, {Token("foo", 8)}));
  EXPECT_NE(TextToken::Make("foo"), TextToken::Make("bar"));
  EXPECT_NE(TextToken::Make("foo", 6), TextToken::Make("foo", 8));
  EXPECT_NE(TextToken::Make("foo", 6, {Token("foo", 8)}),
            TextToken::Make("foo", 6, {Token("foo", 2)}));
  EXPECT_NE(TextToken::Make("foo", 6, {Token("foo", 8), Token("bar", 8)}),
            TextToken::Make("foo", 6, {Token("bar", 8), Token("foo", 8)}));
}

absl::StatusOr<std::vector<TextToken>> ToVector(const TokenList& token_list) {
  GOOGLESQL_RET_CHECK(token_list.IsValid());
  auto iter = token_list.GetIterator();
  std::vector<TextToken> tokens;
  while (!iter->done()) {
    TextToken token;
    GOOGLESQL_RET_CHECK_OK(iter->Next(token));
    tokens.push_back(std::move(token));
  }
  return tokens;
}

TEST(TokenListTest, AddSingleToken) {
  TokenListBuilder builder;
  EXPECT_EQ(builder.max_byte_size(), 0);
  const TextToken token_to_add = TextToken::MakeIndex("foo");
  builder.Add(token_to_add);
  EXPECT_GT(builder.max_byte_size(), 0);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TextToken> tokens,
                       ToVector(builder.Build()));
  EXPECT_THAT(tokens, ElementsAre(token_to_add));
  // Builder is reset after first build.
  EXPECT_EQ(builder.Build().ToBytes(), "");
}

TEST(TokenListTest, AddEmptyToken) {
  TokenListBuilder builder;
  EXPECT_EQ(builder.max_byte_size(), 0);
  const TextToken token_to_add;
  builder.Add(token_to_add);
  EXPECT_GT(builder.max_byte_size(), 0);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TextToken> tokens,
                       ToVector(builder.Build()));
  EXPECT_THAT(tokens, ElementsAre(token_to_add));
  // Builder is reset after first build.
  EXPECT_EQ(builder.Build().ToBytes(), "");
}

TEST(TokenListTest, AddMultipleTokens) {
  TokenListBuilder builder;
  const std::vector<TextToken> tokens_to_add = {
      TextToken::Make("FOO", 1, {Token("are", 10)}),
      TextToken::Make("BaR", 2, {Token("you", 20)}),
      TextToken::Make("#hello", 7, {Token("#hello", 70), Token{"hello", 80}}),
      TextToken::Make("World", 8, {}),
  };
  builder.Add(tokens_to_add);

  TokenList token_list = builder.Build();
  EXPECT_NE(token_list.GetBytes(), "");
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TextToken> tokens, ToVector(token_list));
  EXPECT_THAT(tokens, ElementsAreArray(tokens_to_add));
  // Builder is reset after first build.
  EXPECT_EQ(builder.Build().ToBytes(), "");
}

TEST(TokenListTest, InvalidBytes) {
  TokenList token_list = TokenList::FromBytesUnvalidated("123");
  EXPECT_FALSE(token_list.IsValid());
  EXPECT_EQ("123", token_list.GetBytes());
  EXPECT_THAT(token_list.GetIterator(),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(TokenListTest, EmptyDefaultConstructed) {
  TokenList token_list;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TextToken> tokens, ToVector(token_list));
  EXPECT_THAT(tokens, ElementsAre());
  EXPECT_EQ("", token_list.GetBytes());
  EXPECT_EQ("", std::move(token_list).ToBytes());
}

TEST(TokenListTest, EmptyFromBuilder) {
  TokenListBuilder builder;
  TokenList token_list = builder.Build();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TextToken> tokens, ToVector(token_list));
  EXPECT_THAT(tokens, ElementsAre());
  EXPECT_EQ("", token_list.GetBytes());
  EXPECT_EQ("", std::move(token_list).ToBytes());
}

TEST(TokenListTest, EmptyFromBytes) {
  TokenList token_list = TokenList::FromBytesUnvalidated("");
  EXPECT_EQ("", token_list.GetBytes());
  EXPECT_TRUE(token_list.EquivalentTo(TokenList()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::vector<TextToken> tokens, ToVector(token_list));
  EXPECT_THAT(tokens, ElementsAre());
}

TEST(TokenListTest, MultipleIteration) {
  TokenListBuilder builder;
  builder.Add(TextToken::Make("foo"));
  builder.Add(TextToken::Make("bar"));
  TokenList token_list = builder.Build();

  absl::Status status;
  absl::StatusOr<TokenList::Iterator> it = token_list.GetIterator();
  GOOGLESQL_ASSERT_OK(it);
  absl::StatusOr<TokenList::Iterator> it2 = token_list.GetIterator();
  GOOGLESQL_ASSERT_OK(it2);

  TextToken t;
  TextToken t2;
  GOOGLESQL_EXPECT_OK(it->Next(t));
  GOOGLESQL_EXPECT_OK(it2->Next(t2));
  EXPECT_EQ(t, TextToken::Make("foo"));
  EXPECT_EQ(t, t2);

  GOOGLESQL_EXPECT_OK(it->Next(t));
  GOOGLESQL_EXPECT_OK(it2->Next(t2));
  EXPECT_EQ(t, TextToken::Make("bar"));
  EXPECT_EQ(t, t2);

  EXPECT_TRUE(it->done());
  EXPECT_TRUE(it2->done());
}

TEST(TokenListTest, Size) {
  TokenList empty;
  EXPECT_EQ(empty.SpaceUsed(), sizeof(TokenList));
  TokenListBuilder builder;
  for (int i = 0; i < 100; i++) {
    builder.Add(TextToken::Make("big"));
  }
  TokenList token_list = builder.Build();
  EXPECT_GT(token_list.SpaceUsed(), empty.SpaceUsed());
}

TEST(TokenListTest, Equivalence) {
  EXPECT_TRUE(TokenList().EquivalentTo(TokenList()));

  const TokenList bad = TokenList::FromBytesUnvalidated("BAD!");
  ASSERT_FALSE(bad.GetIterator().ok());

  EXPECT_TRUE(bad.EquivalentTo(bad));
  EXPECT_FALSE(bad.EquivalentTo(TokenList()));

  TokenListBuilder builder;
  builder.Add(TextToken::Make("R.E.M", 0, {Token("r", 6), Token("e", 6)}));
  const TokenList t = builder.Build();
  EXPECT_FALSE(t.EquivalentTo(bad));
  EXPECT_FALSE(bad.EquivalentTo(t));

  builder.Add(TextToken::Make("R.E.M", 0, {Token("r", 6), Token("e", 6)}));
  EXPECT_TRUE(t.EquivalentTo(builder.Build()));
  builder.Add(TextToken::Make("R.E.M", 0, {Token("e", 6), Token("r", 6)}));
  EXPECT_FALSE(t.EquivalentTo(builder.Build()));
  builder.Add(TextToken::Make("R.E.M", 0, {Token("r", 8), Token("e", 6)}));
  EXPECT_FALSE(t.EquivalentTo(builder.Build()));
  builder.Add(TextToken::Make("R.E.M", 3, {Token("r", 6), Token("e", 6)}));
  EXPECT_FALSE(t.EquivalentTo(builder.Build()));
}

}  // namespace
}  // namespace tokens
}  // namespace googlesql
