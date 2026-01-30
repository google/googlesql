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

#include "googlesql/resolved_ast/make_node_vector.h"

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/type_parameters.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "gtest/gtest.h"

namespace googlesql {
namespace {
using make_node_vector_internal::LowestCommonAncestor;
using make_node_vector_internal::NodeVectorT;
}  // namespace

TEST(Construction, Example) {
  TypeFactory type_factory;
  const Type* type = type_factory.get_bool();
  const FunctionSignature signature = {type, {}, nullptr /* context */};
  std::shared_ptr<ResolvedFunctionCallInfo> call_info;
  const ResolvedColumn col = {1, googlesql::IdString::MakeGlobal("table_name"),
                              googlesql::IdString::MakeGlobal("name"), type};
  auto func = std::make_unique<Function>("name", "group", Function::SCALAR);
  auto third_value = MakeResolvedLiteral(Value::Int64(3));
  auto call = MakeResolvedFunctionCall(
      type /* ownership is not transferred for Type objects. */,
      func.get() /* ownership is not transferred for Function objects */,
      signature,
      MakeNodeVector(MakeResolvedColumnRef(type, col, false),
                     MakeResolvedLiteral(Value::Int64(3)),
                     std::move(third_value)),
      ResolvedFunctionCallBase::DEFAULT_ERROR_MODE, call_info);
}

TEST(MakeNodeVector, LowestCommonAncestorTypeTraitsInheritance) {
  using Root = ResolvedNode;
  using Child1 = ResolvedScan;
  using GrandChild1A = ResolvedSingleRowScan;
  using GrandChild1B = ResolvedTableScan;
  using Child2 = ResolvedExpr;
  using GrandChild2A = ResolvedLiteral;
  using GrandChild2B = ResolvedParameter;

  static_assert(
      std::is_same<LowestCommonAncestor<Root, Root>::type, Root>::value,
      "identity on Root");
  static_assert(
      std::is_same<LowestCommonAncestor<Child1, Child1>::type, Child1>::value,
      "identity on Child");
  static_assert(
      std::is_same<LowestCommonAncestor<Child1, Root>::type, Root>::value,
      "Root is LCA of Root and Child");
  static_assert(std::is_same<LowestCommonAncestor<Child1, GrandChild1A>::type,
                             Child1>::value,
                "Parent is LCA of Parent and Child");
  static_assert(
      std::is_same<LowestCommonAncestor<Child1, Child2>::type, Root>::value,
      "Root is LCA of siblings");
  static_assert(
      std::is_same<LowestCommonAncestor<GrandChild1A, GrandChild1B>::type,
                   Child1>::value,
      "Parent is LCA of siblings");
  static_assert(
      std::is_same<LowestCommonAncestor<GrandChild1A, GrandChild2B>::type,
                   Root>::value,
      "Root is LCA of grandchildren (from different families)");

  static_assert(std::is_same<LowestCommonAncestor<Root>::type, Root>::value,
                "Single Arg identity (on root)");
  static_assert(std::is_same<LowestCommonAncestor<Child1>::type, Child1>::value,
                "Sing Arg identity (non-root)");
  static_assert(
      std::is_same<LowestCommonAncestor<Root, Root, Root>::type, Root>::value,
      "multi-arg identity (root)");

  static_assert(std::is_same<LowestCommonAncestor<Child1, Child1, Child1>::type,
                             Child1>::value,
                "multi-arg identity (non-root)");
  static_assert(
      std::is_same<LowestCommonAncestor<Child1, Child1, Child1, Child1>::type,
                   Child1>::value,
      "");
  static_assert(std::is_same<LowestCommonAncestor<GrandChild1A, Child1,
                                                  GrandChild1A, Child1>::type,
                             Child1>::value,
                "Parent is LCA of mixed multi-arg");
  static_assert(
      std::is_same<
          LowestCommonAncestor<GrandChild1A, GrandChild2A, Child1>::type,
          Root>::value,
      "Root is LCA of mixed multi-arg");

  static_assert(
      std::is_same<
          LowestCommonAncestor<
              Child1, LowestCommonAncestor<Child1, Child1>::type>::type,
          Child1>::value,
      "Chaining works");
}

TEST(MakeNodeVector, LowestCommonAncestorTypeTraitsConstness) {
  using Root = ResolvedNode;
  using Child1 = ResolvedScan;
  using CRoot = const Root;
  using CChild = const Child1;

  static_assert(std::is_same<LowestCommonAncestor<CRoot>::type, CRoot>::value,
                "Basic constness");

  static_assert(
      std::is_same<LowestCommonAncestor<Root, CRoot>::type, CRoot>::value,
      "Const wins");

  static_assert(
      std::is_same<LowestCommonAncestor<Root, CChild>::type, CRoot>::value,
      "Const wins, with const child");

  static_assert(
      std::is_same<LowestCommonAncestor<CRoot, CChild>::type, CRoot>::value,
      "Const wins, with all const inputs");

  static_assert(
      std::is_same<LowestCommonAncestor<Root, Root, Root, CChild>::type,
                   CRoot>::value,
      "Const wins, not a democracy");

  static_assert(
      std::is_same<LowestCommonAncestor<CChild, Root, Root, Root>::type,
                   CRoot>::value,
      "Const wins, position doesn't matter");
  static_assert(
      std::is_same<LowestCommonAncestor<Root, CChild, Root, Root>::type,
                   CRoot>::value,
      "Const wins, position doesn't matter");
}

TEST(MakeNodeVector, MakeNodeVectorTypeTraits) {
  using Root = std::unique_ptr<ResolvedNode>;
  using Child1 = std::unique_ptr<ResolvedScan>;
  using CChild1 = std::unique_ptr<const ResolvedScan>;
  using Child2 = std::unique_ptr<ResolvedExpr>;
  using Child3 = std::unique_ptr<ResolvedStatement>;

  using RootVector = std::vector<std::unique_ptr<ResolvedNode>>;
  using CRootVector = std::vector<std::unique_ptr<const ResolvedNode>>;

  static_assert(std::is_same<NodeVectorT<Root>, RootVector>::value, "");
  static_assert(std::is_same<NodeVectorT<Root, Child1>, RootVector>::value, "");
  static_assert(std::is_same<NodeVectorT<Child1, Child2>, RootVector>::value,
                "");
  static_assert(
      std::is_same<NodeVectorT<Child1, Child2, Child3>, RootVector>::value, "");
  static_assert(std::is_same<NodeVectorT<CChild1, Child2>, CRootVector>::value,
                "");
}

TEST(MakeNodeVector, MakeVectorBasicConstruction) {
  std::vector<std::unique_ptr<ResolvedLiteral>> single_literal =
      MakeNodeVector(MakeResolvedLiteral(Value::Int64(5)));
  ASSERT_EQ(single_literal.size(), 1);

  std::vector<std::unique_ptr<ResolvedLiteral>> two_literal =
      MakeNodeVector(MakeResolvedLiteral(Value::Int64(1)),
                     MakeResolvedLiteral(Value::Int64(2)));
  ASSERT_EQ(two_literal.size(), 2);

  std::vector<std::unique_ptr<ResolvedLiteral>> multi_literal =
      MakeNodeVector(MakeResolvedLiteral(Value::Int64(1)),
                     MakeResolvedLiteral(Value::Int64(2)),
                     MakeResolvedLiteral(Value::Int64(3)),
                     MakeResolvedLiteral(Value::Int64(4)));
  ASSERT_EQ(multi_literal.size(), 4);
}

TEST(MakeNodeVectorP, MakeVectorPBasicConstruction) {
  std::vector<std::unique_ptr<const ResolvedLiteral>> single_literal =
      MakeNodeVectorP<const ResolvedLiteral>(
          MakeResolvedLiteral(Value::Int64(5)));
  ASSERT_EQ(single_literal.size(), 1);

  std::vector<std::unique_ptr<const ResolvedLiteral>> two_literal =
      MakeNodeVectorP<const ResolvedLiteral>(
          MakeResolvedLiteral(Value::Int64(1)),
          MakeResolvedLiteral(Value::Int64(2)));
  ASSERT_EQ(two_literal.size(), 2);

  std::vector<std::unique_ptr<const ResolvedLiteral>> multi_literal =
      MakeNodeVectorP<const ResolvedLiteral>(
          MakeResolvedLiteral(Value::Int64(1)),
          MakeResolvedLiteral(Value::Int64(2)),
          MakeResolvedLiteral(Value::Int64(3)),
          MakeResolvedLiteral(Value::Int64(4)));
  ASSERT_EQ(multi_literal.size(), 4);
}

TEST(MakeNodeVector, MakeVectorConstructionLowestCommonAncestor) {
  const ResolvedColumn c1 =
      ResolvedColumn(1, googlesql::IdString::MakeGlobal("MakeColumn"),
                     googlesql::IdString::MakeGlobal("C"), types::Int32Type());

  std::vector<std::unique_ptr<ResolvedExpr>> coerced_to_expr =
      MakeNodeVector(MakeResolvedLiteral(Value::Int64(1)),
                     MakeResolvedColumnRef(c1.type(), c1, false));
  ASSERT_EQ(coerced_to_expr.size(), 2);
}

TEST(MakeNodeVectorP, MakeVectorConstructionIgnoreLowestCommonAncestor) {
  const ResolvedColumn c1 =
      ResolvedColumn(1, googlesql::IdString::MakeGlobal("MakeColumn"),
                     googlesql::IdString::MakeGlobal("C"), types::Int32Type());

  std::vector<std::unique_ptr<ResolvedExpr>> coerced_to_expr =
      MakeNodeVectorP<ResolvedExpr>(
          MakeResolvedLiteral(Value::Int64(1)),
          MakeResolvedColumnRef(c1.type(), c1, false));
  ASSERT_EQ(coerced_to_expr.size(), 2);
  std::vector<std::unique_ptr<const ResolvedExpr>> coerced_to_const_expr =
      MakeNodeVectorP<const ResolvedExpr>(
          MakeResolvedLiteral(Value::Int64(1)),
          MakeResolvedColumnRef(c1.type(), c1, false));
  ASSERT_EQ(coerced_to_const_expr.size(), 2);
}

TEST(MakeNodeVector, MakeVectorConstructionConstness) {
  const ResolvedColumn c1 =
      ResolvedColumn(1, googlesql::IdString::MakeGlobal("MakeColumn"),
                     googlesql::IdString::MakeGlobal("C"), types::Int32Type());

  std::unique_ptr<const ResolvedLiteral> const_literal1 =
      MakeResolvedLiteral(Value::Int64(1));
  std::vector<std::unique_ptr<const ResolvedLiteral>> single_const_literal =
      MakeNodeVector(std::move(const_literal1));
  ASSERT_EQ(single_const_literal.size(), 1);

  std::unique_ptr<const ResolvedLiteral> const_literal2 =
      MakeResolvedLiteral(Value::Int64(1));

  std::vector<std::unique_ptr<const ResolvedLiteral>> coerced_to_const =
      MakeNodeVector(std::move(const_literal2),
                     MakeResolvedLiteral(Value::Int64(1)));
  ASSERT_EQ(coerced_to_const.size(), 2);

  std::unique_ptr<const ResolvedLiteral> const_literal3 =
      MakeResolvedLiteral(Value::Int64(1));

  std::vector<std::unique_ptr<const ResolvedExpr>> coerced_to_const_expr =
      MakeNodeVector(std::move(const_literal3),
                     MakeResolvedColumnRef(c1.type(), c1, false));
  ASSERT_EQ(coerced_to_const_expr.size(), 2);
}

TEST(Construction, MultipleLists) {
  // Ensure Empty initializer lists work okay.
  auto a1 = MakeResolvedColumnAnnotations(/*collation_name=*/nullptr, false, {},
                                          {}, TypeParameters());
  auto a2 = MakeResolvedColumnAnnotations(
      /*collation_name=*/nullptr, false, MakeNodeVector(MakeResolvedOption()),
      {}, TypeParameters());
  auto a3 = MakeResolvedColumnAnnotations(
      /*collation_name=*/nullptr, false, {},
      MakeNodeVector(MakeResolvedColumnAnnotations()), TypeParameters());

  // Ensure non-empty lists all work (and nesting, incidentally).
  auto a4 = MakeResolvedColumnAnnotations(
      /*collation_name=*/nullptr, false, MakeNodeVector(MakeResolvedOption()),
      MakeNodeVector(
          MakeResolvedColumnAnnotations(),
          MakeResolvedColumnAnnotations(/*collation_name=*/nullptr, false, {},
                                        {}, TypeParameters())),
      TypeParameters());
}

}  // namespace googlesql
