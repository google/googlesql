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

#include "googlesql/public/types/graph_path_type.h"

#include <string>

#include "googlesql/common/testing/proto_matchers.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/type_parameters.pb.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/collation.h"
#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_deserializer.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/type_modifiers.h"
#include "googlesql/public/types/type_parameters.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/hash/hash_testing.h"
#include "absl/status/status.h"
#include "google/protobuf/text_format.h"

namespace googlesql {
namespace {

using ::googlesql::testing::EqualsProto;
using ::googlesql::types::Int32Type;
using ::googlesql::types::StringType;
using ::testing::HasSubstr;
using ::testing::IsNull;
using ::testing::NotNull;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;

TEST(GraphPathValueTest, GraphPathTypeCannotContainNull) {
  TypeFactory type_factory;
  const GraphElementType* node_type;
  GOOGLESQL_ASSERT_OK(
      type_factory.MakeGraphElementType({"graph_name"}, GraphElementType::kNode,
                                        {{"p0", StringType()}}, &node_type));
  const GraphElementType* edge_type;
  GOOGLESQL_ASSERT_OK(
      type_factory.MakeGraphElementType({"graph_name"}, GraphElementType::kEdge,
                                        {{"p1", Int32Type()}}, &edge_type));
  const GraphPathType* path_type;
  EXPECT_THAT(type_factory.MakeGraphPathType(/*node_type=*/nullptr, edge_type,
                                             &path_type),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Node type cannot be null")));
  EXPECT_THAT(type_factory.MakeGraphPathType(node_type, /*edge_type=*/nullptr,
                                             &path_type),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Edge type cannot be null")));
  EXPECT_THAT(type_factory.MakeGraphPathType(/*node_type*/ nullptr,
                                             /*edge_type*/ nullptr, &path_type),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("cannot be null")));
}

TEST(GraphPathValueTest, GraphPathTypeWrongKind) {
  TypeFactory type_factory;
  const GraphElementType* node_type;
  GOOGLESQL_ASSERT_OK(
      type_factory.MakeGraphElementType({"graph_name"}, GraphElementType::kNode,
                                        {{"p0", StringType()}}, &node_type));
  const GraphElementType* edge_type;
  GOOGLESQL_ASSERT_OK(
      type_factory.MakeGraphElementType({"graph_name"}, GraphElementType::kEdge,
                                        {{"p1", Int32Type()}}, &edge_type));
  const GraphPathType* path_type;
  EXPECT_THAT(type_factory.MakeGraphPathType(edge_type, edge_type, &path_type),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Node type must be a node")));
  EXPECT_THAT(type_factory.MakeGraphPathType(node_type, node_type, &path_type),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Edge type must be an edge")));
}

TEST(GraphPathValueTest, GraphPathTypeDifferentGraphReferences) {
  TypeFactory type_factory;
  const GraphElementType* node_type;
  GOOGLESQL_ASSERT_OK(
      type_factory.MakeGraphElementType({"graph_name"}, GraphElementType::kNode,
                                        {{"p0", StringType()}}, &node_type));
  const GraphElementType* bad_edge_type;
  GOOGLESQL_ASSERT_OK(type_factory.MakeGraphElementType(
      {"bad_graph_name"}, GraphElementType::kEdge, {{"p1", Int32Type()}},
      &bad_edge_type));
  const GraphElementType* edge_type;
  GOOGLESQL_ASSERT_OK(
      type_factory.MakeGraphElementType({"GRAPH_name"}, GraphElementType::kEdge,
                                        {{"p1", Int32Type()}}, &edge_type));

  const GraphPathType* path_type;
  EXPECT_THAT(
      type_factory.MakeGraphPathType(node_type, bad_edge_type, &path_type),
      StatusIs(
          absl::StatusCode::kInternal,
          HasSubstr("Node and edge types must have the same graph reference")));
  GOOGLESQL_ASSERT_OK(type_factory.MakeGraphPathType(node_type, edge_type, &path_type));
  EXPECT_EQ(path_type->node_type(), node_type);
  EXPECT_EQ(path_type->edge_type(), edge_type);
}

TEST(GraphPathTypeTest, InternalProductModeSupportedTypeTest) {
  TypeFactory factory;
  LanguageOptions options;
  options.set_product_mode(PRODUCT_INTERNAL);
  const GraphElementType* node_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType({"graph_name"},
                                         GraphElementType::kNode,
                                         {{"p0", StringType()}}, &node_type));
  const GraphElementType* edge_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType({"graph_name"},
                                         GraphElementType::kEdge,
                                         {{"p1", Int32Type()}}, &edge_type));
  const GraphPathType* path_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphPathType(node_type, edge_type, &path_type));
  ASSERT_THAT(path_type, NotNull());

  EXPECT_FALSE(path_type->IsSupportedType(options));
  options.EnableLanguageFeature(FEATURE_SQL_GRAPH);
  EXPECT_FALSE(path_type->IsSupportedType(options));
  options.EnableLanguageFeature(FEATURE_SQL_GRAPH_PATH_TYPE);
  EXPECT_TRUE(path_type->IsSupportedType(options));
}

TEST(GraphPathTypeTest, ExternalProductModeSupportedTypeTest) {
  TypeFactory factory;
  LanguageOptions options;
  options.set_product_mode(PRODUCT_EXTERNAL);
  const GraphElementType* node_type;
  GOOGLESQL_ASSERT_OK(
      factory.MakeGraphElementType({"graph_name"}, GraphElementType::kNode,
                                   {{"p0", factory.get_uint64()}}, &node_type));
  const GraphElementType* edge_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType({"graph_name"},
                                         GraphElementType::kEdge,
                                         {{"p1", Int32Type()}}, &edge_type));
  const GraphPathType* path_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphPathType(node_type, edge_type, &path_type));
  ASSERT_THAT(path_type, NotNull());

  EXPECT_FALSE(path_type->IsSupportedType(options));

  // uint64 is not supported in external mode and it's part of the node type.
  options.EnableLanguageFeature(FEATURE_SQL_GRAPH);
  options.EnableLanguageFeature(FEATURE_SQL_GRAPH_PATH_TYPE);
  EXPECT_FALSE(path_type->IsSupportedType(options));
}

TEST(GraphPathTypeTest, BasicTest) {
  TypeFactory factory;
  const Type* string_type = factory.get_string();
  const Type* bytes_type = factory.get_bytes();
  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"a", string_type}, {"b c", bytes_type}},
                                   &struct_type));
  const ArrayType* array_type;
  GOOGLESQL_ASSERT_OK(factory.MakeArrayType(string_type, &array_type));

  const GraphElementType* node_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType({"graph_name"},
                                         GraphElementType::ElementKind::kNode,
                                         {{"a", string_type},
                                          {"b c", bytes_type},
                                          {"e", array_type},
                                          {"d", struct_type}},
                                         &node_type));

  const GraphElementType* edge_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType({"graph_name"},
                                         GraphElementType::kEdge,
                                         {{"p1", Int32Type()}}, &edge_type));

  const GraphPathType* path_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphPathType(node_type, edge_type, &path_type));
  ASSERT_THAT(path_type, NotNull());

  EXPECT_EQ(path_type->node_type(), node_type);
  EXPECT_EQ(path_type->edge_type(), edge_type);

  EXPECT_EQ(path_type->DebugString(),
            "PATH<node: GRAPH_NODE(graph_name)<a STRING, `b c` BYTES, d "
            "STRUCT<a STRING, `b c` BYTES>, e ARRAY<STRING>>, edge: "
            "GRAPH_EDGE(graph_name)<p1 INT32>>");
  EXPECT_EQ(path_type->TypeName(PRODUCT_INTERNAL),
            "PATH<GRAPH_NODE(graph_name)<a STRING, `b c` BYTES, d "
            "STRUCT<a STRING, `b c` BYTES>, e ARRAY<STRING>>, "
            "GRAPH_EDGE(graph_name)<p1 INT32>>");
  EXPECT_EQ(
      path_type->ShortTypeName(PRODUCT_INTERNAL),
      "PATH<GRAPH_NODE(graph_name)<a STRING, `b c` BYTES, d STRUCT<a STRING, "
      "`b c` BYTES>, ...>, GRAPH_EDGE(graph_name)<p1 INT32>>");

  TypeModifiers type_modifiers;
  EXPECT_THAT(
      path_type->TypeNameWithModifiers(type_modifiers, PRODUCT_INTERNAL),
      IsOkAndHolds("PATH<GRAPH_NODE(graph_name)<a STRING, `b c` BYTES, d "
                   "STRUCT<a STRING, `b c` BYTES>, e ARRAY<STRING>>, "
                   "GRAPH_EDGE(graph_name)<p1 INT32>>"));

  StringTypeParametersProto param_proto;
  param_proto.set_is_max_length(true);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_parameters,
                       TypeParameters::MakeStringTypeParameters(param_proto));
  const TypeModifiers inconsistent_type_modifiers =
      TypeModifiers::MakeTypeModifiers(type_parameters, Collation());
  EXPECT_THAT(path_type->TypeNameWithModifiers(inconsistent_type_modifiers,
                                               PRODUCT_INTERNAL),
              StatusIs(absl::StatusCode::kInvalidArgument));

  EXPECT_FALSE(path_type->IsSimpleType());
  EXPECT_FALSE(path_type->IsEnum());
  EXPECT_FALSE(path_type->IsArray());
  EXPECT_FALSE(path_type->IsStruct());
  EXPECT_FALSE(path_type->IsProto());
  EXPECT_FALSE(path_type->IsStructOrProto());
  EXPECT_FALSE(path_type->IsRange());
  EXPECT_FALSE(path_type->IsGraphElement());
  EXPECT_TRUE(path_type->IsGraphPath());
  EXPECT_THAT(path_type->AsArray(), IsNull());
  EXPECT_THAT(path_type->AsStruct(), IsNull());
  EXPECT_THAT(path_type->AsProto(), IsNull());
  EXPECT_THAT(path_type->AsEnum(), IsNull());
  EXPECT_THAT(path_type->AsRange(), IsNull());
  EXPECT_THAT(path_type->AsGraphElement(), IsNull());
  EXPECT_EQ(path_type->AsGraphPath(), path_type);

  LanguageOptions language_options;
  LanguageOptions language_options_with_path_grouping;
  language_options_with_path_grouping.EnableLanguageFeature(
      FEATURE_GROUP_BY_GRAPH_PATH);
  std::string no_grouping_type;
  std::string no_ordering_type;
  std::string no_partitioning_type;
  std::string no_returning_type;
  EXPECT_TRUE(path_type->SupportsEquality());
  EXPECT_TRUE(path_type->SupportsGrouping(language_options_with_path_grouping,
                                          &no_grouping_type));
  EXPECT_TRUE(path_type->SupportsPartitioning(
      language_options_with_path_grouping, &no_partitioning_type));
  EXPECT_FALSE(
      path_type->SupportsGrouping(language_options, &no_grouping_type));
  EXPECT_FALSE(
      path_type->SupportsOrdering(language_options, &no_ordering_type));
  EXPECT_FALSE(
      path_type->SupportsPartitioning(language_options, &no_partitioning_type));
  EXPECT_FALSE(
      path_type->SupportsReturning(language_options, &no_returning_type));
  EXPECT_TRUE(path_type->SupportsGrouping(language_options_with_path_grouping));
  EXPECT_TRUE(
      path_type->SupportsPartitioning(language_options_with_path_grouping));
  EXPECT_FALSE(path_type->SupportsGrouping(language_options));
  EXPECT_FALSE(path_type->SupportsOrdering(language_options,
                                           /*type_description=*/nullptr));
  EXPECT_FALSE(path_type->SupportsPartitioning(language_options));
  EXPECT_FALSE(path_type->SupportsReturning(language_options));
  EXPECT_EQ(no_grouping_type, "GRAPH_PATH");
  EXPECT_EQ(no_ordering_type, "GRAPH_PATH");
  EXPECT_EQ(no_partitioning_type, "GRAPH_PATH");
  EXPECT_EQ(no_returning_type, "GRAPH_PATH");
}

TEST(GraphPathTypeTest, Equality) {
  TypeFactory factory;
  const Type* string_type = factory.get_string();
  const Type* bytes_type = factory.get_bytes();
  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"a", string_type}, {"b c", bytes_type}},
                                   &struct_type));
  const ArrayType* array_type;
  GOOGLESQL_ASSERT_OK(factory.MakeArrayType(string_type, &array_type));

  const GraphElementType* node_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType({"graph_name"},
                                         GraphElementType::ElementKind::kNode,
                                         {{"a", string_type},
                                          {"b c", bytes_type},
                                          {"e", array_type},
                                          {"d", struct_type}},
                                         &node_type));

  const GraphElementType* node_type2;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType({"graph_name"},
                                         GraphElementType::ElementKind::kNode,
                                         {{"p1", Int32Type()}}, &node_type2));

  const GraphElementType* node_type2_supertype;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      {{"p1", Int32Type()}, {"p2", StringType()}}, &node_type2_supertype));

  const GraphElementType* edge_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType({"graph_name"},
                                         GraphElementType::kEdge,
                                         {{"p1", Int32Type()}}, &edge_type));

  const GraphElementType* edge_supertype;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType(
      {"graph_name"}, GraphElementType::kEdge,
      {{"p0", StringType()}, {"p1", Int32Type()}}, &edge_supertype));

  const GraphPathType* path_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphPathType(node_type, edge_type, &path_type));
  const GraphPathType* equivalent_path_type;
  GOOGLESQL_ASSERT_OK(
      factory.MakeGraphPathType(node_type, edge_type, &equivalent_path_type));
  EXPECT_TRUE(path_type->Equals(equivalent_path_type));
  EXPECT_TRUE(path_type->Equivalent(equivalent_path_type));
  EXPECT_TRUE(path_type->CoercibleTo(equivalent_path_type));
  EXPECT_TRUE(equivalent_path_type->CoercibleTo(path_type));

  const GraphPathType* different_path_type;
  GOOGLESQL_ASSERT_OK(
      factory.MakeGraphPathType(node_type2, edge_type, &different_path_type));
  EXPECT_FALSE(path_type->Equals(different_path_type));
  EXPECT_FALSE(path_type->Equivalent(different_path_type));
  EXPECT_FALSE(path_type->CoercibleTo(different_path_type));
  EXPECT_FALSE(different_path_type->CoercibleTo(path_type));

  const GraphPathType* different_path_type_node_supertype;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphPathType(node_type2_supertype, edge_type,
                                      &different_path_type_node_supertype));
  EXPECT_FALSE(path_type->Equals(different_path_type_node_supertype));
  EXPECT_FALSE(path_type->Equivalent(different_path_type_node_supertype));

  EXPECT_FALSE(path_type->CoercibleTo(different_path_type_node_supertype));
  EXPECT_FALSE(different_path_type_node_supertype->CoercibleTo(path_type));
  EXPECT_TRUE(
      different_path_type->CoercibleTo(different_path_type_node_supertype));
  EXPECT_FALSE(
      different_path_type_node_supertype->CoercibleTo(different_path_type));

  const GraphPathType* different_path_type_edge_supertype;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphPathType(node_type, edge_supertype,
                                      &different_path_type_edge_supertype));
  EXPECT_FALSE(path_type->Equals(different_path_type_edge_supertype));
  EXPECT_FALSE(path_type->Equivalent(different_path_type_edge_supertype));
  EXPECT_TRUE(path_type->CoercibleTo(different_path_type_edge_supertype));
  EXPECT_FALSE(different_path_type_edge_supertype->CoercibleTo(path_type));
  EXPECT_FALSE(
      different_path_type->CoercibleTo(different_path_type_edge_supertype));
  EXPECT_FALSE(
      different_path_type_edge_supertype->CoercibleTo(different_path_type));

  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(
      {path_type, equivalent_path_type, different_path_type,
       different_path_type_node_supertype}));
}

TEST(GraphPathTypeTest, TypeDeserializerGraphElementInvalidProto) {
  TypeFactory factory;
  const TypeDeserializer type_deserializer(&factory);
  TypeProto type_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(R"pb(
                                                    type_kind: TYPE_GRAPH_PATH
                                                  )pb",
                                                  &type_proto));
  EXPECT_THAT(
      type_deserializer.Deserialize(type_proto),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Invalid TypeProto provided for deserialization:")));
}

TEST(GraphPathTypeTest, TypeDeserializerGraphPathInvalid) {
  TypeFactory factory;
  const TypeDeserializer type_deserializer(&factory);
  TypeProto type_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        type_kind: TYPE_GRAPH_PATH
        graph_path_type {}
      )pb",
      &type_proto));
  EXPECT_THAT(
      type_deserializer.Deserialize(type_proto),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("GraphElementType must have a non-empty graph reference")));
}

TEST(GraphPathTypeTest, TypeDeserializerGraphPathInvalidNodeType) {
  TypeFactory factory;
  const TypeDeserializer type_deserializer(&factory);
  TypeProto type_proto;

  // `node_type` isn't correct, it needs the `kind` field.
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        type_kind: TYPE_GRAPH_PATH
        graph_path_type {
          node_type { graph_reference: "graph" }
          edge_type {
            graph_reference: "graph"
            kind: KIND_EDGE
            property_type {
              name: "a"
              value_type { type_kind: TYPE_STRING }
            }
          }
        }
      )pb",
      &type_proto));
  EXPECT_THAT(type_deserializer.Deserialize(type_proto),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("while deserializing the node type")));
}

TEST(GraphPathTypeTest, TypeDeserializerGraphPath) {
  TypeFactory factory;
  const TypeDeserializer type_deserializer(&factory);
  TypeProto type_proto;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        type_kind: TYPE_GRAPH_PATH
        graph_path_type {
          node_type {
            graph_reference: "graph"
            kind: KIND_NODE
            is_dynamic: false
          }
          edge_type {
            graph_reference: "graph"
            kind: KIND_EDGE
            property_type {
              name: "a"
              value_type { type_kind: TYPE_STRING }
            }
            is_dynamic: false
          }
        }
      )pb",
      &type_proto));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* deserialized_path_type,
                       type_deserializer.Deserialize(type_proto));
  const GraphElementType* node_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType({"graph"}, GraphElementType::kNode, {},
                                         &node_type));
  const GraphElementType* edge_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType({"graph"}, GraphElementType::kEdge,
                                         {{"a", factory.get_string()}},
                                         &edge_type));
  const GraphPathType* path_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphPathType(node_type, edge_type, &path_type));
  EXPECT_TRUE(deserialized_path_type->Equals(path_type));
  EXPECT_TRUE(deserialized_path_type->Equivalent(path_type));
  TypeProto roundtrip_proto;
  GOOGLESQL_ASSERT_OK(path_type->SerializeToProtoAndFileDescriptors(&roundtrip_proto));
}

}  // namespace
}  // namespace googlesql
