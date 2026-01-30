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

#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_MAP_TRAITS_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_MAP_TRAITS_H_

// Traits classes for performing uniform lookup on different map value types.
//
// The access is computed as follows:
//
//   1. If T has a `first` or `second` field, use them.
//   2. Otherwise if it has `key()` or `value()` methods, use them.
//   3. Otherwise the program is ill-formed.

#include <utility>

namespace googlesql_base {
namespace subtle {
namespace internal_map_traits {
struct Rank1 {};
struct Rank0 : Rank1 {};

template <class V>
auto GetKey(V&& v, Rank0) -> decltype((std::forward<V>(v).first)) {
  return std::forward<V>(v).first;
}
template <class V>
auto GetKey(V&& v, Rank1) -> decltype(std::forward<V>(v).key()) {
  return std::forward<V>(v).key();
}

template <class V>
auto GetMapped(V&& v, Rank0) -> decltype((std::forward<V>(v).second)) {
  return std::forward<V>(v).second;
}
template <class V>
auto GetMapped(V&& v, Rank1) -> decltype(std::forward<V>(v).value()) {
  return std::forward<V>(v).value();
}

}  // namespace internal_map_traits

// Accesses the `key_type` from a `value_type`.
template <typename V>
auto GetKey(V&& v)
    -> decltype(internal_map_traits::GetKey(std::forward<V>(v),
                                            internal_map_traits::Rank0())) {
  return internal_map_traits::GetKey(std::forward<V>(v),
                                     internal_map_traits::Rank0());
}

// Accesses the `mapped_type` from a `value_type`.
template <typename V>
auto GetMapped(V&& v)
    -> decltype(internal_map_traits::GetMapped(std::forward<V>(v),
                                               internal_map_traits::Rank0())) {
  return internal_map_traits::GetMapped(std::forward<V>(v),
                                        internal_map_traits::Rank0());
}

}  // namespace subtle
}  // namespace googlesql_base

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_MAP_TRAITS_H_
