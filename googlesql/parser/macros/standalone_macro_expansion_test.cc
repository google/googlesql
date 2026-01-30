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

#include "googlesql/parser/macros/standalone_macro_expansion.h"

#include <cstddef>

#include "googlesql/parser/tm_token.h"
#include "googlesql/parser/token_with_location.h"
#include "googlesql/public/parse_location.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/btree_map.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace parser {
namespace macros {

using ::testing::ElementsAre;
using ::testing::Pair;

using Location = ParseLocationRange;

static Location MakeLocation(absl::string_view filename, int start_offset,
                             int end_offset) {
  return Location(ParseLocationPoint::FromByteOffset(filename, start_offset),
                  ParseLocationPoint::FromByteOffset(filename, end_offset));
}

static absl::string_view kTopFileName = "top_file.sql";

static Location MakeLocation(int start_offset, int end_offset) {
  return MakeLocation(kTopFileName, start_offset, end_offset);
}

// Ignores location
static TokenWithLocation MakeToken(
    Token kind, absl::string_view text,
    absl::string_view preceding_whitespaces = "") {
  return {.kind = kind,
          .location = MakeLocation(-1, -1),
          .text = text,
          .preceding_whitespaces = preceding_whitespaces};
}

TEST(TokensToStringTest, CreatsLocationToTokenIndexMap) {
  absl::btree_map<size_t, int> location_to_token_index;
  EXPECT_EQ(
      TokensToString(
          {MakeToken(Token::KW_SELECT, "SELECT", ""),
           MakeToken(Token::IDENTIFIER, "a", " "), MakeToken(Token::COMMA, ","),
           MakeToken(Token::IDENTIFIER, "b", " "), MakeToken(Token::COMMA, ","),
           MakeToken(Token::IDENTIFIER, "c", " "),
           MakeToken(Token::KW_FROM, "FROM", " "),
           MakeToken(Token::IDENTIFIER, "test_table", " "),
           MakeToken(Token::EOI, "", "\n")},
          &location_to_token_index),
      "SELECT a, b, c FROM test_table\n");
  EXPECT_THAT(
      location_to_token_index,
      ElementsAre(Pair(6, 0), Pair(8, 1), Pair(9, 2), Pair(11, 3), Pair(12, 4),
                  Pair(14, 5), Pair(19, 6), Pair(30, 7), Pair(31, 8)));
}

}  // namespace macros
}  // namespace parser
}  // namespace googlesql
