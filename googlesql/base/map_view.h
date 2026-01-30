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

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_MAP_VIEW_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_MAP_VIEW_H_

#include <initializer_list>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "googlesql/base/associative_view_internal.h"  

namespace googlesql_base {

// A type that is provided to `googlesql_base::MapView` as `ExtraLookupTypes`
// parameter in order to add lookup overloads.
//
// Example:
//
//   using WithThreadIdLookup =
//   googlesql_base::AlsoSupportsLookupWith<std::thread::id>; template <typename
//   T> using ThreadMapView = googlesql_base::MapView<std::thread, T,
//   WithThreadIdLookup>;
//
//   std::map<std::thread, int, ThreadCmp> threads = ...;
//   ThreadMapView thread_map_view = threads;
//
//   std::thread::id id = ...;
//   auto it = thread_map_view.find(id);  // Didn't have to create a thread!
//
// By default, `googlesql_base::MapView` sets ExtraLookupTypes to
// `googlesql_base::AlsoSupportsLookupWith<>` except for `std::string` and
// `absl::Cord` for which it is
// `googlesql_base::AlsoSupportsLookupWith<absl::string_view>` allowing you to
// take advantage of sets that support heterogeneous lookup.
using ::googlesql_base::
    AlsoSupportsLookupWith;  // NOLINT(misc-unused-using-decls)

// MapView is a type-erased, read-only view for associative containers. This
// class supports a useful subset of operations that are found in most ordered
// and unordered maps, namely find(), begin(), and end(), and it expects
// underlying maps to support the same operations.
//
// MapView requires the underlying map to have unique keys. This is enforced by
// requiring the map to provide operator[] or at().
//
// MapView does not take ownership of the underlying container. Callers must
// ensure that containers outlive any MapViews that point to them.
//
// The overhead of MapView should be substantially lower than a deep copy, which
// makes it useful for handling different map types when the alternatives (such
// as using a template or a specific type of map) are cumbersome or impossible.
//
// To write a function that can accept std::map, std::unordered_map, or
// absl::flat_hash_map as inputs, you can use MapView as a function argument:
//
//   void MyFunction(MapView<string, Thing> things_by_name);
//
// You can invoke MyFunction with any compatible associative container:
//
//  absl::flat_hash_map<string, Thing> things_by_name = ...;
//  MyFunction(things_by_name);
//
// Or an initializer list:
//
//  MyFunction({{"a", thing_a}, {"b", thing_b}});
//
// Note that MapView does not work with maps that define value_type
// differently from `std::pair<const K, V>`.
template <typename K, typename V,
          typename ExtraLookupTypes =
              internal_associative_view::DefaultExtraLookupForKey<K>>
class MapView
    : public internal_associative_view::MapViewBase<K, V, ExtraLookupTypes> {
  using Base = typename MapView::MapViewBase;
  struct Compare;
  template <typename C>
  using ViewEnabler = typename Base::template ViewEnabler<C>;

 public:
  using size_type = typename Base::size_type;
  using iterator = typename Base::iterator;
  using key_type = typename Base::key_type;
  using value_type = typename Base::value_type;
  using reference = typename Base::reference;
  using mapped_type = typename Base::mapped_type;

  // A default constructed MapView behaves as if it is wrapping an empty
  // container.
  constexpr MapView() : Base() {}

  // Constructs a MapView that wraps the given container. Behavior is
  // undefined if the container is resized after the view is constructed. The
  // resulting view must not outlive the container.
  template <typename C, typename = ViewEnabler<C>>
  constexpr MapView(  // NOLINT(google-explicit-constructor)
      const C& c ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : Base(c) {
    static_assert(internal_associative_view::MapTypeHasUniqueKeys<C>(),
                  "MapView requires maps with unique keys.");
  }

  // Constructs a MapView that wraps an initializer_list.  This constructor is
  // O(1) in release builds and performs no allocations or copies, but find()
  // may have poor runtime characteristics for large lists.
  //
  // Beware of dangling references: `init` binds to temporaries (and
  // initializer_list is a view itself).
  //
  //   void foo(googlesql_base::MapView<int, int> view);
  //
  //   // Okay, the temporary `init` outlives the view.
  //   foo({{1, 1}, {2, 2}});
  //
  //   // Not okay, the temporary `init` is destroyed on this very line.
  //   auto view = googlesql_base::MapView<int, int>({{1, 1}, {2, 2}});
  constexpr MapView(  // NOLINT(google-explicit-constructor)
      const std::initializer_list<value_type>& init
          ABSL_ATTRIBUTE_LIFETIME_BOUND)
      : Base(init, Compare{}) {
    assert(internal_associative_view::HasUniqueKeys(init));
  }

  // Lookup methods are overloaded: they support `key_type` and all extra types
  // provided via `ExtraLookupTypes`.
  using Base::contains;
  using Base::find;

  using Base::at;
  using Base::begin;
  using Base::empty;
  using Base::end;
  using Base::size;

 private:
  struct Compare {
    using is_transparent = void;

    template <typename T>
    bool operator()(const value_type& v, const T& k) const {
      return v.first == k;
    }
  };
};

}  // namespace googlesql_base

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_MAP_VIEW_H_
