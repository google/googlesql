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

#ifndef GOOGLESQL_PARSER_AST_NODE_INTERNAL_H_
#define GOOGLESQL_PARSER_AST_NODE_INTERNAL_H_

#include <type_traits>

namespace googlesql::ast_node_internal {

// Concrete types (the 'leaves' of the hierarchy) must be constructible.
template <typename T>
using EnableIfConcrete =
    typename std::enable_if<std::is_constructible<T>::value, int>::type;

// Non Concrete types (internal nodes) of the hierarchy must _not_ be
// constructible.
template <typename T>
using EnableIfNotConcrete =
    typename std::enable_if<!std::is_constructible<T>::value, int>::type;

// GetAsOrNull implementation optimized for concrete types.  We assume that all
// concrete types define:
//   static constexpr Type kConcreteNodeKind;
//
// This allows us to avoid invoking dynamic_cast.
template <typename T, typename MaybeConstRoot, EnableIfConcrete<T> = 0>
inline T* GetAsOrNullImpl(MaybeConstRoot* n) {
  if (n->node_kind() == T::kConcreteNodeKind) {
    return static_cast<T*>(n);
  } else {
    return nullptr;
  }
}

// GetAsOrNull implemented simply with dynamic_cast.  This is used for
// intermediate nodes (such as ASTExpression).
template <typename T, typename MaybeConstRoot, EnableIfNotConcrete<T> = 0>
inline T* GetAsOrNullImpl(MaybeConstRoot* r) {
  // Note, if this proves too slow, it could be implemented with ancestor enums
  // sets.
  return dynamic_cast<T*>(r);
}

}  // namespace googlesql::ast_node_internal

#endif  // GOOGLESQL_PARSER_AST_NODE_INTERNAL_H_
