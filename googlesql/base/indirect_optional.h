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

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_INDIRECT_OPTIONAL_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_INDIRECT_OPTIONAL_H_

#include <concepts>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "googlesql/base/optional_ref.h"

namespace googlesql_base {

// Akin to std::optional<T>, but the object is stored indirectly (by address).
//
// This is primarily a storage optimization for rarely-used objects that must be
// copyable. There are tradeoffs; in most cases, `IndirectOptional` is *not* the
// best choice. From most preferable to less preferable:
//   1) Store T directly. No overhead from allocation or indirection.
//   2) Store std::optional<T>. Added storage overhead, typically 4-8 bytes.
//   3) Store IndirectOptional<T>. Added storage overhead of +8 bytes, plus
//      overhead from heap allocation and indirection. This may be a viable
//      choice for an object that is rarely populated (e.g. only for debugging)
//      but occupies significant memory (e.g. 100+MiB of per-task RAM).
//      IndirectOptional<T> handles a corner-case where we want to migrate from
//      storing T directly (to reduce storage of rarely-used objects); but we
//      also need to copy T (hence cannot use std::unique_ptr).
//
// Suppose we are working with (many many instances of) a class/struct of the
// form
// ```
// struct Foo {
//   MyLargeButRarelyUsedClass field;
// };
// ```
//
// We could migrate to std::optional with its large well-supported API:
// ```
//   struct Foo {
//     std::optional<MyLargeButRarelyUsedClass> field;
//   };
// ```
// This encodes optionality, at the cost of *increasing* sizeof(Foo).
//
// We could migrate to IndirectOptional with a similar API:
// ```
//   struct Foo {
//     IndirectOptional<MyLargeButRarelyUsedClass> field;
//   };
// ```
// This reduces sizeof(Foo) in trade for pointer-indirections.
//
// IndirectOptional should behave as a "semiregular" value, i.e. copyable,
// default-constructible, and having the same operator== as the stored type T.
// (If copying is not needed, use std::unique_ptr instead.) Hence the stored
// type T is required to be std::copy_constructible. In addition,
// IndirectOptional<bool> is not allowed; it offers minimal utility but creates
// much ambiguity.
//
// Like std::unique_ptr, the underlying value (if present) has a stable address.
// For example, a reference to `value()` will remain valid even after a
// `std::move` into a different IndirectOptional instance.
template <typename T>
  requires(std::copy_constructible<T> && !std::is_reference_v<T> &&
           !std::same_as<bool, std::remove_cvref_t<T>>)
class IndirectOptional {
 public:
  using value_type = T;

  // Constructs an empty (valueless) instance.
  constexpr IndirectOptional() noexcept = default;
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr IndirectOptional(std::nullopt_t) noexcept {}

  // Converting constructors.
  template <typename U = T>
  constexpr explicit IndirectOptional(U&& value)
    requires(!std::same_as<std::in_place_t, std::remove_cvref_t<U>> &&
             !std::same_as<IndirectOptional, std::remove_cvref_t<U>> &&
             std::constructible_from<T, U>)
      : ptr_(std::make_unique<T>(std::forward<U>(value))) {}

  // Forwarding constructor.
  //
  // Note that "in-place" construction is something of a misnomer: the value is
  // still stored indirectly.
  template <class... Args>
  constexpr explicit IndirectOptional(std::in_place_t, Args&&... args)
      : ptr_(std::make_unique<T>(std::forward<Args>(args)...)) {}

  // Copyable.
  IndirectOptional(const IndirectOptional& src)
      : ptr_(src ? std::make_unique<T>(*src) : std::unique_ptr<T>()) {}
  IndirectOptional& operator=(const IndirectOptional& src) {
    *this = IndirectOptional(src);
    return *this;
  }

  // Efficiently movable.
  IndirectOptional(IndirectOptional&& src) noexcept = default;
  IndirectOptional& operator=(IndirectOptional&& src) noexcept = default;

  // Specialized assignment.
  IndirectOptional& operator=(std::nullopt_t) noexcept {
    reset();
    return *this;
  }
  // For scalar T, `U = void` keeps `opt = {}` from matching here, so it resets.
  template <typename U = std::conditional_t<std::is_scalar_v<T>, void, T>>
  IndirectOptional& operator=(U&& value)
    requires(!std::same_as<IndirectOptional, std::remove_cvref_t<U>> &&
             std::constructible_from<T, U> && std::is_assignable_v<T&, U>)
  {
    if (has_value()) {
      *ptr_ = std::forward<U>(value);
    } else {
      ptr_ = std::make_unique<T>(std::forward<U>(value));
    }
    return *this;
  }

  // Observers.
  // The behavior is undefined if the value is not present.
  constexpr const T* operator->() const noexcept { return ptr_.operator->(); }
  constexpr T* operator->() noexcept { return ptr_.operator->(); }
  constexpr const T& operator*() const& noexcept(noexcept(*ptr_)) {
    return *ptr_;
  }
  constexpr T& operator*() & noexcept(noexcept(*ptr_)) { return *ptr_; }
  constexpr const T&& operator*() const&& noexcept(noexcept(*ptr_)) {
    return std::move(*ptr_);
  }
  constexpr T&& operator*() && noexcept(noexcept(*ptr_)) {
    return std::move(*ptr_);
  }

  // Returns true if a value is present.
  constexpr bool has_value() const noexcept { return ptr_ != nullptr; }
  constexpr explicit operator bool() const noexcept { return has_value(); }

  // Implicit conversion to optional_ref<U>, matching std::optional<T>.
  template <typename U = const T>
    requires(std::convertible_to<const T*, U*>)
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr operator optional_ref<U>() const& noexcept
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return ptr_.get();
  }
  template <typename U = T>
    requires(std::convertible_to<T*, U*>)
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr operator optional_ref<U>() & noexcept
      ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return ptr_.get();
  }

  // Returns the value (if present) or enters an error-state (either throws an
  // exception or terminates the program, just like std::optional::value()).
  constexpr T& value() & {
    if (has_value()) return *ptr_;
    (void)std::optional<T>().value();
    return *ptr_;
  }
  constexpr const T& value() const& {
    if (has_value()) return *ptr_;
    (void)std::optional<T>().value();
    return *ptr_;
  }
  constexpr T&& value() && { return std::move(this->value()); }
  constexpr const T&& value() const&& { return std::move(this->value()); }

  // Returns the value (if present) or the default value.
  template <typename U = T>
  constexpr T value_or(U&& default_value) const& {
    return ptr_ ? *ptr_ : static_cast<T>(std::forward<U>(default_value));
  }
  template <typename U = T>
  constexpr T value_or(U&& default_value) && {
    return ptr_ ? std::move(*ptr_)
                : static_cast<T>(std::forward<U>(default_value));
  }

  // Swaps the contents.
  constexpr void swap(IndirectOptional& other) noexcept {
    ptr_.swap(other.ptr_);
  }

  // Destructs the value (if present).
  constexpr void reset() noexcept { ptr_.reset(); }

  // Emplaces a newly-constructed value.
  template <class... Args>
  constexpr T& emplace(Args&&... args) {
    if (T* p = ptr_.get(); p != nullptr) {
      p->~T();
      new (p) T(std::forward<Args>(args)...);
    } else {
      ptr_ = std::make_unique<T>(std::forward<Args>(args)...);
    }
    return *ptr_;
  }

  // Equality with IndirectOptional instances.
  template <typename U, typename V>
    requires(std::equality_comparable_with<U, V>)
  friend constexpr bool operator==(const IndirectOptional<U>& lhs,
                                   const IndirectOptional<V>& rhs);

  // IndirectOptional is hashable iff T is hashable.
  template <typename H>
  friend H AbslHashValue(H h, const IndirectOptional& opt) {
    if (const T* p = opt.ptr_.get(); p != nullptr) {
      return H::combine(std::move(h), *p);
    } else {
      return H::combine(std::move(h), nullptr);
    }
  }

 private:
  std::unique_ptr<T> ptr_;
};

// Equality between IndirectOptional instances.
template <typename U, typename V>
  requires(std::equality_comparable_with<U, V>)
constexpr bool operator==(const IndirectOptional<U>& lhs,
                          const IndirectOptional<V>& rhs) {
  return lhs.ptr_ == rhs.ptr_ ||
         (lhs.ptr_ && rhs.ptr_ && *lhs.ptr_ == *rhs.ptr_);
}

// Equality with std::nullopt.
template <typename T>
constexpr bool operator==(const IndirectOptional<T>& opt,
                          std::nullopt_t) noexcept {
  return !opt.has_value();
}

// Equality with a value.
template <typename U, typename V = U>
  requires(!std::convertible_to<V, IndirectOptional<U>> &&
           std::equality_comparable_with<U, V>)
constexpr bool operator==(const IndirectOptional<U>& opt,
                          const V& value) noexcept {
  return opt.has_value() && *opt == value;
}

// MakeIndirectOptional factory functions.
template <typename T>
constexpr IndirectOptional<std::decay_t<T>> MakeIndirectOptional(T&& value) {
  return IndirectOptional<std::decay_t<T>>(std::in_place,
                                           std::forward<T>(value));
}
template <typename T, class... Args>
constexpr IndirectOptional<T> MakeIndirectOptional(Args&&... args) {
  return IndirectOptional<T>(std::in_place, std::forward<Args>(args)...);
}

}  // namespace googlesql_base

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_INDIRECT_OPTIONAL_H_
