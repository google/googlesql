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

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/macros.h"
#include "googlesql/base/stl_util.h"

namespace googlesql_base {
namespace {

using testing::ElementsAre;

// This templatized helper can subclass any class and count how many times
// instances have been constructed or destructed.  It is used to make sure
// that deletes are actually occurring.

template <typename T>
class InstanceCounter: public T {
 public:
  InstanceCounter<T>() : T() {
    ++instance_count;
  }
  ~InstanceCounter<T>() {
    --instance_count;
  }
  static int instance_count;
};
template <typename T>
int InstanceCounter<T>::instance_count = 0;

TEST(STLDeleteElementsTest, STLDeleteElements) {
  std::vector<InstanceCounter<std::string> *> v;
  v.push_back(new InstanceCounter<std::string>());
  v.push_back(new InstanceCounter<std::string>());
  v.push_back(new InstanceCounter<std::string>());
  EXPECT_EQ(3, InstanceCounter<std::string>::instance_count);
  STLDeleteElements(&v);
  EXPECT_EQ(0, InstanceCounter<std::string>::instance_count);
  EXPECT_EQ(0, v.size());

  // Deleting nullptrs to containers is ok.
  std::vector<InstanceCounter<std::string> *> *p = nullptr;
  STLDeleteElements(p);
}

TEST(STLDeleteElementsTest, ElementDeleter) {
  std::vector<InstanceCounter<std::string> *> v;
  { // Create a new scope
    ElementDeleter d(&v);
    v.push_back(new InstanceCounter<std::string>());
    v.push_back(new InstanceCounter<std::string>());
    v.push_back(new InstanceCounter<std::string>());
    EXPECT_EQ(3, InstanceCounter<std::string>::instance_count);
  }
  EXPECT_EQ(0, InstanceCounter<std::string>::instance_count);
  EXPECT_EQ(0, v.size());
}

TEST(STLSetDifference, SimpleSet) {
  std::set<int> a, b, c;
  a.insert(1);
  a.insert(3);
  b = a;
  a.insert(2);
  STLSetDifference(a, b, &c);
  ASSERT_EQ(1, c.size());
  ASSERT_TRUE(c.count(2));
  std::set<int> d = STLSetDifference(a, b);
  ASSERT_TRUE(c == d);
}

TEST(STLDeleteContainerPairSecondPointers, Simple) {
  std::map<int, InstanceCounter<std::string> *> m;
  m.emplace(1, new InstanceCounter<std::string>());
  m.emplace(2, new InstanceCounter<std::string>());
  m.emplace(3, new InstanceCounter<std::string>());
  EXPECT_EQ(3, InstanceCounter<std::string>::instance_count);
  STLDeleteContainerPairSecondPointers(m.begin(), m.end());
  EXPECT_EQ(0, InstanceCounter<std::string>::instance_count);
}

// We only test that vectors work for differencing since
// it's all identical for unions and intersections.
TEST(STLSetDifference, SimpleVector) {
  std::vector<int> a, b, c;
  a.push_back(1);
  a.push_back(2);
  a.push_back(3);
  b.push_back(1);
  b.push_back(3);
  STLSetDifference(a, b, &c);
  ASSERT_EQ(1, c.size());
  ASSERT_EQ(c[0], 2);
  std::vector<int> d = STLSetDifference(a, b);
  ASSERT_TRUE(c == d);
}

TEST(STLSetDifference, MultipleTypes) {
  // Same as above, but a is a set and b is a vector instead of set.
  std::set<int> a, c;
  std::vector<int> b;
  a.insert(1);
  a.insert(2);
  a.insert(3);
  b.push_back(1);
  b.push_back(3);
  STLSetDifference(a, b, &c);
  ASSERT_EQ(1, c.size());
  ASSERT_TRUE(c.count(2));
}

TEST(STLSetDifference, CustomCompare) {
  // Same as above, but a is a set and b is a vector instead of set.
  const int a_arr[] = {3, 2, 1};
  const int b_arr[] = {3, 1};
  std::set<int, std::greater<int>> a(a_arr, a_arr + ABSL_ARRAYSIZE(a_arr));
  std::vector<int> b(b_arr, b_arr + ABSL_ARRAYSIZE(b_arr));
  std::vector<int> c;
  STLSetDifference(a, b, &c, a.key_comp());
  EXPECT_THAT(c, ElementsAre(2));
}

static bool Greater(int a, int b) { return b < a; }

TEST(STLSetDifference, CustomCompareWithFuncPtr) {
  // Same as above, but a is a set and b is a vector instead of set.
  const int a_arr[] = {1, 2, 3};
  const int b_arr[] = {1, 3};
  std::vector<int> a(a_arr, a_arr + ABSL_ARRAYSIZE(a_arr));
  std::vector<int> b(b_arr, b_arr + ABSL_ARRAYSIZE(b_arr));
  std::vector<int> c;
  std::sort(a.begin(), a.end(), Greater);
  std::sort(b.begin(), b.end(), Greater);
  STLSetDifference(a, b, &c, Greater);
  EXPECT_THAT(c, ElementsAre(2));
  EXPECT_THAT(STLSetDifference(a, b, Greater), ElementsAre(2));
  EXPECT_THAT(STLSetDifferenceAs<std::deque<int> >(a, b, Greater),
    ElementsAre(2));
  EXPECT_THAT(STLSetDifferenceAs<std::vector<int>>(a, b, Greater),
              ElementsAre(2));
}

template<typename R, typename F, typename A1, typename A2>
static R TakesFunctor(F f, const A1& a1, const A2& a2) { return f(a1, a2); }

TEST(STLSetDifference, UsableAsFunctors) {
  std::vector<int> vec_a;
  std::vector<int> vec_b;
  std::vector<int> vec_c;
  std::deque<int> deque_b;
  std::deque<int> deque_c;

  // STLSetDifference: 1-arg form must be usable as functor.
  vec_c = TakesFunctor<std::vector<int> >(
      &STLSetDifference<std::vector<int> >,
      vec_a, vec_b);

  // STLSetDifference: 2-arg form must be usable as functor.
  vec_c = TakesFunctor<std::vector<int> >(
      &STLSetDifference<std::vector<int>, std::deque<int> >,
      vec_a, deque_b);

  // STLSetDifferenceAs: 3-arg form usable as functor.
  deque_c = TakesFunctor<std::deque<int> >(
      &STLSetDifferenceAs<std::deque<int>, std::vector<int>, std::deque<int> >,
      vec_a, deque_b);
}

TEST(STLSetDifferenceDeathTest, BadArgs) {
  // Make sure that in debug mode we crash (assert only runs in debug mode).
#if NDEBUG
#else
  std::set<int> a, b;
  ASSERT_DEATH(STLSetDifference(a, b, &a), "");
#endif
}

TEST(STLSetUnion, Simple) {
  std::set<int> a, b, c;
  a.insert(1);
  b.insert(2);
  STLSetUnion(a, b, &c);
  ASSERT_EQ(2, c.size());
  ASSERT_TRUE(c.count(1));
  ASSERT_TRUE(c.count(2));
  std::set<int> d = STLSetUnion(a, b);
  ASSERT_TRUE(c == d);
}

TEST(STLSetIntersection, Simple) {
  std::set<int> a, b, c;
  a.insert(1);
  a.insert(2);
  b.insert(2);
  b.insert(3);
  c = STLSetIntersection(a, b);
  ASSERT_EQ(1, c.size());
  ASSERT_TRUE(c.count(2));
  std::set<int> d = STLSetIntersection(a, b);
  ASSERT_TRUE(c == d);
}

// Check the simplest case: begin() and end() of two containers of the same
// type.
TEST(SortedRangesHaveIntersection, WorksOnVectorSameType) {
  const int ia1[] = { 1, 5, 8, 9 };
  const int ia2[] = { 3, 6, 10 };
  std::vector<int> v1(ia1, ia1 + ABSL_ARRAYSIZE(ia1));
  std::vector<int> v2(ia2, ia2 + ABSL_ARRAYSIZE(ia2));

  EXPECT_FALSE(SortedRangesHaveIntersection(v1.begin(), v1.end(),
                                            v2.begin(), v2.end()));
  EXPECT_FALSE(SortedRangesHaveIntersection(v2.begin(), v2.end(),
                                            v1.begin(), v1.end()));
  EXPECT_FALSE(SortedRangesHaveIntersection(v1.rbegin(), v1.rend(),
                                            v2.rbegin(), v2.rend(),
                                            std::greater<int>()));
  EXPECT_FALSE(SortedRangesHaveIntersection(v2.rbegin(), v2.rend(),
                                            v1.rbegin(), v1.rend(),
                                            std::greater<int>()));

  v1.push_back(12);
  v1.push_back(15);
  v1.push_back(18);
  v2.push_back(12);

  EXPECT_TRUE(SortedRangesHaveIntersection(v1.begin(), v1.end(),
                                           v2.begin(), v2.end()));
  EXPECT_TRUE(SortedRangesHaveIntersection(v2.begin(), v2.end(),
                                           v1.begin(), v1.end()));
  EXPECT_TRUE(SortedRangesHaveIntersection(v1.rbegin(), v1.rend(),
                                           v2.rbegin(), v2.rend(),
                                           std::greater<int>()));
  EXPECT_TRUE(SortedRangesHaveIntersection(v2.rbegin(), v2.rend(),
                                           v1.rbegin(), v1.rend(),
                                           std::greater<int>()));
}


// Two containers with value_types which are different but comparable.
TEST(SortedRangesHaveIntersection, WorksOnVectorDifferentType) {
  const int ia1[] = { 1, 5, 8, 9 };
  const char ca2[] = { 3, 6, 10 };
  std::vector<int> v1(ia1, ia1 + ABSL_ARRAYSIZE(ia1));
  std::vector<char> v2(ca2, ca2 + ABSL_ARRAYSIZE(ca2));

  EXPECT_FALSE(SortedRangesHaveIntersection(v1.begin(), v1.end(),
                                            v2.begin(), v2.end()));
  EXPECT_FALSE(SortedRangesHaveIntersection(v2.begin(), v2.end(),
                                            v1.begin(), v1.end()));
  EXPECT_FALSE(SortedRangesHaveIntersection(v1.rbegin(), v1.rend(),
                                            v2.rbegin(), v2.rend(),
                                            std::greater<int>()));
  EXPECT_FALSE(SortedRangesHaveIntersection(v2.rbegin(), v2.rend(),
                                            v1.rbegin(), v1.rend(),
                                            std::greater<int>()));

  v1.push_back(12);
  v1.push_back(15);
  v1.push_back(18);
  v2.push_back(12);

  EXPECT_TRUE(SortedRangesHaveIntersection(v1.begin(), v1.end(),
                                           v2.begin(), v2.end()));
  EXPECT_TRUE(SortedRangesHaveIntersection(v2.begin(), v2.end(),
                                           v1.begin(), v1.end()));
  EXPECT_TRUE(SortedRangesHaveIntersection(v1.rbegin(), v1.rend(),
                                           v2.rbegin(), v2.rend(),
                                           std::greater<int>()));
  EXPECT_TRUE(SortedRangesHaveIntersection(v2.rbegin(), v2.rend(),
                                           v1.rbegin(), v1.rend(),
                                           std::greater<int>()));
}


// Containers of different types (and with different value_types.)
TEST(SortedRangesHaveIntersection, WorksOnDifferentContainers) {
  const int ia1[] = { 1, 5, 8, 9 };
  const char ca2[] = { 3, 6, 10 };

  std::set<int> c1(ia1, ia1 + ABSL_ARRAYSIZE(ia1));
  std::vector<char> c2(ca2, ca2 + ABSL_ARRAYSIZE(ca2));

  EXPECT_FALSE(SortedRangesHaveIntersection(c1.begin(), c1.end(),
                                            c2.begin(), c2.end()));
  EXPECT_FALSE(SortedRangesHaveIntersection(c2.begin(), c2.end(),
                                            c1.begin(), c1.end()));
  EXPECT_FALSE(SortedRangesHaveIntersection(c1.rbegin(), c1.rend(),
                                            c2.rbegin(), c2.rend(),
                                            std::greater<int>()));
  EXPECT_FALSE(SortedRangesHaveIntersection(c2.rbegin(), c2.rend(),
                                            c1.rbegin(), c1.rend(),
                                            std::greater<int>()));

  c1.insert(12);
  c1.insert(15);
  c1.insert(18);
  c2.push_back(12);

  EXPECT_TRUE(SortedRangesHaveIntersection(c1.begin(), c1.end(),
                                           c2.begin(), c2.end()));
  EXPECT_TRUE(SortedRangesHaveIntersection(c2.begin(), c2.end(),
                                           c1.begin(), c1.end()));
  EXPECT_TRUE(SortedRangesHaveIntersection(c1.rbegin(), c1.rend(),
                                           c2.rbegin(), c2.rend(),
                                           std::greater<int>()));
  EXPECT_TRUE(SortedRangesHaveIntersection(c2.rbegin(), c2.rend(),
                                           c1.rbegin(), c1.rend(),
                                           std::greater<int>()));
}

// Check the case where we don't compare the begin() and end() of a container.
TEST(SortedRangesHaveIntersection, WorksOnParts) {
  std::set<int> s1, s2;
  s1.insert(1);
  s1.insert(2);
  s1.insert(3);
  s1.insert(4);

  s2.insert(1);
  s2.insert(5);

  EXPECT_TRUE(SortedRangesHaveIntersection(s1.begin(), s1.end(),
                                           s2.begin(), s2.end()));

  std::set<int>::const_iterator iter = s1.begin();
  ++iter;
  EXPECT_FALSE(SortedRangesHaveIntersection(iter, s1.end(),
                                            s2.begin(), s2.end()));
}

// Check where the ranges are empty.
TEST(SortedRangesHaveIntersection, WorksOnEmpty) {
  std::set<int> s1, s2;
  EXPECT_FALSE(SortedRangesHaveIntersection(s1.begin(), s1.end(),
                                            s2.begin(), s2.end()));
}

TEST(SortedRangesHaveIntersectionDeathTest, UnsortedRangesDie) {
  std::vector<int> v;
  v.push_back(4);
  v.push_back(5);
  v.push_back(3);
  EXPECT_DEBUG_DEATH(SortedRangesHaveIntersection(v.begin(), v.begin(),
                                                  v.begin(), v.end()),
                     "");
}

TEST(SortedRangesHaveIntersectionDeathTest, UnsortedRangesDieCustomComparator) {
  std::vector<int> v;
  v.push_back(4);
  v.push_back(5);
  v.push_back(3);
  EXPECT_DEBUG_DEATH(SortedRangesHaveIntersection(v.begin(), v.end(),
                                                  v.begin(), v.begin(),
                                                  std::greater<int>()),
                     "");
}

TEST(SortedContainersHaveIntersection, WorksOnSameContainer) {
  EXPECT_TRUE(SortedContainersHaveIntersection(std::vector<int>{1, 3, 5},
                                               std::vector<int>{2, 3, 4}));
  EXPECT_FALSE(SortedContainersHaveIntersection(std::vector<int>{1, 2, 3},
                                                std::vector<int>{4, 5, 6}));
}

TEST(SortedContainersHaveIntersection, WorksOnDifferentContainer) {
  EXPECT_TRUE(SortedContainersHaveIntersection(std::vector<int>{1, 3, 5},
                                               std::set<int>{2, 3, 4}));
  EXPECT_FALSE(SortedContainersHaveIntersection(std::set<int>{1, 2, 3},
                                                std::vector<int>{4, 5, 6}));
}

TEST(SortedContainersHaveIntersection, WorksOnDifferentValueType) {
  EXPECT_TRUE(SortedContainersHaveIntersection(std::vector<int>{1, 3, 5},
                                               std::vector<char>{2, 3, 4}));
  EXPECT_FALSE(SortedContainersHaveIntersection(std::vector<int>{1, 2, 3},
                                                std::vector<char>{4, 5, 6}));
}

TEST(SortedContainersHaveIntersection, WorksOnEmpty) {
  EXPECT_FALSE(SortedContainersHaveIntersection(std::vector<int>{1, 3, 5},
                                                std::vector<int>{}));
}

TEST(SortedContainersHaveIntersection, WorksWithCustomComparator) {
  EXPECT_TRUE(SortedContainersHaveIntersection(std::vector<int>{5, 3, 1},
                                               std::vector<int>{4, 3, 2},
                                               std::greater<int>()));
  EXPECT_FALSE(SortedContainersHaveIntersection(std::vector<int>{3, 2, 1},
                                                std::vector<int>{6, 5, 4},
                                                std::greater<int>()));
}

TEST(SortedContainersHaveIntersectionDeathTest, UnsortedContainersDie) {
  EXPECT_DEBUG_DEATH(SortedContainersHaveIntersection(std::vector<int>{1, 3, 2},
                                                      std::vector<int>{1, 2, 3},
                                                      std::greater<int>()),
                     "");
}

TEST(SortedContainersHaveIntersectionDeathTest,
     UnsortedContainersDieCustomComparator) {
  EXPECT_DEBUG_DEATH(SortedContainersHaveIntersection(std::vector<int>{3, 2, 1},
                                                      std::vector<int>{3, 2, 3},
                                                      std::greater<int>()),
                     "");
}

}  // namespace
}  // namespace googlesql_base
