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

#include "googlesql/reference_impl/parameters.h"

#include "googlesql/reference_impl/variable_id.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::ElementsAreArray;
using ::testing::IsEmpty;
using ::testing::UnorderedElementsAreArray;

namespace googlesql {
namespace {

TEST(Parameters, Map) {
  ParameterMap map;
  map["p1"] = VariableId("v1");
  map["p2"] = VariableId("v2");

  Parameters params(map);
  EXPECT_TRUE(params.is_named());
  ASSERT_THAT(params.named_parameters(), UnorderedElementsAreArray(map));

  params.named_parameters()["p3"] = VariableId("v3");
  ParameterMap new_map = map;
  new_map["p3"] = VariableId("v3");
  EXPECT_THAT(params.named_parameters(), UnorderedElementsAreArray(new_map));

  // No effect since 'params.is_named()' is already true.
  params.set_named(/*named=*/true);
  ASSERT_TRUE(params.is_named());
  EXPECT_THAT(params.named_parameters(), UnorderedElementsAreArray(new_map));

  // Clears 'params'.
  params.set_named(/*named=*/false);
  ASSERT_FALSE(params.is_named());
  EXPECT_THAT(params.positional_parameters(), IsEmpty());
}

TEST(Parameters, List) {
  const ParameterList list = {VariableId("v1"), VariableId("v2")};

  Parameters params(list);
  EXPECT_FALSE(params.is_named());
  ASSERT_THAT(params.positional_parameters(), ElementsAreArray(list));

  params.positional_parameters().emplace_back("v3");
  ParameterList new_list = list;
  new_list.emplace_back("v3");
  EXPECT_THAT(params.positional_parameters(), ElementsAreArray(new_list));

  // No effect since 'params.is_named()' is already false.
  params.set_named(/*named=*/false);
  ASSERT_FALSE(params.is_named());
  EXPECT_THAT(params.positional_parameters(), ElementsAreArray(new_list));

  // Clears 'params'.
  params.set_named(/*named=*/true);
  ASSERT_TRUE(params.is_named());
  EXPECT_THAT(params.named_parameters(), IsEmpty());
}

}  // namespace
}  // namespace googlesql
