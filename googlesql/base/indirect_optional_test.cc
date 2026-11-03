//
// Copyright 2026 Google LLC
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

#include "googlesql/base/indirect_optional.h"

#include <concepts>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/hash/hash_testing.h"
#include "googlesql/base/optional_ref.h"

namespace googlesql_base {
namespace {

using ::testing::Optional;

// IndirectOptional is intentionally very similar to std::optional. These
// unit-tests are written against the API of IndirectOptional. But most of the
// invariants should also hold for std::optional.

// Proxy for instantiating std::optional<T> in type-parameterized tests.
struct StdOptionalProxy {
  template <typename T>
  using OptionalType = std::optional<T>;
};

// MakeOptional(StdOptionalProxy, ...) factory functions.
template <typename T>
std::optional<std::decay_t<T>> MakeOptional(StdOptionalProxy, T&& value) {
  return std::make_optional<std::decay_t<T>>(std::forward<T>(value));
}
template <typename T, typename... Args>
std::optional<T> MakeOptional(StdOptionalProxy, Args&&... args) {
  return std::make_optional<T>(std::forward<Args>(args)...);
}

// Proxy for instantiating IndirectOptional<T> in type-parameterized tests.
struct IndirectOptionalProxy {
  template <typename T>
  using OptionalType = IndirectOptional<T>;
};

// MakeOptional(IndirectOptionalProxy, ...) factory functions.
template <typename T>
IndirectOptional<std::decay_t<T>> MakeOptional(IndirectOptionalProxy,
                                               T&& value) {
  return MakeIndirectOptional<std::decay_t<T>>(std::forward<T>(value));
}
template <typename T, typename... Args>
IndirectOptional<T> MakeOptional(IndirectOptionalProxy, Args&&... args) {
  return MakeIndirectOptional<T>(std::forward<Args>(args)...);
}

// Typed test-fixture for ensuring identical behavior with std::optional.
template <typename Proxy>
class OptionalTest : public ::testing::Test {};

using OptionalTestTypes =
    ::testing::Types<StdOptionalProxy, IndirectOptionalProxy>;

class OptionalTypesNameGenerator {
 public:
  template <typename Proxy>
  static std::string GetName(int) {
    if constexpr (std::is_same_v<Proxy, StdOptionalProxy>) {
      return "std_optional";
    }
    if constexpr (std::is_same_v<Proxy, IndirectOptionalProxy>) {
      return "IndirectOptional";
    }
  }
};

TYPED_TEST_SUITE(OptionalTest, OptionalTestTypes, OptionalTypesNameGenerator);

TYPED_TEST(OptionalTest, Empty) {
  using Optional = typename TypeParam::template OptionalType<int>;
  Optional empties[] = {
      Optional(),
      Optional(std::nullopt),
  };
  for (const auto& empty : empties) {
    EXPECT_FALSE(empty);
    EXPECT_FALSE(empty.has_value());
    EXPECT_EQ(empty, std::nullopt);
    EXPECT_EQ(std::nullopt, empty);
    EXPECT_EQ(empty.value_or(123), 123);
  }
  for (const auto& lhs : empties) {
    for (const auto& rhs : empties) {
      EXPECT_EQ(lhs, rhs);
    }
  }
  Optional value(std::in_place, 123);
  for (const auto& empty : empties) {
    EXPECT_NE(empty, value);
    EXPECT_NE(value, empty);
  }
}

TYPED_TEST(OptionalTest, WithValue) {
  using Optional = typename TypeParam::template OptionalType<std::string>;
  Optional to_copy(std::in_place, "4444");
  Optional to_move(to_copy);
  Optional emplaced("22");
  emplaced.emplace(4, '4');
  Optional values[] = {
      Optional("4444"),
      Optional(std::in_place, "4444"),
      Optional(std::in_place, 4, '4'),
      to_copy,
      std::move(to_move),
      std::move(emplaced),
      MakeOptional(TypeParam(), std::string("4444")),
      MakeOptional<std::string>(TypeParam(), 4, '4'),
  };

  for (const auto& value : values) {
    EXPECT_TRUE(value);
    EXPECT_TRUE(value.has_value());
    EXPECT_NE(value, std::nullopt);
    EXPECT_NE(std::nullopt, value);
    EXPECT_EQ(value.value_or("foo"), "4444");
    EXPECT_EQ(value, std::string("4444"));
    EXPECT_EQ(std::string("4444"), value);
    EXPECT_EQ("4444", value);  // Implicit conversion of "4444" to std::string.
    EXPECT_EQ(value, "4444");  // Implicit conversion of "4444" to std::string.
    EXPECT_EQ(*value, "4444");
    EXPECT_EQ(value->size(), 4);
  }
  for (const auto& lhs : values) {
    for (const auto& rhs : values) {
      EXPECT_EQ(lhs, rhs);
    }
  }
  Optional empty;
  for (const auto& value : values) {
    EXPECT_NE(empty, value);
    EXPECT_NE(value, empty);
  }
}

std::optional<std::string> ConvertToOptional(
    IndirectOptional<std::string> opt) {
  if (opt.has_value()) return *std::move(opt);
  return std::nullopt;
}

TEST(IndirectOptionalTest, ConvertToOptional) {
  const std::string expected("4444");
  EXPECT_EQ(ConvertToOptional(std::nullopt), std::nullopt);
  EXPECT_THAT(ConvertToOptional(MakeIndirectOptional(expected)),
              Optional(expected));
}

TYPED_TEST(OptionalTest, CopyOps) {
  using Optional = typename TypeParam::template OptionalType<std::string>;
  auto src = MakeOptional(TypeParam(), std::string("4444"));
  Optional copy(src);
  EXPECT_EQ(src, copy);
  EXPECT_NE(&*src, &*copy) << "src and copy should have different addresses.";
  *copy = "5555";
  EXPECT_NE(src, copy);
}

TYPED_TEST(OptionalTest, MoveOps) {
  using Optional = typename TypeParam::template OptionalType<std::string>;
  // Set up a source and destination.
  Optional src("4444");
  Optional dest;
  EXPECT_EQ(src, "4444");
  EXPECT_EQ(dest, std::nullopt);
  // Swap them.
  using std::swap;
  swap(src, dest);
  EXPECT_EQ(src, std::nullopt);
  EXPECT_EQ(dest, "4444");
  // std::move back into the source.
  src = std::move(dest);
  EXPECT_EQ(src, "4444");
}

TYPED_TEST(OptionalTest, AssignmentOps) {
  using Optional = typename TypeParam::template OptionalType<std::string>;
  Optional value;
  value = "4444";
  EXPECT_EQ(value, "4444");
  value = std::nullopt;
  EXPECT_FALSE(value);
  value = "5555";
  EXPECT_EQ(value, "5555");
  value = {};
  EXPECT_FALSE(value);

  using IntOptional = typename TypeParam::template OptionalType<int>;
  IntOptional scalar;
  scalar = 42;
  EXPECT_EQ(scalar, 42);
  scalar = {};
  EXPECT_FALSE(scalar);

  using NestedOptional =
      typename TypeParam::template OptionalType<std::optional<int>>;
  NestedOptional nested(std::in_place, 7);
  nested = std::nullopt;
  EXPECT_FALSE(nested);
}

TYPED_TEST(OptionalTest, ValueMethod) {
  using Optional = typename TypeParam::template OptionalType<std::string>;
  Optional value("4444");
  EXPECT_EQ(value.value(), "4444") << "value() &";
  EXPECT_EQ(Optional(value).value(), "4444") << "value() &&";
  const Optional copy(value);
  EXPECT_EQ(copy.value(), "4444") << "value() const&";
  EXPECT_EQ(std::move(copy).value(), "4444") << "value() const&&";
}

struct Simple {
  int a = 0;
  int b = 1;
  bool operator==(const Simple&) const = default;
};

TYPED_TEST(OptionalTest, UseWithAggregate) {
  using Optional = typename TypeParam::template OptionalType<Simple>;
  Optional from_default;
  from_default.emplace();
  EXPECT_EQ(from_default, Simple{});
  Optional from_designated_initializer;
  from_designated_initializer = {.a = 4, .b = 5};
  EXPECT_EQ(from_designated_initializer, (Simple{.a = 4, .b = 5}));
  from_designated_initializer = {};
  EXPECT_FALSE(from_designated_initializer.has_value());
  Optional from_braces({.a = 9, .b = 10});
  EXPECT_EQ(from_braces, (Simple{.a = 9, .b = 10}));
}

TEST(IndirectOptionalTest, ValueOrBracedInit) {
  // OSS C++20 std::optional::value_or lacks the C++26 default template argument
  // (template <typename U = T>), so braced-init-list arguments to value_or({})
  // are tested specifically on IndirectOptional.
  IndirectOptional<int> empty;
  EXPECT_EQ(empty.value_or({}), 0);

  IndirectOptional<Simple> from_designated_initializer;
  from_designated_initializer = {.a = 4, .b = 5};
  EXPECT_EQ(from_designated_initializer.value_or({.a = 1, .b = 2}),
            (Simple{.a = 4, .b = 5}));
}

TYPED_TEST(OptionalTest, SupportsAbslHash) {
  using Optional = typename TypeParam::template OptionalType<std::string>;
  Optional to_copy("4444");
  Optional to_move(to_copy);
  Optional emplaced;
  emplaced.emplace(4, '4');
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly({
      // Empty instances.
      Optional(),
      Optional(std::nullopt),
      // Non-empty instances.
      Optional("4444"),
      Optional(std::in_place, "4444"),
      Optional(std::in_place, 4, '4'),
      to_copy,
      std::move(to_move),
      MakeOptional(TypeParam(), std::string("4444")),
      MakeOptional<std::string>(TypeParam(), 4, '4'),
      std::move(emplaced),
      // Non-empty instances with different values.
      Optional("22"),
      Optional(std::in_place, "22"),
      Optional(std::in_place, 2, '2'),
      MakeOptional(TypeParam(), std::string("22")),
      MakeOptional<std::string>(TypeParam(), 2, '2'),
  }));
}

struct HashNotEnabled {
  int value = 0;
  bool operator==(const HashNotEnabled& other) const = default;
};

TYPED_TEST(OptionalTest, HashNotEnabled) {
  using Optional = typename TypeParam::template OptionalType<HashNotEnabled>;
  Optional opt1(std::in_place, HashNotEnabled{.value = 123});
  Optional opt2 = MakeOptional(TypeParam(), HashNotEnabled{.value = 123});
  EXPECT_EQ(opt1, opt2);
}

class Base {
 public:
  Base() = default;
  explicit Base(int base) : base_(base) {}
  virtual ~Base() = default;
  bool operator==(const Base& other) const = default;

 private:
  int base_ = 0;
};

class Derived : public Base {
 public:
  Derived() = default;
  Derived(int base, int derived) : Base(base), derived_(derived) {}
  bool operator==(const Derived& other) const = default;

 private:
  int derived_ = 0;
};

TYPED_TEST(OptionalTest, Conversions) {
  using OptBase = typename TypeParam::template OptionalType<Base>;
  using OptDerived = typename TypeParam::template OptionalType<Derived>;
  const Base base(123);
  const Derived derived(123, 456);
  OptBase base_opt(derived);
  OptDerived derived_opt(derived);
  EXPECT_EQ(base_opt, base);
  EXPECT_EQ(base_opt, derived);
  EXPECT_EQ(derived_opt, base);  // This behavior is surprising!
  EXPECT_EQ(derived_opt, derived);
  EXPECT_EQ(derived_opt, base_opt);
  base_opt.emplace(base);
  EXPECT_EQ(base_opt, base);
  EXPECT_EQ(base_opt, derived);
  EXPECT_EQ(base_opt, derived_opt);
}

TYPED_TEST(OptionalTest, ConvertToOptionalRef) {
  using OptInt = typename TypeParam::template OptionalType<int>;

  const OptInt const_val(std::in_place, 5);
  optional_ref<const int> const_ref = const_val;
  EXPECT_THAT(const_ref, Optional(5));
  EXPECT_EQ(const_ref.as_pointer(), &*const_val);

  const OptInt const_empty;
  optional_ref<const int> const_empty_ref = const_empty;
  EXPECT_EQ(const_empty_ref, std::nullopt);

  static_assert(!std::is_convertible_v<const OptInt&, optional_ref<int>>);

  OptInt mutable_val(std::in_place, 5);
  optional_ref<int> mutable_ref = mutable_val;
  EXPECT_THAT(mutable_ref, Optional(5));
  EXPECT_EQ(mutable_ref.as_pointer(), &*mutable_val);
  *mutable_ref = 10;
  EXPECT_EQ(*mutable_val, 10);

  optional_ref<const int> const_from_mutable_ref = mutable_val;
  EXPECT_THAT(const_from_mutable_ref, Optional(10));
  EXPECT_EQ(const_from_mutable_ref.as_pointer(), &*mutable_val);

  OptInt mutable_empty;
  optional_ref<int> mutable_empty_ref = mutable_empty;
  EXPECT_EQ(mutable_empty_ref, std::nullopt);
  optional_ref<const int> const_from_mutable_empty_ref = mutable_empty;
  EXPECT_EQ(const_from_mutable_empty_ref, std::nullopt);

  // Rvalues convert to optional_ref<const T>, but not optional_ref<T>.
  static_assert(std::is_convertible_v<OptInt&&, optional_ref<const int>>);
  static_assert(!std::is_convertible_v<OptInt&&, optional_ref<int>>);
  static_assert(std::is_convertible_v<const OptInt&&, optional_ref<const int>>);
  static_assert(!std::is_convertible_v<const OptInt&&, optional_ref<int>>);

  auto accepts_const_ref = [](optional_ref<const int> ref) {
    return ref.value_or(-1);
  };
  EXPECT_EQ(accepts_const_ref(MakeOptional(TypeParam(), 42)), 42);
  EXPECT_EQ(accepts_const_ref(OptInt()), -1);
  EXPECT_THAT(const_ref.as_optional(), Optional(5));
  EXPECT_EQ(const_empty_ref.as_optional(), std::nullopt);
}

TEST(IndirectOptionalTest, ConvertToOptionalRefPolymorphism) {
  // Base/Derived polymorphism against IndirectOptional.
  // Note: googlesql_base::optional_ref's std::optional<U> constructor requires
  // std::is_same_v<std::decay_t<U>, std::decay_t<T>>, whereas IndirectOptional
  // supports std::convertible_to<const T*, U*>.
  using OptBase = IndirectOptional<Base>;
  using OptDerived = IndirectOptional<Derived>;

  const OptDerived const_derived(std::in_place, 123, 456);
  optional_ref<const Base> const_base_ref = const_derived;
  EXPECT_NE(const_base_ref, std::nullopt);
  EXPECT_EQ(const_base_ref.as_pointer(), &*const_derived);

  const OptDerived const_empty_derived;
  optional_ref<const Base> const_empty_base_ref = const_empty_derived;
  EXPECT_EQ(const_empty_base_ref, std::nullopt);

  OptDerived mutable_derived(std::in_place, 123, 456);
  optional_ref<Base> mutable_base_ref = mutable_derived;
  EXPECT_NE(mutable_base_ref, std::nullopt);
  EXPECT_EQ(mutable_base_ref.as_pointer(), &*mutable_derived);

  optional_ref<const Base> const_from_mutable_base_ref = mutable_derived;
  EXPECT_NE(const_from_mutable_base_ref, std::nullopt);
  EXPECT_EQ(const_from_mutable_base_ref.as_pointer(), &*mutable_derived);

  OptDerived mutable_empty_derived;
  optional_ref<Base> mutable_empty_base_ref = mutable_empty_derived;
  EXPECT_EQ(mutable_empty_base_ref, std::nullopt);

  static_assert(
      !std::is_convertible_v<const OptBase&, optional_ref<const Derived>>);
  static_assert(!std::is_convertible_v<OptBase&, optional_ref<Derived>>);
  static_assert(!std::is_convertible_v<OptBase&, optional_ref<const Derived>>);
  static_assert(!std::is_convertible_v<const OptDerived&, optional_ref<Base>>);
}

struct NoDefaultCtor {
  NoDefaultCtor() = delete;
  explicit NoDefaultCtor(int v) : value(v) {}
  bool operator==(const NoDefaultCtor&) const = default;
  int value;
};

struct WithConstMember {
  const int value;
  bool operator==(const WithConstMember&) const = default;
};

struct ExplicitlyConstructibleAssignable {
  explicit ExplicitlyConstructibleAssignable(int v) : value(v) {}
  ExplicitlyConstructibleAssignable& operator=(int v) {
    value = v;
    return *this;
  }
  bool operator==(const ExplicitlyConstructibleAssignable&) const = default;
  int value;
};

struct OuterWithNestedDefaultMemberInit {
  struct Inner {
    int x = 0;
    bool operator==(const Inner&) const = default;
  };
  IndirectOptional<Inner> field;
};

TYPED_TEST(OptionalTest, RelaxedTypeRequirements) {
  // Non-default-constructible type.
  static_assert(!std::semiregular<NoDefaultCtor>);
  static_assert(std::regular<IndirectOptional<NoDefaultCtor>>);
  using NoDefaultOpt = typename TypeParam::template OptionalType<NoDefaultCtor>;
  NoDefaultOpt no_default(std::in_place, 42);
  EXPECT_EQ(no_default, NoDefaultCtor(42));

  // Non-assignable type (const member).
  static_assert(!std::semiregular<WithConstMember>);
  static_assert(std::regular<IndirectOptional<WithConstMember>>);
  using ConstMemberOpt =
      typename TypeParam::template OptionalType<WithConstMember>;
  static_assert(!std::is_assignable_v<ConstMemberOpt&, WithConstMember>);
  ConstMemberOpt const_member(std::in_place, 10);
  ConstMemberOpt const_member_copy(const_member);
  EXPECT_EQ(const_member_copy, const_member);

  // Explicitly constructible and assignable from U (no common_reference_with).
  static_assert(!std::assignable_from<ExplicitlyConstructibleAssignable&, int>);
  using ExplicitAssignOpt = typename TypeParam::template OptionalType<
      ExplicitlyConstructibleAssignable>;
  ExplicitAssignOpt explicit_assign;
  explicit_assign = 42;
  EXPECT_EQ(explicit_assign, ExplicitlyConstructibleAssignable(42));
}

TEST(IndirectOptionalTest, NestedDefaultMemberInit) {
  OuterWithNestedDefaultMemberInit outer;
  outer.field.emplace();
  EXPECT_EQ(outer.field, OuterWithNestedDefaultMemberInit::Inner{.x = 0});
}

TEST(IndirectOptionalTest, StableAddress) {
  // Note the stable-address invariant does *not* hold for std::optional.
  IndirectOptional<int> xx(123), yy(456);
  const int* const xx_ptr = &xx.value();
  const int* const yy_ptr = &yy.value();
  EXPECT_NE(xx_ptr, yy_ptr);
  xx = 789;
  EXPECT_EQ(xx_ptr, &xx.value());
  EXPECT_EQ(*xx, 789);
  xx.swap(yy);
  EXPECT_EQ(xx_ptr, &yy.value());
  EXPECT_EQ(yy_ptr, &xx.value());
  xx = std::move(yy);
  EXPECT_EQ(xx_ptr, &xx.value());
  EXPECT_FALSE(yy.has_value());  // NOLINT(bugprone-use-after-move)
}

struct Constness {
  bool is_const() { return false; }
  bool is_const() const { return true; }
};

TYPED_TEST(OptionalTest, Constness) {
  // Note these do not test const-rvalue-ref behavior.
  using Optional = typename TypeParam::template OptionalType<Constness>;
  Optional value_mutable(std::in_place);
  const Optional value_const(std::in_place);
  const Optional& value_const_ref = value_mutable;
  // .value()
  EXPECT_FALSE(value_mutable.value().is_const());
  EXPECT_TRUE(value_const.value().is_const());
  EXPECT_TRUE(value_const_ref.value().is_const());
  // operator->()
  EXPECT_FALSE(value_mutable->is_const());
  EXPECT_TRUE(value_const->is_const());
  EXPECT_TRUE(value_const_ref->is_const());
  // operator*()
  EXPECT_FALSE((*value_mutable).is_const());
  EXPECT_TRUE((*value_const).is_const());
  EXPECT_TRUE((*value_const_ref).is_const());
}

struct SansEquality {
  int x = 0;
};

enum class TypeTrait {
  kRegular,
  kSemiregular,
  kUnsupported,
};

template <typename T>
TypeTrait ValidateTypeTraits() {
  if constexpr (std::regular<T>) {
    // If the type is regular, then the optional versions should be regular.
    static_assert(std::regular<std::optional<T>>);
    static_assert(std::regular<IndirectOptional<T>>);
    return TypeTrait::kRegular;
  } else if constexpr (std::semiregular<T>) {
    // If the type is only semiregular, then the optional versions should be
    // semiregular, but not regular.
    static_assert(std::semiregular<std::optional<T>>);
    static_assert(std::semiregular<IndirectOptional<T>>);
    // Although the type does not have operator==, it is still possible to
    // compare the optional versions to std::nullopt.
    IndirectOptional<T> opt;
    EXPECT_EQ(opt, std::nullopt);
    EXPECT_EQ(std::nullopt, opt);

    static_assert(!std::regular<T>);
    static_assert(!std::regular<std::optional<T>>);
    static_assert(!std::regular<IndirectOptional<T>>);
    return TypeTrait::kSemiregular;
  } else {
    return TypeTrait::kUnsupported;
  }
}

TEST(IndirectOptionalTest, TypeTraits) {
  // std::regular types
  EXPECT_EQ(TypeTrait::kRegular, ValidateTypeTraits<int>());
  EXPECT_EQ(TypeTrait::kRegular, ValidateTypeTraits<std::string>());
  EXPECT_EQ(TypeTrait::kRegular, ValidateTypeTraits<HashNotEnabled>());
  EXPECT_EQ(TypeTrait::kRegular, ValidateTypeTraits<Base>());
  EXPECT_EQ(TypeTrait::kRegular, ValidateTypeTraits<Derived>());

  // std::semiregular types (lacking operator==)
  EXPECT_EQ(TypeTrait::kSemiregular, ValidateTypeTraits<Constness>());
  EXPECT_EQ(TypeTrait::kSemiregular, ValidateTypeTraits<SansEquality>());
}

}  // namespace
}  // namespace googlesql_base
