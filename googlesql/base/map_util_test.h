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

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_MAP_UTIL_TEST_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_MAP_UTIL_TEST_H_

// Contains map_util tests templated on STL std::map-like types.

#include <stddef.h>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "googlesql/base/logging.h"
#include "googlesql/base/map_util.h"

namespace googlesql_base {

template <class T>
class MapUtilIntIntTest : public ::testing::Test {};
TYPED_TEST_SUITE_P(MapUtilIntIntTest);

template <class T>
class MapUtilIntIntPtrTest : public ::testing::Test {};
TYPED_TEST_SUITE_P(MapUtilIntIntPtrTest);

template <class T>
class MapUtilIntIntSharedPtrTest : public ::testing::Test {};
TYPED_TEST_SUITE_P(MapUtilIntIntSharedPtrTest);

template <class T>
class MapUtilIntIntSharedPtrOnlyTest : public ::testing::Test {};
TYPED_TEST_SUITE_P(MapUtilIntIntSharedPtrOnlyTest);

template <class T>
class MultiMapUtilIntIntTest : public ::testing::Test {};
TYPED_TEST_SUITE_P(MultiMapUtilIntIntTest);

TYPED_TEST_P(MapUtilIntIntTest, ValueMapTests) {
  using Map = TypeParam;
  Map m;

  // Check that I get a default when the key is not present.
  EXPECT_EQ(0, FindWithDefault(m, 0, 0));
  EXPECT_EQ(0, FindWithDefault(m, 0));
  // Check that I can insert a value.
  EXPECT_TRUE(InsertOrUpdate(&m, 0, 1));
  // .. and get that value back.
  EXPECT_EQ(1, FindWithDefault(m, 0, 0));
  EXPECT_EQ(1, FindWithDefault(m, 0));
  // Check that I can update a value.
  EXPECT_FALSE(InsertOrUpdate(&m, 0, 2));
  // .. and get that value back.
  EXPECT_EQ(2, FindWithDefault(m, 0, 0));
  EXPECT_EQ(2, FindWithDefault(m, 0));
  // Check that FindOrDie works when the value exists.
  EXPECT_EQ(2, FindOrDie(m, 0));
  EXPECT_EQ(2, FindOrDieNoPrint(m, 0));

  // Check FindCopy
  int i = 0;
  EXPECT_TRUE(FindCopy(m, 0, &i));
  EXPECT_EQ(i, 2);
  EXPECT_FALSE(FindCopy(m, 1, &i));
  EXPECT_TRUE(FindCopy(m, 0, static_cast<int*>(nullptr)));
  EXPECT_FALSE(FindCopy(m, 1, static_cast<int*>(nullptr)));

  // Check FindOrNull
  int* p1 = FindOrNull(m, 0);
  ASSERT_EQ(*p1, 2);
  ++(*p1);
  const int* p2 = FindOrNull(const_cast<const Map&>(m), 0);
  ASSERT_EQ(*p2, 3);
  ASSERT_TRUE(FindOrNull(m, 1) == nullptr);

  // Check contains
  EXPECT_TRUE(ContainsKey(m, 0));
  EXPECT_FALSE(ContainsKey(m, 1));

  // Check ContainsKeyValuePair
  EXPECT_TRUE(ContainsKeyValuePair(m, 0, 3));
  EXPECT_FALSE(ContainsKeyValuePair(m, 0, 4));
  EXPECT_FALSE(ContainsKeyValuePair(m, 1, 0));

  // Check insert if not present
  EXPECT_FALSE(InsertIfNotPresent(&m, 0, 2));
  EXPECT_TRUE(InsertIfNotPresent(&m, 1, 3));

  // Check lookup or insert
  EXPECT_EQ(3, LookupOrInsert(&m, 0, 2));
  EXPECT_EQ(4, LookupOrInsert(&m, 2, 4));
  EXPECT_EQ(4, FindWithDefault(m, 2, 0));
  EXPECT_EQ(4, FindWithDefault(m, 2));

  EXPECT_FALSE(InsertOrUpdate(&m, typename Map::value_type(0, 2)));
  EXPECT_EQ(2, FindWithDefault(m, 0, 0));
  EXPECT_EQ(2, FindWithDefault(m, 0));

  // Check InsertOrUpdateMany
  std::vector<std::pair<int, int> > entries;
  entries.push_back(std::make_pair(0, 100));
  entries.push_back(std::make_pair(100, 101));
  entries.push_back(std::make_pair(200, 102));

  InsertOrUpdateMany(&m, entries.begin(), entries.end());
  EXPECT_EQ(100, FindWithDefault(m, 0, 0));
  EXPECT_EQ(100, FindWithDefault(m, 0));
  EXPECT_EQ(101, FindWithDefault(m, 100, 0));
  EXPECT_EQ(101, FindWithDefault(m, 100));
  EXPECT_EQ(102, FindWithDefault(m, 200, 0));
  EXPECT_EQ(102, FindWithDefault(m, 200));
}

TYPED_TEST_P(MapUtilIntIntPtrTest, LookupOrInsertNewTest) {
  using PtrMap = TypeParam;
  PtrMap m;
  int *v1, *v2, *v3, *v4;

  // Check inserting one item.
  v1 = LookupOrInsertNew(&m, 7);
  EXPECT_EQ(0, *v1);
  ASSERT_TRUE(v1 != nullptr);
  EXPECT_TRUE(ContainsKey(m, 7));
  EXPECT_EQ(m.size(), 1);

  // Check inserting the same item.
  v2 = LookupOrInsertNew(&m, 7);
  ASSERT_TRUE(v2 != nullptr);
  EXPECT_EQ(v1, v2);
  EXPECT_EQ(m.size(), 1);

  // Check a couple more items.
  v1 = LookupOrInsertNew(&m, 8);
  ASSERT_TRUE(v1 != nullptr);
  EXPECT_NE(v1, v2);
  EXPECT_TRUE(ContainsKey(m, 8));
  EXPECT_EQ(m.size(), 2);

  v2 = LookupOrInsertNew(&m, 8);
  EXPECT_EQ(v1, v2);
  EXPECT_EQ(m.size(), 2);

  v3 = LookupOrInsertNew(&m, 8, 88);
  EXPECT_NE(88, *v3);
  EXPECT_EQ(v3, v2);
  EXPECT_EQ(m.size(), 2);

  v4 = LookupOrInsertNew(&m, 9, 99);
  EXPECT_EQ(99, *v4);
  EXPECT_NE(v1, v4);
  EXPECT_NE(v2, v4);
  EXPECT_NE(v3, v4);
  EXPECT_EQ(m.size(), 3);

  // Return by reference, so that the stored value can be modified in the map.
  // We check this by verifying the address of the returned value is identical.
  EXPECT_EQ(&LookupOrInsertNew(&m, 9), &LookupOrInsertNew(&m, 9, 999));

  for (auto& kv : m) delete kv.second;
}

TYPED_TEST_P(MapUtilIntIntSharedPtrTest, LookupOrInsertNewSharedPtrTest) {
  using SharedPtrMap = TypeParam;
  using SharedPtr = typename TypeParam::value_type::second_type;
  SharedPtrMap m;
  SharedPtr v1, v2, v3, v4, v5;

  // Check inserting one item.
  v1 = LookupOrInsertNew(&m, 7);
  ASSERT_TRUE(v1.get() != nullptr);
  EXPECT_TRUE(ContainsKey(m, 7));
  EXPECT_EQ(m.size(), 1);
  *v1 = 25;

  // Check inserting the same item.
  v2 = LookupOrInsertNew(&m, 7);
  ASSERT_TRUE(v2.get() != nullptr);
  EXPECT_EQ(v1.get(), v2.get());
  EXPECT_EQ(m.size(), 1);
  EXPECT_EQ(25, *v2.get());

  // Check a couple more items.
  v2 = LookupOrInsertNew(&m, 8);
  ASSERT_TRUE(v2.get() != nullptr);
  EXPECT_NE(v1.get(), v2.get());
  EXPECT_TRUE(ContainsKey(m, 8));
  EXPECT_EQ(m.size(), 2);
  *v2 = 42;

  v3 = LookupOrInsertNew(&m, 8);
  EXPECT_NE(v1.get(), v2.get());
  EXPECT_EQ(v2.get(), v3.get());
  EXPECT_EQ(m.size(), 2);
  EXPECT_EQ(25, *v1.get());
  EXPECT_EQ(42, *v2.get());
  EXPECT_EQ(42, *v3.get());

  m.clear();
  // Since the container does not own the elements and because we still have the
  // shared pointers we can still access the old values.
  v3 = LookupOrInsertNew(&m, 7);
  EXPECT_NE(v1.get(), v3.get());
  EXPECT_NE(v2.get(), v3.get());
  EXPECT_EQ(m.size(), 1);
  EXPECT_EQ(25, *v1.get());
  EXPECT_EQ(42, *v2.get());
  EXPECT_EQ(0, *v3.get());  // Also checks for default init of POD elements

  v4 = LookupOrInsertNew(&m, 7, 77);
  EXPECT_NE(v1.get(), v4.get());
  EXPECT_NE(v2.get(), v4.get());
  EXPECT_EQ(v3.get(), v4.get());
  EXPECT_EQ(m.size(), 1);
  EXPECT_EQ(25, *v1.get());
  EXPECT_EQ(42, *v2.get());
  EXPECT_EQ(0, *v3.get());
  EXPECT_EQ(0, *v4.get());

  v5 = LookupOrInsertNew(&m, 8, 88);
  EXPECT_NE(v1.get(), v5.get());
  EXPECT_NE(v2.get(), v5.get());
  EXPECT_NE(v3.get(), v5.get());
  EXPECT_NE(v4.get(), v5.get());
  EXPECT_EQ(m.size(), 2);
  EXPECT_EQ(25, *v1.get());
  EXPECT_EQ(42, *v2.get());
  EXPECT_EQ(0, *v3.get());
  EXPECT_EQ(0, *v4.get());
  EXPECT_EQ(88, *v5.get());
}

TYPED_TEST_P(MapUtilIntIntSharedPtrOnlyTest,
             LookupOrInsertNewSharedPtrSwapTest) {
  using SharedPtrMap = TypeParam;
  using SharedPtr = typename TypeParam::value_type::second_type;
  SharedPtrMap m;
  SharedPtr v1, v2, v3, v4;

  v1.reset(new int(1));
  LookupOrInsertNew(&m, 11).swap(v1);
  EXPECT_TRUE(v1.get() != nullptr);
  EXPECT_EQ(0, *v1.get());  // The element created by LookupOrInsertNew
  EXPECT_TRUE(ContainsKey(m, 11));
  EXPECT_EQ(1, m.size());
  // If the functions does not correctly return by ref then v2 will contain 0
  // instead of 1 even though v2 still points to the held entry. The tests that
  // depend on return by ref use ASSERT_*().
  v2 = LookupOrInsertNew(&m, 11);
  ASSERT_EQ(1, *v2.get());
  EXPECT_EQ(v2.get(), LookupOrInsertNew(&m, 11).get());

  *v2 = 2;
  v3 = LookupOrInsertNew(&m, 11);
  EXPECT_EQ(2, *v2.get());
  EXPECT_EQ(2, *v3.get());
  ASSERT_NE(v1.get(), v2.get());
  EXPECT_EQ(v2.get(), v3.get());
  ASSERT_NE(v1.get(), LookupOrInsertNew(&m, 11).get());
  EXPECT_EQ(v2.get(), LookupOrInsertNew(&m, 11).get());
  EXPECT_EQ(v3.get(), LookupOrInsertNew(&m, 11).get());

  v4.reset(new int(4));
  LookupOrInsertNew(&m, 11).swap(v4);
  EXPECT_EQ(2, *v4.get());
  ASSERT_EQ(4, *LookupOrInsertNew(&m, 11).get());
  ASSERT_EQ(v3.get(), v4.get());
}

TYPED_TEST_P(MapUtilIntIntPtrTest, InsertAndDeleteExistingTest) {
  using PtrMap = TypeParam;
  PtrMap m;

  // Add a few items.
  int* v1 = new int;
  int* v2 = new int;
  int* v3 = new int;
  EXPECT_TRUE(InsertAndDeleteExisting(&m, 1, v1));
  EXPECT_TRUE(InsertAndDeleteExisting(&m, 2, v2));
  EXPECT_TRUE(InsertAndDeleteExisting(&m, 3, v3));
  EXPECT_EQ(v1, FindPtrOrNull(m, 1));
  EXPECT_EQ(v2, FindPtrOrNull(m, 2));
  EXPECT_EQ(v3, FindPtrOrNull(m, 3));

  // Replace a couple.
  int* v4 = new int;
  int* v5 = new int;
  EXPECT_FALSE(InsertAndDeleteExisting(&m, 1, v4));
  EXPECT_FALSE(InsertAndDeleteExisting(&m, 2, v5));
  EXPECT_EQ(v4, FindPtrOrNull(m, 1));
  EXPECT_EQ(v5, FindPtrOrNull(m, 2));
  EXPECT_EQ(v3, FindPtrOrNull(m, 3));

  // Add one more item.
  int* v6 = new int;
  EXPECT_TRUE(InsertAndDeleteExisting(&m, 6, v6));
  EXPECT_EQ(v4, FindPtrOrNull(m, 1));
  EXPECT_EQ(v5, FindPtrOrNull(m, 2));
  EXPECT_EQ(v3, FindPtrOrNull(m, 3));
  EXPECT_EQ(v6, FindPtrOrNull(m, 6));

  // 6 total allocations, this will only delete 4.  Heap-check will fail
  // here if the existing entries weren't properly deleted.
  EXPECT_EQ(4, m.size());
  for (auto& kv : m) delete kv.second;
}

TYPED_TEST_P(MapUtilIntIntTest, UpdateReturnCopyTest) {
  using Map = TypeParam;
  Map m;

  int p = 10;
  EXPECT_FALSE(UpdateReturnCopy(&m, 0, 5, &p));
  EXPECT_EQ(10, p);

  EXPECT_TRUE(UpdateReturnCopy(&m, 0, 7, &p));
  EXPECT_EQ(5, p);

  // Check UpdateReturnCopy using value_type
  p = 10;
  EXPECT_FALSE(UpdateReturnCopy(&m, typename Map::value_type(1, 4), &p));
  EXPECT_EQ(10, p);

  EXPECT_TRUE(UpdateReturnCopy(&m, typename Map::value_type(1, 8), &p));
  EXPECT_EQ(4, p);
}

TYPED_TEST_P(MapUtilIntIntTest, InsertOrReturnExistingTest) {
  using Map = TypeParam;
  Map m;

  EXPECT_EQ(nullptr, InsertOrReturnExisting(&m, 25, 42));
  EXPECT_EQ(42, m[25]);

  int* previous = InsertOrReturnExisting(&m, 25, 666);
  EXPECT_EQ(42, *previous);
  EXPECT_EQ(42, m[25]);
}

TYPED_TEST_P(MapUtilIntIntPtrTest, FindPtrOrNullTest) {
  // Check FindPtrOrNull
  using PtrMap = TypeParam;
  PtrMap ptr_map;
  InsertOrUpdate(&ptr_map, 35, new int(35));
  int* p1 = FindPtrOrNull(ptr_map, 3);
  EXPECT_TRUE(nullptr == p1);
  const int* p2 = FindPtrOrNull(const_cast<const PtrMap&>(ptr_map), 3);
  EXPECT_TRUE(nullptr == p2);
  EXPECT_EQ(35, *FindPtrOrNull(ptr_map, 35));

  for (auto& kv : ptr_map) delete kv.second;
}

TYPED_TEST_P(MapUtilIntIntSharedPtrTest, FindPtrOrNullTest) {
  using SharedPtrMap = TypeParam;
  using SharedPtr = typename TypeParam::value_type::second_type;
  SharedPtrMap shared_ptr_map;
  InsertOrUpdate(&shared_ptr_map, 35, SharedPtr(new int(35)));
  const SharedPtr p1 = FindPtrOrNull(shared_ptr_map, 3);
  EXPECT_TRUE(nullptr == p1.get());
  const SharedPtr p2 = FindPtrOrNull(
      const_cast<const SharedPtrMap&>(shared_ptr_map), 3);
  EXPECT_TRUE(nullptr == p2.get());
  const SharedPtr p3 = FindPtrOrNull(shared_ptr_map, 35);
  const SharedPtr p4 = FindPtrOrNull(shared_ptr_map, 35);
  EXPECT_EQ(35, *p3.get());
  EXPECT_EQ(35, *p4.get());
}

TYPED_TEST_P(MapUtilIntIntTest, FindOrDieTest) {
  using Map = TypeParam;
  Map m;
  m[10] = 15;
  EXPECT_EQ(15, FindOrDie(m, 10));
  ASSERT_DEATH(FindOrDie(m, 8), "Map key not found: 8");
  EXPECT_EQ(15, FindOrDieNoPrint(m, 10));
  ASSERT_DEATH(FindOrDieNoPrint(m, 8), "Map key not found");

  // Make sure the non-const reference returning version works.
  FindOrDie(m, 10) = 20;
  EXPECT_EQ(20, FindOrDie(m, 10));

  // Make sure we can lookup values in a const map.
  const Map& const_m = m;
  EXPECT_EQ(20, FindOrDie(const_m, 10));
}

TYPED_TEST_P(MapUtilIntIntTest, InsertOrDieTest) {
  using Map = TypeParam;
  Map m;
  InsertOrDie(&m, 1, 2);
  EXPECT_EQ(m[1], 2);
  ASSERT_DEATH(InsertOrDie(&m, 1, 3), "duplicate");
}

TYPED_TEST_P(MapUtilIntIntTest, InsertKeyOrDieTest) {
  using Map = TypeParam;
  Map m;
  int& v = InsertKeyOrDie(&m, 1);
  EXPECT_EQ(m[1], 0);
  v = 2;
  EXPECT_EQ(m[1], 2);
  ASSERT_DEATH(InsertKeyOrDie(&m, 1), "duplicate");
}

TYPED_TEST_P(MapUtilIntIntPtrTest, EraseKeyReturnValuePtrTest) {
  using PtrMap = TypeParam;
  PtrMap ptr_map;
  int* v = new int(35);
  InsertOrUpdate(&ptr_map, 35, v);
  EXPECT_TRUE(EraseKeyReturnValuePtr(&ptr_map, 0) == nullptr);  // Test no-op.
  EXPECT_EQ(ptr_map.size(), 1);
  int* retv = EraseKeyReturnValuePtr(&ptr_map, 35);  // Successful operation
  EXPECT_EQ(ptr_map.size(), 0);
  EXPECT_EQ(v, retv);
  delete v;
  EXPECT_TRUE(EraseKeyReturnValuePtr(&ptr_map, 35) == nullptr);  // Empty map.
}

TYPED_TEST_P(MultiMapUtilIntIntTest, ContainsKeyValuePairTest) {
  using Map = TypeParam;

  Map m;

  m.insert(std::make_pair(1, 10));
  m.insert(std::make_pair(1, 11));
  m.insert(std::make_pair(1, 12));

  m.insert(std::make_pair(3, 13));

  EXPECT_FALSE(ContainsKeyValuePair(m, 0, 0));
  EXPECT_FALSE(ContainsKeyValuePair(m, 1, 0));
  EXPECT_TRUE(ContainsKeyValuePair(m, 1, 10));
  EXPECT_TRUE(ContainsKeyValuePair(m, 1, 11));
  EXPECT_TRUE(ContainsKeyValuePair(m, 1, 12));
  EXPECT_FALSE(ContainsKeyValuePair(m, 1, 13));
}

}  // namespace googlesql_base
#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_MAP_UTIL_TEST_H_
