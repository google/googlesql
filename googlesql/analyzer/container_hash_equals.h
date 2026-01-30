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

#ifndef GOOGLESQL_ANALYZER_CONTAINER_HASH_EQUALS_H_
#define GOOGLESQL_ANALYZER_CONTAINER_HASH_EQUALS_H_

#include <stddef.h>

#include <utility>

#include "absl/hash/hash.h"
#include "absl/types/span.h"

namespace googlesql {

// Defines two template functors, ContainerHash and ContainerEquals, which
// allow computation of a hash code an equality of a container class using
// a custom hash/equals implementation for each element.
//
// <Container> represents a container class to support hashing/equals, and
//   must satisfy all of the following properties:
//    - Supported by absl::Hash()
//    - Represents its elements as a linear array, and provides member functions
//        size() and data() returning the number of elements and a pointer to
//        the first element, respectively.
//    - Contains an operator==() function which calls operator==() to compare
//      each element.
//
//   Examples of supported containers include std::vector and absl::Span.
//
// <ElementHash> is a user-provided functor which consumes a const reference to
//   an element of <Container> and returns a hash code as a size_t.
//
// <ElementEquals> is a user-provided functor which consumes a const reference
//   to two elements of <Container> and returns true if the two elements are
//   equal.
//
// As an example, the following map associates vectors of IdString's with
// integers, using case-sensitive comparison:
//
//  absl::flat_hash_map<
//      std::vector<IdString>, int,
//      ContainerHash<std::vector<IdString>, IdStringHash>,
//      ContainerEquals<std::vector<IdString>, IdStringEqualFunc>> m;
//
// While the following example does the same, but with case-insensitive
// comparison:
//
//  absl::flat_hash_map<
//      std::vector<IdString>, int,
//      ContainerHash<std::vector<IdString>, IdStringCaseHash>,
//      ContainerEquals<std::vector<IdString>, IdStringCaseEqualFunc>> m;
//

// For internal use by this header file only
namespace container_hash_equals_internal {

// Helper function to create a view of a container with all elements converted
// to a wrapper class, which can have custom versions of operator== and
// AbslHashValue().
template <typename Container, typename ElementType, typename ElementWrap>
static absl::Span<const ElementWrap> Wrap(const Container& v) {
  // This reinterpret_cast is valid because ElementWrap is assumed to be a
  // struct with a single field of type ElementType; thus an array of
  // ElementType and ElementWrap must have the same memory layout.
  static_assert(sizeof(ElementWrap) == sizeof(ElementType));
  return absl::MakeSpan(reinterpret_cast<const ElementWrap*>(v.data()),
                        v.size());
}
}  // namespace container_hash_equals_internal

// Functor to compute the hash of a container.
template <typename Container, typename ElementHash>
class ContainerHash {
 public:
  size_t operator()(const Container& c) const {
    return absl::Hash<absl::Span<const ElementWrap>>()(
        container_hash_equals_internal::Wrap<Container, ElementType,
                                             ElementWrap>(c));
  }

 private:
  using ElementType = typename Container::value_type;

  struct ElementWrap {
    ElementWrap() = delete;
    ElementWrap(const ElementWrap&) = delete;
    ElementWrap& operator=(const ElementWrap&) = delete;
    const ElementType elem;
  };

  template <typename H>
  friend H AbslHashValue(H h, const ElementWrap& wrap) {
    return H::combine(std::move(h), ElementHash()(wrap.elem));
  }
};

// Functor to compare two containers for equality.
template <typename Container, typename ElementEquals>
class ContainerEquals {
 public:
  bool operator()(const Container& a, const Container& b) const {
    return container_hash_equals_internal::Wrap<Container, ElementType,
                                                ElementWrap>(a) ==
           container_hash_equals_internal::Wrap<Container, ElementType,
                                                ElementWrap>(b);
  }

 private:
  using ElementType = typename Container::value_type;

  struct ElementWrap {
    ElementWrap() = delete;
    ElementWrap(const ElementWrap&) = delete;
    ElementWrap& operator=(const ElementWrap&) = delete;
    const ElementType elem;
  };
  friend bool operator==(const ElementWrap& a, const ElementWrap& b) {
    return ElementEquals()(a.elem, b.elem);
  }
};
}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_CONTAINER_HASH_EQUALS_H_
