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
#include "googlesql/base/flat_set.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "googlesql/base/logging.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/inlined_vector.h"
#include "absl/hash/hash_testing.h"
#include "absl/memory/memory.h"
#include "absl/random/random.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "googlesql/base/stl_util.h"

namespace googlesql_base {
namespace {

using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Pair;

// This ensures that we don't depend on operators other than <.
struct OnlyLT {
  int i;
  OnlyLT() {}
  explicit OnlyLT(int new_i) : i(new_i) {}
  bool operator<(const OnlyLT& other) const { return i < other.i; }
};

struct ReverseCmp {
  bool operator()(OnlyLT x, OnlyLT y) const { return x.i > y.i; }
};

// Get an integer from a few supported types. Used as a test helper, as well as
// for a transparent comparator.
int Get(const OnlyLT& x) { return x.i; }
int Get(const std::unique_ptr<int>& x) { return *x; }
int Get(const std::shared_ptr<int>& x) { return *x; }
template <typename T>
int Get(T t) {
  return t;
}

// Allows comparing int to std::unique_ptr<int>.
struct TransparentCmp {
  template <typename T, typename U>
  bool operator()(const T& t, const U& u) const {
    return Get(t) < Get(u);
  }

  using is_transparent = void;
};

struct NonTransparent {
  template <typename T, typename U>
  bool operator()(const T& t, const U& u) const {
    // Treating all comparators as transparent can cause inefficiencies (see
    // N3657 C++ proposal). Test that for comparators without 'is_transparent'
    // typedef (like this one), we do not attempt heterogeneous lookup.
    EXPECT_TRUE((std::is_same<T, U>()));
    return t < u;
  }
};

std::vector<std::unique_ptr<int>> UniquePtrs(const std::vector<int>& v) {
  std::vector<std::unique_ptr<int>> res;
  for (int i : v) {
    res.push_back(std::make_unique<int>(i));
  }
  return res;
}

// Test expectation methods.
template <typename It>
void ExpectElements(It begin, It end, const std::vector<int>& expected) {
  std::vector<int> actual;
  for (; begin != end; ++begin) {
    actual.push_back(Get(*begin));
  }

  EXPECT_THAT(actual, ElementsAreArray(expected));
}

template <typename Set>
void ExpectElements(const Set& set, const std::vector<int>& expected) {
  ExpectElements(set.begin(), set.end(), expected);
}

TEST(FlatSetTest, DefaultIsSane) {
  flat_set<OnlyLT> s;

  EXPECT_TRUE(s.empty());
  EXPECT_EQ(0, s.size());
  EXPECT_EQ(std::vector<int>().max_size(), s.max_size());
  EXPECT_EQ(s.end(), s.find(OnlyLT()));
  EXPECT_EQ(s.end(), s.lower_bound(OnlyLT()));
  EXPECT_EQ(s.end(), s.upper_bound(OnlyLT()));
}

TEST(FlatSetTest, EmplaceDoesNotConfuseConstructors) {
  flat_set<std::vector<int>> s;
  s.emplace(1, 10);
  s.emplace_hint(s.begin(), 1, 8);
  EXPECT_THAT(s, ElementsAre(ElementsAre(8), ElementsAre(10)));
}

TEST(FlatSetTest, MovableNoExcept) {
  EXPECT_TRUE(std::is_nothrow_move_constructible<flat_set<int>>::value);
  EXPECT_TRUE(std::is_nothrow_move_assignable<flat_set<int>>::value);
}

TEST(FlatSetTest, RangeAndListConstruction) {
  std::vector<OnlyLT> v{OnlyLT(4), OnlyLT(9), OnlyLT(1), OnlyLT(17)};
  std::vector<int> expected = {1, 4, 9, 17};

  ExpectElements(flat_set<OnlyLT>(v.begin(), v.end()), expected);
  ExpectElements(flat_set<OnlyLT>(v.rbegin(), v.rend()), expected);
  ExpectElements(
      flat_set<OnlyLT>({OnlyLT(4), OnlyLT(9), OnlyLT(1), OnlyLT(17)}),
      expected);
}

TEST(FlatSetTest, ContainerMoveConstruction) {
  std::vector<int> v = {4, 9, 1, 4};
  std::vector<int> expected = {1, 4, 9};

  ExpectElements(flat_set<std::unique_ptr<int>, TransparentCmp>(UniquePtrs(v)),
                 expected);
}

TEST(FlatSetTest, ExplicitComparator) {
  flat_set<OnlyLT, ReverseCmp> s = {OnlyLT(4), OnlyLT(1), OnlyLT(17)};
  ExpectElements(s, {17, 4, 1});
  s.insert(OnlyLT(5));
  ExpectElements(s, {17, 5, 4, 1});
}

TEST(FlatSetTest, Iterators) {
  flat_set<OnlyLT> s = {OnlyLT(4), OnlyLT(9), OnlyLT(1), OnlyLT(17)};
  ExpectElements(s.begin(), s.end(), {1, 4, 9, 17});
  ExpectElements(s.cbegin(), s.cend(), {1, 4, 9, 17});
  ExpectElements(s.rbegin(), s.rend(), {17, 9, 4, 1});
  ExpectElements(s.crbegin(), s.crend(), {17, 9, 4, 1});

  const flat_set<OnlyLT>& c = s;
  ExpectElements(c.begin(), c.end(), {1, 4, 9, 17});
  ExpectElements(c.rbegin(), c.rend(), {17, 9, 4, 1});
}

TEST(FlatSetTest, InsertHandlesDuplicatesCorrectly) {
  flat_set<OnlyLT> s;
  auto p = s.insert(OnlyLT(10));
  EXPECT_THAT(p, Pair(s.begin(), true));
  p = s.insert(OnlyLT(10));
  EXPECT_THAT(p, Pair(s.begin(), false));
  p = s.insert(OnlyLT(5));
  EXPECT_THAT(p, Pair(s.begin(), true));
  p = s.insert(OnlyLT(5));
  EXPECT_THAT(p, Pair(s.begin(), false));
}

TEST(FlatSetTest, InsertMaintainsSortedOrder) {
  flat_set<OnlyLT> s;
  OnlyLT lt5{5};

  // Test both lvalue & rvalue insert.
  s.insert(OnlyLT(10));
  s.insert(lt5);
  EXPECT_EQ(2, s.size());
  EXPECT_EQ(2, s.end() - s.begin());
  EXPECT_TRUE(std::is_sorted(s.begin(), s.end()));

  OnlyLT lt100{100};
  s.insert(lt100);
  s.insert(OnlyLT(1));
  EXPECT_EQ(4, s.size());
  EXPECT_TRUE(std::is_sorted(s.begin(), s.end()));
}

TEST(FlatSetTest, InsertInitializerList) {
  flat_set<OnlyLT> s;
  s.insert({OnlyLT(7), OnlyLT(3)});
  ExpectElements(s, {3, 7});
  s.insert({OnlyLT(4), OnlyLT(2), OnlyLT(6)});
  ExpectElements(s, {2, 3, 4, 6, 7});
}

TEST(FlatSetTest, InsertWithHintWorks) {
  flat_set<OnlyLT> s;
  // Empty, insert with begin hint:
  s.insert(s.begin(), OnlyLT(1));
  EXPECT_EQ(1, s.size());
  s.clear();
  // Empty, insert with end hint:
  OnlyLT lt1{1};
  s.insert(s.end(), lt1);
  EXPECT_EQ(1, s.size());
  // Insert with correct hint:
  s.insert(s.end(), OnlyLT(2));
  EXPECT_EQ(2, s.size());
  // Insert value already present, ensure no duplicate added:
  auto it = s.insert(s.end(), OnlyLT(2));
  EXPECT_EQ(2, s.size());
  // Verify that the iterator returned is accurate.
  EXPECT_TRUE(s.find(OnlyLT(2)) == it);
  s.erase(OnlyLT(2));
  EXPECT_EQ(1, s.size());
  // Insertion in the middle of two other elements.
  it = s.insert(OnlyLT(3)).first;
  s.insert(it, OnlyLT(2));
  EXPECT_EQ(3, s.size());
  EXPECT_TRUE(std::is_sorted(s.begin(), s.end()));
}

TEST(FlatSetTest, InsertWithBadHint) {
  flat_set<OnlyLT> s = {OnlyLT(1), OnlyLT(9)};
  // Bad hint (too small), should still insert:
  auto it = s.insert(s.begin(), OnlyLT(2));
  EXPECT_EQ(it, s.begin() + 1);
  ExpectElements(s, {1, 2, 9});
  // Bad hint, too large this time:
  it = s.insert(s.begin() + 2, OnlyLT(0));
  EXPECT_EQ(it, s.begin());
  ExpectElements(s, {0, 1, 2, 9});
}

TEST(FlatSetTest, IteratorInsertWorks) {
  flat_set<OnlyLT> s;
  std::vector<OnlyLT> v = {OnlyLT(1), OnlyLT(3), OnlyLT(2), OnlyLT(2)};
  s.insert(v.begin(), v.end());
  ExpectElements(s, {1, 2, 3});
}

TEST(FlatSetTest, InsertRangeWorks) {
  flat_set<OnlyLT> s;
  s.insert(OnlyLT(1));
  s.insert(OnlyLT(6));
  std::vector<OnlyLT> to_insert;
  // Duplicates...
  for (int i = 0; i < 6; ++i) {
    to_insert.push_back(OnlyLT(i));
    to_insert.push_back(OnlyLT(i));
  }
  // And unsorted!
  std::shuffle(to_insert.begin(), to_insert.end(), absl::BitGen());
  s.insert(to_insert.begin(), to_insert.end());
  EXPECT_EQ(7, s.size());
  for (int i = 0; i <= 6; ++i) {
    EXPECT_EQ(1, s.count(OnlyLT(i))) << "Missing " << i;
  }
}

TEST(FlatSetTest, Emplace) {
  flat_set<OnlyLT> s;
  auto result = s.emplace(1);
  EXPECT_EQ(result.first, s.begin());
  EXPECT_TRUE(result.second);
  result = s.emplace(9);
  EXPECT_EQ(result.first, s.begin() + 1);
  EXPECT_TRUE(result.second);
  ExpectElements(s, {1, 9});

  result = s.emplace(9);
  EXPECT_EQ(result.first, s.begin() + 1);
  EXPECT_FALSE(result.second);
  ExpectElements(s, {1, 9});

  // Basic test of emplace_hint; the more extensive test for insert with hint
  // (which emplace_hint calls) exists above.
  auto it = s.emplace_hint(s.end(), 10);
  EXPECT_EQ(it, s.begin() + 2);
  ExpectElements(s, {1, 9, 10});
  // Bad hint, still inserts.
  it = s.emplace_hint(s.end(), 0);
  EXPECT_EQ(it, s.begin());
  ExpectElements(s, {0, 1, 9, 10});
  // Correct hint in the middle:
  it = s.emplace_hint(s.begin() + 2, 7);
  EXPECT_EQ(it, s.begin() + 2);
  ExpectElements(s, {0, 1, 7, 9, 10});
  // Element exists.
  it = s.emplace_hint(s.begin() + 3, 7);
  EXPECT_EQ(it, s.begin() + 2);
  ExpectElements(s, {0, 1, 7, 9, 10});
}

TEST(FlatSetTest, EraseWorks) {
  flat_set<OnlyLT> s = {OnlyLT(4), OnlyLT(9), OnlyLT(1), OnlyLT(17)};
  EXPECT_EQ(0, s.erase(OnlyLT(0)));
  EXPECT_EQ(1, s.erase(OnlyLT(4)));
  ExpectElements(s, {1, 9, 17});

  auto it = s.erase(s.begin() + 1);
  EXPECT_EQ(it, s.begin() + 1);
  ExpectElements(s, {1, 17});
}

TEST(FlatSetTest, RangeEraseWorks) {
  flat_set<OnlyLT> s = {OnlyLT(4), OnlyLT(9), OnlyLT(1), OnlyLT(17)};
  auto it = s.erase(s.begin() + 1, s.begin() + 3);
  EXPECT_EQ(it, s.begin() + 1);
  ExpectElements(s, {1, 17});
  // Empty range.
  it = s.erase(s.begin() + 1, s.begin() + 1);
  EXPECT_EQ(it, s.begin() + 1);
  ExpectElements(s, {1, 17});
}

TEST(FlatSetTest, RemoveIfWorks) {
  flat_set<OnlyLT> s = {OnlyLT(4), OnlyLT(9), OnlyLT(2), OnlyLT(17)};
  size_t n_removed = s.remove_if([](OnlyLT x) { return x.i % 2 == 0; });
  EXPECT_EQ(n_removed, 2);
  ExpectElements(s, {9, 17});
}

TEST(FlatSetTest, FindWorks) {
  OnlyLT good = OnlyLT(10);
  OnlyLT bad = OnlyLT(13);
  flat_set<OnlyLT> s = {good};
  const auto& c = s;
  EXPECT_EQ(s.begin(), s.find(good));
  EXPECT_EQ(s.end(), s.find(bad));
  EXPECT_EQ(c.begin(), c.find(good));
  EXPECT_EQ(c.cend(), c.find(bad));
}

void TestConstness(const OnlyLT&) {}
void TestConstness(OnlyLT&) { FAIL() << "Expected const reference"; }  // NOLINT

TEST(FlatSetTest, ValueTypeIsConst) {
  flat_set<OnlyLT> s = {OnlyLT(1), OnlyLT(3), OnlyLT(5)};
  TestConstness(*s.begin());
  TestConstness(*s.rbegin());
  TestConstness(*s.find(OnlyLT(3)));
  TestConstness(*s.lower_bound(OnlyLT(4)));
}

// Helper method to cover const / non-const set.
template <typename Set>
void TestBinarySearches() {
  Set s = {OnlyLT(1), OnlyLT(3), OnlyLT(5)};
  EXPECT_EQ(s.lower_bound(OnlyLT(3)), s.begin() + 1);
  EXPECT_EQ(s.upper_bound(OnlyLT(3)), s.begin() + 2);
  EXPECT_EQ(s.lower_bound(OnlyLT(4)), s.begin() + 2);
  EXPECT_EQ(s.upper_bound(OnlyLT(4)), s.begin() + 2);

  EXPECT_THAT(s.equal_range(OnlyLT(3)), Pair(s.begin() + 1, s.begin() + 2));
  EXPECT_THAT(s.equal_range(OnlyLT(4)), Pair(s.begin() + 2, s.begin() + 2));
}

TEST(FlatSetTest, BinarySearchesWork) {
  TestBinarySearches<flat_set<OnlyLT>>();
  TestBinarySearches<const flat_set<OnlyLT>>();
}

TEST(FlatSetTest, CopyAndAssignmentWork) {
  flat_set<OnlyLT> s;
  s.insert(OnlyLT(1));
  ExpectElements(s, {1});
  flat_set<OnlyLT> s2(s);
  ExpectElements(s2, {1});
  flat_set<OnlyLT> s3;
  s3 = s;
  EXPECT_EQ(1, s3.size());

  // Assignment from initializer list.
  s = {OnlyLT(7)};
  ExpectElements(s, {7});
}

TEST(FlatSetTest, CountWorks) {
  flat_set<OnlyLT> s;
  OnlyLT v(1);
  EXPECT_EQ(0, s.count(v));
  s.insert(v);
  EXPECT_EQ(1, s.count(v));
  s.insert(v);
  EXPECT_EQ(1, s.count(v));
}

TEST(FlatSetTest, ContainsWorks) {
  flat_set<OnlyLT> s;
  OnlyLT v(1);
  EXPECT_FALSE(s.contains(v));
  s.insert(v);
  EXPECT_TRUE(s.contains(v));
  s.insert(v);
  EXPECT_TRUE(s.contains(v));
}

TEST(FlatSetTest, InstantiatesWithInlinedVector) {
  flat_set<OnlyLT, std::less<OnlyLT>, absl::InlinedVector<OnlyLT, 7>> m;
  m.insert(OnlyLT(1));
  EXPECT_TRUE(m.size());
}

TEST(FlatSetTest, RelationalOperatorsWork) {
  OnlyLT v1(1);
  OnlyLT v2(2);

  flat_set<OnlyLT> s1, s2;
  EXPECT_FALSE(s1 < s2);
  EXPECT_FALSE(s1 > s2);
  EXPECT_TRUE(s1 <= s2);
  EXPECT_TRUE(s1 >= s2);

  s2.insert(v1);
  EXPECT_TRUE(s1 < s2);
  EXPECT_FALSE(s1 > s2);
  EXPECT_TRUE(s1 <= s2);
  EXPECT_FALSE(s1 >= s2);

  s1.insert(v2);
  EXPECT_FALSE(s1 < s2);
  EXPECT_TRUE(s1 > s2);
  EXPECT_FALSE(s1 <= s2);
  EXPECT_TRUE(s1 >= s2);

  s1.insert(v1);
  s2.insert(v2);
  EXPECT_FALSE(s1 < s2);
  EXPECT_FALSE(s1 > s2);
  EXPECT_TRUE(s1 <= s2);
  EXPECT_TRUE(s1 >= s2);
}

TEST(FlatSetTest, ComparisonsWork) {
  flat_set<int> s1, s2;
  EXPECT_FALSE(s1 != s2);
  EXPECT_TRUE(s1 == s2);

  s2.insert(1);
  EXPECT_TRUE(s1 != s2);
  EXPECT_FALSE(s1 == s2);

  s1.insert(2);
  EXPECT_TRUE(s1 != s2);
  EXPECT_FALSE(s1 == s2);

  s1.insert(1);
  s2.insert(2);
  EXPECT_FALSE(s1 != s2);
  EXPECT_TRUE(s1 == s2);
}

TEST(FlatSetTest, SwapWorks) {
  flat_set<OnlyLT> s1 = {OnlyLT(1)};
  flat_set<OnlyLT> s2 = {OnlyLT(2)};
  s1.swap(s2);
  ExpectElements(s1, {2});
  ExpectElements(s2, {1});
  using std::swap;
  swap(s1, s2);
  ExpectElements(s1, {1});
  ExpectElements(s2, {2});
}

TEST(FlatSetTest, VectorExtensions) {
  flat_set<OnlyLT> s;
  EXPECT_EQ(0, s.capacity());
  for (int i : {1, 2, 3, 4, 5}) {
    s.insert(OnlyLT(i));
  }
  EXPECT_GE(s.capacity(), 5);
  s.reserve(1000);
  EXPECT_GE(s.capacity(), 1000);

  // shrink_to_fit is non-binding, but - given that one motivation for flat_set
  // is memory optimization - we would really like it to work. If we have a
  // standard library which does not honour shrink_to_fit, we should reimplement
  // it ourselves in flat_set.
  s.shrink_to_fit();
  EXPECT_EQ(s.capacity(), 5);
}

// Tests for transparent comparator (a.k.a. heterogeneous lookup).

// Helper method to cover const & non-const overloads (depending on the template
// argument).
template <typename Set>
void TestHeterogeneousLookup() {
  Set s = {OnlyLT(3), OnlyLT(1), OnlyLT(5)};
  EXPECT_EQ(s.begin() + 1, s.find(3));
  EXPECT_EQ(s.begin() + 1, s.find(3.14));
  EXPECT_EQ(s.end(), s.find(4));

  EXPECT_EQ(1, s.count(3));
  EXPECT_EQ(0, s.count(4));
  EXPECT_TRUE(s.contains(3));
  EXPECT_FALSE(s.contains(4));

  EXPECT_EQ(s.lower_bound(3), s.begin() + 1);
  EXPECT_EQ(s.upper_bound(3), s.begin() + 2);
  EXPECT_EQ(s.lower_bound(4), s.begin() + 2);
  EXPECT_EQ(s.upper_bound(4), s.begin() + 2);

  EXPECT_THAT(s.equal_range(3), Pair(s.begin() + 1, s.begin() + 2));
  EXPECT_THAT(s.equal_range(4), Pair(s.begin() + 2, s.begin() + 2));
}

TEST(FlatSetTest, HeterogeneousLookup) {
  TestHeterogeneousLookup<flat_set<OnlyLT, TransparentCmp>>();
  TestHeterogeneousLookup<const flat_set<OnlyLT, TransparentCmp>>();
}

TEST(FlatSetTest, NoHeterogeneousLookupWithoutTypedef) {
  flat_set<std::string, NonTransparent> s = {"hello", "world"};
  EXPECT_EQ(s.end(), s.find("blah"));
  EXPECT_EQ(s.begin(), s.lower_bound("hello"));
  EXPECT_EQ(1, s.count("world"));
  EXPECT_TRUE(s.contains("world"));
}

// Tests for noncopyable elements.

TEST(FlatSetTest, NoncopyableCreation) {
  // Using transparent comparator just for testing convenience.
  flat_set<std::unique_ptr<int>, TransparentCmp> s;
  EXPECT_TRUE(s.empty());
}

TEST(FlatSetTest, NoncopyableFromRange) {
  // Creation from iterators.
  std::vector<std::unique_ptr<int>> v = UniquePtrs({42, 7, 7});
  flat_set<std::unique_ptr<int>, TransparentCmp> s(
      std::make_move_iterator(v.begin()), std::make_move_iterator(v.end()));
  ExpectElements(s, {7, 42});

  // insert range
  v = UniquePtrs({10, 10, 100});
  s.insert(std::make_move_iterator(v.begin()),
           std::make_move_iterator(v.end()));
  ExpectElements(s, {7, 10, 42, 100});
}

TEST(FlatSetTest, NonCopyableMovesAndAssignments) {
  flat_set<std::unique_ptr<int>, TransparentCmp> s1;
  s1.insert(std::make_unique<int>(1));
  s1.insert(std::make_unique<int>(0));

  flat_set<std::unique_ptr<int>, TransparentCmp> s2 = std::move(s1);
  ExpectElements(s1, {});  // NOLINT misc-use-after-move
  ExpectElements(s2, {0, 1});
  s1 = std::move(s2);
  ExpectElements(s1, {0, 1});
  ExpectElements(s2, {});  // NOLINT misc-use-after-move
  // Swaps.
  s1.swap(s2);
  ExpectElements(s1, {});
  ExpectElements(s2, {0, 1});
  using std::swap;
  swap(s1, s2);
  ExpectElements(s1, {0, 1});
  ExpectElements(s2, {});
}

TEST(FlatSetTest, NoncopyableInsertAndEmplace) {
  flat_set<std::unique_ptr<int>, TransparentCmp> s;
  s.insert(std::make_unique<int>(0));
  s.insert(std::make_unique<int>(5));
  s.insert(std::make_unique<int>(5));
  s.emplace(new int(7));
  s.emplace(new int(3));
  // We do not leak memory, even if emplace fails (note that this is not
  // generally guaranteed by std::set/map).
  s.emplace(new int(3));
  ExpectElements(s, {0, 3, 5, 7});

  // Insert with hint:
  s.insert(s.begin() + 1, std::make_unique<int>(2));
  s.insert(s.begin() + 2, std::make_unique<int>(2));  // already exists
  s.insert(s.begin(), std::make_unique<int>(8));      // incorrect hint
  ExpectElements(s, {0, 2, 3, 5, 7, 8});
  // emplace_hint
  s.emplace_hint(s.begin() + 1, std::make_unique<int>(1));
  s.emplace_hint(s.begin() + 2, std::make_unique<int>(1));  // already exists
  s.emplace_hint(s.begin(), std::make_unique<int>(9));      // incorrect hint
  ExpectElements(s, {0, 1, 2, 3, 5, 7, 8, 9});
}

// Tests for stateful comparator.

TEST(FlatSetTest, StatefulComparator) {
  // Lambda has no default constructor, so this test guarantees that we do not
  // accidentally try to default-construct a comparator.
  int calls = 0;
  auto cmp = [&calls](int x, int y) {
    ++calls;
    return x < y;
  };
  using Cmp = decltype(cmp);

  // Create flat_sets using all constructors, with the same stateful comparator.
  flat_set<int, Cmp> s1(cmp);
  flat_set<int, Cmp> s2(s1.begin(), s1.begin(), cmp);
  flat_set<int, Cmp> s3({}, cmp);
  flat_set<int, Cmp> s4 = s1;
  for (auto* s : {&s1, &s2, &s3, &s4}) {
    s->insert(0);
    EXPECT_EQ(calls, 0);
    s->insert(1);
    EXPECT_GT(calls, 0);
    calls = 0;
    EXPECT_EQ(1, s->erase(0));
    EXPECT_GT(calls, 0);
    calls = 0;
  }
}

// Test that the moved-from set is in valid state, even if moved-from comparator
// is not (as is the case for std::function).
TEST(FlatSetTest, MoveCopyAssignStatefulComparator) {
  using Cmp = std::function<bool(int, int)>;
  flat_set<int, Cmp> s1(std::greater<int>{});
  flat_set<int, Cmp> s2(std::greater<int>{});
  flat_set<int, Cmp> s3(s1);
  flat_set<int, Cmp> s4(std::move(s1));
  flat_set<int, Cmp> s5, s6;
  s5 = s2;
  s6 = std::move(s2);
  // All the sets use the 'greater' comparator (though in case of moved-from
  // sets, this is an implementation detail; we only guarantee a valid state).
  for (auto* s :
       {&s1, &s2, &s3, &s4, &s5, &s6}) {  // NOLINT misc-use-after-move
    s->insert({2, 3});
    ExpectElements(*s, {3, 2});
  }
}

TEST(FlatSetTest, SortedUniqueContainerConstructor) {
  // Using unique_ptrs guarantees we do not incur additional copies.
  flat_set<std::unique_ptr<int>, TransparentCmp> s1(sorted_unique_container,
                                                    UniquePtrs({1, 2, 3}));
  flat_set<std::unique_ptr<int>, TransparentCmp> s2(
      sorted_unique_container, TransparentCmp(), UniquePtrs({1, 2, 3}));
  ExpectElements(s1, {1, 2, 3});
  ExpectElements(s2, {1, 2, 3});

  // Try a more complex vector constructor.
  std::vector<int> v = {3, 2, 1};
  flat_set<int, std::greater<int>> s3(sorted_unique_container, v.begin(),
                                      v.end(), std::allocator<int>());
  flat_set<int, std::greater<int>> s4(sorted_unique_container,
                                      std::greater<int>(), v.begin(), v.end(),
                                      std::allocator<int>());
  ExpectElements(s3, {3, 2, 1});
  ExpectElements(s4, {3, 2, 1});
}

TEST(FlatSetTest, SortedUniqueContainerDeathTest) {
  std::vector<int> ordered = {1, 2, 3};
  std::vector<int> reversed = {3, 2, 1};
  std::vector<int> repeated = {7, 7};

  flat_set<int> s1(sorted_unique_container, ordered);
  for (const auto& v : {reversed, repeated}) {
    ASSERT_DEBUG_DEATH(flat_set<int>(sorted_unique_container, v),
                       "[aA]ssertion.*failed");
  }

  // Non-default comparator.
  flat_set<int, std::greater<int>> s2(sorted_unique_container, reversed);
  flat_set<int, std::greater<int>> s3(sorted_unique_container,
                                      std::greater<int>(), reversed);
  for (const auto& v : {ordered, repeated}) {
    ASSERT_DEBUG_DEATH(
        (flat_set<int, std::greater<int>>(sorted_unique_container, v)),
        "[aA]ssertion.*failed");
    ASSERT_DEBUG_DEATH((flat_set<int, std::greater<int>>(
                           sorted_unique_container, std::greater<int>(), v)),
                       "[aA]ssertion.*failed");
  }
}

TEST(FlatSetTest, SortedUniqueStdFunctionComparator) {
  flat_set<std::unique_ptr<int>,
           std::function<bool(const std::unique_ptr<int>&,
                              const std::unique_ptr<int>&)>>
      s(sorted_unique_container, TransparentCmp(), UniquePtrs({1, 2, 3}));
  ExpectElements(s, {1, 2, 3});
}

TEST(FlatSetTest, SortedUniqueContainerSpan) {
  int array[] = {1, 2, 3};
  flat_set<int, std::less<int>, absl::Span<int>> s(sorted_unique_container,
                                                   array, 3);
  ExpectElements(s, {1, 2, 3});
  EXPECT_EQ(&*s.begin(), array);
}

TEST(FlatSetTest, StdArrayRep) {
  // shared_ptr allows easily testing move behavior of flat_set due to its
  // well-defined move semantics.
  std::array<std::shared_ptr<int>, 2> array = {
      {std::make_shared<int>(1), std::make_shared<int>(2)}};
  flat_set<int, TransparentCmp, std::array<std::shared_ptr<int>, 2>> s(
      sorted_unique_container, std::move(array));

  ExpectElements(s, {1, 2});

  // For std::array, we copy the Rep instead of moving it. See
  // internal_flat::Impl for rationale.
  auto s1(std::move(s));
  ExpectElements(s, {1, 2});  // NOLINT misc-use-after-move
  ExpectElements(s1, {1, 2});
  s1 = std::move(s);
  ExpectElements(s, {1, 2});  // NOLINT misc-use-after-move
  ExpectElements(s1, {1, 2});
  EXPECT_EQ(*s.begin(), *s1.begin());
}

TEST(FlatSetTest, StdArrayRepTest) {
  // Do not allow default construction where it doesn't make sense.
  ASSERT_DEBUG_DEATH((flat_set<int, std::less<int>, std::array<int, 7>>()),
                     "[aA]ssertion.*failed");
}

TEST(FlatSetTest, Hash) {
  using S = flat_set<int>;
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      {S{},                                                   //
       S{0}, S{1}, S{2},                                      //
       S{0, 1}, S{1, 0}, S{0, 2}, S{2, 0}, S{1, 2}, S{2, 1},  //
       S{0, 1, 2}}));
}

}  // namespace
}  // namespace googlesql_base
