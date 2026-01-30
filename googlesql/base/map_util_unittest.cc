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

#include "googlesql/base/map_util.h"

#include <deque>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "googlesql/base/logging.h"

// All of the templates for the tests are defined here.
// This file is critical to understand what is tested.
#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "googlesql/base/map_util_test.h"

namespace googlesql_base {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::Pointee;
using ::testing::UnorderedElementsAre;

TEST(MapUtil, ImplicitTypeConversion) {
  using Map = absl::btree_map<std::string, std::string>;
  Map m;

  // Check that I can use a type that's implicitly convertible to the
  // key or value type, such as const char* -> string.
  EXPECT_EQ("", FindWithDefault(m, "foo", ""));
  EXPECT_EQ("", FindWithDefault(m, "foo"));
  EXPECT_TRUE(InsertOrUpdate(&m, "foo", "bar"));
  EXPECT_EQ("bar", FindWithDefault(m, "foo", ""));
  EXPECT_EQ("bar", FindWithDefault(m, "foo"));
  EXPECT_EQ("bar", *FindOrNull(m, "foo"));
  std::string str;
  EXPECT_TRUE(FindCopy(m, "foo", &str));
  EXPECT_EQ("bar", str);
  EXPECT_TRUE(ContainsKey(m, "foo"));
}

TEST(MapUtil, HeterogeneousLookup) {
  absl::flat_hash_map<std::string, std::string> m;
  const auto& const_m = m;

  // Verify that I can use a key type that's appropriate for heterogeneous
  // lookup, such as string_view -> string.
  constexpr absl::string_view kLookupKey = "foo";
  EXPECT_EQ(FindWithDefault(m, kLookupKey), "");
  EXPECT_EQ(FindWithDefault(const_m, kLookupKey), "");

  m["foo"] = "bar";

  EXPECT_EQ(FindOrDie(m, kLookupKey), "bar");
  EXPECT_EQ(FindOrDie(const_m, kLookupKey), "bar");

  EXPECT_EQ(FindOrDieNoPrint(m, kLookupKey), "bar");
  EXPECT_EQ(FindOrDieNoPrint(const_m, kLookupKey), "bar");

  EXPECT_EQ(FindWithDefault(m, kLookupKey), "bar");
  EXPECT_EQ(FindWithDefault(const_m, kLookupKey), "bar");

  EXPECT_THAT(FindOrNull(m, kLookupKey), Pointee(testing::Eq("bar")));
  EXPECT_THAT(FindOrNull(const_m, kLookupKey), Pointee(testing::Eq("bar")));

  std::string str;
  EXPECT_TRUE(FindCopy(m, kLookupKey, &str));
  EXPECT_EQ(str, "bar");

  std::string str_from_const;
  EXPECT_TRUE(FindCopy(const_m, kLookupKey, &str_from_const));
  EXPECT_EQ(str_from_const, "bar");

  absl::flat_hash_map<std::string, void*> ptr_m;
  const auto& const_ptr_m = ptr_m;

  // Insert an arbitrary non-null pointer into the map.
  ASSERT_TRUE(InsertOrUpdate(&ptr_m, "foo", &ptr_m));

  EXPECT_EQ(FindPtrOrNull(ptr_m, kLookupKey), &ptr_m);
  EXPECT_EQ(FindPtrOrNull(const_ptr_m, kLookupKey), &ptr_m);
}

TEST(MapUtil, SetOperations) {
  // Set operations
  using Set = std::set<int>;
  Set s;
  EXPECT_TRUE(InsertIfNotPresent(&s, 0));
  EXPECT_FALSE(InsertIfNotPresent(&s, 0));
  EXPECT_TRUE(ContainsKey(s, 0));
}

TEST(MapUtil, ReverseMapWithoutDups) {
  std::map<std::string, int> forward;
  forward["1"] = 1;
  forward["2"] = 2;
  forward["3"] = 3;
  forward["4"] = 4;
  forward["5"] = 5;
  std::map<int, std::string> reverse;
  EXPECT_TRUE(ReverseMap(forward, &reverse));
  EXPECT_THAT(reverse, ElementsAre(
      Pair(1, "1"),
      Pair(2, "2"),
      Pair(3, "3"),
      Pair(4, "4"),
      Pair(5, "5")));
}

TEST(MapUtil, ReverseMapWithDups) {
  std::map<std::string, int> forward;
  forward["1"] = 1;
  forward["2"] = 2;
  forward["3"] = 3;
  forward["4"] = 4;
  forward["5"] = 5;
  forward["6"] = 1;
  forward["7"] = 2;
  std::map<int, std::string> reverse;
  EXPECT_FALSE(ReverseMap(forward, &reverse));
  // There are 5 distinct values in forward.
  EXPECT_THAT(reverse, ElementsAre(
      Pair(1, "6"),
      Pair(2, "7"),
      Pair(3, "3"),
      Pair(4, "4"),
      Pair(5, "5")));
}

TEST(MapUtil, SingleArgumentReverseMapWithoutDups) {
  std::map<std::string, int> forward;
  forward["1"] = 1;
  forward["2"] = 2;
  forward["3"] = 3;
  forward["4"] = 4;
  forward["5"] = 5;
  const std::map<int, std::string> reverse =
      ReverseMap<std::map<int, std::string>>(forward);
  EXPECT_THAT(reverse, ElementsAre(
      Pair(1, "1"),
      Pair(2, "2"),
      Pair(3, "3"),
      Pair(4, "4"),
      Pair(5, "5")));
}

TEST(MapUtil, SingleArgumentReverseMapWithDups) {
  std::map<std::string, int> forward;
  forward["1"] = 1;
  forward["2"] = 2;
  forward["3"] = 3;
  forward["4"] = 4;
  forward["5"] = 5;
  forward["6"] = 1;
  forward["7"] = 2;
  const std::map<int, std::string> reverse =
      ReverseMap<std::map<int, std::string>>(forward);
  // There are 5 distinct values in forward.
  EXPECT_THAT(reverse, ElementsAre(
      Pair(1, "6"),
      Pair(2, "7"),
      Pair(3, "3"),
      Pair(4, "4"),
      Pair(5, "5")));
}

// Wrapper around an int that we can use to test a key without operator<<.
struct Unprintable {
  int a;
  explicit Unprintable(int a) : a(a) {}
  bool operator<(const Unprintable& other) const { return a < other.a; }
  bool operator==(const Unprintable& other) const { return a == other.a; }
};

TEST(MapUtilDeathTest, FindOrDieNoPrint) {
  // Test FindOrDieNoPrint with a value with no operator<<.
  std::map<Unprintable, int> m;
  m[Unprintable(1)] = 8;
  EXPECT_EQ(8, FindOrDieNoPrint(m, Unprintable(1)));
  ASSERT_DEATH(FindOrDieNoPrint(m, Unprintable(2)), "Map key not found");

  // Make sure the non-const reference returning version works.
  FindOrDieNoPrint(m, Unprintable(1)) = 20;
  EXPECT_EQ(20, FindOrDieNoPrint(m, Unprintable(1)));

  // Make sure we can lookup values in a const std::map.
  const std::map<Unprintable, int>& const_m = m;
  EXPECT_EQ(20, FindOrDieNoPrint(const_m, Unprintable(1)));
}

TEST(MapUtilDeathTest, SetInsertOrDieTest) {
  std::set<int> s;
  InsertOrDie(&s, 1);
  EXPECT_TRUE(ContainsKey(s, 1));
  ASSERT_DEATH(InsertOrDie(&s, 1), "duplicate");
}

TEST(MapUtilDeathTest, InsertOrDieNoPrint) {
  std::pair<int, int> key = std::make_pair(1, 1);

  std::map<std::pair<int, int>, int> m;
  InsertOrDieNoPrint(&m, key, 2);
  EXPECT_EQ(m[key], 2);
  ASSERT_DEATH(InsertOrDieNoPrint(&m, key, 3), "duplicate");

  std::set<std::pair<int, int>> s;
  InsertOrDieNoPrint(&s, key);
  EXPECT_TRUE(ContainsKey(s, key));
  ASSERT_DEATH(InsertOrDieNoPrint(&s, key), "duplicate");
}

TEST(MapUtil, InsertKeysFromMap) {
  const std::map<int, int> empty_map;
  std::set<int> keys_as_ints;
  InsertKeysFromMap(empty_map, &keys_as_ints);
  EXPECT_TRUE(keys_as_ints.empty());

  std::set<long> keys_as_longs;  // NOLINT
  InsertKeysFromMap(empty_map, &keys_as_longs);
  EXPECT_TRUE(keys_as_longs.empty());

  const std::pair<const std::string, int> number_names_array[] = {
      std::make_pair("one", 1), std::make_pair("two", 2),
      std::make_pair("three", 3)};
  std::map<std::string, int> number_names_map(
      number_names_array, number_names_array + sizeof number_names_array /
                                                   sizeof *number_names_array);
  absl::btree_set<std::string> names;
  InsertKeysFromMap(number_names_map, &names);
  // No two numbers have the same name, so the container sizes must match.
  EXPECT_EQ(names.size(), number_names_map.size());
  EXPECT_EQ(names.count("one"), 1);
  EXPECT_EQ(names.count("two"), 1);
  EXPECT_EQ(names.count("three"), 1);
}

TEST(MapUtil, AppendKeysFromMap) {
  const std::map<int, int> empty_map;
  std::vector<int> keys_as_ints;
  AppendKeysFromMap(empty_map, &keys_as_ints);
  EXPECT_TRUE(keys_as_ints.empty());

  std::list<long> keys_as_longs;  // NOLINT
  AppendKeysFromMap(empty_map, &keys_as_longs);
  EXPECT_TRUE(keys_as_longs.empty());

  const std::pair<const std::string, int> number_names_array[] = {
      std::make_pair("one", 1), std::make_pair("two", 2),
      std::make_pair("three", 3)};
  std::map<std::string, int> number_names_map(
      number_names_array, number_names_array + sizeof number_names_array /
                                                   sizeof *number_names_array);
  std::deque<std::string> names;
  AppendKeysFromMap(number_names_map, &names);
  // No two numbers have the same name, so the container sizes must match.
  EXPECT_EQ(names.size(), number_names_map.size());
  // The names are appended in the order in which they are found in the
  // map, i.e., lexicographical order.
  EXPECT_EQ(names[0], "one");
  EXPECT_EQ(names[1], "three");
  EXPECT_EQ(names[2], "two");

  // Appending again should double the size of the std::deque
  AppendKeysFromMap(number_names_map, &names);
  EXPECT_EQ(names.size(), 2 * number_names_map.size());
}

// Vector is a special case.
TEST(MapUtil, AppendKeysFromMapIntoVector) {
  const std::map<int, int> empty_map;
  std::vector<int> keys_as_ints;
  AppendKeysFromMap(empty_map, &keys_as_ints);
  EXPECT_TRUE(keys_as_ints.empty());

  std::vector<long> keys_as_longs;  // NOLINT
  AppendKeysFromMap(empty_map, &keys_as_longs);
  EXPECT_TRUE(keys_as_longs.empty());

  const std::pair<const std::string, int> number_names_array[] = {
      std::make_pair("one", 1), std::make_pair("two", 2),
      std::make_pair("three", 3)};
  std::map<std::string, int> number_names_map(
      number_names_array, number_names_array + sizeof number_names_array /
                                                   sizeof *number_names_array);
  std::vector<std::string> names;
  AppendKeysFromMap(number_names_map, &names);
  // No two numbers have the same name, so the container sizes must match.
  EXPECT_EQ(names.size(), number_names_map.size());
  // The names are appended in the order in which they are found in the
  // map, i.e., lexicographical order.
  EXPECT_EQ(names[0], "one");
  EXPECT_EQ(names[1], "three");
  EXPECT_EQ(names[2], "two");

  // Appending again should double the size of the std::deque
  AppendKeysFromMap(number_names_map, &names);
  EXPECT_EQ(names.size(), 2 * number_names_map.size());
}

TEST(MapUtil, AppendValuesFromMap) {
  const std::map<int, int> empty_map;
  std::vector<int> values_as_ints;
  AppendValuesFromMap(empty_map, &values_as_ints);
  EXPECT_TRUE(values_as_ints.empty());

  std::list<long> values_as_longs;  // NOLINT
  AppendValuesFromMap(empty_map, &values_as_longs);
  EXPECT_TRUE(values_as_longs.empty());

  const std::pair<const std::string, int> number_names_array[] = {
      std::make_pair("one", 1), std::make_pair("two", 2),
      std::make_pair("three", 3)};
  std::map<std::string, int> number_names_map(
      number_names_array, number_names_array + sizeof number_names_array /
                                                   sizeof *number_names_array);
  std::deque<int> numbers;
  AppendValuesFromMap(number_names_map, &numbers);
  // No two numbers have the same name, so the container sizes must match.
  EXPECT_EQ(numbers.size(), number_names_map.size());
  // The numbers are appended in the order in which they are found in the
  // map, i.e., lexicographical order.
  EXPECT_EQ(numbers[0], 1);
  EXPECT_EQ(numbers[1], 3);
  EXPECT_EQ(numbers[2], 2);

  // Appending again should double the size of the std::deque
  AppendValuesFromMap(number_names_map, &numbers);
  EXPECT_EQ(numbers.size(), 2 * number_names_map.size());
}

TEST(MapUtil, AppendValuesFromMapIntoVector) {
  const std::map<int, int> empty_map;
  std::vector<int> values_as_ints;
  AppendValuesFromMap(empty_map, &values_as_ints);
  EXPECT_TRUE(values_as_ints.empty());

  std::list<long> values_as_longs;  // NOLINT
  AppendValuesFromMap(empty_map, &values_as_longs);
  EXPECT_TRUE(values_as_longs.empty());

  const std::pair<const std::string, int> number_names_array[] = {
      std::make_pair("one", 1), std::make_pair("two", 2),
      std::make_pair("three", 3)};
  std::map<std::string, int> number_names_map(
      number_names_array, number_names_array + sizeof number_names_array /
                                                   sizeof *number_names_array);
  std::vector<int> numbers;
  AppendValuesFromMap(number_names_map, &numbers);
  // No two numbers have the same name, so the container sizes must match.
  EXPECT_EQ(numbers.size(), number_names_map.size());
  // The numbers are appended in the order in which they are found in the
  // map, i.e., lexicographical order.
  EXPECT_EQ(numbers[0], 1);
  EXPECT_EQ(numbers[1], 3);
  EXPECT_EQ(numbers[2], 2);

  // Appending again should double the size of the std::deque
  AppendValuesFromMap(number_names_map, &numbers);
  EXPECT_EQ(numbers.size(), 2 * number_names_map.size());
}


////////////////////////////////////////////////////////////////////////////////
// Instantiate tests for std::map and std::unordered_map.
////////////////////////////////////////////////////////////////////////////////

// Finish setup for MapType<int, int>
REGISTER_TYPED_TEST_SUITE_P(MapUtilIntIntTest, ValueMapTests,
                            UpdateReturnCopyTest, InsertOrReturnExistingTest,
                            FindOrDieTest, InsertOrDieTest, InsertKeyOrDieTest);
using MapIntIntTypes =
    ::testing::Types<std::map<int, int>, absl::node_hash_map<int, int>,
                     absl::node_hash_map<int, int>>;
INSTANTIATE_TYPED_TEST_SUITE_P(MapUtilTest, MapUtilIntIntTest, MapIntIntTypes);

// Finish setup for MapType<int, int*>
REGISTER_TYPED_TEST_SUITE_P(MapUtilIntIntPtrTest, LookupOrInsertNewTest,
                            InsertAndDeleteExistingTest, FindPtrOrNullTest,
                            EraseKeyReturnValuePtrTest);
using MapIntIntPtrTypes =
    ::testing::Types<std::map<int, int*>, absl::node_hash_map<int, int*>>;
INSTANTIATE_TYPED_TEST_SUITE_P(MapUtilTest, MapUtilIntIntPtrTest,
                               MapIntIntPtrTypes);

// Finish setup for MapType<int, shared_ptr<int> >
REGISTER_TYPED_TEST_SUITE_P(MapUtilIntIntSharedPtrTest, FindPtrOrNullTest,
                            LookupOrInsertNewSharedPtrTest);
using MapIntIntSharedPtrTypes =
    ::testing::Types<std::map<int, std::shared_ptr<int>>,
                     absl::node_hash_map<int, std::shared_ptr<int>>>;
INSTANTIATE_TYPED_TEST_SUITE_P(MapUtilTest, MapUtilIntIntSharedPtrTest,
                               MapIntIntSharedPtrTypes);

REGISTER_TYPED_TEST_SUITE_P(MapUtilIntIntSharedPtrOnlyTest,
                            LookupOrInsertNewSharedPtrSwapTest);
typedef ::testing::Types<std::map<int, std::shared_ptr<int>>,
                         absl::node_hash_map<int, std::shared_ptr<int>>>
    MapIntIntSharedPtrOnlyTypes;
INSTANTIATE_TYPED_TEST_SUITE_P(MapUtilTest, MapUtilIntIntSharedPtrOnlyTest,
                               MapIntIntSharedPtrOnlyTypes);

using AssociateEraseMapTypes =
    ::testing::Types<std::map<std::string, int>,
                     absl::node_hash_map<std::string, int>>;

template <class UnordMap>
class AssociativeEraseIfTest : public ::testing::Test {};
TYPED_TEST_SUITE_P(AssociativeEraseIfTest);

TYPED_TEST_P(AssociativeEraseIfTest, Basic) {
  using ValueType = std::pair<const std::string, int>;
  TypeParam m;
  m["a"] = 1;
  m["b"] = 2;
  m["c"] = 3;
  m["d"] = 4;

  // Test that none of the elements are removed when the predicate always
  // returns false.
  struct FalseFunc {
    bool operator()(const ValueType& unused) const { return 0; }
  };
  AssociativeEraseIf(&m, FalseFunc());
  EXPECT_THAT(m, UnorderedElementsAre(Pair("a", 1), Pair("b", 2), Pair("c", 3),
                                      Pair("d", 4)));

  // Test removing a single element.
  struct KeyEqualsA {
    bool operator()(const ValueType& pair) const {
      return pair.first == "a";
    }
  };
  AssociativeEraseIf(&m, KeyEqualsA());
  EXPECT_THAT(m,
              UnorderedElementsAre(Pair("b", 2), Pair("c", 3), Pair("d", 4)));

  // Put the element back and test removing a couple elements,
  m["a"] = 1;
  struct ValueGreaterThanTwo {
    bool operator()(const ValueType& pair) const {
      return pair.second > 2;
    }
  };
  AssociativeEraseIf(&m, ValueGreaterThanTwo());
  EXPECT_THAT(m, UnorderedElementsAre(Pair("a", 1), Pair("b", 2)));

  // Put the elements back and test removing all of them.
  m["c"] = 3;
  m["d"] = 4;
  struct TrueFunc {
    bool operator()(const ValueType& unused) const { return 1; }
  };
  AssociativeEraseIf(&m, TrueFunc());
  EXPECT_THAT(m, IsEmpty());
}
REGISTER_TYPED_TEST_SUITE_P(AssociativeEraseIfTest, Basic);

INSTANTIATE_TYPED_TEST_SUITE_P(MapUtilTest, AssociativeEraseIfTest,
                               AssociateEraseMapTypes);

TEST(MapUtil, InsertKeyOrDie_SmartPtrTest) {
  absl::node_hash_map<int, std::unique_ptr<int>> m;
  m[1].reset(new int(10));
  m[2].reset(new int(20));

  EXPECT_THAT(m, UnorderedElementsAre(Pair(1, ::testing::Pointee(10)),
                                      Pair(2, ::testing::Pointee(20))));
  InsertKeyOrDie(&m, 3).reset(new int(30));
  EXPECT_THAT(m, UnorderedElementsAre(Pair(1, ::testing::Pointee(10)),
                                      Pair(2, ::testing::Pointee(20)),
                                      Pair(3, ::testing::Pointee(30))));
}

TEST(MapUtil, EraseKeyReturnValuePtr_SmartPtrTest) {
  std::map<int, std::unique_ptr<int>> m;
  m[1] = std::unique_ptr<int>(new int(10));
  m[2] = std::unique_ptr<int>(new int(20));

  std::unique_ptr<int> val1 = EraseKeyReturnValuePtr(&m, 1);
  EXPECT_EQ(10, *val1);
  EXPECT_THAT(m, ElementsAre(Pair(2, ::testing::Pointee(20))));
  auto val2 = EraseKeyReturnValuePtr(&m, 2);
  EXPECT_EQ(20, *val2);
}

TEST(MapUtil, LookupOrInsertNewVariadicTest) {
  struct TwoArg {
    TwoArg(int one_in, int two_in) : one(one_in), two(two_in) {}
    int one;
    int two;
  };

  std::map<int, std::unique_ptr<TwoArg>> m;
  TwoArg *val = LookupOrInsertNew(&m, 1, 100, 200).get();
  EXPECT_EQ(100, val->one);
  EXPECT_EQ(200, val->two);
}

}  // namespace googlesql_base
