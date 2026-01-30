//
// Copyright 2024 Google LLC
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

#include "googlesql/base/optional_ref.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include "googlesql/base/logging.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/optional_ref_matchers.h"

namespace googlesql_base {
namespace {

using ::googlesql_base::testing::HasValue;
using ::testing::Pointee;

TEST(OptionalRefTest, SimpleType) {
  int val = 5;
  optional_ref<int> ref = optional_ref(val);
  EXPECT_THAT(ref, HasValue(5));
  EXPECT_TRUE(ref.has_value());
  EXPECT_EQ(*ref, val);
  EXPECT_EQ(ref.value(), val);
}

TEST(OptionalRefTest, SimpleConstType) {
  const int val = 5;
  optional_ref<const int> ref = optional_ref(val);
  EXPECT_THAT(ref, HasValue(5));
}

TEST(OptionalRefTest, DefaultConstructed) {
  optional_ref<int> ref;
  EXPECT_EQ(ref, std::nullopt);
}

TEST(OptionalRefTest, EmptyOptional) {
  auto ref = optional_ref<int>(std::nullopt);
  EXPECT_EQ(ref, std::nullopt);
}

TEST(OptionalRefTest, OptionalType) {
  const auto val = std::optional(5);
  optional_ref<const int> ref = optional_ref(val);
  EXPECT_THAT(ref, HasValue(5));

  const std::optional<int> empty;
  optional_ref<const int> empty_ref = empty;
  EXPECT_EQ(empty_ref, std::nullopt);

  static_assert(
      !std::is_constructible_v<optional_ref<int>, const std::optional<int>&>,
      "optional_ref<int> must not be constructible with const "
      "std::optional<int>");
}

class TestInterface {};
class TestDerivedClass : public TestInterface {};

TEST(OptionalRefTest, PointerCtor) {
  int val = 5;
  optional_ref<const int> ref = &val;
  EXPECT_THAT(ref, HasValue(5));

  auto auto_ref = optional_ref(&val);
  static_assert(std::is_same_v<decltype(auto_ref), optional_ref<int>>,
                "optional_ref(T*) should deduce to optional_ref<T>.");
  EXPECT_THAT(auto_ref, HasValue(5));

  int* foo = nullptr;
  optional_ref<const int> empty_ref = foo;
  EXPECT_EQ(empty_ref, std::nullopt);

  optional_ref<int*> ptr_ref = foo;
  EXPECT_THAT(ptr_ref, HasValue(nullptr));
  static_assert(
      !std::is_constructible_v<optional_ref<int*>, std::nullptr_t>,
      "optional_ref should not be constructible with std::nullptr_t.");

  // Pointer polymorphism works.
  TestDerivedClass dc;
  optional_ref<const TestInterface> dc_ref = &dc;
  EXPECT_NE(dc_ref, std::nullopt);
}

TEST(OptionalRefTest, ImplicitCtor) {
  const int val = 5;
  optional_ref<const int> ref = val;
  EXPECT_THAT(ref, HasValue(5));
}

TEST(OptionalRefTest, DoesNotCopy) {
  // Non-copyable type.
  auto val = std::make_unique<int>(5);
  optional_ref<std::unique_ptr<int>> ref = optional_ref(val);
  EXPECT_THAT(ref, HasValue(Pointee(5)));
}

TEST(OptionalRefTest, DoesNotCopyConst) {
  // Non-copyable type.
  const auto val = std::make_unique<int>(5);
  optional_ref<const std::unique_ptr<int>> ref = optional_ref(val);
  EXPECT_THAT(ref, HasValue(Pointee(5)));
}

TEST(OptionalRefTest, RefCopyable) {
  auto val = std::make_unique<int>(5);
  optional_ref<std::unique_ptr<int>> ref = optional_ref(val);
  optional_ref<std::unique_ptr<int>> copy = ref;
  EXPECT_THAT(copy, HasValue(Pointee(5)));
}

TEST(OptionalRefTest, ConstConvertible) {
  auto val = std::make_unique<int>(5);
  optional_ref<std::unique_ptr<int>> ref = optional_ref(val);
  optional_ref<const std::unique_ptr<int>> copy = ref;
  EXPECT_THAT(copy, HasValue(Pointee(5)));
}

TEST(OptionalRefTest, TriviallyCopyable) {
  EXPECT_TRUE(std::is_trivially_copyable_v<optional_ref<std::unique_ptr<int>>>);
}

TEST(OptionalRefTest, TriviallyDestructible) {
  EXPECT_TRUE(
      std::is_trivially_destructible_v<optional_ref<std::unique_ptr<int>>>);
}

TEST(OptionalRefTest, RefNotAssignable) {
  EXPECT_FALSE(std::is_copy_assignable_v<optional_ref<int>>);
  EXPECT_FALSE(std::is_move_assignable_v<optional_ref<int>>);
}

struct TestStructWithCopy {
  TestStructWithCopy() = default;
  TestStructWithCopy(TestStructWithCopy&&) {
    ABSL_LOG(FATAL) << "Move constructor should not be called";
  }
  TestStructWithCopy(const TestStructWithCopy&) {
    ABSL_LOG(FATAL) << "Copy constructor should not be called";
  }
  TestStructWithCopy& operator=(const TestStructWithCopy&) {
    ABSL_LOG(FATAL) << "Assign operator should not be called";
  }
};

TEST(OptionalRefTest, DoesNotCopyUsingFatalCopyAssignOps) {
  TestStructWithCopy val;
  optional_ref<TestStructWithCopy> ref = optional_ref(val);
  EXPECT_NE(ref, std::nullopt);
  EXPECT_NE(optional_ref(TestStructWithCopy{}), std::nullopt);
}

std::string AddExclamation(optional_ref<const std::string> input) {
  if (!input.has_value()) {
    return "";
  }
  return absl::StrCat(*input, "!");
}

TEST(OptionalRefTest, RefAsFunctionParameter) {
  EXPECT_EQ(AddExclamation(std::nullopt), "");
  EXPECT_EQ(AddExclamation(std::string("abc")), "abc!");
  std::string s = "def";
  EXPECT_EQ(AddExclamation(s), "def!");
  EXPECT_EQ(AddExclamation(std::make_optional<std::string>(s)), "def!");
}

TEST(OptionalRefTest, ValueOrWhenHasValue) {
  std::optional<int> val = 5;
  EXPECT_EQ(optional_ref(val).value_or(2), 5);
}

TEST(OptionalRefTest, ValueOrWhenEmpty) {
  std::optional<int> val = std::nullopt;
  EXPECT_EQ(optional_ref(val).value_or(2), 2);
}

TEST(OptionalRefTest, AsOptional) {
  EXPECT_EQ(optional_ref<int>().as_optional(), std::nullopt);
  std::string val = "foo";
  optional_ref<const std::string> ref = val;
  static_assert(
      std::is_same_v<decltype(ref.as_optional()), std::optional<std::string>>,
      "The type parameter of optional_ref should decay by default for the "
      "return type in as_optional().");
  std::optional<std::string> opt_string = ref.as_optional();
  EXPECT_THAT(opt_string, HasValue(val));

  std::optional<std::string_view> opt_view =
      ref.as_optional<std::string_view>();
  EXPECT_THAT(opt_view, HasValue(val));
}

TEST(OptionalRefTest, Constexpr) {
  static constexpr int foo = 123;
  constexpr optional_ref<const int> ref(foo);
  static_assert(ref.has_value() && *ref == foo && ref.value() == foo, "");
}

}  // namespace
}  // namespace googlesql_base
