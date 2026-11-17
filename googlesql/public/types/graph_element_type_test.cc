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

#include "googlesql/public/types/graph_element_type.h"

#include <string>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/type_parameters.pb.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/collation.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_deserializer.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/type_modifiers.h"
#include "googlesql/public/types/type_parameters.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/hash/hash.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "google/protobuf/text_format.h"

namespace googlesql {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::Pointee;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;

MATCHER_P2(IsPropertyType, name, value_type, "") {
  return arg.name == name && arg.value_type->Equals(value_type);
}

TEST(GraphElementTypeTest, MakeGraphElementType) {
  TypeFactory factory;
  const Type* string_type = factory.get_string();
  const Type* bytes_type = factory.get_bytes();

  const GraphElementType* empty_graph_element_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode, {},
      /*element_tables=*/{}, &empty_graph_element_type));
  EXPECT_THAT(empty_graph_element_type->property_types(), IsEmpty());

  const GraphElementType* graph_element_type;
  EXPECT_THAT(
      factory.MakeGraphElementType({}, GraphElementType::ElementKind::kNode, {},
                                   /*element_tables=*/{}, &graph_element_type),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Graph reference cannot be empty")));

  EXPECT_THAT(factory.MakeGraphElementType(
                  {"graph_name"}, GraphElementType::ElementKind::kNode,
                  {{"", string_type}},
                  /*element_tables=*/{}, &graph_element_type),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Property type name cannot be empty")));

  EXPECT_THAT(factory.MakeGraphElementType(
                  {"graph_name"}, GraphElementType::ElementKind::kNode,
                  {{"a", string_type}, {"a", bytes_type}},
                  /*element_tables=*/{}, &graph_element_type),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Inconsistent property: a")));

  EXPECT_THAT(factory.MakeGraphElementType(
                  {"graph_name"}, GraphElementType::ElementKind::kNode,
                  {{"a", string_type}, {"A", bytes_type}},
                  /*element_tables=*/{}, &graph_element_type),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Inconsistent property: A")));

  EXPECT_THAT(factory.MakeGraphElementType(
                  {"graph_name"}, GraphElementType::ElementKind::kNode,
                  {{"", string_type}},
                  /*element_tables=*/{}, &graph_element_type),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Property type name cannot be empty")));

  EXPECT_THAT(graph_element_type, IsNull());

  GOOGLESQL_EXPECT_OK(factory.MakeGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      {{"a", string_type}, {"A", string_type}},
      /*element_tables=*/{}, &graph_element_type));
}

TEST(GraphElementTypeTest, InternalProductModeSupportedTypeTest) {
  TypeFactory factory;
  LanguageOptions options;
  options.set_product_mode(PRODUCT_INTERNAL);
  const GraphElementType* graph_element_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      {{"a", factory.get_uint64()}},
      /*element_tables=*/{}, &graph_element_type));
  ASSERT_THAT(graph_element_type, NotNull());

  EXPECT_FALSE(graph_element_type->IsSupportedType(options));

  options.EnableLanguageFeature(FEATURE_SQL_GRAPH);
  EXPECT_TRUE(graph_element_type->IsSupportedType(options));
}

TEST(GraphElementTypeTest, ExternalProductModeSupportedTypeTest) {
  TypeFactory factory;
  LanguageOptions options;
  options.set_product_mode(PRODUCT_EXTERNAL);
  const GraphElementType* graph_element_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      {{"a", factory.get_uint64()}},
      /*element_tables=*/{}, &graph_element_type));
  ASSERT_THAT(graph_element_type, NotNull());

  EXPECT_FALSE(graph_element_type->IsSupportedType(options));

  // uint64 is not supported in external mode.
  options.EnableLanguageFeature(FEATURE_SQL_GRAPH);
  EXPECT_FALSE(graph_element_type->IsSupportedType(options));
}

TEST(GraphElementTypeTest, LookupFieldTest) {
  // HasField and FindField should behave the same.
  TypeFactory factory;
  const GraphElementType* graph_element_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      {{"a", factory.get_int32()}},
      /*element_tables=*/{}, &graph_element_type));
  int field_idx = -1;
  Type::FindFieldResult result;

  EXPECT_TRUE(graph_element_type->HasField("a", &field_idx));
  EXPECT_EQ(field_idx, 0);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(result, graph_element_type->FindField("a"));
  EXPECT_EQ(Type::HAS_FIELD, result.has_field);
  EXPECT_EQ(result.field_id, 0);

  EXPECT_FALSE(graph_element_type->HasField("b", &field_idx));
  EXPECT_EQ(field_idx, -1);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(result, graph_element_type->FindField("b"));
  EXPECT_EQ(Type::HAS_NO_FIELD, result.has_field);
  EXPECT_EQ(result.field_id, -1);
}

TEST(GraphElementTypeTest, BasicTest) {
  TypeFactory factory;
  const Type* string_type = factory.get_string();
  const Type* bytes_type = factory.get_bytes();
  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"a", string_type}, {"b c", bytes_type}},
                                   &struct_type));
  const ArrayType* array_type;
  GOOGLESQL_ASSERT_OK(factory.MakeArrayType(string_type, &array_type));

  const GraphElementType* graph_element_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      {{"a", string_type},
       {"b c", bytes_type},
       {"e", array_type},
       {"d", struct_type}},
      /*element_tables=*/{}, &graph_element_type));
  EXPECT_THAT(graph_element_type->graph_reference(), ElementsAre("graph_name"));
  EXPECT_TRUE(graph_element_type->IsNode());
  EXPECT_FALSE(graph_element_type->IsEdge());

  EXPECT_EQ(graph_element_type->DebugString(),
            "GRAPH_NODE(graph_name)<a STRING, `b c` BYTES, d STRUCT<a STRING, "
            "`b c` BYTES>, e "
            "ARRAY<STRING>>");
  EXPECT_EQ(graph_element_type->TypeName(PRODUCT_INTERNAL),
            "GRAPH_NODE(graph_name)<a STRING, `b c` BYTES, d STRUCT<a STRING, "
            "`b c` BYTES>, e "
            "ARRAY<STRING>>");
  EXPECT_EQ(
      graph_element_type->ShortTypeName(PRODUCT_INTERNAL),
      "GRAPH_NODE(graph_name)<a STRING, `b c` BYTES, d STRUCT<a STRING, `b c` "
      "BYTES>, ...>");

  TypeModifiers type_modifiers;
  EXPECT_THAT(
      graph_element_type->TypeNameWithModifiers(type_modifiers,
                                                PRODUCT_INTERNAL),
      IsOkAndHolds("GRAPH_NODE(graph_name)<a STRING, `b c` BYTES, d STRUCT<a "
                   "STRING, `b c` BYTES>, e ARRAY<STRING>>"));

  StringTypeParametersProto param_proto;
  param_proto.set_is_max_length(true);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters type_parameters,
                       TypeParameters::MakeStringTypeParameters(param_proto));
  const TypeModifiers inconsistent_type_modifiers =
      TypeModifiers::MakeTypeModifiers(type_parameters, Collation());
  EXPECT_THAT(graph_element_type->TypeNameWithModifiers(
                  inconsistent_type_modifiers, PRODUCT_INTERNAL),
              StatusIs(absl::StatusCode::kInvalidArgument));

  EXPECT_THAT(graph_element_type->property_types(),
              ElementsAre(IsPropertyType("a", string_type),
                          IsPropertyType("b c", bytes_type),
                          IsPropertyType("d", struct_type),
                          IsPropertyType("e", array_type)));

  EXPECT_THAT(graph_element_type->FindPropertyType("a"),
              Pointee(IsPropertyType("a", string_type)));
  EXPECT_THAT(graph_element_type->FindPropertyType("b c"),
              Pointee(IsPropertyType("b c", bytes_type)));
  EXPECT_THAT(graph_element_type->FindPropertyType("d"),
              Pointee(IsPropertyType("d", struct_type)));
  EXPECT_THAT(graph_element_type->FindPropertyType("e"),
              Pointee(IsPropertyType("e", array_type)));
  EXPECT_THAT(graph_element_type->FindPropertyType("unknown"), IsNull());

  const GraphElementType* empty_graph_element_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode, {},
      /*element_tables=*/{}, &empty_graph_element_type));
  EXPECT_THAT(empty_graph_element_type->FindPropertyType("a"), IsNull());

  EXPECT_FALSE(graph_element_type->IsSimpleType());
  EXPECT_FALSE(graph_element_type->IsEnum());
  EXPECT_FALSE(graph_element_type->IsArray());
  EXPECT_FALSE(graph_element_type->IsStruct());
  EXPECT_FALSE(graph_element_type->IsProto());
  EXPECT_FALSE(graph_element_type->IsStructOrProto());
  EXPECT_FALSE(graph_element_type->IsRange());
  EXPECT_TRUE(graph_element_type->IsGraphElement());
  EXPECT_THAT(graph_element_type->AsArray(), IsNull());
  EXPECT_THAT(graph_element_type->AsStruct(), IsNull());
  EXPECT_THAT(graph_element_type->AsProto(), IsNull());
  EXPECT_THAT(graph_element_type->AsEnum(), IsNull());
  EXPECT_THAT(graph_element_type->AsRange(), IsNull());
  EXPECT_EQ(graph_element_type->AsGraphElement(), graph_element_type);

  LanguageOptions language_options;
  std::string no_grouping_type;
  std::string no_ordering_type;
  std::string no_partitioning_type;
  EXPECT_TRUE(graph_element_type->SupportsEquality());
  EXPECT_TRUE(graph_element_type->SupportsGrouping(language_options,
                                                   &no_grouping_type));
  EXPECT_FALSE(graph_element_type->SupportsOrdering(language_options,
                                                    &no_ordering_type));
  EXPECT_TRUE(graph_element_type->SupportsPartitioning(language_options,
                                                       &no_partitioning_type));
  EXPECT_TRUE(graph_element_type->SupportsGrouping(language_options));
  EXPECT_FALSE(
      graph_element_type->SupportsOrdering(language_options,
                                           /*type_description=*/nullptr));
  EXPECT_TRUE(graph_element_type->SupportsPartitioning(language_options));
  EXPECT_EQ(no_ordering_type, "GRAPH_ELEMENT");

  // HashTypeParameter hashes sorted lowercase element table names (non-empty
  // case-insensitive/order-insensitive hashing is tested in
  // ElementTablesEqualityAndHashConsistency).
  EXPECT_EQ(
      absl::HashOf(*graph_element_type),
      absl::HashOf(graph_element_type->is_dynamic(),
                   graph_element_type->element_kind(),
                   graph_element_type->property_types(),
                   /*lowercase_element_tables=*/std::vector<std::string>{},
                   graph_element_type->kind()));
}

TEST(GraphElementTypeTest, MakeDynamicGraphElementType) {
  TypeFactory factory;
  const Type* string_type = factory.get_string();
  const Type* bytes_type = factory.get_bytes();

  const GraphElementType* dynamic_type_without_static_properties;
  GOOGLESQL_ASSERT_OK(factory.MakeDynamicGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode, {},
      /*element_tables=*/{}, &dynamic_type_without_static_properties));
  EXPECT_TRUE(dynamic_type_without_static_properties->is_dynamic());
  EXPECT_THAT(dynamic_type_without_static_properties->property_types(),
              IsEmpty());

  const GraphElementType* graph_element_type;
  EXPECT_THAT(factory.MakeDynamicGraphElementType(
                  {}, GraphElementType::ElementKind::kNode, {},
                  /*element_tables=*/{}, &graph_element_type),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Graph reference cannot be empty")));

  EXPECT_THAT(factory.MakeDynamicGraphElementType(
                  {"graph_name"}, GraphElementType::ElementKind::kNode,
                  {{"", string_type}},
                  /*element_tables=*/{}, &graph_element_type),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Property type name cannot be empty")));

  EXPECT_THAT(factory.MakeDynamicGraphElementType(
                  {"graph_name"}, GraphElementType::ElementKind::kNode,
                  {{"a", string_type}, {"a", bytes_type}},
                  /*element_tables=*/{}, &graph_element_type),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Inconsistent property: a")));

  EXPECT_THAT(factory.MakeDynamicGraphElementType(
                  {"graph_name"}, GraphElementType::ElementKind::kNode,
                  {{"a", string_type}, {"A", bytes_type}},
                  /*element_tables=*/{}, &graph_element_type),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Inconsistent property: A")));

  EXPECT_THAT(factory.MakeDynamicGraphElementType(
                  {"graph_name"}, GraphElementType::ElementKind::kNode,
                  {{"", string_type}},
                  /*element_tables=*/{}, &graph_element_type),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Property type name cannot be empty")));

  EXPECT_THAT(graph_element_type, IsNull());

  GOOGLESQL_EXPECT_OK(factory.MakeDynamicGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      {{"a", string_type}, {"A", string_type}},
      /*element_tables=*/{}, &graph_element_type));
}

TEST(GraphElementTypeTest,
     InternalProductModeDynamicGraphElementTypeIsSupportedType) {
  TypeFactory factory;
  LanguageOptions options;
  options.set_product_mode(PRODUCT_INTERNAL);
  const GraphElementType* graph_element_type;
  GOOGLESQL_ASSERT_OK(factory.MakeDynamicGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      {{"a", factory.get_uint64()}},
      /*element_tables=*/{}, &graph_element_type));
  ASSERT_THAT(graph_element_type, NotNull());

  EXPECT_FALSE(graph_element_type->IsSupportedType(options));

  options.EnableLanguageFeature(FEATURE_SQL_GRAPH);
  EXPECT_FALSE(graph_element_type->IsSupportedType(options));

  options.EnableLanguageFeature(FEATURE_JSON_TYPE);
  EXPECT_FALSE(graph_element_type->IsSupportedType(options));

  options.EnableLanguageFeature(FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE);
  EXPECT_TRUE(graph_element_type->IsSupportedType(options));
}

TEST(GraphElementTypeTest,
     ExternalProductModeDynamicGraphElementTypeIsSupportedType) {
  TypeFactory factory;
  LanguageOptions options;
  options.set_product_mode(PRODUCT_EXTERNAL);
  const GraphElementType* graph_element_type;
  GOOGLESQL_ASSERT_OK(factory.MakeDynamicGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      {{"a", factory.get_uint64()}},
      /*element_tables=*/{}, &graph_element_type));
  ASSERT_THAT(graph_element_type, NotNull());

  EXPECT_FALSE(graph_element_type->IsSupportedType(options));

  // uint64 is not supported in external mode.
  options.EnableLanguageFeature(FEATURE_SQL_GRAPH);
  EXPECT_FALSE(graph_element_type->IsSupportedType(options));

  options.EnableLanguageFeature(FEATURE_JSON_TYPE);
  EXPECT_FALSE(graph_element_type->IsSupportedType(options));

  options.EnableLanguageFeature(FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE);
  EXPECT_FALSE(graph_element_type->IsSupportedType(options));
}

TEST(GraphElementTypeTest, DynamicGraphElementTypeDebugString) {
  TypeFactory factory;
  const Type* string_type = factory.get_string();
  const Type* bytes_type = factory.get_bytes();
  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"a", string_type}, {"b c", bytes_type}},
                                   &struct_type));
  const ArrayType* array_type;
  GOOGLESQL_ASSERT_OK(factory.MakeArrayType(string_type, &array_type));

  const GraphElementType* graph_element_type;
  GOOGLESQL_ASSERT_OK(factory.MakeDynamicGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      {{"a", string_type},
       {"b c", bytes_type},
       {"e", array_type},
       {"d", struct_type}},
      /*element_tables=*/{}, &graph_element_type));

  EXPECT_EQ(graph_element_type->DebugString(),
            "GRAPH_NODE(graph_name)<a STRING, `b c` BYTES, d STRUCT<a STRING, "
            "`b c` BYTES>, e ARRAY<STRING>, DYNAMIC>");
  EXPECT_EQ(graph_element_type->TypeName(PRODUCT_INTERNAL),
            "GRAPH_NODE(graph_name)<a STRING, `b c` BYTES, d STRUCT<a STRING, "
            "`b c` BYTES>, e ARRAY<STRING>, DYNAMIC>");
  EXPECT_EQ(
      graph_element_type->ShortTypeName(PRODUCT_INTERNAL),
      "GRAPH_NODE(graph_name)<a STRING, `b c` BYTES, d STRUCT<a STRING, `b c` "
      "BYTES>, ..., DYNAMIC>");

  TypeModifiers type_modifiers;
  EXPECT_THAT(
      graph_element_type->TypeNameWithModifiers(type_modifiers,
                                                PRODUCT_INTERNAL),
      IsOkAndHolds("GRAPH_NODE(graph_name)<a STRING, `b c` BYTES, d STRUCT<a "
                   "STRING, `b c` BYTES>, e ARRAY<STRING>, DYNAMIC>"));
}

class GraphElementTypeEqualityTest : public testing::Test {
 protected:
  void SetUp() override {
    GOOGLESQL_ASSERT_OK(factory_.MakeGraphElementType(
        {"graph_name"}, GraphElementType::ElementKind::kNode,
        {{"aaA", factory_.get_string()}},
        /*element_tables=*/{}, &static_graph_element_type_));
    GOOGLESQL_ASSERT_OK(factory_.MakeDynamicGraphElementType(
        {"graph_name"}, GraphElementType::ElementKind::kNode,
        {{"aaA", factory_.get_string()}},
        /*element_tables=*/{}, &dynamic_graph_element_type_));
  }

  TypeFactory factory_;
  const GraphElementType* static_graph_element_type_;
  const GraphElementType* dynamic_graph_element_type_;
};

TEST_F(GraphElementTypeEqualityTest, SameGraphElementType) {
  const GraphElementType* graph_element_type2;
  GOOGLESQL_ASSERT_OK(factory_.MakeGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      {{"aaA", factory_.get_string()}},
      /*element_tables=*/{}, &graph_element_type2));
  EXPECT_TRUE(static_graph_element_type_->Equals(graph_element_type2));
  EXPECT_TRUE(static_graph_element_type_->Equivalent(graph_element_type2));
}

TEST_F(GraphElementTypeEqualityTest, CaseInsensitiveGraphReference) {
  const GraphElementType* graph_element_type2;
  GOOGLESQL_ASSERT_OK(factory_.MakeGraphElementType(
      {"graph_NAME"}, static_graph_element_type_->element_kind(),
      {{"aaa", factory_.get_string()}},
      /*element_tables=*/{}, &graph_element_type2));
  EXPECT_TRUE(static_graph_element_type_->Equals(graph_element_type2));
  EXPECT_TRUE(static_graph_element_type_->Equivalent(graph_element_type2));
}

TEST_F(GraphElementTypeEqualityTest, CaseInsensitivePropertyTypeName) {
  const GraphElementType* graph_element_type2;
  GOOGLESQL_ASSERT_OK(factory_.MakeGraphElementType(
      {"graph_name"}, static_graph_element_type_->element_kind(),
      {{"aaa", factory_.get_string()}},
      /*element_tables=*/{}, &graph_element_type2));
  EXPECT_TRUE(static_graph_element_type_->Equals(graph_element_type2));
  EXPECT_TRUE(static_graph_element_type_->Equivalent(graph_element_type2));
}

TEST_F(GraphElementTypeEqualityTest, DifferentPropertyTypeName) {
  const GraphElementType* graph_element_type2;
  GOOGLESQL_ASSERT_OK(factory_.MakeGraphElementType(
      {"graph_name"}, static_graph_element_type_->element_kind(),
      {{"aaB", factory_.get_string()}},
      /*element_tables=*/{}, &graph_element_type2));
  EXPECT_FALSE(static_graph_element_type_->Equals(graph_element_type2));
  EXPECT_FALSE(static_graph_element_type_->Equivalent(graph_element_type2));
}

TEST_F(GraphElementTypeEqualityTest, DifferentPropertyTypeValueType) {
  const GraphElementType* graph_element_type2;
  GOOGLESQL_ASSERT_OK(factory_.MakeGraphElementType(
      {"graph_name"}, static_graph_element_type_->element_kind(),
      {{"aaA", factory_.get_bytes()}},
      /*element_tables=*/{}, &graph_element_type2));
  EXPECT_FALSE(static_graph_element_type_->Equals(graph_element_type2));
  EXPECT_FALSE(static_graph_element_type_->Equivalent(graph_element_type2));
}

TEST_F(GraphElementTypeEqualityTest, DuplicatePropertyTypes) {
  const Type* string_type = factory_.get_string();
  const GraphElementType* graph_element_type2;
  GOOGLESQL_ASSERT_OK(factory_.MakeGraphElementType(
      {"graph_name"}, static_graph_element_type_->element_kind(),
      {{"aaa", string_type}, {"aaA", string_type}, {"AAA", string_type}},
      /*element_tables=*/{}, &graph_element_type2));
  EXPECT_TRUE(static_graph_element_type_->Equals(graph_element_type2));
  EXPECT_TRUE(static_graph_element_type_->Equivalent(graph_element_type2));
}

TEST_F(GraphElementTypeEqualityTest, DifferentElementKind) {
  const GraphElementType* graph_element_type2;
  GOOGLESQL_ASSERT_OK(factory_.MakeGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kEdge,
      static_graph_element_type_->property_types(),
      /*element_tables=*/{}, &graph_element_type2));
  EXPECT_FALSE(static_graph_element_type_->Equals(graph_element_type2));
  EXPECT_FALSE(static_graph_element_type_->Equivalent(graph_element_type2));
}

TEST_F(GraphElementTypeEqualityTest, DifferentGraphReference) {
  const GraphElementType* graph_element_type2;
  GOOGLESQL_ASSERT_OK(factory_.MakeGraphElementType(
      {"another_graph_name"}, GraphElementType::ElementKind::kNode,
      static_graph_element_type_->property_types(),
      /*element_tables=*/{}, &graph_element_type2));
  EXPECT_FALSE(static_graph_element_type_->Equals(graph_element_type2));
  EXPECT_FALSE(static_graph_element_type_->Equivalent(graph_element_type2));
}

TEST_F(GraphElementTypeEqualityTest, NestedCatalogNameInGraphReference) {
  const GraphElementType* graph_element_type2;
  GOOGLESQL_ASSERT_OK(factory_.MakeGraphElementType(
      {"foo", "graph_name"}, GraphElementType::ElementKind::kNode,
      static_graph_element_type_->property_types(),
      /*element_tables=*/{}, &graph_element_type2));
  EXPECT_FALSE(static_graph_element_type_->Equals(graph_element_type2));
  EXPECT_FALSE(static_graph_element_type_->Equivalent(graph_element_type2));
}

TEST_F(GraphElementTypeEqualityTest, PeriodInPath) {
  const GraphElementType* graph_element_type1;
  const GraphElementType* graph_element_type2;
  GOOGLESQL_ASSERT_OK(factory_.MakeGraphElementType(
      {"foo.bar", "graph_name"}, GraphElementType::ElementKind::kNode,
      static_graph_element_type_->property_types(),
      /*element_tables=*/{}, &graph_element_type1));
  GOOGLESQL_ASSERT_OK(factory_.MakeGraphElementType(
      {"foo", "bar", "graph_name"}, GraphElementType::ElementKind::kNode,
      static_graph_element_type_->property_types(),
      /*element_tables=*/{}, &graph_element_type2));
  EXPECT_FALSE(graph_element_type1->Equals(graph_element_type2));
  EXPECT_FALSE(graph_element_type1->Equivalent(graph_element_type2));
}

TEST_F(GraphElementTypeEqualityTest, StaticGraphElementTypeEquivalent) {
  const StructType *struct_type, *equivalent_struct_type;
  GOOGLESQL_ASSERT_OK(
      factory_.MakeStructType({{"aaA", factory_.get_string()}}, &struct_type));
  GOOGLESQL_ASSERT_OK(factory_.MakeStructType({{"aaB", factory_.get_string()}},
                                    &equivalent_struct_type));
  EXPECT_FALSE(struct_type->Equals(equivalent_struct_type));
  EXPECT_TRUE(struct_type->Equivalent(equivalent_struct_type));

  GOOGLESQL_ASSERT_OK(factory_.MakeGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      {{"property", struct_type}},
      /*element_tables=*/{}, &static_graph_element_type_));
  const GraphElementType* graph_element_type2;
  GOOGLESQL_ASSERT_OK(factory_.MakeGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      {{"property", equivalent_struct_type}},
      /*element_tables=*/{}, &graph_element_type2));

  EXPECT_FALSE(static_graph_element_type_->Equals(graph_element_type2));
  EXPECT_TRUE(static_graph_element_type_->Equivalent(graph_element_type2));
}

TEST_F(GraphElementTypeEqualityTest, DynamicGraphElementTypeEquivalent) {
  const StructType *struct_type, *equivalent_struct_type;
  GOOGLESQL_ASSERT_OK(
      factory_.MakeStructType({{"aaA", factory_.get_string()}}, &struct_type));
  GOOGLESQL_ASSERT_OK(factory_.MakeStructType({{"aaB", factory_.get_string()}},
                                    &equivalent_struct_type));

  GOOGLESQL_ASSERT_OK(factory_.MakeDynamicGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      {{"property", struct_type}},
      /*element_tables=*/{}, &dynamic_graph_element_type_));
  const GraphElementType* dynamic_graph_element_type2;
  GOOGLESQL_ASSERT_OK(factory_.MakeDynamicGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      {{"property", equivalent_struct_type}},
      /*element_tables=*/{}, &dynamic_graph_element_type2));

  EXPECT_FALSE(
      dynamic_graph_element_type_->Equals(dynamic_graph_element_type2));
  EXPECT_TRUE(
      dynamic_graph_element_type_->Equivalent(dynamic_graph_element_type2));
}

TEST_F(GraphElementTypeEqualityTest,
       StaticAndDynamicGraphElementTypeNotEquivalent) {
  EXPECT_FALSE(dynamic_graph_element_type_->Equals(static_graph_element_type_));
  EXPECT_FALSE(
      dynamic_graph_element_type_->Equivalent(static_graph_element_type_));
  EXPECT_FALSE(static_graph_element_type_->Equals(dynamic_graph_element_type_));
  EXPECT_FALSE(
      static_graph_element_type_->Equivalent(dynamic_graph_element_type_));
}

TEST_F(GraphElementTypeEqualityTest, StaticGraphElementTypeCoercion) {
  const GraphElementType* static_graph_element_type2;
  GOOGLESQL_ASSERT_OK(factory_.MakeGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      {{"aaA", factory_.get_string()}, {"bbB", factory_.get_bool()}},
      /*element_tables=*/{}, &static_graph_element_type2));
  EXPECT_TRUE(
      static_graph_element_type_->CoercibleTo(static_graph_element_type2));
  EXPECT_FALSE(
      static_graph_element_type2->CoercibleTo(static_graph_element_type_));
}

TEST_F(GraphElementTypeEqualityTest, DynamicGraphElementTypeCoercion) {
  const GraphElementType* dynamic_graph_element_type2;
  GOOGLESQL_ASSERT_OK(factory_.MakeDynamicGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      {{"aaA", factory_.get_string()}, {"bbB", factory_.get_bool()}},
      /*element_tables=*/{}, &dynamic_graph_element_type2));
  EXPECT_TRUE(
      dynamic_graph_element_type_->CoercibleTo(dynamic_graph_element_type2));
  EXPECT_FALSE(
      dynamic_graph_element_type2->CoercibleTo(dynamic_graph_element_type_));
}

TEST_F(GraphElementTypeEqualityTest, StaticAndDynamicGraphElementTypeCoercion) {
  EXPECT_FALSE(
      dynamic_graph_element_type_->CoercibleTo(static_graph_element_type_));
  EXPECT_TRUE(
      static_graph_element_type_->CoercibleTo(dynamic_graph_element_type_));

  // Graph element type with more static properties does not coerce to another
  // graph element type with fewer static properties, regardless of whether the
  // type is dynamic or not.
  const GraphElementType* static_graph_element_type2;
  GOOGLESQL_ASSERT_OK(factory_.MakeGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      {{"aaA", factory_.get_string()}, {"bbB", factory_.get_bool()}},
      /*element_tables=*/{}, &static_graph_element_type2));
  const GraphElementType* dynamic_graph_element_type2;
  GOOGLESQL_ASSERT_OK(factory_.MakeDynamicGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      {{"aaA", factory_.get_string()}, {"bbB", factory_.get_bool()}},
      /*element_tables=*/{}, &dynamic_graph_element_type2));
  EXPECT_FALSE(
      static_graph_element_type2->CoercibleTo(dynamic_graph_element_type_));
  EXPECT_FALSE(
      dynamic_graph_element_type2->CoercibleTo(dynamic_graph_element_type_));
}

TEST(TypeTest, TypeDeserializerGraphElementInvalidProto) {
  TypeFactory factory;
  const TypeDeserializer type_deserializer(&factory);
  TypeProto type_proto;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(R"pb(
                                               type_kind: TYPE_GRAPH_ELEMENT
                                             )pb",
                                             &type_proto));
  EXPECT_THAT(
      type_deserializer.Deserialize(type_proto),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Invalid TypeProto provided for deserialization:")));
}

TEST(TypeTest, TypeDeserializerGraphElementInvalidGraphReference) {
  TypeFactory factory;
  const TypeDeserializer type_deserializer(&factory);
  TypeProto type_proto;

  // graph reference must be specified.
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        type_kind: TYPE_GRAPH_ELEMENT
        graph_element_type {}
      )pb",
      &type_proto));
  EXPECT_THAT(
      type_deserializer.Deserialize(type_proto),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("GraphElementType must have a non-empty graph reference")));
}

TEST(TypeTest, TypeDeserializerGraphElementInvalidElementKind) {
  TypeFactory factory;
  const TypeDeserializer type_deserializer(&factory);
  TypeProto type_proto;

  // default element kind is invalid.
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        type_kind: TYPE_GRAPH_ELEMENT
        graph_element_type { graph_reference: "graph_name" }
      )pb",
      &type_proto));
  EXPECT_THAT(type_deserializer.Deserialize(type_proto),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid element kind:")));

  // invalid element enum is invalid.
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        type_kind: TYPE_GRAPH_ELEMENT
        graph_element_type { graph_reference: "graph_name" kind: KIND_INVALID }
      )pb",
      &type_proto));
  EXPECT_THAT(type_deserializer.Deserialize(type_proto),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid element kind:")));
}

TEST(TypeTest, TypeDeserializerGraphElementInvalidPropertyValueType) {
  TypeFactory factory;
  const TypeDeserializer type_deserializer(&factory);
  TypeProto type_proto;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        type_kind: TYPE_GRAPH_ELEMENT
        graph_element_type {
          graph_reference: "graph_name"
          kind: KIND_NODE
          property_type {
            name: "a"
            value_type {
              type_kind: TYPE_GRAPH_ELEMENT
              graph_element_type { graph_reference: "g1" kind: KIND_NODE }
            }
          }
        }
      )pb",
      &type_proto));
  EXPECT_THAT(
      type_deserializer.Deserialize(type_proto),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Property value type cannot be GraphElementType")));
}

TEST(TypeTest, TypeDeserializerGraphElementEmptyElementTableName) {
  TypeFactory factory;
  const TypeDeserializer type_deserializer(&factory);
  TypeProto type_proto;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        type_kind: TYPE_GRAPH_ELEMENT
        graph_element_type {
          graph_reference: "graph_name"
          kind: KIND_NODE
          element_tables: ""
        }
      )pb",
      &type_proto));
  EXPECT_THAT(type_deserializer.Deserialize(type_proto),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Element table name cannot be empty")));
}

TEST(TypeTest, TypeDeserializerStaticGraphElement) {
  TypeFactory factory;
  const TypeDeserializer type_deserializer(&factory);
  TypeProto type_proto;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        type_kind: TYPE_GRAPH_ELEMENT
        graph_element_type {
          graph_reference: "graph_name"
          kind: KIND_NODE
          property_type {
            name: "a"
            value_type { type_kind: TYPE_BOOL }
          }
        }
      )pb",
      &type_proto));
  const GraphElementType* graph_element_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType(
      {"graph_name"}, GraphElementType::kNode, {{"a", factory.get_bool()}},
      /*element_tables=*/{}, &graph_element_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* deserialized_type,
                       type_deserializer.Deserialize(type_proto));
  EXPECT_TRUE(deserialized_type->Equals(graph_element_type));
  EXPECT_THAT(deserialized_type->AsGraphElement()->element_tables(), IsEmpty());

  TypeProto roundtrip_proto;
  GOOGLESQL_ASSERT_OK(
      deserialized_type->SerializeToProtoAndFileDescriptors(&roundtrip_proto));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* roundtrip_type,
                       type_deserializer.Deserialize(roundtrip_proto));
  EXPECT_TRUE(roundtrip_type->Equals(graph_element_type));
  EXPECT_THAT(roundtrip_type->AsGraphElement()->element_tables(), IsEmpty());
}

TEST(TypeTest, TypeDeserializerStaticGraphElementWithElementTables) {
  TypeFactory factory;
  const TypeDeserializer type_deserializer(&factory);
  TypeProto type_proto;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        type_kind: TYPE_GRAPH_ELEMENT
        graph_element_type {
          graph_reference: "graph_name"
          kind: KIND_NODE
          property_type {
            name: "a"
            value_type { type_kind: TYPE_BOOL }
          }
          element_tables: "TableB"
          element_tables: "TableA"
          element_tables: "tableb"
        }
      )pb",
      &type_proto));
  const GraphElementType* graph_element_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType(
      {"graph_name"}, GraphElementType::kNode, {{"a", factory.get_bool()}},
      {"TableB", "TableA"}, &graph_element_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* deserialized_type,
                       type_deserializer.Deserialize(type_proto));
  EXPECT_TRUE(deserialized_type->Equals(graph_element_type));
  EXPECT_THAT(deserialized_type->AsGraphElement()->element_tables(),
              ElementsAre("TableB", "TableA"));

  TypeProto roundtrip_proto;
  GOOGLESQL_ASSERT_OK(
      deserialized_type->SerializeToProtoAndFileDescriptors(&roundtrip_proto));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* roundtrip_type,
                       type_deserializer.Deserialize(roundtrip_proto));
  EXPECT_TRUE(roundtrip_type->Equals(graph_element_type));
  EXPECT_THAT(roundtrip_type->AsGraphElement()->element_tables(),
              ElementsAre("TableB", "TableA"));
}

TEST(TypeTest, TypeDeserializerDynamicGraphElement) {
  TypeFactory factory;
  const TypeDeserializer type_deserializer(&factory);
  TypeProto type_proto;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        type_kind: TYPE_GRAPH_ELEMENT
        graph_element_type {
          graph_reference: "graph_name"
          kind: KIND_NODE
          property_type {
            name: "a"
            value_type { type_kind: TYPE_BOOL }
          }
          is_dynamic: true
        }
      )pb",
      &type_proto));
  const GraphElementType* graph_element_type;
  GOOGLESQL_ASSERT_OK(factory.MakeDynamicGraphElementType(
      {"graph_name"}, GraphElementType::kNode, {{"a", factory.get_bool()}},
      /*element_tables=*/{}, &graph_element_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* deserialized_type,
                       type_deserializer.Deserialize(type_proto));
  EXPECT_TRUE(deserialized_type->Equals(graph_element_type));
  EXPECT_THAT(deserialized_type->AsGraphElement()->element_tables(), IsEmpty());

  TypeProto roundtrip_proto;
  GOOGLESQL_ASSERT_OK(
      deserialized_type->SerializeToProtoAndFileDescriptors(&roundtrip_proto));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* roundtrip_type,
                       type_deserializer.Deserialize(roundtrip_proto));
  EXPECT_TRUE(roundtrip_type->Equals(graph_element_type));
  EXPECT_THAT(roundtrip_type->AsGraphElement()->element_tables(), IsEmpty());
}

TEST(TypeTest, TypeDeserializerDynamicGraphElementWithElementTables) {
  TypeFactory factory;
  const TypeDeserializer type_deserializer(&factory);
  TypeProto type_proto;
  ABSL_QCHECK(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        type_kind: TYPE_GRAPH_ELEMENT
        graph_element_type {
          graph_reference: "graph_name"
          kind: KIND_NODE
          property_type {
            name: "a"
            value_type { type_kind: TYPE_BOOL }
          }
          is_dynamic: true
          element_tables: "TableA"
        }
      )pb",
      &type_proto));
  const GraphElementType* graph_element_type;
  GOOGLESQL_ASSERT_OK(factory.MakeDynamicGraphElementType(
      {"graph_name"}, GraphElementType::kNode, {{"a", factory.get_bool()}},
      {"TableA"}, &graph_element_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* deserialized_type,
                       type_deserializer.Deserialize(type_proto));
  EXPECT_TRUE(deserialized_type->Equals(graph_element_type));
  EXPECT_THAT(deserialized_type->AsGraphElement()->element_tables(),
              ElementsAre("TableA"));

  TypeProto roundtrip_proto;
  GOOGLESQL_ASSERT_OK(
      deserialized_type->SerializeToProtoAndFileDescriptors(&roundtrip_proto));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* roundtrip_type,
                       type_deserializer.Deserialize(roundtrip_proto));
  EXPECT_TRUE(roundtrip_type->Equals(graph_element_type));
  EXPECT_THAT(roundtrip_type->AsGraphElement()->element_tables(),
              ElementsAre("TableA"));
}

TEST(GraphElementTypeTest, ValidateResolvedTypeParameters) {
  TypeFactory factory;
  const Type* string_type = factory.get_string();
  const Type* int64_type = factory.get_int64();

  const GraphElementType* graph_element_type;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      {{"char_field", string_type}, {"int_field", int64_type}},
      /*element_tables=*/{}, &graph_element_type));

  EXPECT_THAT(graph_element_type->property_types(),
              testing::ElementsAre(IsPropertyType("char_field", string_type),
                                   IsPropertyType("int_field", int64_type)));

  // 1. Empty TypeParameters is OK.
  GOOGLESQL_EXPECT_OK(graph_element_type->ValidateResolvedTypeParameters(
      TypeParameters(), PRODUCT_INTERNAL));

  // 2. Valid TypeParameters has matching children count (2).
  StringTypeParametersProto param_proto;
  param_proto.set_max_length(10);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters string_type_param,
                       TypeParameters::MakeStringTypeParameters(param_proto));
  TypeParameters valid_type_params =
      TypeParameters::MakeTypeParametersWithChildList(
          {string_type_param, TypeParameters()});
  GOOGLESQL_EXPECT_OK(graph_element_type->ValidateResolvedTypeParameters(
      valid_type_params, PRODUCT_INTERNAL));

  // 3. Invalid TypeParameters: incorrect number of children (1 instead of 2).
  TypeParameters invalid_child_count_params =
      TypeParameters::MakeTypeParametersWithChildList({string_type_param});
  EXPECT_THAT(graph_element_type->ValidateResolvedTypeParameters(
                  invalid_child_count_params, PRODUCT_INTERNAL),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(GraphElementTypeTest, ElementTablesEqualityAndHashConsistency) {
  TypeFactory factory;
  const Type* string_type = factory.get_string();

  // Empty element table name is rejected.
  const GraphElementType* invalid_type = nullptr;
  EXPECT_THAT(
      factory.MakeGraphElementType(
          {"graph_name"}, GraphElementType::ElementKind::kNode,
          {{"prop", string_type}}, /*element_tables=*/{""}, &invalid_type),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Element table name cannot be empty")));
  EXPECT_THAT(
      factory.MakeDynamicGraphElementType(
          {"graph_name"}, GraphElementType::ElementKind::kNode,
          {{"prop", string_type}}, /*element_tables=*/{""}, &invalid_type),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Element table name cannot be empty")));

  // Type 0: element_tables = empty list (not associated with any element table)
  const GraphElementType* type_empty = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      {{"prop", string_type}}, /*element_tables=*/{}, &type_empty));
  ASSERT_THAT(type_empty, NotNull());
  EXPECT_THAT(type_empty->element_tables(), IsEmpty());

  // Type 1: element_tables passed as ["TableA", "TableB"]
  const GraphElementType* type_ab = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      {{"prop", string_type}}, {"TableA", "TableB"}, &type_ab));
  ASSERT_THAT(type_ab, NotNull());

  // Type 2: element_tables passed in reverse order ["TableB", "TableA"] with
  // case-insensitive duplicates
  const GraphElementType* type_ba = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      {{"prop", string_type}}, {"TableB", "TableA", "tableb", "TableA"},
      &type_ba));
  ASSERT_THAT(type_ba, NotNull());

  // Type 2b: element_tables with different casing ["tableb", "tablea"]
  const GraphElementType* type_ba_lower = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      {{"prop", string_type}}, {"tableb", "tablea"}, &type_ba_lower));
  ASSERT_THAT(type_ba_lower, NotNull());

  // Type 3: element_tables = ["TableA", "TableC"]
  const GraphElementType* type_ac = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      {{"prop", string_type}}, {"TableA", "TableC"}, &type_ac));
  ASSERT_THAT(type_ac, NotNull());

  // element_tables() preserves order of first appearance after deduplication.
  EXPECT_THAT(type_ab->element_tables(), ElementsAre("TableA", "TableB"));
  EXPECT_THAT(type_ba->element_tables(), ElementsAre("TableB", "TableA"));
  EXPECT_THAT(type_ba_lower->element_tables(), ElementsAre("tableb", "tablea"));

  // Equals() compares element_tables as an order-insensitive, case-insensitive
  // set; Equivalent() ignores element_tables.
  EXPECT_TRUE(type_ab->Equals(type_ba));
  EXPECT_TRUE(type_ba->Equals(type_ab));
  EXPECT_TRUE(type_ab->Equals(type_ba_lower));
  EXPECT_TRUE(type_ab->Equivalent(type_ba));
  EXPECT_TRUE(type_ab->Equivalent(type_ba_lower));

  EXPECT_FALSE(type_ab->Equals(type_ac));
  EXPECT_TRUE(type_ab->Equivalent(type_ac));

  EXPECT_FALSE(type_ab->Equals(type_empty));
  EXPECT_TRUE(type_ab->Equivalent(type_empty));

  // Hash consistency for Abseil hash and Swiss Table (TypeFlatHashSet).
  EXPECT_EQ(absl::HashOf(*type_ab), absl::HashOf(*type_ba));
  EXPECT_EQ(absl::HashOf(*type_ab), absl::HashOf(*type_ba_lower));
  EXPECT_EQ(TypeHash{}(type_ab), TypeHash{}(type_ba));
  EXPECT_EQ(TypeHash{}(type_ab), TypeHash{}(type_ba_lower));
  TypeFlatHashSet<> type_set;
  type_set.insert(type_ab);
  EXPECT_NE(type_set.find(type_ba), type_set.end());
  EXPECT_NE(type_set.find(type_ba_lower), type_set.end());
  EXPECT_EQ(type_set.find(type_ac), type_set.end());
  EXPECT_EQ(type_set.find(type_empty), type_set.end());
}

TEST(GraphElementTypeTest, ElementTablesCoercion) {
  TypeFactory factory;
  const Type* string_type = factory.get_string();
  const Type* bool_type = factory.get_bool();

  const GraphElementType* type_empty = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      {{"a", string_type}}, /*element_tables=*/{}, &type_empty));

  const GraphElementType* type_a = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      {{"a", string_type}}, {"TableA"}, &type_a));

  const GraphElementType* type_ab = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      {{"a", string_type}, {"b", bool_type}}, {"TableB", "tablea"}, &type_ab));

  const GraphElementType* type_ac = nullptr;
  GOOGLESQL_ASSERT_OK(factory.MakeGraphElementType(
      {"graph_name"}, GraphElementType::ElementKind::kNode,
      {{"a", string_type}, {"b", bool_type}}, {"TableA", "TableC"}, &type_ac));

  // Empty element_tables is a subset of {"TableA"}.
  EXPECT_TRUE(type_empty->CoercibleTo(type_a));
  EXPECT_FALSE(type_a->CoercibleTo(type_empty));

  // {"TableA"} is a subset of {"TableB", "tablea"} (case-insensitive).
  EXPECT_TRUE(type_a->CoercibleTo(type_ab));
  EXPECT_FALSE(type_ab->CoercibleTo(type_a));

  // {"TableB", "tablea"} is not a subset of {"TableA", "TableC"}.
  EXPECT_FALSE(type_ab->CoercibleTo(type_ac));
  EXPECT_FALSE(type_ac->CoercibleTo(type_ab));
}

}  // namespace
}  // namespace googlesql
