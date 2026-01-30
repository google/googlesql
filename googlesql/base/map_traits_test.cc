//
// Copyright 2018 Google LLC
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

#include "googlesql/base/map_traits.h"

#include <unordered_map>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/node_hash_map.h"

namespace googlesql_base {
namespace subtle {
namespace {

TEST(MapTraitsTest, UnorderedMap) {
  absl::node_hash_map<int, int> m = {{1, 2}};
  EXPECT_EQ(1, GetKey(*m.begin()));
  EXPECT_EQ(2, GetMapped(*m.begin()));
}

TEST(MapTraitsTest, UnorderedMapReferences) {
  absl::node_hash_map<int, int> m = {{1, 2}};
  auto it = m.begin();
  const int* k = &it->first;
  int* v = &it->second;
  EXPECT_EQ(k, &GetKey(*it));
  EXPECT_EQ(v, &GetMapped(*it));
  GetMapped(*it) = 3;
  EXPECT_EQ(3, m[1]);
}

TEST(MapTraitsTest, UnorderedMapConstReferences) {
  const absl::node_hash_map<int, int> m = {{1, 2}};
  auto it = m.begin();
  const int* k = &it->first;
  const int* v = &it->second;
  EXPECT_EQ(k, &GetKey(*it));
  EXPECT_EQ(v, &GetMapped(*it));
}

struct CustomMapValueType {
  int first;
  int second;

  // Intentionally add 1 to the result to verify that first/second are preferred
  // to key()/value().
  int key() const { return first + 1; }
  int value() const { return second + 1; }
};

TEST(MapTraitsTest, ValueTypeHasBothFieldsAndGetters) {
  CustomMapValueType entry = {100, 1000};
  EXPECT_EQ(100, GetKey(entry));
  EXPECT_EQ(1000, GetMapped(entry));
}

}  // namespace
}  // namespace subtle
}  // namespace googlesql_base
