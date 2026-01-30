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

#include "googlesql/common/search/public/token_list_util.h"

#include <string>

#include "googlesql/public/simple_token_list.h"
#include "googlesql/public/simple_token_list.h"
#include "googlesql/public/simple_token_list.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"

namespace googlesql::search {
namespace {

using ::testing::ElementsAre;
using tokens::IndexAttribute;    
using tokens::TextAttribute;     
using tokens::TextToken;         
using tokens::Token;             
using tokens::TokenList;         
using tokens::TokenListBuilder;  

TEST(AppendPhraseGapTest, DefaultPhraseGapSize) {
  TokenListBuilder builder;
  builder.Add(TextToken::MakeIndex("foo"));
  AppendPhraseGap(builder);
  EXPECT_EQ(FormatTokenList(builder.Build(), /*debug_mode=*/true,
                            /*collapse_identical_tokens=*/true),
            "{text: 'foo', attribute: 0, index_tokens: ['foo':0]}\n{text: '', "
            "attribute: 0, index_tokens: []} (25 times)");
}

TEST(AppendPhraseGapTest, CustomPhraseGapSize) {
  TokenListBuilder builder;
  builder.Add(TextToken::MakeIndex("foo"));
  AppendPhraseGap(builder, /*size=*/8);
  EXPECT_EQ(FormatTokenList(builder.Build(), /*debug_mode=*/true,
                            /*collapse_identical_tokens=*/true),
            "{text: 'foo', attribute: 0, index_tokens: ['foo':0]}\n{text: '', "
            "attribute: 0, index_tokens: []} (8 times)");
}

TEST(FormatTokenListTest, DebugMode) {
  {
    TokenListBuilder builder;
    builder.Add(TextToken::MakeIndex("foo"));
    builder.Add(TextToken::MakeIndex("bar"));
    EXPECT_EQ(FormatTokenList(builder.Build(), /*debug_mode=*/true),
              "{text: 'foo', attribute: 0, index_tokens: ['foo':0]}\n{text: "
              "'bar', attribute: 0, index_tokens: ['bar':0]}");
  }
  {
    TokenListBuilder builder;
    builder.Add(
        TextToken::MakeIndex("foo", TextAttribute(3), IndexAttribute(5)));
    builder.Add(
        TextToken::MakeIndex("bar", TextAttribute(6), IndexAttribute(8)));
    EXPECT_EQ(FormatTokenList(builder.Build(), /*debug_mode=*/true),
              "{text: 'foo', attribute: 3, index_tokens: ['foo':5]}\n{text: "
              "'bar', attribute: 6, index_tokens: ['bar':8]}");
  }
}

TEST(FormatTokenListTest, EmptyTokenList) {
  EXPECT_EQ(FormatTokenList(TokenList(),
                            /*debug_mode=*/true),
            "<empty>");

  EXPECT_EQ(FormatTokenList(TokenList(),
                            /*debug_mode=*/false),
            "CAST(b\"\" AS TOKENLIST)");
}


TEST(FormatTokenListTest, InvalidTokenList) {
  EXPECT_EQ(FormatTokenList(TokenList::FromBytesUnvalidated("imposssible"),
                            /*debug_mode=*/true),
            "Invalid tokenlist encoding");

  EXPECT_EQ(FormatTokenList(TokenList::FromBytesUnvalidated("imposssible"),
                            /*debug_mode=*/false),
            "CAST(b\"imposssible\" AS TOKENLIST)");
}

TEST(FormatTokenListTest, CollapseIdenticalTokens) {
  {
    TokenListBuilder builder;
    builder.Add(TextToken::MakeIndex("foo"));
    builder.Add(TextToken::MakeIndex("foo"));
    EXPECT_EQ(FormatTokenList(builder.Build(), /*debug_mode=*/true,
                              /*collapse_identical_tokens=*/true),
              "{text: 'foo', attribute: 0, index_tokens: ['foo':0]} (2 times)");
  }
  {
    TokenListBuilder builder;
    builder.Add(TextToken::MakeIndex("foo", TextAttribute(3)));
    builder.Add(TextToken::MakeIndex("foo"));
    builder.Add(TextToken::MakeIndex("foo"));
    EXPECT_EQ(
        FormatTokenList(builder.Build(), /*debug_mode=*/true,
                        /*collapse_identical_tokens=*/true),
        "{text: 'foo', attribute: 3, index_tokens: ['foo':0]}\n{text: 'foo', "
        "attribute: 0, index_tokens: ['foo':0]} (2 times)");
  }
  {
    TokenListBuilder builder;
    builder.Add(TextToken::MakeIndex("foo"));
    builder.Add(TextToken::MakeIndex("foo"));
    builder.Add(TextToken::MakeIndex("foo"));
    builder.Add(TextToken::MakeIndex("bar"));
    builder.Add(TextToken::MakeIndex("foo"));
    builder.Add(TextToken::MakeIndex("foo"));
    EXPECT_EQ(
        FormatTokenList(builder.Build(), /*debug_mode=*/true,
                        /*collapse_identical_tokens=*/true),
        "{text: 'foo', attribute: 0, index_tokens: ['foo':0]} (3 "
        "times)\n{text: 'bar', attribute: 0, index_tokens: ['bar':0]}\n{text: "
        "'foo', attribute: 0, index_tokens: ['foo':0]} (2 times)");
  }
}

TEST(FormatTokenListTest, CustomAttributeFormat) {
  const auto format_attribute_fn = [](std::string& out,
                                      TextAttribute attribute) {
    absl::StrAppend(&out, "test-format-token-attribute-", attribute.value());
  };
  {
    TokenListBuilder builder;
    builder.Add(TextToken::MakeIndex("foo", TextAttribute(3)));
    EXPECT_EQ(FormatTokenList(builder.Build(), /*debug_mode=*/true,
                              /*collapse_identical_tokens=*/false,
                              format_attribute_fn),
              "{text: 'foo', attribute: test-format-token-attribute-3, "
              "index_tokens: ['foo':0]}");
  }
  {
    TokenListBuilder builder;
    builder.Add(TextToken::MakeIndex("foo"));
    builder.Add(TextToken::MakeIndex("foo"));
    EXPECT_EQ(
        FormatTokenList(builder.Build(), /*debug_mode=*/true,
                        /*collapse_identical_tokens=*/true,
                        format_attribute_fn),
        "{text: 'foo', attribute: test-format-token-attribute-0, index_tokens: "
        "['foo':0]} (2 times)");
  }
}

TEST(FormatTokenLinesTest, ReturnsLinesOfTokens) {
  {
    TokenListBuilder builder;
    builder.Add(TextToken::MakeIndex("foo"));
    builder.Add(TextToken::MakeIndex("bar"));
    EXPECT_THAT(
        FormatTokenLines(builder.Build()),
        ElementsAre("{text: 'foo', attribute: 0, index_tokens: ['foo':0]}",
                    "{text: 'bar', attribute: 0, index_tokens: ['bar':0]}"));
  }
  {
    TokenListBuilder builder;
    builder.Add(TextToken::MakeIndex("foo"));
    builder.Add(TextToken::MakeIndex("foo"));
    builder.Add(TextToken::MakeIndex("bar"));
    EXPECT_THAT(
        FormatTokenLines(builder.Build(), /*collapse_identical_tokens=*/true),
        ElementsAre(
            "{text: 'foo', attribute: 0, index_tokens: ['foo':0]} (2 times)",
            "{text: 'bar', attribute: 0, index_tokens: ['bar':0]}"));
  }
}

TEST(FormatTextTokenTest, DefaultAttributeFormat) {
  std::string out;
  FormatTextToken(out, TextToken::MakeIndex("foo", TextAttribute(3)));
  EXPECT_EQ(out, "{text: 'foo', attribute: 3, index_tokens: ['foo':0]}");
}

TEST(FormatTextTokenTest, CustomAttributeFormat) {
  std::string out;
  FormatTextToken(out, TextToken::MakeIndex("foo", TextAttribute(3)),
                  [](std::string& out, TextAttribute attribute) {
                    absl::StrAppend(&out, "test-format-token-attribute-",
                                    attribute.value());
                  });
  EXPECT_EQ(out,
            "{text: 'foo', attribute: test-format-token-attribute-3, "
            "index_tokens: ['foo':0]}");
}

TEST(FormatTextTokenTest, IndexTokensAreSorted) {
  std::string out;
  FormatTextToken(
      out,
      TextToken::Make("foo", 0, {Token("zoo"), Token("air"), Token("bar")}));
  EXPECT_EQ(
      out,
      "{text: 'foo', attribute: 0, index_tokens: ['air':0, 'bar':0, 'zoo':0]}");
}

}  // namespace
}  // namespace googlesql::search
