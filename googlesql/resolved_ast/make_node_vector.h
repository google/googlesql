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

#ifndef GOOGLESQL_RESOLVED_AST_MAKE_NODE_VECTOR_H_
#define GOOGLESQL_RESOLVED_AST_MAKE_NODE_VECTOR_H_

#include <memory>
#include <utility>
#include <vector>

#include "googlesql/resolved_ast/make_node_vector_internal.h"  
#include "googlesql/resolved_ast/resolved_ast.h"

// This library provides a more readable mechanism for construction of
// ResolvedAST nodes with node vector inputs. This is primarily useful for
// single statement construction of node trees for unit tests.
//
// Example:
// #include "googlesql/resolved_ast/resolved_ast.h"
// #include "googlesql/resolved_ast/testing/make_node_vector.h"
// auto call = MakeResolvedFunctionCall(
//      ...,
//      // MakeNodeVector will return a value of type:
//      //   vector<unique_ptr<ResolvedExpr>> argument_list
//      MakeNodeVector(
//          // Returns unique_ptr<ResolvedColumnRef>
//          // which is a descendant of ResolvedExpr.
//          MakeResolvedColumnRef(type, col, false),
//          // Returns unique_ptr<ResolvedLiteral>
//          // which is a descendant of ResolvedExpr.
//          MakeResolvedLiteral(Value::Int64(5)))
//          ...);
//
// MakeNodeVector works in tandem with generated factory methods in
// resolved_ast.h to provide more natural type coercion of node vector inputs
// similar to initializer lists (which are incompatible with unique_ptr).
namespace googlesql {

// Creates a "NodeVector" (vector<unique_ptr<NodeX>>) from unique_ptr inputs.
//
// More precisely, MakeNodeVector constructs a
// std::vector<std::unique_ptr<NodeT>> from a heterogeneous list of arguments
// consisting of std::unique_ptr<NodeN> such that each NodeN is a descendant of
// NodeT in the ResolvedNode class hierarchy.
//
// This is necessary because initializer lists do not work with move-only types
// like unique_ptr. Thus you cannot simply construct a vector as:
// std::vector<std::unique_ptr<MakeResolvedLiteral>> v =
//    { MakeResolvedLiteral(), MakeResolvedLiteral() };
//
// The returned vector will have an element type of:
//   std::unique_ptr<lowest-common-ancestor<input-types>>
//
// Simple example with inputs and outputs with the same type:
//   std::vector<std::unique_ptr<ResolvedLiteral>> v =
//       MakeNodeVector(MakeResolvedLiteral(...), MakeResolvedLiteral(...));
//
// Element type is computed to be the lowest-common-ancestor of the inputs.
//   std::vector<std::unique_ptr<ResolvedExpr>> v =
//       MakeNodeVector(MakeResolvedScan(...), MakeResolvedParameter(...));
//
// Constness is also computed from the inputs.
//   std::unique_ptr<const ResolvedScan> scan = ...;
//   std::vector<std::unique_ptr<const ResolvedExpr>> v =
//       testing::MakeNodeVector(std::move(scan), MakeResolvedParameter(...));
template <class... Ts>
make_node_vector_internal::NodeVectorT<std::unique_ptr<Ts>...> MakeNodeVector(
    std::unique_ptr<Ts>&&... args) {
  make_node_vector_internal::NodeVectorT<std::unique_ptr<Ts>...> vec;
  make_node_vector_internal::MakeNodeVectorInternal(
      &vec, std::forward<std::unique_ptr<Ts>>(args)...);
  return vec;
}

template <typename R, class... Ts>
std::vector<std::unique_ptr<R>> MakeNodeVectorP(std::unique_ptr<Ts>&&... args) {
  std::vector<std::unique_ptr<R>> vec;
  make_node_vector_internal::MakeNodeVectorInternal(
      &vec, std::forward<std::unique_ptr<Ts>>(args)...);
  return vec;
}

}  // namespace googlesql

#endif  // GOOGLESQL_RESOLVED_AST_MAKE_NODE_VECTOR_H_
