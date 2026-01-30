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

// flat_set is a set implementation that uses a contiguous container (by
// default, std::vector) instead of a binary tree to store ordered values.
//
// The main benefit of flat_set is memory efficiency and fast read access.
// Storing values in a contiguous range allows zero per-node memory overhead,
// and improves memory locality. The downside is that mutations (insert, erase)
// are potentially linear and invalidate iterators.
//
// The typical use cases for flat_set include:
//  * minimizing memory usage of the set (or many small sets)
//  * "const" sets - created once and then accessed in a read-only fashion
//  * small sets, where linear access is more efficient than std::set (with
//    InlinedVector as an underlying type, this also makes small sets inlined)
//
//
// Detailed comparison with std::set (using std::vector as an underlying type):
//   + zero per-value memory overhead
//   + smaller set object size
//   + random-access iterators provided
//   + reserve(), capacity(), shrink_to_fit() provided
//   + mutations (insert, erase) at the end() are O(1)
//   - mutations are generally O(n)
//   - mutations may invalidate all iterators & pointers to elements

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_FLAT_SET_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_FLAT_SET_H_

#include <functional>
#include <initializer_list>
#include <vector>

#include "absl/meta/type_traits.h"
#include "googlesql/base/flat_internal.h"

namespace googlesql_base {

// TODO: In C++14, we need to use std::less<>, which is transparent.
template <typename Key, typename Compare = std::less<Key>,
          typename Rep = std::vector<Key>>
class flat_set {
  // Whether Compare is a transparent comparator. The fake dependency on K only
  // exists to allow SFINAE in methods templated by K.
  template <typename K>
  static constexpr bool CompareIsTransparent() {
    return internal_flat::is_transparent<Compare>();
  }

 public:
  using container_type = Rep;
  using key_type = Key;
  using value_type = Key;
  using key_compare = Compare;

  using reference = typename container_type::reference;
  using const_reference = typename container_type::const_reference;
  using pointer = typename container_type::pointer;
  using const_pointer = typename container_type::const_pointer;
  using size_type = typename container_type::size_type;
  using difference_type = typename container_type::difference_type;

  // These are random access iterators, making this interface different from
  // set<Key>.  Different and *awesome*.
  using const_iterator = typename container_type::const_iterator;
  using const_reverse_iterator =
      typename container_type::const_reverse_iterator;
  // Unlike the underlying class, we only expose const iterators.
  using iterator = const_iterator;
  using reverse_iterator = const_reverse_iterator;

  // construct/copy/destroy:
  flat_set() : flat_set(Compare()) {}
  explicit flat_set(const Compare& cmp) : impl_(cmp) {}

  // If multiple elements in the input range compare equal, the first of them
  // will be present in the set.
  template <typename InputIterator>
  flat_set(InputIterator first, InputIterator last,
           const Compare& cmp = Compare())
      : flat_set(cmp) {
    insert(first, last);
  }

  flat_set(std::initializer_list<value_type> init,
           const Compare& cmp = Compare())
      : flat_set(init.begin(), init.end(), cmp) {}

  flat_set(const flat_set& s) = default;
  flat_set(flat_set&& s) = default;
  flat_set& operator=(const flat_set& s) = default;
  flat_set& operator=(flat_set&& s) = default;

  // Constructs underlying container by moving the given one.
  // Note: the given container need not be pre-sorted.
  explicit flat_set(Rep rep, const Compare& cmp = Compare())
      : impl_(cmp, std::move(rep)) {}

  // Constructs underlying container directly by perfect forwarding arguments to
  // its constructor.
  //
  // This constructor enables optimizing construction if the input is known to
  // be ordered, as well as passing custom arguments to the underlying
  // container.
  //
  // The constructed container MUST be strictly ordered. If not, the behavior is
  // undefined (and the ordering is verified with assert() in debug builds).
  template <typename... Args, typename = absl::enable_if_t<
                                  std::is_constructible<Rep, Args...>::value>>
  explicit flat_set(sorted_unique_container_t, Args&&... args)
      : impl_(sorted_container, Compare(), std::forward<Args>(args)...) {
  }

  template <typename... Args, typename = absl::enable_if_t<
                                  std::is_constructible<Rep, Args...>::value>>
  flat_set(sorted_unique_container_t, const Compare& cmp, Args&&... args)
      : impl_(sorted_container, cmp, std::forward<Args>(args)...) {}

  // iterators:
  iterator begin() { return rep().begin(); }
  const_iterator begin() const { return rep().begin(); }
  iterator end() { return rep().end(); }
  const_iterator end() const { return rep().end(); }

  reverse_iterator rbegin() { return rep().rbegin(); }
  const_reverse_iterator rbegin() const { return rep().rbegin(); }
  const_reverse_iterator rend() const { return rep().rend(); }
  reverse_iterator rend() { return rep().rend(); }

  const_iterator cbegin() const { return rep().cbegin(); }
  const_iterator cend() const { return rep().cend(); }
  const_reverse_iterator crbegin() const { return rep().crbegin(); }
  const_reverse_iterator crend() const { return rep().crend(); }

  // capacity:
  bool empty() const { return rep().empty(); }
  size_type size() const { return rep().size(); }
  size_type max_size() const { return rep().max_size(); }

  // modifiers:

  // Unlike std::set, we cannot construct an element in place, as we do not have
  // a layer of indirection like std::set nodes. Therefore, emplace* methods do
  // not provide a performance advantage over insert + move.
  template <typename... Args>
  std::pair<iterator, bool> emplace(Args&&... args) {
    return insert(value_type(std::forward<Args>(args)...));
  }
  template <typename... Args>
  iterator emplace_hint(const_iterator position, Args&&... args) {
    return insert(position, value_type(std::forward<Args>(args)...));
  }

  std::pair<iterator, bool> insert(const value_type& v) {
    return internal_flat::insert(&rep(), v, key_comp());
  }
  std::pair<iterator, bool> insert(value_type&& v) {
    return internal_flat::insert(&rep(), std::move(v), key_comp());
  }

  iterator insert(const_iterator hint, const value_type& v) {
    return internal_flat::insert_hint(&rep(), hint, v, key_comp());
  }
  iterator insert(const_iterator hint, value_type&& v) {
    return internal_flat::insert_hint(&rep(), hint, std::move(v), key_comp());
  }

  // If multiple elements in the input range compare equal, the first of them
  // will be present in the set.
  template <typename InputIterator>
  void insert(InputIterator first, InputIterator last) {
    internal_flat::insert_range(&rep(), first, last, key_comp());
  }

  void insert(std::initializer_list<value_type> ilist) {
    insert(ilist.begin(), ilist.end());
  }

  iterator erase(const_iterator it) { return rep().erase(it); }
  size_type erase(const value_type& v) {
    auto it = find(v);
    if (it == end()) return 0;
    erase(it);
    return 1;
  }
  iterator erase(const_iterator first, const_iterator last) {
    return rep().erase(first, last);
  }

  // Removes all elements for which predicate 'p' returns 'true'.
  template <class UnaryPredicate>
  size_t remove_if(UnaryPredicate p) {
    const auto it = std::remove_if(rep().begin(), rep().end(), p);
    auto n_erased = std::distance(it, rep().end());
    rep().erase(it, rep().end());
    return n_erased;
  }

  void swap(flat_set& other) { rep().swap(other.rep()); }
  void clear() { rep().clear(); }

  // observers:
  key_compare key_comp() const { return impl_.cmp(); }
  // value_comp is not implemented , as it is deprecated in C++17 anyway.

  // set operations:
  iterator find(const value_type& v) {
    return internal_flat::ordered_find(begin(), end(), v, key_comp());
  }
  const_iterator find(const value_type& v) const {
    return internal_flat::ordered_find(begin(), end(), v, key_comp());
  }
  template <typename K>
  absl::enable_if_t<CompareIsTransparent<K>(), iterator> find(const K& v) {
    return internal_flat::ordered_find(begin(), end(), v, key_comp());
  }
  template <typename K>
  absl::enable_if_t<CompareIsTransparent<K>(), const_iterator> find(
      const K& v) const {
    return internal_flat::ordered_find(begin(), end(), v, key_comp());
  }

  size_type count(const value_type& v) const {
    return find(v) == end() ? 0 : 1;
  }
  template <typename K>
  absl::enable_if_t<CompareIsTransparent<K>(), size_type> count(
      const K& v) const {
    return find(v) == end() ? 0 : 1;
  }

  bool contains(const value_type& k) const { return find(k) != end(); }
  template <typename K>
  typename std::enable_if<CompareIsTransparent<K>(), bool>::type contains(
      const K& k) const {
    return find(k) != end();
  }

  iterator lower_bound(const value_type& v) {
    return std::lower_bound(rep().begin(), rep().end(), v, key_comp());
  }
  const_iterator lower_bound(const value_type& v) const {
    return std::lower_bound(rep().begin(), rep().end(), v, key_comp());
  }
  template <typename K>
  absl::enable_if_t<CompareIsTransparent<K>(), iterator> lower_bound(
      const K& v) {
    return std::lower_bound(rep().begin(), rep().end(), v, key_comp());
  }
  template <typename K>
  absl::enable_if_t<CompareIsTransparent<K>(), const_iterator> lower_bound(
      const K& v) const {
    return std::lower_bound(rep().begin(), rep().end(), v, key_comp());
  }

  iterator upper_bound(const value_type& v) {
    return std::upper_bound(rep().begin(), rep().end(), v, key_comp());
  }
  const_iterator upper_bound(const value_type& v) const {
    return std::upper_bound(rep().begin(), rep().end(), v, key_comp());
  }
  template <typename K>
  absl::enable_if_t<CompareIsTransparent<K>(), iterator> upper_bound(
      const K& v) {
    return std::upper_bound(rep().begin(), rep().end(), v, key_comp());
  }
  template <typename K>
  absl::enable_if_t<CompareIsTransparent<K>(), const_iterator> upper_bound(
      const K& v) const {
    return std::upper_bound(rep().begin(), rep().end(), v, key_comp());
  }

  std::pair<iterator, iterator> equal_range(const value_type& v) {
    return std::equal_range(rep().begin(), rep().end(), v, key_comp());
  }
  std::pair<const_iterator, const_iterator> equal_range(
      const value_type& v) const {
    return std::equal_range(rep().begin(), rep().end(), v, key_comp());
  }
  template <typename K>
  absl::enable_if_t<CompareIsTransparent<K>(), std::pair<iterator, iterator>>
  equal_range(const K& v) {
    return std::equal_range(rep().begin(), rep().end(), v, key_comp());
  }
  template <typename K>
  absl::enable_if_t<CompareIsTransparent<K>(),
                    std::pair<const_iterator, const_iterator>>
  equal_range(const K& v) const {
    return std::equal_range(rep().begin(), rep().end(), v, key_comp());
  }

  // capacity-related extensions from std::vector interface:
  void reserve(size_type capacity) { rep().reserve(capacity); }
  size_type capacity() const { return rep().capacity(); }
  void shrink_to_fit() { rep().shrink_to_fit(); }

  template <typename H>
  friend H AbslHashValue(H h, const flat_set& set) {
    return H::combine(std::move(h), set.rep());
  }

 private:
  Rep& rep() { return impl_.rep; }
  const Rep& rep() const { return impl_.rep; }

  friend bool operator==(const flat_set& x, const flat_set& y) {
    // This could be implemented as a non-friend method by using iterators.
    // However, vector's comparison can potentially be more efficient for some
    // types, so we prefer to use it directly.
    return x.rep() == y.rep();
  }
  friend bool operator<(const flat_set& x, const flat_set& y) {
    // As specified by § 23.2.1.12, ordering associative containers always uses
    // default '<' operator - even if otherwise the container uses custom
    // functor.
    return x.rep() < y.rep();
  }

  internal_flat::Impl<Compare, Rep> impl_;
};

template <typename Key, typename C, typename R>
bool operator!=(const flat_set<Key, C, R>& x, const flat_set<Key, C, R>& y) {
  return !(x == y);
}

template <typename Key, typename C, typename R>
bool operator>(const flat_set<Key, C, R>& x, const flat_set<Key, C, R>& y) {
  return y < x;
}

template <typename Key, typename C, typename R>
bool operator<=(const flat_set<Key, C, R>& x, const flat_set<Key, C, R>& y) {
  return !(y < x);
}

template <typename Key, typename C, typename R>
bool operator>=(const flat_set<Key, C, R>& x, const flat_set<Key, C, R>& y) {
  return !(x < y);
}

template <typename Key, typename C, typename R>
void swap(flat_set<Key, C, R>& x, flat_set<Key, C, R>& y) {
  return x.swap(y);
}

}  // namespace googlesql_base

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_FLAT_SET_H_
