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

#ifndef GOOGLESQL_RESOLVED_AST_MAKE_NODE_VECTOR_INTERNAL_H_
#define GOOGLESQL_RESOLVED_AST_MAKE_NODE_VECTOR_INTERNAL_H_

// This file contains the implementation of MakeNodeVector, in particular the
// derivation of the correct element type of the constructed vector.
// See `make_node_vector.h` for documentation on
// MakeNodeVector.
// Do not directly include this file or depend on its internals.
//


#include <type_traits>

#include "googlesql/resolved_ast/resolved_node.h"
#include "absl/meta/type_traits.h"

namespace googlesql {
namespace make_node_vector_internal {

// If T is the root type `ResolvedNode` a.k.a SUPER==void, creates a valid
// type_trait with type=T; value=true. Otherwise, value=false and type is
// undefined.
template <class T>
struct try_root : std::is_same<void, typename T::SUPER> {
  using type = T;
};

// If `From` is convertible to `To` creates a valid type_trait with
// type=To; value=true. Otherwise, value=false and type is undefined.
template <class From, class To>
struct try_convert : std::is_convertible<From*, To*> {
  using type = To;
};

// A type_trait with the lowest common ancestor between `T1` and `T2`.
// If such a type exists (and it should for all ResolvedAST nodes). Ignores
// constness.
template <class T1, class T2>
struct RawLowestCommonAncestor
    : public absl::disjunction<
          try_root<T1>, try_root<T2>, try_convert<T1, T2>, try_convert<T2, T1>,
          RawLowestCommonAncestor<typename T1::SUPER, T2>> {
  static_assert(std::is_convertible<T1*, ResolvedNode*>::value,
                "MakeNodeVector members must be convertible to ResolvedNode");
  static_assert(std::is_convertible<T2*, ResolvedNode*>::value,
                "MakeNodeVector members must be convertible to ResolvedNode");
};

// A type_trait with the lowest common ancestor between `T1` and `T2`
// respecting constness (i.e. if T1 is const or T2 is const, the result type
// will also be const.
template <class T1, class T2>
struct LowestCommonAncestorImpl {
  constexpr static bool kShouldAddConst =
      std::is_const<T1>::value || std::is_const<T2>::value;

  // Strip const-ness and determine the lowest common ancestor of the
  // the two types.
  using DecayedLowestCommonAncestor =
      typename RawLowestCommonAncestor<typename std::decay<T1>::type,
                                       typename std::decay<T2>::type>::type;
  // If either was const, the result is const.
  using type = typename std::conditional<
      kShouldAddConst,
      typename std::add_const<DecayedLowestCommonAncestor>::type,
      DecayedLowestCommonAncestor>::type;
};

template <class... T>
struct LowestCommonAncestor;

template <class T>
struct LowestCommonAncestor<T> : std::true_type {
  using type = T;
};

template <class T1, class T2, class... Ts>
struct LowestCommonAncestor<T1, T2, Ts...>
    : LowestCommonAncestor<typename LowestCommonAncestorImpl<T1, T2>::type,
                           Ts...> {};

template <class T, class T1>
void MakeNodeVectorInternal(std::vector<T>* vec, T1&& arg1) {
  vec->emplace_back(std::forward<T1>(arg1));
}

template <class T, class T1, class... Ts>
void MakeNodeVectorInternal(std::vector<T>* vec, T1&& arg1, Ts&&... args) {
  vec->emplace_back(std::forward<T1>(arg1));
  MakeNodeVectorInternal<T, Ts...>(vec, std::forward<Ts>(args)...);
}

// NodeVectorT is vector<unique_ptr<Node>>. The exact type for 'Node' is based
// on the inputs by finding the lowest common ancestor of the arguments. If
// any input element_type is const, element_type of the output will be as well.
template <class... Ts>
using NodeVectorT = std::vector<std::unique_ptr<
    typename LowestCommonAncestor<typename Ts::element_type...>::type>>;
}  // namespace make_node_vector_internal
}  // namespace googlesql

#endif  // GOOGLESQL_RESOLVED_AST_MAKE_NODE_VECTOR_INTERNAL_H_
