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

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_ASSOCIATIVE_VIEW_INTERNAL_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_ASSOCIATIVE_VIEW_INTERNAL_H_

#include <algorithm>
#include <cassert>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/base/optimization.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/requires.h"

namespace googlesql_base {

// An empty type that is used to instruct `AssociateView` to add `find` and
// `contains` overloads for all `T`s.
template <typename... T>
struct AlsoSupportsLookupWith {};

namespace internal_associative_view {

template <typename Key>
struct DefaultAdditionalLookupForKeyHelper {
  using type = AlsoSupportsLookupWith<>;
};

template <>
struct DefaultAdditionalLookupForKeyHelper<std::string> {
  using type = AlsoSupportsLookupWith<absl::string_view>;
};

template <>
struct DefaultAdditionalLookupForKeyHelper<absl::Cord> {
  using type = AlsoSupportsLookupWith<absl::string_view>;
};

template <typename Key>
using DefaultExtraLookupForKey =
    typename internal_associative_view::DefaultAdditionalLookupForKeyHelper<
        Key>::type;

//
// kHasSubscript checks if operator[] exists for a given map type.
//

template <typename C, typename K = typename C::key_type>
inline constexpr bool kHasSubscript = googlesql_base::Requires<C, const K>(
    [](auto&& c, auto&& k) -> decltype(c[k]) {});

//
// kHasAt checks if at() exists for a given map type.
//

template <typename C, typename K = typename C::key_type>
inline constexpr bool kHasAt = googlesql_base::Requires<const C, const K>(
    [](auto&& c, auto&& k) -> decltype(c.at(k)) {});

struct NoneSuch {};

template <typename C>
inline constexpr bool kHasHeterogeneousLookup =
    googlesql_base::Requires<const C>(
        [](auto&& c) -> decltype(c.find(NoneSuch{})) {});

// We assume that the presence of "at()" or "operator[]" is a strong signal that
// a given map type has unique keys. Most modern map types provide at(), but
// there are a few older map implementations that only provide operator[]. This
// is a function instead of a trait class because the static_assert failure is
// easier to read.
template <typename Map>
constexpr bool MapTypeHasUniqueKeys() {
  return kHasSubscript<Map> || kHasAt<Map>;
}

template <typename C, typename K>
inline constexpr bool kHasContainsMethod =
    googlesql_base::Requires<const C, const K>(
        [](auto&& c, auto&& k) -> decltype(c.contains(k)) {});

// Returns true if no elements in the list have the same keys.
template <typename ValueType>
constexpr bool HasUniqueKeys(const std::initializer_list<ValueType>& values) {
  for (auto i = values.begin(); i < values.end(); ++i) {
    for (auto j = i + 1; j < values.end(); ++j) {
      if (i->first == j->first) {
        return false;
      }
    }
  }
  return true;
}

// `FindImpl` is an implementation of CRTP pattern to transform template methods
// `FindInternal` and `ContainsInternal` into a set of non-template overloads of
// `find` and `contains` for fixed number of supported lookup types.
template <typename View, typename Key>
class FindImpl {
 public:
  auto find(const Key& key) const {
    return static_cast<const View*>(this)->FindInternal(key);
  }
  bool contains(const Key& key) const {
    return static_cast<const View*>(this)->ContainsInternal(key);
  }
};

template <typename View>
class FindImpl<View, absl::string_view> {
  // Note: without `const char*` overloads, lookup calls with raw/literal
  // strings would be ambiguous.
 public:
  auto find(absl::string_view key) const {
    return static_cast<const View*>(this)->FindInternal(key);
  }
  bool contains(absl::string_view key) const {
    return static_cast<const View*>(this)->ContainsInternal(key);
  }
  auto find(const char* key) const { return find(absl::string_view(key)); }
  bool contains(const char* key) const {
    return contains(absl::string_view(key));
  }
};

template <typename View, typename... Keys>
class LookupOverloads : public FindImpl<View, std::remove_cv_t<Keys>>... {
 public:
  using FindImpl<View, std::remove_cv_t<Keys>>::find...;
  using FindImpl<View, std::remove_cv_t<Keys>>::contains...;
};

template <typename mapped_type, typename View, typename Key>
const mapped_type& AtFunction(const View& v, const Key& key) {
  auto it = v.find(key);
  if (ABSL_PREDICT_FALSE(it == v.end())) {
    absl::base_internal::ThrowStdOutOfRange("MapView::at failed bounds check");
  }
  return it->second;
}

template <typename View, typename Key, typename mapped_type>
class AtImpl {
 public:
  const mapped_type& at(const Key& key) const {
    return AtFunction<mapped_type>(*static_cast<const View*>(this), key);
  }
};
template <typename View, typename mapped_type>
class AtImpl<View, absl::string_view, mapped_type> {
  // Note: without `const char*` overloads, lookup calls with raw/literal
  // strings would be ambiguous.
 public:
  const mapped_type& at(absl::string_view key) const {
    return AtFunction<mapped_type>(*static_cast<const View*>(this), key);
  }
  const mapped_type& at(const char* key) const {
    return at(absl::string_view(key));
  }
};

template <typename View, typename mapped_type, typename... Keys>
class AtOverloads
    : public AtImpl<View, std::remove_cv_t<Keys>, mapped_type>... {
 public:
  using AtImpl<View, std::remove_cv_t<Keys>, mapped_type>::at...;
};

// Provides a minimal stl::set like interface over a value using a key for
// lookup.  Shared implementation for SetView and MapView.
// `ExtraLookupTypes` parameter is used to provide additional `find`/`contains`
// overloads. It must be one of the `AlsoSupportsLookupWith` types. Consider an
// example:
//
//   using WithStringViewLookup =
//       googlesql_base::AlsoSupportsLookupWith<absl::string_view>;
//   using StringSetView = googlesql_base::SetView<std::string,
//   WithStringViewLookup>;
//
// The type `StringSetView`:
//  * supports lookup with std::string, as well as with absl::string_view;
//  * can only be constructed from a set that has `find` overloads for both
//    std::string and absl::string_view.
//
// Note that the latter requirement is automatically met for containers with
// heterogeneous lookup.
template <typename KeyType, typename ValueType, typename ExtraLookupTypes>
class AssociateView {
  // Preventing accidental typos when `ExtraLookupTypes` is not one of
  // `AlsoSupportsLookupWith`, for example, `SetView<std::string, absl::Cord>`.
  static_assert(!std::is_same_v<ExtraLookupTypes, ExtraLookupTypes>,
                "Invalid template argument ExtraLookupTypes. It must be an "
                "instantiation of googlesql_base::AlsoSupportsLookupWith");
};

template <typename KeyType, typename ValueType, typename ExtraLookupTypes>
class MapViewBase {
  // Preventing accidental typos when `ExtraLookupTypes` is not one of
  // `AlsoSupportsLookupWith`, for example, `SetView<std::string, absl::Cord>`.
  static_assert(!std::is_same_v<ExtraLookupTypes, ExtraLookupTypes>,
                "Invalid template argument ExtraLookupTypes. It must be an "
                "instantiation of googlesql_base::AlsoSupportsLookupWith");
};

template <typename K, typename V, typename... Keys>
class MapViewBase<K, V, AlsoSupportsLookupWith<Keys...>>
    : public AssociateView<K, std::pair<const K, V>,
                           AlsoSupportsLookupWith<Keys...>>,
      public AtOverloads<MapViewBase<K, V, AlsoSupportsLookupWith<Keys...>>, V,
                         K, Keys...> {
  using Base = typename MapViewBase::AssociateView;

 public:
  using mapped_type = V;

 protected:
  using Base::Base;
};

struct DefaultIteratorAdapter {
  template <typename Iterator>
  static Iterator&& Wrap(Iterator&& it) {
    return std::forward<Iterator>(it);
  }
};

// Note: key types may have type modifiers (i.e. googlesql_base::SetView<const
// int>), and we remove them when interacting with the internal virtual table.
// Consider:
//    googlesql_base::SetView<const int> my_view = ...;
//    bool x = my_view.contains(123);
// The line above calls `ContainsInternal<int>` rather than
// `ContainsInternal<const int>`, hence it is easier to keep entries in the
// virtual table without type modifiers.
template <typename KeyType, typename ValueType, typename... Keys>
class AssociateView<KeyType, ValueType, AlsoSupportsLookupWith<Keys...>>
    : public LookupOverloads<
          AssociateView<KeyType, ValueType, AlsoSupportsLookupWith<Keys...>>,
          KeyType, Keys...> {
 public:
  using size_type = std::size_t;
  using key_type = KeyType;
  using value_type = ValueType;
  using reference = const value_type&;
  using const_reference = reference;
  using pointer = const value_type*;
  using const_pointer = pointer;
  class iterator;
  using const_iterator = iterator;

  iterator begin() const { return dispatch_table_->begin_fn(c_); }
  iterator end() const { return {}; }
  size_type size() const { return dispatch_table_->size_fn(c_); }
  bool empty() const { return size() == 0; }

 protected:
  template <typename C>
  using ViewEnabler =
      std::enable_if_t<std::is_same_v<typename C::key_type, KeyType> &&
                       std::is_same_v<typename C::value_type, ValueType>>;

  // A default constructed AssociateView behaves as if it is wrapping an empty
  // container.
  constexpr AssociateView() {}

  // Constructs an AssociateView that wraps the given container. The resulting
  // view must not outlive the container.
  //
  // We distinguish two types of containers: with and without heterogeneous
  // support.
  // First type:
  //   These containers are required to support lookup with `key_type`
  //   and all types from `AlsoSupportsLookupWith`. Failure to meet this
  //   criteria will result in a compilation error.
  //   Lookup will never make a copy of the key.
  // Second type:
  //   These containers are required to support lookup with only `key_type`
  //   and `key_type` must be constructible from every typ in
  //   `AlsoSupportsLookupWith`.
  //   Lookup with any type except for `key_type` makes a copy of the key.
  template <typename C, typename IteratorAdapter = DefaultIteratorAdapter>
  constexpr explicit AssociateView(const C& c ABSL_ATTRIBUTE_LIFETIME_BOUND,
                                   IteratorAdapter = {})
      : c_(&c),
        dispatch_table_(
            &kTable<std::conditional_t<
                kHasHeterogeneousLookup<C>,
                HeterogeneousContainerDispatcher<C, IteratorAdapter>,
                NonHeterogeneousContainerDispatcher<C, IteratorAdapter>>>) {
    static_assert(std::is_empty_v<IteratorAdapter>,
                  "Only stateless iterator adapters are supported.");
  }
  // Constructs an AssociateView that wraps an initializer_list.  `Compare` must
  // be a stateless function that compares `Compare{}(key_type, value_type)`.
  template <typename Compare>
  constexpr AssociateView(const std::initializer_list<value_type>& init,
                          const Compare&)
      : c_(&init),
        dispatch_table_(
            &kTable<InitListDispatcher<std::initializer_list<value_type>,
                                       Compare>>) {
    static_assert(std::is_empty_v<Compare>,
                  "Only stateless comparators are supported.");
  }

 private:
  // A function that implements 'find'. First argument is a pointer to a
  // container, second argument is the key being searched for.
  template <typename K>
  using FindFn = iterator (*)(const void*, const K&);

  // A function that implements 'contains'. First argument is a pointer to a
  // container, second argument is the key being searched for.
  template <typename K>
  using ContainsFn = bool (*)(const void*, const K&);

  // A function that implements 'begin'. The argument is a pointer to a
  // container.
  using BeginFn = iterator (*)(const void*);

  // A function that implements 'size'. The argument is a pointer to a
  // container.
  using SizeFn = size_type (*)(const void*);

  template <typename K>
  struct VirtualTableForKey {
    FindFn<K> find_fn;
    ContainsFn<K> contains_fn;
  };

  struct BaseVirtualTable {
    BeginFn begin_fn;
    SizeFn size_fn;
  };

  struct DispatchTable : BaseVirtualTable,
                         VirtualTableForKey<std::remove_cv_t<key_type>>,
                         VirtualTableForKey<std::remove_cv_t<Keys>>... {};

  template <typename Impl>
  inline constexpr static DispatchTable kTable = {
      BaseVirtualTable{
          .begin_fn = Impl::begin,
          .size_fn = Impl::size,
      },
      VirtualTableForKey<std::remove_cv_t<key_type>>{
          .find_fn = Impl::template find<std::remove_cv_t<key_type>>,
          .contains_fn = Impl::template contains<std::remove_cv_t<key_type>>,
      },
      VirtualTableForKey<std::remove_cv_t<Keys>>{
          .find_fn = Impl::template find<std::remove_cv_t<Keys>>,
          .contains_fn = Impl::template contains<std::remove_cv_t<Keys>>,
      }...,
  };

  struct EmptyDispatcher {
    template <typename K>
    static iterator find(const void*, const K&) {
      return {};
    }
    template <typename K>
    static bool contains(const void*, const K&) {
      return false;
    }
    static iterator begin(const void*) { return {}; }
    static size_type size(const void*) { return 0; }
  };

  template <typename C, typename IteratorAdapter>
  struct BaseDispatcher {
    static iterator begin(const void* c_ptr) {
      const auto* c = static_cast<const C*>(c_ptr);
      auto it = c->begin();
      if (it == c->end()) return {};
      return iterator(*c, std::move(it), IteratorAdapter{});
    }

    static size_type size(const void* c_ptr) {
      return static_cast<const C*>(c_ptr)->size();
    }
  };

  template <typename C, typename IteratorAdapter>
  struct HeterogeneousContainerDispatcher : BaseDispatcher<C, IteratorAdapter> {
    template <typename K>
    static iterator find(const void* c_ptr, const K& k) {
      const auto* c = static_cast<const C*>(c_ptr);
      auto it = c->find(k);
      if (it == c->end()) return {};
      return iterator(*c, std::move(it), IteratorAdapter{});
    }

    template <typename K>
    static bool contains(const void* c_ptr, const K& k) {
      const auto* c = static_cast<const C*>(c_ptr);
      if constexpr (kHasContainsMethod<C, K>) {
        return c->contains(k);
      } else {
        return c->find(k) != c->end();
      }
    }
  };

  template <typename C, typename IteratorAdapter>
  struct NonHeterogeneousContainerDispatcher
      : BaseDispatcher<C, IteratorAdapter> {
    static iterator find(const void* c_ptr, const key_type& k) {
      const auto* c = static_cast<const C*>(c_ptr);
      auto it = c->find(k);
      if (it == c->end()) return {};
      return iterator(*c, std::move(it), IteratorAdapter{});
    }

    static bool contains(const void* c_ptr, const key_type& k) {
      const auto* c = static_cast<const C*>(c_ptr);
      if constexpr (kHasContainsMethod<C, key_type>) {
        return c->contains(k);
      } else {
        return c->find(k) != c->end();
      }
    }

    template <typename K>
    static iterator find(const void* c_ptr, const K& k) {
      return find(c_ptr, static_cast<key_type>(k));
    }

    template <typename K>
    static bool contains(const void* c_ptr, const K& k) {
      return contains(c_ptr, static_cast<key_type>(k));
    }
  };

  template <typename InitList, typename Compare>
  struct InitListDispatcher : BaseDispatcher<InitList, DefaultIteratorAdapter> {
    template <typename K>
    static iterator find(const void* c_ptr, const K& k) {
      const auto* init = static_cast<const InitList*>(c_ptr);
      auto it =
          std::find_if(init->begin(), init->end(),
                       [&](const value_type& v) { return Compare{}(v, k); });
      if (it == init->end()) return {};
      return iterator(*init, std::move(it), DefaultIteratorAdapter{});
    }

    template <typename K>
    static bool contains(const void* c_ptr, const K& k) {
      const auto* init = static_cast<const InitList*>(c_ptr);
      return std::find_if(init->begin(), init->end(), [&](const value_type& v) {
               return Compare{}(v, k);
             }) != init->end();
    }
  };

  template <typename, typename>
  friend class FindImpl;

  template <typename K>
  iterator FindInternal(const K& key) const {
    return dispatch_table_->VirtualTableForKey<K>::find_fn(c_, key);
  }

  template <typename K>
  bool ContainsInternal(const K& key) const {
    // Not using `FindInternal` to avoid constructing type-erased iterators.
    return dispatch_table_->VirtualTableForKey<K>::contains_fn(c_, key);
  }

  // The underlying container.
  const void* c_ = nullptr;

  // Implements find(), begin() and size() for the correct type of c_. This is
  // always non-null and callable, even if c_ is null.
  const DispatchTable* dispatch_table_ = &kTable<EmptyDispatcher>;
};

template <typename K, typename V, typename... Keys>
class AssociateView<K, V, AlsoSupportsLookupWith<Keys...>>::iterator {
 public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = AssociateView::value_type;
  using difference_type = std::ptrdiff_t;
  using reference = const value_type&;
  using pointer = const value_type*;

  // Default constructed instance is considered equal to end().
  iterator() {}

  iterator(const iterator& other) { CopyFrom(other); }

  ~iterator() { Destroy(); }

  iterator& operator=(const iterator& other) {
    Destroy();
    CopyFrom(other);
    return *this;
  }

  reference operator*() const {
    assert(value_ != nullptr);
    return *value_;
  }

  pointer operator->() const {
    assert(value_ != nullptr);
    return value_;
  }

  iterator& operator++() {
    assert(value_ != nullptr);
    value_ = inc_(c_, &iter_);
    return *this;
  }

  iterator operator++(int) {
    iterator tmp = *this;
    operator++();
    return tmp;
  }

  friend bool operator==(const iterator& a, const iterator& b) {
    return a.value_ == b.value_;
  }

  friend bool operator!=(const iterator& a, const iterator& b) {
    return a.value_ != b.value_;
  }

 private:
  friend class AssociateView;

  enum { kInlineIterSize = sizeof(void*) * 4 };

  // Used to store the type-erased iterator.
  union IterStorage {
    // Used for iterators that are smaller than kInlineIterSize.
    typename std::aligned_storage<kInlineIterSize>::type small_value;

    // Used for allocated storage for iterators that are larger than
    // kInlineIterSize.
    void* ptr;
  };

  // True if `Iterator` can be stored in `IterStorage::small_value`.
  template <typename Iterator>
  static constexpr bool IsInline() {
    return (sizeof(Iterator) <= kInlineIterSize &&
            alignof(Iterator) <= alignof(IterStorage));
  }

  // Casts the given storage to the actual type.
  template <typename Iterator>
  static const Iterator* IterCast(const IterStorage* storage) {
    if constexpr (IsInline<Iterator>()) {
      return reinterpret_cast<const Iterator*>(&storage->small_value);
    } else {
      return static_cast<const Iterator*>(storage->ptr);
    }
  }

  // Non-const overload of the above function.
  template <typename Iterator>
  static Iterator* IterCast(IterStorage* storage) {
    const IterStorage* cstorage = storage;
    return const_cast<Iterator*>(IterCast<Iterator>(cstorage));
  }

  // Copies an iterator from the first argument to the uninitialized second
  // argument.
  using CopyFn = void (*)(const IterStorage*, IterStorage*);

  // Destroys the iterator.
  using DestroyFn = void (*)(IterStorage*);

  // An implementation of CopyFn.
  template <typename Iterator>
  static void CopyImpl(const IterStorage* src_storage,
                       IterStorage* dst_storage) {
    auto* src = IterCast<Iterator>(src_storage);
    if constexpr (IsInline<Iterator>()) {
      ::new (static_cast<void*>(&dst_storage->small_value)) Iterator(*src);
    } else {
      dst_storage->ptr = new Iterator(*src);
    }
  }

  // An implementation of DestroyFn.
  template <typename Iterator>
  static void DestroyImpl(IterStorage* storage) {
    auto* it = IterCast<Iterator>(storage);
    if constexpr (IsInline<Iterator>()) {
      it->~Iterator();
    } else {
      delete it;
    }
  }

  // Contains the functions for copying and destroying the underlying iterator.
  // These are grouped together because many iterators do not require them, in
  // which case copy_and_destroy_ will be null.
  struct CopyAndDestroy {
    CopyFn copy;
    DestroyFn destroy;
  };

  // Returns a statically allocated CopyAndDestroy if `Iterator` requires
  // it. Returns null if the underlying iterator is small and trivial.
  template <typename Iterator>
  static const CopyAndDestroy* MakeCopyAndDestroy() {
    if constexpr (IsInline<Iterator>() &&
                  std::is_trivially_destructible<Iterator>() &&
                  std::is_trivially_copy_constructible<Iterator>()) {
      return nullptr;
    } else {
      static constexpr CopyAndDestroy kImplementation = {
          .copy = CopyImpl<Iterator>, .destroy = DestroyImpl<Iterator>};
      return &kImplementation;
    }
  }

  // Increments the underlying iterator. The first argument should be the
  // container that the iterator points to.
  using IncFn = pointer (*)(const void*, IterStorage*);

  template <typename C, typename Iterator, typename IteratorAdapter>
  static pointer IncImpl(const void* c_ptr, IterStorage* it_storage) {
    auto& it = *IterCast<Iterator>(it_storage);
    const auto& c = *static_cast<const C*>(c_ptr);
    assert(it != c.end());
    if (++it == c.end()) {
      DestroyImpl<Iterator>(it_storage);
      return nullptr;
    }
    return std::addressof(*IteratorAdapter::Wrap(it));
  }

  // Constructs an iterator that points to 'it' in the given container.
  // 'it' must be != c.end().
  template <typename C, typename It, typename IteratorAdapter,
            typename Iterator = std::decay_t<It>>
  iterator(const C& c, It&& it, IteratorAdapter)
      : value_(std::addressof(*IteratorAdapter::Wrap(it))),
        c_(&c),
        inc_(&IncImpl<C, Iterator, IteratorAdapter>),
        copy_and_destroy_(MakeCopyAndDestroy<Iterator>()) {
    assert(it != c.end());
    if constexpr (IsInline<Iterator>()) {
      ::new (static_cast<void*>(&iter_.small_value))
          Iterator(std::forward<It>(it));
    } else {
      iter_.ptr = new Iterator(std::forward<It>(it));
    }
  }

  // Destroys the underlying iterator if it is large or non-trivial. Does not
  // guard against being called twice.
  void Destroy() {
    if (ABSL_PREDICT_FALSE(copy_and_destroy_ != nullptr && value_ != nullptr)) {
      copy_and_destroy_->destroy(&iter_);
    }
  }

  // Initializes this iterator as a copy of 'other'. Does not perform
  // destruction, so Destroy() must be called first if this iterator has already
  // been initialized.
  void CopyFrom(const iterator& other) {
    value_ = other.value_;
    c_ = other.c_;
    inc_ = other.inc_;
    copy_and_destroy_ = other.copy_and_destroy_;
    if (value_ != nullptr) {
      if (ABSL_PREDICT_FALSE(copy_and_destroy_ != nullptr)) {
        copy_and_destroy_->copy(&other.iter_, &iter_);
      } else {
        iter_ = other.iter_;
      }
    }
  }

  // The value that the iterator points to, or nullptr if the iterator ==
  // c_->end().
  pointer value_ = nullptr;

  // The underlying container. Used to detect when the iterator has reached the
  // end.
  const void* c_ = nullptr;

  // Increments the iterator. Always non-null for valid iterator.
  IncFn inc_ = nullptr;

  // Functions for copying and destroying the iterator. Null unless the iterator
  // is large or non-trivial.
  const CopyAndDestroy* copy_and_destroy_ = nullptr;

  // Storage for the underlying iterator. For end() this stores nothing.
  IterStorage iter_;
};

}  // namespace internal_associative_view
}  // namespace googlesql_base

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_ASSOCIATIVE_VIEW_INTERNAL_H_
