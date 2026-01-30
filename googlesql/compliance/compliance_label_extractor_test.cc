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

#include "googlesql/compliance/compliance_label_extractor.h"

#include <string>

#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

TEST(ComplianceLabelExtractorTest, ExtractPrefixGroupAndFunctionPrefixTest) {
  auto expect_prefix_and_group = [](absl::string_view sql_name,
                                    absl::string_view expected_prefix,
                                    PrefixGroup expected_group) {
    absl::string_view prefix_out = "";
    PrefixGroup prefix_group_out = PrefixGroup::kNone;
    ExtractPrefixGroupAndFunctionPrefix(sql_name, prefix_out, prefix_group_out);
    EXPECT_EQ(prefix_out, expected_prefix);
    EXPECT_EQ(prefix_group_out, expected_group);
  };

  // Operator does not have prefix and prefix group.
  expect_prefix_and_group("<", "", PrefixGroup::kNone);
  expect_prefix_and_group("COUNT", "", PrefixGroup::kNone);
  expect_prefix_and_group("NET.MAKE_NET", "NET", PrefixGroup::kDot);
  expect_prefix_and_group("HLL_COUNT.MERGE_PARTIAL", "HLL_COUNT",
                          PrefixGroup::kDot);
  // DARK_NET is not a valid prefix in GoogleSQL function catalog because it
  // does not exist, so it is expected to return empty prefix.
  expect_prefix_and_group("DARK_NET.MAKE_NET", "", PrefixGroup::kNone);
  expect_prefix_and_group("DATE_ADD", "DATE", PrefixGroup::kUnderscore);
  expect_prefix_and_group("SAFE_ADD", "SAFE", PrefixGroup::kUnderscore);
  expect_prefix_and_group("IS_NAN", "IS", PrefixGroup::kUnderscore);
  expect_prefix_and_group("ARRAY_INCLUDES", "ARRAY", PrefixGroup::kUnderscore);
  // FIRST is not a valid prefix in GoogleSQL function catalog because it only
  // shows up in one function name, so it is expected to return empty prefix.
  expect_prefix_and_group("FIRST_VALUE", "", PrefixGroup::kNone);
}

}  // namespace googlesql
