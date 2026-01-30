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

#include "googlesql/public/types/collation.h"

#include <memory>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/simple_value.h"
#include "googlesql/public/types/type_factory.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace googlesql {

namespace {

const ArrayType* MakeArrayType(const Type* element_type,
                               TypeFactory* type_factory) {
  const ArrayType* array_type;
  GOOGLESQL_QCHECK_OK(type_factory->MakeArrayType(element_type, &array_type));
  return array_type;
}

const StructType* MakeStructType(
    absl::Span<const StructType::StructField> fields,
    TypeFactory* type_factory) {
  const StructType* struct_type;
  GOOGLESQL_CHECK_OK(type_factory->MakeStructType(
      std::vector<StructType::StructField>(fields.begin(), fields.end()),
      &struct_type));
  return struct_type;
}

// Make type
//   STRUCT< a STRING, b ARRAY < STRUCT < a STRING, b INT64 > > >
const Type* MakeNestedStructType(TypeFactory* type_factory) {
  return MakeStructType(
      {{"a", types::StringType()},
       {"b", MakeArrayType(MakeStructType({{"a", types::StringType()},
                                           {"b", types::Int64Type()}},
                                          type_factory),
                           type_factory)}},
      type_factory);
}

}  // namespace

TEST(CollationTest, Creation) {
  {
    // Test empty collation name. An empty Collation should be created.
    std::unique_ptr<AnnotationMap> annotation_map =
        AnnotationMap::Create(types::StringType());
    annotation_map->SetAnnotation(static_cast<int>(AnnotationKind::kCollation),
                                  SimpleValue::String(""));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(Collation collation,
                         Collation::MakeCollation(*annotation_map));

    // Test serialization / deserialization.
    CollationProto proto;
    GOOGLESQL_ASSERT_OK(collation.Serialize(&proto));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(Collation deserialized_collation,
                         Collation::Deserialize(proto));
    ASSERT_TRUE(collation.Equals(deserialized_collation));

    EXPECT_EQ(collation.CollationName(), "");
    ASSERT_TRUE(collation.Empty());
    EXPECT_EQ(collation.DebugString(), "_");
    // Test ToAnnotationMap.
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<AnnotationMap> to_annotation_map,
                         collation.ToAnnotationMap(types::StringType()));
    EXPECT_NE(to_annotation_map, nullptr);
    EXPECT_TRUE(to_annotation_map->HasCompatibleStructure(types::StringType()));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        bool equals_collation_annotation,
        collation.EqualsCollationAnnotation(to_annotation_map.get()));
    EXPECT_TRUE(equals_collation_annotation);
    EXPECT_TRUE(to_annotation_map->IsNormalized());
  }
  {
    std::unique_ptr<AnnotationMap> annotation_map =
        AnnotationMap::Create(types::StringType());
    annotation_map->SetAnnotation(static_cast<int>(AnnotationKind::kCollation),
                                  SimpleValue::String("unicode:ci"));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(Collation collation,
                         Collation::MakeCollation(*annotation_map));

    // Test serialization / deserialization.
    CollationProto proto;
    GOOGLESQL_ASSERT_OK(collation.Serialize(&proto));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(Collation deserialized_collation,
                         Collation::Deserialize(proto));
    ASSERT_TRUE(collation.Equals(deserialized_collation));

    EXPECT_EQ(collation.CollationName(), "unicode:ci");
    EXPECT_EQ(collation.num_children(), 0);
    EXPECT_EQ(collation.DebugString(), "unicode:ci");
    // Test ToAnnotationMap.
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<AnnotationMap> to_annotation_map,
                         collation.ToAnnotationMap(types::StringType()));
    EXPECT_NE(to_annotation_map, nullptr);
    EXPECT_TRUE(to_annotation_map->HasCompatibleStructure(types::StringType()));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        bool equals_collation_annotation,
        collation.EqualsCollationAnnotation(to_annotation_map.get()));
    EXPECT_TRUE(equals_collation_annotation);
    EXPECT_TRUE(to_annotation_map->IsNormalized());
  }
  {
    // Test empty nested annotation map.
    TypeFactory type_factory;
    // STRUCT< a STRING, b ARRAY < STRUCT < a STRING, b INT64 > > >
    const Type* struct_type = MakeNestedStructType(&type_factory);
    std::unique_ptr<AnnotationMap> annotation_map =
        AnnotationMap::Create(struct_type);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(Collation collation,
                         Collation::MakeCollation(*annotation_map));
    EXPECT_TRUE(collation.Empty());

    // Test serialization / deserialization.
    CollationProto proto;
    GOOGLESQL_ASSERT_OK(collation.Serialize(&proto));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(Collation deserialized_collation,
                         Collation::Deserialize(proto));
    ASSERT_TRUE(collation.Equals(deserialized_collation));
    EXPECT_EQ(collation.DebugString(), "_");
    // Test ToAnnotationMap.
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<AnnotationMap> to_annotation_map,
                         collation.ToAnnotationMap(struct_type));
    EXPECT_NE(to_annotation_map, nullptr);
    EXPECT_TRUE(to_annotation_map->HasCompatibleStructure(struct_type));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        bool equals_collation_annotation,
        collation.EqualsCollationAnnotation(to_annotation_map.get()));
    EXPECT_TRUE(equals_collation_annotation);
    EXPECT_TRUE(to_annotation_map->IsNormalized());
  }
  {
    // Test struct with the first field having collation.
    TypeFactory type_factory;
    // STRUCT< a STRING, b ARRAY < STRUCT < a STRING, b INT64 > > >
    const Type* nested_struct_type = MakeNestedStructType(&type_factory);
    std::unique_ptr<AnnotationMap> annotation_map =
        AnnotationMap::Create(nested_struct_type);
    // Set collation on a.
    annotation_map->AsStructMap()->mutable_field(0)->SetAnnotation(
        static_cast<int>(AnnotationKind::kCollation),
        SimpleValue::String("unicode:ci"));

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(Collation collation,
                         Collation::MakeCollation(*annotation_map));

    EXPECT_FALSE(collation.HasCollation());
    EXPECT_EQ(collation.num_children(), 2);
    EXPECT_TRUE(collation.child(0).HasCollation());
    EXPECT_EQ(collation.child(0).CollationName(), "unicode:ci");
    EXPECT_EQ(collation.child(0).num_children(), 0);
    EXPECT_TRUE(collation.child(1).Empty());

    // Test serialization / deserialization.
    CollationProto proto;
    GOOGLESQL_ASSERT_OK(collation.Serialize(&proto));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(Collation deserialized_collation,
                         Collation::Deserialize(proto));
    ASSERT_TRUE(collation.Equals(deserialized_collation));
    EXPECT_EQ(collation.DebugString(), "[unicode:ci,_]");
    // Test ToAnnotationMap.
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<AnnotationMap> to_annotation_map,
                         collation.ToAnnotationMap(nested_struct_type));
    EXPECT_NE(to_annotation_map, nullptr);
    EXPECT_TRUE(to_annotation_map->HasCompatibleStructure(nested_struct_type));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        bool equals_collation_annotation,
        collation.EqualsCollationAnnotation(to_annotation_map.get()));
    EXPECT_TRUE(equals_collation_annotation);
    EXPECT_TRUE(to_annotation_map->IsNormalized());
  }

  {
    // Test struct with nested array child having collation.
    TypeFactory type_factory;
    // STRUCT< a STRING, b ARRAY < STRUCT < a STRING, b INT64 > > >
    const Type* nested_struct_type = MakeNestedStructType(&type_factory);
    std::unique_ptr<AnnotationMap> annotation_map =
        AnnotationMap::Create(nested_struct_type);
    // Set collation on b.[].a
    annotation_map->AsStructMap()
        ->mutable_field(1)
        ->AsStructMap()
        ->mutable_field(0)
        ->AsStructMap()
        ->mutable_field(0)
        ->SetAnnotation(static_cast<int>(AnnotationKind::kCollation),
                        SimpleValue::String("unicode:ci"));
    annotation_map->Normalize();

    GOOGLESQL_ASSERT_OK_AND_ASSIGN(Collation collation,
                         Collation::MakeCollation(*annotation_map));

    EXPECT_FALSE(collation.HasCollation());
    EXPECT_EQ(collation.num_children(), 2);
    EXPECT_TRUE(collation.child(0).Empty());
    EXPECT_EQ(collation.child(1).num_children(), 1);
    EXPECT_EQ(collation.child(1).child(0).num_children(), 2);
    EXPECT_EQ(collation.child(1).child(0).child(0).CollationName(),
              "unicode:ci");
    EXPECT_TRUE(collation.child(1).child(0).child(1).Empty());
    EXPECT_EQ(collation.DebugString(), "[_,[[unicode:ci,_]]]");

    // Test serialization / deserialization.
    CollationProto proto;
    GOOGLESQL_ASSERT_OK(collation.Serialize(&proto));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(Collation deserialized_collation,
                         Collation::Deserialize(proto));
    ASSERT_TRUE(collation.Equals(deserialized_collation));
    // Test ToAnnotationMap.
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<AnnotationMap> to_annotation_map,
                         collation.ToAnnotationMap(nested_struct_type));
    EXPECT_NE(to_annotation_map, nullptr);
    EXPECT_TRUE(to_annotation_map->HasCompatibleStructure(nested_struct_type));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        bool equals_collation_annotation,
        collation.EqualsCollationAnnotation(to_annotation_map.get()));
    EXPECT_TRUE(equals_collation_annotation);
    EXPECT_TRUE(to_annotation_map->IsNormalized());
  }
  {
    // Test creating collation with empty child list.
    Collation collation = Collation::MakeCollationWithChildList({});
    EXPECT_TRUE(collation.Empty());

    // Test creating collation with child list of all empty collations.
    collation =
        Collation::MakeCollationWithChildList({Collation(), Collation()});
    EXPECT_TRUE(collation.Empty());
  }
  {
    // Test creating collations with child lists.
    Collation empty_collation = Collation();
    Collation single_string_collation = Collation::MakeScalar("und:ci");
    Collation array_collation =
        Collation::MakeCollationWithChildList({single_string_collation});
    EXPECT_FALSE(array_collation.HasCollation());
    EXPECT_EQ(array_collation.num_children(), 1);
    EXPECT_EQ(array_collation.child(0).CollationName(), "und:ci");
    EXPECT_EQ(array_collation.child(0).num_children(), 0);
    EXPECT_EQ(array_collation.DebugString(), "[und:ci]");

    Collation struct_collation = Collation::MakeCollationWithChildList(
        {single_string_collation, empty_collation, array_collation,
         empty_collation});
    EXPECT_FALSE(struct_collation.HasCollation());
    EXPECT_EQ(struct_collation.num_children(), 4);
    EXPECT_EQ(struct_collation.child(0).CollationName(), "und:ci");
    EXPECT_EQ(struct_collation.child(0).num_children(), 0);
    EXPECT_TRUE(struct_collation.child(1).Empty());
    EXPECT_FALSE(struct_collation.child(2).HasCollation());
    EXPECT_EQ(struct_collation.child(2).num_children(), 1);
    EXPECT_EQ(struct_collation.child(2).child(0).CollationName(), "und:ci");
    EXPECT_EQ(struct_collation.child(2).child(0).num_children(), 0);
    EXPECT_TRUE(struct_collation.child(3).Empty());

    EXPECT_EQ(struct_collation.DebugString(), "[und:ci,_,[und:ci],_]");

    // Test serialization / deserialization.
    CollationProto proto;
    GOOGLESQL_ASSERT_OK(struct_collation.Serialize(&proto));
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(Collation deserialized_collation,
                         Collation::Deserialize(proto));
    ASSERT_TRUE(struct_collation.Equals(deserialized_collation));
  }
}

TEST(CollationTest, EqualAndCompatibilityTest) {
  std::unique_ptr<AnnotationMap> single_string_annotation_map =
      AnnotationMap::Create(types::StringType());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Collation empty_single_string,
                       Collation::MakeCollation(*single_string_annotation_map));
  Collation non_empty_single_string = Collation::MakeScalar("unicode:ci");

  EXPECT_FALSE(empty_single_string.Equals(non_empty_single_string));
  EXPECT_FALSE(non_empty_single_string.Equals(empty_single_string));
  EXPECT_TRUE(empty_single_string.HasCompatibleStructure(types::StringType()));
  EXPECT_TRUE(
      non_empty_single_string.HasCompatibleStructure(types::StringType()));
  EXPECT_FALSE(
      non_empty_single_string.HasCompatibleStructure(types::Int64Type()));

  // Test struct with the first field having collation.
  TypeFactory type_factory;
  // STRUCT< a STRING, b ARRAY < STRUCT < a STRING, b INT64 > > >
  const StructType* struct_type =
      MakeNestedStructType(&type_factory)->AsStruct();
  const ArrayType* array_type = struct_type->field(1).type->AsArray();
  std::unique_ptr<AnnotationMap> struct_annotation_map =
      AnnotationMap::Create(struct_type);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Collation empty_struct,
                       Collation::MakeCollation(*struct_annotation_map));

  // Set collation on a and b.a
  struct_annotation_map->AsStructMap()->mutable_field(0)->SetAnnotation(
      static_cast<int>(AnnotationKind::kCollation),
      SimpleValue::String("unicode:ci"));
  struct_annotation_map->AsStructMap()
      ->mutable_field(1)
      ->AsStructMap()
      ->mutable_field(0)
      ->AsStructMap()
      ->mutable_field(0)
      ->SetAnnotation(static_cast<int>(AnnotationKind::kCollation),
                      SimpleValue::String("unicode:ci"));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Collation non_empty_struct,
                       Collation::MakeCollation(*struct_annotation_map));

  Collation non_empty_array = non_empty_struct.child(1);

  EXPECT_FALSE(empty_struct.Equals(non_empty_struct));
  EXPECT_TRUE(empty_struct.HasCompatibleStructure(struct_type));
  EXPECT_TRUE(empty_struct.HasCompatibleStructure(array_type));

  EXPECT_FALSE(non_empty_struct.Equals(empty_struct));
  EXPECT_TRUE(non_empty_struct.HasCompatibleStructure(struct_type));
  EXPECT_FALSE(non_empty_struct.HasCompatibleStructure(array_type));
  EXPECT_FALSE(non_empty_struct.HasCompatibleStructure(types::StringType()));

  EXPECT_FALSE(non_empty_array.Equals(non_empty_struct));
  EXPECT_TRUE(non_empty_array.HasCompatibleStructure(array_type));
  EXPECT_FALSE(non_empty_array.HasCompatibleStructure(struct_type));
  EXPECT_FALSE(non_empty_array.HasCompatibleStructure(types::StringType()));

  EXPECT_FALSE(non_empty_single_string.HasCompatibleStructure(struct_type));
  EXPECT_FALSE(non_empty_single_string.HasCompatibleStructure(array_type));

  // Cross comparison between single_string and struct.
  EXPECT_TRUE(empty_single_string.Equals(empty_struct));
  EXPECT_TRUE(empty_struct.Equals(empty_single_string));
  EXPECT_FALSE(non_empty_single_string.Equals(empty_struct));
  EXPECT_FALSE(empty_struct.Equals(non_empty_single_string));
  EXPECT_FALSE(empty_single_string.Equals(non_empty_struct));
  EXPECT_FALSE(non_empty_struct.Equals(empty_single_string));
  EXPECT_FALSE(non_empty_single_string.Equals(non_empty_struct));
  EXPECT_FALSE(non_empty_struct.Equals(non_empty_single_string));
}

}  // namespace googlesql
