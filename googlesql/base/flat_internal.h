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

// Internal helpers for flat_set.h

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_FLAT_INTERNAL_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_FLAT_INTERNAL_H_

#include <algorithm>
#include <cassert>
#include <type_traits>
#include <utility>

#include "absl/meta/type_traits.h"

namespace googlesql_base {

// Tag types for unsafe constructors.
struct sorted_container_t {
  constexpr explicit sorted_container_t() {}
};
extern sorted_container_t sorted_container;

struct sorted_unique_container_t : sorted_container_t {
  constexpr explicit sorted_unique_container_t() {}
};
extern sorted_unique_container_t sorted_unique_container;

namespace internal_flat {

template <typename, typename = void>
struct is_transparent : std::false_type {};
template <typename T>
struct is_transparent<T, absl::void_t<typename T::is_transparent>>
    : std::true_type {};

// Same idea as std::forward_as_tuple, but for std::pair specifically.
template <class T, class U>
constexpr std::pair<T&&, U&&> forward_as_pair(T&& t, U&& u) noexcept {
  return {std::forward<T>(t), std::forward<U>(u)};
}

// Enum to control whether to use insert() vs insert_or_assign() logic
// in the shared utility functions below.
enum class OnKeyCollision { kKeepOldValue, kAssignNewValue };

// Implementation of insert() variants. Note: since these methods are heavily
// templated, we keep their names different to avoid any ambiguity.

template <typename Rep, typename V, typename Cmp>
typename Rep::iterator multi_insert(Rep* rep, V&& v, Cmp cmp) {
  return rep->insert(std::upper_bound(rep->begin(), rep->end(), v, cmp),
                     std::forward<V>(v));
}

template <typename Rep, typename V, typename Cmp>
std::pair<typename Rep::iterator, bool> insert(
    Rep* rep, V&& v, Cmp cmp,
    const OnKeyCollision policy = OnKeyCollision::kKeepOldValue) {
  auto it = std::lower_bound(rep->begin(), rep->end(), v, cmp);
  if (it == rep->end()) {
    rep->push_back(std::forward<V>(v));
    return {rep->end() - 1, true};
  } else if (cmp(v, *it)) {
    return {rep->insert(it, std::forward<V>(v)), true};
  } else {
    if (policy == OnKeyCollision::kAssignNewValue) {
      *it = std::forward<V>(v);
    }
    return {it, false};
  }
}

// Possible alignments of 'hint' and 'v' against each other.
enum class VerifyHintResult {
  kPerfectHint,  // Insertion of 'v' on 'hint' is ordered by 'cmp'.
  kKeyExists,    // A duplicate was found at 'hint' or at 'hint-1'.
  kBadHint,      // Hint couldn't be used. Fallback to default operation.
};
// Finds out how 'hint' could be used for insertion of 'v'. Returns a pair of
// 'VerifyHintResult' and 'Iterator' where the latter is:
// - for 'kPerfectHint': set to the good position for insertion;
// - for 'kKeyExists': set to the location of the duplicate;
// - for 'kBadHint': not set.
template <typename Container, typename V, typename Cmp, typename Iterator>
std::pair<VerifyHintResult, Iterator> verify_hint(const Container& c,
                                                  const Iterator& hint,
                                                  const V& v, const Cmp& cmp) {
  if (hint != c.end() && !cmp(v, *hint)) {
    return cmp(*hint, v)
               ? std::make_pair(VerifyHintResult::kBadHint, Iterator())
               : std::make_pair(VerifyHintResult::kKeyExists, hint);
  }
  if (hint != c.begin() && !cmp(*(hint - 1), v)) {
    return cmp(v, *(hint - 1))
               ? std::make_pair(VerifyHintResult::kBadHint, Iterator())
               : std::make_pair(VerifyHintResult::kKeyExists, hint - 1);
  }
  return {VerifyHintResult::kPerfectHint, hint};
}

template <typename Rep, typename V, typename Cmp>
typename Rep::iterator multi_insert_hint(Rep* rep,
                                         typename Rep::const_iterator hint,
                                         V&& v, Cmp cmp) {
  if (hint != rep->end() && cmp(*hint, v)) {
    // Hint points below an actual location of `v`. Insert at lower bound.
    hint = std::lower_bound(rep->begin(), rep->end(), v, cmp);
  } else if (hint != rep->begin() && cmp(v, *(hint - 1))) {
    // Hint points above an actual location of `v`. Insert at upper bound.
    hint = std::upper_bound(rep->begin(), rep->end(), v, cmp);
  }
  // Either hint is already perfect, or we have just adjusted it as needed.
  return rep->insert(hint, std::forward<V>(v));
}

template <typename Rep, typename V, typename Cmp>
typename Rep::iterator insert_hint(
    Rep* rep, typename Rep::const_iterator hint, V&& v, Cmp cmp,
    const OnKeyCollision policy = OnKeyCollision::kKeepOldValue) {
  const auto result = verify_hint(*rep, hint, v, cmp);
  switch (result.first) {
    case VerifyHintResult::kPerfectHint:
      return rep->insert(result.second, std::forward<V>(v));
    case VerifyHintResult::kKeyExists: {
      const auto existing_it = rep->begin() + (result.second - rep->begin());
      if (policy == OnKeyCollision::kAssignNewValue) {
        *existing_it = std::forward<V>(v);
      }
      return existing_it;
    }
    case VerifyHintResult::kBadHint:
      // Hint is useless, fallback to insert without hint.
      return insert(rep, std::forward<V>(v), cmp, policy).first;
  }
}

template <typename Rep, typename InputIterator, typename Cmp>
void multi_insert_range(Rep* rep, InputIterator first, InputIterator last,
                        Cmp cmp) {
  const auto original_size = rep->size();
  rep->insert(rep->end(), first, last);
  // We use stable_sort to guarantee that the relative order of multiple equal
  // inserted values is preserved.
  std::stable_sort(rep->begin() + original_size, rep->end(), cmp);
  std::inplace_merge(rep->begin(), rep->begin() + original_size, rep->end(),
                     cmp);
}

// Removes duplicates from the given container, which must be sorted.
template <typename Rep, typename Cmp>
void remove_duplicates(Rep* rep, Cmp cmp) {
  using value_type = typename Rep::value_type;
  auto eq = [cmp](const value_type& left, const value_type& right) {
    // We don't need to compare both ways.
    // comp(right, left) can never be true because the range is sorted.
    return !cmp(left, right);
  };
  rep->erase(std::unique(rep->begin(), rep->end(), eq), rep->end());
}

template <typename Rep, typename InputIterator, typename Cmp>
void insert_range(Rep* rep, InputIterator first, InputIterator last, Cmp cmp) {
  // First insert the whole range allowing duplicates if any.
  multi_insert_range(rep, first, last, cmp);
  // Then remove any duplicates.
  remove_duplicates(rep, cmp);
}

// Constructor helper: sorts the given container.
template <typename Rep, typename Cmp>
void sort(Rep* rep, Cmp cmp) {
  // We use stable_sort to guarantee that the relative order of multiple equal
  // values in the container is preserved.
  std::stable_sort(rep->begin(), rep->end(), cmp);
}

template <typename Rep, typename V, typename Cmp>
std::pair<typename Rep::iterator, bool> insert_or_assign(Rep* rep, V&& v,
                                                         Cmp cmp) {
  return insert(rep, std::forward<V>(v), cmp, OnKeyCollision::kAssignNewValue);
}

template <typename Rep, typename V, typename Cmp>
typename Rep::iterator insert_or_assign_hint(Rep* rep,
                                             typename Rep::const_iterator hint,
                                             V&& v, Cmp cmp) {
  return insert_hint(rep, hint, std::forward<V>(v), cmp,
                     OnKeyCollision::kAssignNewValue);
}

template <typename Iterator>
typename std::iterator_traits<Iterator>::difference_type count(
    const std::pair<Iterator, Iterator>& rng) {
  return std::distance(rng.first, rng.second);
}

template <typename Rep, typename Iterator>
typename Rep::size_type erase(Rep* rep,
                              const std::pair<Iterator, Iterator>& rng) {
  const auto count_rng = count(rng);
  if (count_rng != 0) {
    rep->erase(rng.first, rng.second);
  }
  return count_rng;
}

// Finds a value in an ordered sequence.
template <typename Iterator, typename V, typename Cmp>
Iterator ordered_find(Iterator begin, Iterator end, const V& v, Cmp cmp) {
  auto it = std::lower_bound(begin, end, v, cmp);
  if (it == end) return it;
  if (cmp(v, *it)) return end;
  return it;
}

// Value comparator. Inherits from Compare for Empty Base Class
// Optimization to work.
template <typename Compare>
struct value_compare : public Compare {
  value_compare() = default;
  explicit value_compare(Compare cmp) : Compare(std::move(cmp)) {}

  template <typename T, typename U>
  bool operator()(const T& left, const U& right) const {
    return Compare::operator()(left.first, right.first);
  }
};

// Helpers for handling "clearable" vs "non-clearable" containers in non-uniform
// ways. See the "Impl" class below for details.
template <typename, typename = void>
struct has_clear : std::false_type {};
template <typename T>
struct has_clear<T, absl::void_t<decltype(std::declval<T>().clear())>>
    : std::true_type {};

template <typename T>
T&& MoveIf(std::true_type, T* t) {
  return std::move(*t);
}
template <typename T>
const T& MoveIf(std::false_type, T* t) {
  return *t;
}
template <typename T>
void ClearIf(std::true_type, T* t) {
  t->clear();
}
template <typename T>
void ClearIf(std::false_type, T*) {}

// EBCO-enabled storage for flat_set. The reason why we use this
// instead of std::tuple is that tuple lacks "piecewise" constructor.
//
// To ensure flat_set/map's invariants, we must clear() a moved-from flat
// container. This is not possible if its Rep does not have a clear() method. In
// that case, we fall back to a copy (at compile time).
template <typename Compare, typename Rep, bool allow_duplicates = false>
struct Impl : private Compare {
  Impl(const Impl& other) = default;
  Impl& operator=(const Impl& other) = default;
  // We always copy Compare; moving it could leave 'other' in an invalid state.
  Impl(Impl&& other) noexcept(
      has_clear<Rep>::value&& std::is_nothrow_copy_constructible<
          Compare>::value&& std::is_nothrow_move_constructible<Rep>::value)
      : Compare(other.cmp()),  // NOLINT misc-move-constructor-init
        rep(MoveIf(has_clear<Rep>(), &other.rep)) {
    ClearIf(has_clear<Rep>(), &other.rep);
  }
  Impl& operator=(Impl&& other) noexcept(
      has_clear<Rep>::value&& std::is_nothrow_copy_assignable<Compare>::value&&
          std::is_nothrow_move_assignable<Rep>::value) {
    Compare::operator=(other.cmp());
    rep = MoveIf(has_clear<Rep>(), &other.rep);
    ClearIf(has_clear<Rep>(), &other.rep);
    return *this;
  }

  explicit Impl(const Compare& cmp) : Compare(cmp), rep() {
    // Assert invariants to avoid any funny business of the part of Rep's
    // default constructor, as it is user-supplied.
    assert(check_invariants());
  }

  Impl(const Compare& cmp, Rep rep_arg)
      : Compare(cmp), rep(std::move(rep_arg)) {
    sort(&rep, cmp);
    if (!allow_duplicates) {
      remove_duplicates(&rep, cmp);
    }
    assert(check_invariants());
  }

  // This constructor asserts that the Rep instance constructed from args need
  // not be sorted and possibly deduplicated. It says "sorted_container_t", but
  // if allow_duplicates is false we really mean that the rep is sorted and
  // that entries are unique.
  template <typename... Args>
  Impl(sorted_container_t, const Compare& cmp, Args&&... args)
      : Compare(cmp), rep(std::forward<Args>(args)...) {
    assert(check_invariants());
  }

  // Converting Impl to Compare may invoke a conversion constructor, rather than
  // Compare's copy constructor (e.g. if Compare is std::function<...>).
  // Do not allow relying on the implicit conversion, forcing the explicit cast
  // through this method.
  const Compare& cmp() const { return *this; }

  Rep rep;

 private:
  bool check_invariants() const {
    if (!rep.empty()) {
      auto next_it = rep.begin();
      for (auto it = next_it++; next_it != rep.end(); it = next_it++) {
        if (allow_duplicates ? cmp()(*next_it, *it) : !cmp()(*it, *next_it)) {
          return false;
        }
      }
    }
    return true;
  }
};

}  // namespace internal_flat
}  // namespace googlesql_base

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_FLAT_INTERNAL_H_
