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

#include "googlesql/public/token_list_util.h"

#include <string>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/value.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace googlesql {

using ::testing::ElementsAre;
using ::absl_testing::IsOkAndHolds;

TEST(TokenListUtilTest, StringArrayTokenListRoundTrip) {
  std::vector<std::string> tokens = {"foo", "bar", "baz"};
  Value token_list = TokenListFromStringArray(tokens);
  EXPECT_EQ(token_list.type_kind(), TypeKind::TYPE_TOKENLIST);
  EXPECT_THAT(StringArrayFromTokenList(token_list),
              IsOkAndHolds(ElementsAre("foo", "bar", "baz")));
}

TEST(TokenListUtilTest, BytesTokenListRoundTrip) {
  // Create a TokenList value from a vector of strings.
  std::vector<std::string> tokens = {"foo", "bar", "baz"};
  Value token_list = TokenListFromStringArray(tokens);
  EXPECT_EQ(token_list.type_kind(), TypeKind::TYPE_TOKENLIST);

  // Round trip the TokenList value through bytes.
  Value token_list_from_bytes =
      TokenListFromBytes(token_list.tokenlist_value().GetBytes());
  EXPECT_EQ(token_list_from_bytes.type_kind(), TypeKind::TYPE_TOKENLIST);
  EXPECT_THAT(StringArrayFromTokenList(token_list_from_bytes),
              IsOkAndHolds(ElementsAre("foo", "bar", "baz")));
}

}  // namespace googlesql
