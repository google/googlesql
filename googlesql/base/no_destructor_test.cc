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

#include "googlesql/base/no_destructor.h"

#include <initializer_list>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "googlesql/base/logging.h"

namespace googlesql_base {
namespace {

struct Blob {
  Blob() : val(42) {}
  Blob(int x, int y) : val(x + y) {}
  Blob(std::initializer_list<int> xs) {
    val = 0;
    for (auto& x : xs) val += x;
  }

  Blob(const Blob& /*b*/) = delete;
  Blob(Blob&& b) noexcept : val(b.val) {
    b.moved_out = true;
  }  // moving is fine

  // no crash: NoDestructor indeed does not destruct (the moved-out Blob
  // temporaries do get destroyed though)
  ~Blob() { ABSL_CHECK(moved_out) << "~Blob"; }

  int val;
  bool moved_out = false;
};

struct TypeWithDeletedDestructor {
  ~TypeWithDeletedDestructor() = delete;
};

TEST(NoDestructorTest, DestructorNeverCalled) {
  NoDestructor<TypeWithDeletedDestructor> a;
  (void)a;
}

TEST(NoDestructorTest, Noncopyable) {
  using T = NoDestructor<int>;

  EXPECT_FALSE((std::is_constructible<T, T>::value));
  EXPECT_FALSE((std::is_constructible<T, const T>::value));
  EXPECT_FALSE((std::is_constructible<T, T&>::value));
  EXPECT_FALSE((std::is_constructible<T, const T&>::value));

  EXPECT_FALSE((std::is_assignable<T&, T>::value));
  EXPECT_FALSE((std::is_assignable<T&, const T>::value));
  EXPECT_FALSE((std::is_assignable<T&, T&>::value));
  EXPECT_FALSE((std::is_assignable<T&, const T&>::value));
}

TEST(NoDestructorTest, Interface) {
  EXPECT_TRUE(std::is_trivially_destructible<NoDestructor<Blob>>::value);
  EXPECT_TRUE(std::is_trivially_destructible<NoDestructor<const Blob>>::value);
  {
    NoDestructor<Blob> b;  // default c-tor
    // access: *, ->, get()
    EXPECT_EQ(42, (*b).val);
    (*b).val = 55;
    EXPECT_EQ(55, b->val);
    b->val = 66;
    EXPECT_EQ(66, b.get()->val);
    b.get()->val = 42;
    EXPECT_EQ(42, (*b).val);
  }
  {
    NoDestructor<const Blob> b(70, 7);  // regular c-tor, const
    EXPECT_EQ(77, (*b).val);
    EXPECT_EQ(77, b->val);
    EXPECT_EQ(77, b.get()->val);
  }
  {
    const NoDestructor<Blob> b{{20, 28, 40}};  // init-list c-tor, deep const
    // This only works in clang, not in gcc:
    // const NoDestructor<Blob> b({20, 28, 40});
    EXPECT_EQ(88, (*b).val);
    EXPECT_EQ(88, b->val);
    EXPECT_EQ(88, b.get()->val);
  }
}

// ========================================================================= //

std::string* Str0() {
  static NoDestructor<std::string> x;
  return x.get();
}

extern const std::string& Str2();

const char* Str1() {
  static NoDestructor<std::string> x(Str2() + "_Str1");
  return x->c_str();
}

const std::string& Str2() {
  static NoDestructor<std::string> x("Str2");
  return *x;
}

const std::string& Str2Copy() {
  static NoDestructor<std::string> x(Str2());  // exercise copy construction
  return *x;
}

typedef std::array<std::string, 3> MyArray;
const MyArray& Array() {
  static NoDestructor<MyArray> x{{{"foo", "bar", "baz"}}};
  // This only works in clang, not in gcc:
  // static NoDestructor<MyArray> x({{"foo", "bar", "baz"}});
  return *x;
}

typedef std::vector<int> MyVector;
const MyVector& Vector() {
  static NoDestructor<MyVector> x{{1, 2, 3}};
  return *x;
}

const int& Int() {
  static NoDestructor<int> x;
  return *x;
}

TEST(NoDestructorTest, StaticPattern) {
  EXPECT_TRUE(std::is_trivially_destructible<NoDestructor<std::string>>::value);
  EXPECT_TRUE(std::is_trivially_destructible<NoDestructor<MyArray>>::value);
  EXPECT_TRUE(std::is_trivially_destructible<NoDestructor<MyVector>>::value);
  EXPECT_TRUE(std::is_trivially_destructible<NoDestructor<int>>::value);

  EXPECT_EQ(*Str0(), "");
  Str0()->append("foo");
  EXPECT_EQ(*Str0(), "foo");

  EXPECT_EQ(std::string(Str1()), "Str2_Str1");

  EXPECT_EQ(Str2(), "Str2");
  EXPECT_EQ(Str2Copy(), "Str2");

  EXPECT_THAT(Array(), testing::ElementsAre("foo", "bar", "baz"));

  EXPECT_THAT(Vector(), testing::ElementsAre(1, 2, 3));

  EXPECT_EQ(0, Int());  // should get zero-initialized
}

}  // namespace
}  // namespace googlesql_base
