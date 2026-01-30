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

#include "googlesql/public/types/measure_type.h"

#include <memory>
#include <string>

#include "googlesql/base/testing/status_matchers.h"  
#include "googlesql/public/options.pb.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/collation.h"
#include "googlesql/public/types/simple_value.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"

namespace googlesql {

namespace {

using testing::HasSubstr;
using absl_testing::IsOkAndHolds;
using absl_testing::StatusIs;

TEST(MeasureTypeTest, TestNames) {
  TypeFactory factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* measure_type_with_int64,
                       factory.MakeMeasureType(types::Int64Type()));

  EXPECT_EQ(measure_type_with_int64->DebugString(), "MEASURE<INT64>");
  EXPECT_EQ(measure_type_with_int64->ShortTypeName(PRODUCT_INTERNAL),
            "MEASURE<INT64>");
  EXPECT_EQ(measure_type_with_int64->TypeName(PRODUCT_INTERNAL),
            "MEASURE<INT64>");
  EXPECT_TRUE(measure_type_with_int64->IsMeasureType());
  EXPECT_TRUE(measure_type_with_int64->AsMeasure()->result_type()->IsInt64());
  EXPECT_EQ(measure_type_with_int64->AsMeasure()->CapitalizedName(),
            "Measure<Int64>");
  EXPECT_EQ(measure_type_with_int64->AsMeasure()->nesting_depth(), 1);

  googlesql_test::KitchenSinkPB kitchen_sink;
  const ProtoType* proto_type = nullptr;
  GOOGLESQL_EXPECT_OK(factory.MakeProtoType(kitchen_sink.GetDescriptor(), &proto_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* measure_type_with_proto,
                       factory.MakeMeasureType(proto_type));
  EXPECT_EQ(measure_type_with_proto->DebugString(),
            "MEASURE<PROTO<googlesql_test.KitchenSinkPB>>");
  EXPECT_EQ(measure_type_with_proto->ShortTypeName(PRODUCT_INTERNAL),
            "MEASURE<googlesql_test.KitchenSinkPB>");
  EXPECT_EQ(measure_type_with_proto->TypeName(PRODUCT_INTERNAL),
            "MEASURE<`googlesql_test.KitchenSinkPB`>");
  EXPECT_TRUE(measure_type_with_proto->IsMeasureType());
  EXPECT_TRUE(measure_type_with_proto->AsMeasure()->result_type()->IsProto());
  EXPECT_EQ(measure_type_with_proto->AsMeasure()->CapitalizedName(),
            "Measure<Proto<googlesql_test.KitchenSinkPB>>");
  EXPECT_EQ(measure_type_with_proto->AsMeasure()->nesting_depth(), 1);

  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(factory.MakeStructType({{"a", types::StringType()}}, &struct_type));
  const ArrayType* array_type;
  GOOGLESQL_ASSERT_OK(factory.MakeArrayType(struct_type, &array_type));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* measure_type_with_nested,
                       factory.MakeMeasureType(array_type));
  EXPECT_EQ(measure_type_with_nested->DebugString(),
            "MEASURE<ARRAY<STRUCT<a STRING>>>");
  EXPECT_EQ(measure_type_with_nested->ShortTypeName(PRODUCT_INTERNAL),
            "MEASURE<ARRAY<STRUCT<a STRING>>>");
  EXPECT_EQ(measure_type_with_nested->TypeName(PRODUCT_INTERNAL),
            "MEASURE<ARRAY<STRUCT<a STRING>>>");
  EXPECT_TRUE(measure_type_with_nested->IsMeasureType());
  EXPECT_TRUE(measure_type_with_nested->AsMeasure()->result_type()->IsArray());
  EXPECT_EQ(measure_type_with_nested->AsMeasure()->CapitalizedName(),
            "Measure<Array<STRUCT<a STRING>>>");
  EXPECT_EQ(measure_type_with_nested->AsMeasure()->nesting_depth(), 3);
}

TEST(MeasureTypeTest, TestTypeNameWithModifiers) {
  TypeFactory factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* measure_type_with_string,
                       factory.MakeMeasureType(types::StringType()));

  // No modifiers prints the same as TypeName() and other functions.
  TypeModifiers modifiers_empty =
      TypeModifiers::MakeTypeModifiers(TypeParameters(), Collation());
  EXPECT_THAT(measure_type_with_string->TypeNameWithModifiers(modifiers_empty,
                                                              PRODUCT_INTERNAL),
              IsOkAndHolds("MEASURE<STRING>"));

  // String length parameter on the result type.
  StringTypeParametersProto string_parameters_proto;
  string_parameters_proto.set_max_length(10);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      TypeParameters string_parameters,
      TypeParameters::MakeStringTypeParameters(string_parameters_proto));
  TypeParameters parameters_string_max_length =
      TypeParameters::MakeTypeParametersWithChildList({string_parameters});
  TypeModifiers modifiers_string_max_length = TypeModifiers::MakeTypeModifiers(
      parameters_string_max_length, Collation());
  EXPECT_THAT(measure_type_with_string->TypeNameWithModifiers(
                  modifiers_string_max_length, PRODUCT_INTERNAL),
              IsOkAndHolds("MEASURE<STRING(10)>"));

  // Invalid type parameter on the MEASURE itself instead of the result type.
  TypeModifiers modifiers_bad_type_param =
      TypeModifiers::MakeTypeModifiers(string_parameters, Collation());
  EXPECT_THAT(
      measure_type_with_string->TypeNameWithModifiers(modifiers_bad_type_param,
                                                      PRODUCT_INTERNAL),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr(
                   "Input type parameter does not correspond to MeasureType")));

  // Collation annotation on the result type.
  std::unique_ptr<AnnotationMap> annotation_map =
      AnnotationMap::Create(types::StringType());
  annotation_map->SetAnnotation(static_cast<int>(AnnotationKind::kCollation),
                                SimpleValue::String("und:ci"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Collation string_collation,
                       Collation::MakeCollation(*annotation_map));
  Collation measure_collation =
      Collation::MakeCollationWithChildList({string_collation});
  TypeModifiers modifiers_collation =
      TypeModifiers::MakeTypeModifiers(TypeParameters(), measure_collation);
  EXPECT_THAT(measure_type_with_string->TypeNameWithModifiers(
                  modifiers_collation, PRODUCT_INTERNAL),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("MeasureType does not support collation")));
}

TEST(MeasureTypeTest, TestSupportsOrdering) {
  TypeFactory factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* measure_type_with_string,
                       factory.MakeMeasureType(types::StringType()));
  EXPECT_FALSE(measure_type_with_string->SupportsOrdering());

  std::string type_description = "";
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_ARRAY_ORDERING);
  EXPECT_FALSE(measure_type_with_string->SupportsOrdering(language_options,
                                                          &type_description));
  EXPECT_EQ(type_description, "MEASURE");

  type_description = "";
  const ArrayType* array_type_with_measure;
  GOOGLESQL_ASSERT_OK(factory.MakeArrayType(measure_type_with_string,
                                  &array_type_with_measure));
  EXPECT_FALSE(array_type_with_measure->SupportsOrdering(language_options,
                                                         &type_description));
  EXPECT_EQ(type_description, "ARRAY containing MEASURE");
}

TEST(MeasureTypeTest, TestSupportsPartitioning) {
  TypeFactory factory;
  std::string type_description = "";
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_GROUP_BY_ARRAY);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* measure_type_with_string,
                       factory.MakeMeasureType(types::StringType()));
  EXPECT_FALSE(measure_type_with_string->SupportsPartitioning(
      language_options, &type_description));
  EXPECT_EQ(type_description, "MEASURE");

  type_description = "";
  const ArrayType* array_type_with_measure;
  GOOGLESQL_ASSERT_OK(factory.MakeArrayType(measure_type_with_string,
                                  &array_type_with_measure));
  EXPECT_FALSE(array_type_with_measure->SupportsPartitioning(
      language_options, &type_description));
  EXPECT_EQ(type_description, "ARRAY containing MEASURE");
}

TEST(MeasureTypeTest, TestSupportsEquality) {
  TypeFactory factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* measure_type_with_string,
                       factory.MakeMeasureType(types::StringType()));
  EXPECT_FALSE(measure_type_with_string->SupportsEquality());
}

TEST(MeasureTypeTest, TestTypeEqualityAndEquivalence) {
  TypeFactory factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* measure_type_with_int64,
                       factory.MakeMeasureType(types::Int64Type()));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* another_measure_type_with_int64,
                       factory.MakeMeasureType(types::Int64Type()));
  EXPECT_TRUE(measure_type_with_int64->IsMeasureType());

  // A measure type is equal to itself.
  EXPECT_TRUE(measure_type_with_int64->Equals(measure_type_with_int64));
  EXPECT_TRUE(measure_type_with_int64->AsMeasure()->EqualsForSameKind(
      measure_type_with_int64, /*equivalent=*/false));
  EXPECT_TRUE(measure_type_with_int64->AsMeasure()->EqualsForSameKind(
      measure_type_with_int64, /*equivalent=*/true));

  // Two measure types with the same result type are not considered equal or
  // equivalent.
  EXPECT_FALSE(
      measure_type_with_int64->Equals(another_measure_type_with_int64));
  EXPECT_FALSE(measure_type_with_int64->AsMeasure()->EqualsForSameKind(
      another_measure_type_with_int64, /*equivalent=*/false));
  EXPECT_FALSE(measure_type_with_int64->AsMeasure()->EqualsForSameKind(
      another_measure_type_with_int64, /*equivalent=*/true));
}

TEST(MeasureTypeTest, TestIsSupportedType) {
  TypeFactory factory;
  LanguageOptions language_options;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* measure_type_with_int64,
                       factory.MakeMeasureType(types::Int64Type()));
  EXPECT_FALSE(measure_type_with_int64->IsSupportedType(language_options));

  // Type is supported when FEATURE_ENABLE_MEASURES is enabled.
  language_options.EnableLanguageFeature(FEATURE_ENABLE_MEASURES);
  EXPECT_TRUE(measure_type_with_int64->IsSupportedType(language_options));
}

}  // namespace

}  // namespace googlesql
