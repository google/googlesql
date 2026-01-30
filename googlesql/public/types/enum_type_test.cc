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

#include "googlesql/public/types/enum_type.h"

#include <string>

#include "googlesql/public/functions/rounding_mode.pb.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/testdata/bad_test_schema.pb.h"
#include "googlesql/testdata/recursive_schema.pb.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/descriptor.h"

namespace googlesql {

using google::protobuf::EnumDescriptor;
using testing::ElementsAre;
using testing::IsEmpty;
using testing::NotNull;

bool TestEquals(const Type* type1, const Type* type2) {
  // Test that Equivalent also returns the same thing as Equals for this
  // comparison.  This helper is not used for cases where Equals and
  // Equivalent may differ.
  EXPECT_EQ(type1->Equals(type2), type1->Equivalent(type2))
      << "type1: " << type1->DebugString()
      << "\ntype2: " << type2->DebugString();
  return type1->Equals(type2);
}

TEST(EnumTypeTest, Basics) {
  TypeFactory factory;
  const EnumType* enum_type;
  const EnumDescriptor* enum_descriptor = googlesql_test::TestEnum_descriptor();
  GOOGLESQL_EXPECT_OK(factory.MakeEnumType(enum_descriptor, &enum_type));
  EXPECT_THAT(enum_type, NotNull());
  EXPECT_FALSE(enum_type->UsingFeatureV12CivilTimeType());
  {
    LanguageOptions options;
    options.set_product_mode(PRODUCT_INTERNAL);
    EXPECT_TRUE(enum_type->IsSupportedType(options));
  }
  {
    LanguageOptions options;
    options.set_product_mode(PRODUCT_EXTERNAL);
    EXPECT_FALSE(enum_type->IsSupportedType(options));
    options.EnableLanguageFeature(FEATURE_PROTO_BASE);
    EXPECT_TRUE(enum_type->IsSupportedType(options));
  }
}

TEST(EnumTypeTest, EnumTypeFromCompiledEnumAndDescriptor) {
  TypeFactory factory;

  const EnumDescriptor* enum_descriptor = googlesql_test::TestEnum_descriptor();
  const EnumType* enum_type;
  GOOGLESQL_ASSERT_OK(factory.MakeEnumType(enum_descriptor, &enum_type));
  EXPECT_THAT(enum_type, NotNull());
  EXPECT_EQ("ENUM<googlesql_test.TestEnum>", enum_type->DebugString());
  EXPECT_EQ("`googlesql_test.TestEnum`", enum_type->TypeName(PRODUCT_INTERNAL));
  EXPECT_THAT(enum_type->CatalogNamePath(), IsEmpty());

  const ArrayType* array_type;
  GOOGLESQL_ASSERT_OK(factory.MakeArrayType(enum_type, &array_type));
  EXPECT_EQ("ARRAY<ENUM<googlesql_test.TestEnum>>", array_type->DebugString());
  EXPECT_EQ("ARRAY<`googlesql_test.TestEnum`>",
            array_type->TypeName(PRODUCT_INTERNAL));

  absl::string_view name;
  EXPECT_TRUE(enum_type->FindName(1, &name));
  EXPECT_EQ("TESTENUM1", name);
  EXPECT_FALSE(enum_type->FindName(777, &name));

  int number = 777;
  EXPECT_TRUE(enum_type->FindNumber("TESTENUM0", &number));
  EXPECT_EQ(0, number);
  EXPECT_FALSE(enum_type->FindNumber("BLAH", &number));

  EXPECT_FALSE(enum_type->IsSimpleType());
  EXPECT_FALSE(enum_type->IsArray());
  EXPECT_FALSE(enum_type->IsProto());
  EXPECT_FALSE(enum_type->IsStruct());
  EXPECT_FALSE(enum_type->IsStructOrProto());
  EXPECT_FALSE(enum_type->IsRange());
  EXPECT_FALSE(enum_type->IsMap());

  EXPECT_TRUE(enum_type->IsEnum());

  EXPECT_EQ(enum_type, enum_type->AsEnum());
  EXPECT_EQ(nullptr, enum_type->AsStruct());
  EXPECT_EQ(nullptr, enum_type->AsArray());
  EXPECT_EQ(nullptr, enum_type->AsProto());
  EXPECT_EQ(nullptr, enum_type->AsRange());

  EXPECT_EQ(enum_type->DebugString(true),
            "ENUM<googlesql_test.TestEnum, "
                      "file name: googlesql/testdata/test_schema.proto, "
            R"(<enum TestEnum {
  TESTENUM0 = 0;
  TESTENUM1 = 1;
  TESTENUM2 = 2;
  TESTENUM2147483647 = 2147483647;
  TESTENUMNEGATIVE = -1;
}
>>)");
}

TEST(EnumTypeTest, EnumTypeWithCatalogNameFromCompiledEnumAndDescriptor) {
  TypeFactory factory;

  const EnumDescriptor* enum_descriptor = googlesql_test::TestEnum_descriptor();
  const EnumType* enum_type;
  GOOGLESQL_ASSERT_OK(
      factory.MakeEnumType(enum_descriptor, &enum_type, {"all", "catalogs"}));

  EXPECT_EQ("`all`.catalogs.`googlesql_test.TestEnum`",
            enum_type->TypeName(PRODUCT_INTERNAL));
  EXPECT_EQ("`all`.catalogs.googlesql_test.TestEnum",
            enum_type->ShortTypeName(PRODUCT_INTERNAL));
  EXPECT_THAT(enum_type->CatalogNamePath(), ElementsAre("all", "catalogs"));

  EXPECT_EQ(enum_type->DebugString(false),
            "`all`.catalogs.ENUM<googlesql_test.TestEnum>");
  EXPECT_EQ(enum_type->DebugString(true),
            "`all`.catalogs.ENUM<googlesql_test.TestEnum, "
                      "file name: googlesql/testdata/test_schema.proto, "
            R"(<enum TestEnum {
  TESTENUM0 = 0;
  TESTENUM1 = 1;
  TESTENUM2 = 2;
  TESTENUM2147483647 = 2147483647;
  TESTENUMNEGATIVE = -1;
}
>>)");

  const EnumType* enum_type_without_catalog;
  GOOGLESQL_ASSERT_OK(factory.MakeEnumType(enum_descriptor, &enum_type_without_catalog));
  EXPECT_FALSE(enum_type->Equals(enum_type_without_catalog));
  EXPECT_TRUE(enum_type->Equivalent(enum_type_without_catalog));

  const EnumType* enum_type_another_catalog;
  GOOGLESQL_ASSERT_OK(factory.MakeEnumType(enum_descriptor, &enum_type_another_catalog,
                                 {"another_catalog"}));
  EXPECT_FALSE(enum_type->Equals(enum_type_another_catalog));
  EXPECT_TRUE(enum_type->Equivalent(enum_type_another_catalog));
}

TEST(EnumTypeTest, Equalities) {
  TypeFactory factory;
  const EnumDescriptor* enum_descriptor = googlesql_test::TestEnum_descriptor();
  const EnumType* enum_type;
  GOOGLESQL_ASSERT_OK(factory.MakeEnumType(enum_descriptor, &enum_type));
  ASSERT_TRUE(TestEquals(enum_type, enum_type));
}

TEST(EnumTypeTest, IsValidEnumValue) {
  TypeFactory factory;

  const EnumType* opaque_type = types::RoundingModeEnumType();
  ASSERT_TRUE(opaque_type->IsOpaque());
  EXPECT_FALSE(opaque_type->IsValidEnumValue(nullptr));
  // this also returns null, but ensures the call pattern works as expected.
  EXPECT_FALSE(opaque_type->IsValidEnumValue(
      opaque_type->enum_descriptor()->FindValueByName("Fake Name")));

  // Marked invalid in rounding_mode.proto
  EXPECT_FALSE(opaque_type->IsValidEnumValue(
      opaque_type->enum_descriptor()->FindValueByName(
          "ROUNDING_MODE_UNSPECIFIED")));
  EXPECT_FALSE(opaque_type->IsValidEnumValue(
      opaque_type->enum_descriptor()->FindValueByNumber(
          functions::ROUNDING_MODE_UNSPECIFIED)));

  // Normal
  EXPECT_TRUE(opaque_type->IsValidEnumValue(
      opaque_type->enum_descriptor()->FindValueByName("ROUND_HALF_EVEN")));
}

TEST(EnumTypeTest, IsValidOpaqueEnumValue) {
  TypeFactory factory;
  const EnumType* opaque_type = types::RoundingModeEnumType();

  // Non-opaque variant shouldn't care about the invalid_enum_value annotation.
  const EnumType* non_opaque_type;
  GOOGLESQL_ASSERT_OK(
      factory.MakeEnumType(opaque_type->enum_descriptor(), &non_opaque_type));

  EXPECT_TRUE(non_opaque_type->IsValidEnumValue(
      opaque_type->enum_descriptor()->FindValueByName(
          "ROUNDING_MODE_UNSPECIFIED")));

  // Ever enum value should be valid, since it is non-opaque
  const google::protobuf::EnumDescriptor* type_descriptor =
      non_opaque_type->enum_descriptor();
  ASSERT_GT(type_descriptor->value_count(), 0);
  for (int i = 0; i < type_descriptor->value_count(); ++i) {
    EXPECT_TRUE(non_opaque_type->IsValidEnumValue(type_descriptor->value(i)));
  }
}

TEST(EnumTypeTest, OpaqueEnumTypesAreNotEqualToTheirEnumTypes) {
  TypeFactory factory;

  const EnumType* opaque_type = nullptr;
  GOOGLESQL_ASSERT_OK(internal::TypeFactoryHelper::MakeOpaqueEnumType(
      &factory, googlesql_test::TestEnum_descriptor(), &opaque_type, {}));
  ASSERT_TRUE(opaque_type->IsOpaque());
  ASSERT_TRUE(opaque_type->Equals(opaque_type));

  const EnumType* non_opaque_type;
  GOOGLESQL_ASSERT_OK(
      factory.MakeEnumType(opaque_type->enum_descriptor(), &non_opaque_type));
  ASSERT_FALSE(non_opaque_type->IsOpaque());
  ASSERT_FALSE(opaque_type->Equals(non_opaque_type));
  ASSERT_FALSE(non_opaque_type->Equals(opaque_type));
}

TEST(EnumTypeTest, OpaqueEnumTypesAreNotEquilvalentToTheirEnumTypes) {
  TypeFactory factory;

  const EnumType* opaque_type = nullptr;
  GOOGLESQL_ASSERT_OK(internal::TypeFactoryHelper::MakeOpaqueEnumType(
      &factory, googlesql_test::TestEnum_descriptor(), &opaque_type, {}));
  ASSERT_TRUE(opaque_type->IsOpaque());
  ASSERT_TRUE(opaque_type->Equals(opaque_type));

  const EnumType* non_opaque_type;
  GOOGLESQL_ASSERT_OK(
      factory.MakeEnumType(opaque_type->enum_descriptor(), &non_opaque_type));
  ASSERT_FALSE(non_opaque_type->IsOpaque());
  ASSERT_FALSE(opaque_type->Equivalent(non_opaque_type));
  ASSERT_FALSE(non_opaque_type->Equivalent(opaque_type));
}

TEST(EnumTypeTest, OpaqueEnumTypesCacheCorrectly) {
  TypeFactory factory;
  const EnumType* enum_type = nullptr;
  GOOGLESQL_ASSERT_OK(internal::TypeFactoryHelper::MakeOpaqueEnumType(
      &factory, googlesql_test::TestEnum_descriptor(), &enum_type, {}));
  const EnumType* same_enum_type = nullptr;

  GOOGLESQL_ASSERT_OK(internal::TypeFactoryHelper::MakeOpaqueEnumType(
      &factory, googlesql_test::TestEnum_descriptor(), &same_enum_type, {}));
  // Should be pointer equality.
  ASSERT_EQ(enum_type, same_enum_type);
}

TEST(EnumTypeTest, OpaqueEnumTypesHaveCorrectStringFunctions) {
  const EnumType* rounding_mode = types::RoundingModeEnumType();
  EXPECT_EQ(rounding_mode->ShortTypeName(), "ROUNDING_MODE");
  EXPECT_EQ(rounding_mode->TypeName(), "ROUNDING_MODE");
}

TEST(EnumTypeTest, EnumTypesAreCached) {
  TypeFactory factory;

  const Type* type1;
  GOOGLESQL_ASSERT_OK(
      factory.MakeEnumType(googlesql_test::TestEnum_descriptor(), &type1));

  const Type* type2;
  GOOGLESQL_ASSERT_OK(
      factory.MakeEnumType(googlesql_test::TestEnum_descriptor(), &type2));
  EXPECT_EQ(type1, type2);

  const Type* type_with_empty_catalog;
  GOOGLESQL_ASSERT_OK(factory.MakeEnumType(googlesql_test::TestEnum_descriptor(),
                                 &type_with_empty_catalog, {}));
  EXPECT_EQ(type_with_empty_catalog, type1);

  const Type* type_with_catalog1;
  GOOGLESQL_ASSERT_OK(factory.MakeEnumType(googlesql_test::TestEnum_descriptor(),
                                 &type_with_catalog1, {"catalog", "a.b"}));
  EXPECT_NE(type_with_catalog1, type1);

  const Type* type_with_catalog2;
  GOOGLESQL_ASSERT_OK(factory.MakeEnumType(googlesql_test::TestEnum_descriptor(),
                                 &type_with_catalog2, {"catalog", "a.b"}));
  EXPECT_EQ(type_with_catalog2, type_with_catalog1);

  const Type* type_with_catalog3;
  GOOGLESQL_ASSERT_OK(factory.MakeEnumType(googlesql_test::TestEnum_descriptor(),
                                 &type_with_catalog3, {"catalog.a", "b"}));
  EXPECT_NE(type_with_catalog3, type_with_catalog1);
}

TEST(EnumTypeTest, EnumTypeIsSupported) {
  LanguageOptions product_external;
  product_external.set_product_mode(ProductMode::PRODUCT_EXTERNAL);

  LanguageOptions product_internal;
  product_internal.set_product_mode(ProductMode::PRODUCT_INTERNAL);

  LanguageOptions product_external_dp_enabled;
  product_external_dp_enabled.set_product_mode(ProductMode::PRODUCT_EXTERNAL);
  product_external_dp_enabled.EnableLanguageFeature(
      FEATURE_DIFFERENTIAL_PRIVACY);

  LanguageOptions product_external_report_enabled = product_external;
  product_external_report_enabled.EnableLanguageFeature(
      FEATURE_DIFFERENTIAL_PRIVACY);
  product_external_report_enabled.EnableLanguageFeature(
      FEATURE_DIFFERENTIAL_PRIVACY_REPORT_FUNCTIONS);

  LanguageOptions product_internal_report_enabled = product_internal;
  product_internal_report_enabled.EnableLanguageFeature(
      FEATURE_DIFFERENTIAL_PRIVACY);
  product_internal_report_enabled.EnableLanguageFeature(
      FEATURE_DIFFERENTIAL_PRIVACY_REPORT_FUNCTIONS);

  LanguageOptions proto_base_enabled;
  proto_base_enabled.EnableLanguageFeature(FEATURE_PROTO_BASE);

  LanguageOptions proto_base_enabled_report_enabled;
  proto_base_enabled_report_enabled.EnableLanguageFeature(FEATURE_PROTO_BASE);
  proto_base_enabled_report_enabled.EnableLanguageFeature(
      FEATURE_DIFFERENTIAL_PRIVACY);
  proto_base_enabled_report_enabled.EnableLanguageFeature(
      FEATURE_DIFFERENTIAL_PRIVACY_REPORT_FUNCTIONS);

  EXPECT_FALSE(
      types::DifferentialPrivacyReportFormatEnumType()->IsSupportedType(
          product_external));
  EXPECT_FALSE(
      types::DifferentialPrivacyReportFormatEnumType()->IsSupportedType(
          product_internal));
  EXPECT_TRUE(types::DifferentialPrivacyReportFormatEnumType()->IsSupportedType(
      product_external_report_enabled));
  EXPECT_TRUE(types::DifferentialPrivacyReportFormatEnumType()->IsSupportedType(
      product_internal_report_enabled));
  EXPECT_FALSE(
      types::DifferentialPrivacyReportFormatEnumType()->IsSupportedType(
          proto_base_enabled));
  EXPECT_TRUE(types::DifferentialPrivacyReportFormatEnumType()->IsSupportedType(
      proto_base_enabled_report_enabled));

  EXPECT_TRUE(types::DifferentialPrivacyGroupSelectionStrategyEnumType()
                  ->IsSupportedType(product_internal));
  EXPECT_FALSE(types::DifferentialPrivacyGroupSelectionStrategyEnumType()
                   ->IsSupportedType(product_external));

  EXPECT_TRUE(types::DatePartEnumType()->IsSupportedType(product_external));
  EXPECT_TRUE(types::DatePartEnumType()->IsSupportedType(product_internal));
  EXPECT_TRUE(types::DatePartEnumType()->IsSupportedType(proto_base_enabled));

  ASSERT_TRUE(
      types::
          DifferentialPrivacyCountDistinctContributionBoundingStrategyEnumType()
              .ok());
  EXPECT_TRUE(
      types::
          DifferentialPrivacyCountDistinctContributionBoundingStrategyEnumType()
              .value()
              ->IsSupportedType(product_external_dp_enabled));
  EXPECT_TRUE(
      types::
          DifferentialPrivacyCountDistinctContributionBoundingStrategyEnumType()
              .value()
              ->IsSupportedType(product_external_dp_enabled));

  EXPECT_TRUE(
      types::NormalizeModeEnumType()->IsSupportedType(product_external));
  EXPECT_TRUE(
      types::NormalizeModeEnumType()->IsSupportedType(product_internal));
  EXPECT_TRUE(
      types::NormalizeModeEnumType()->IsSupportedType(proto_base_enabled));

  EXPECT_TRUE(types::RoundingModeEnumType()->IsSupportedType(product_external));
  EXPECT_TRUE(types::RoundingModeEnumType()->IsSupportedType(product_internal));
  EXPECT_TRUE(
      types::RoundingModeEnumType()->IsSupportedType(proto_base_enabled));

  EXPECT_FALSE(
      types::ArrayFindModeEnumType()->IsSupportedType(product_external));
  EXPECT_TRUE(
      types::ArrayFindModeEnumType()->IsSupportedType(product_internal));
  EXPECT_TRUE(
      types::ArrayFindModeEnumType()->IsSupportedType(proto_base_enabled));

  EXPECT_TRUE(types::ArrayZipModeEnumType()->IsSupportedType(product_external));
  EXPECT_TRUE(types::ArrayZipModeEnumType()->IsSupportedType(product_internal));
  EXPECT_TRUE(
      types::ArrayZipModeEnumType()->IsSupportedType(proto_base_enabled));

  LanguageOptions range_type_enabled;
  range_type_enabled.EnableLanguageFeature(FEATURE_RANGE_TYPE);
  EXPECT_FALSE(
      types::RangeSessionizeModeEnumType()->IsSupportedType(product_external));
  EXPECT_FALSE(
      types::RangeSessionizeModeEnumType()->IsSupportedType(product_internal));
  EXPECT_FALSE(types::RangeSessionizeModeEnumType()->IsSupportedType(
      proto_base_enabled));
  EXPECT_TRUE(types::RangeSessionizeModeEnumType()->IsSupportedType(
      range_type_enabled));

  EXPECT_TRUE(
      types::UnsupportedFieldsEnumType()->IsSupportedType(product_external));
  EXPECT_TRUE(
      types::UnsupportedFieldsEnumType()->IsSupportedType(product_internal));
  EXPECT_TRUE(
      types::UnsupportedFieldsEnumType()->IsSupportedType(proto_base_enabled));

  EXPECT_TRUE(
      types::BitwiseAggModeEnumType()->IsSupportedType(product_external));
  EXPECT_TRUE(
      types::BitwiseAggModeEnumType()->IsSupportedType(product_internal));
  EXPECT_TRUE(
      types::BitwiseAggModeEnumType()->IsSupportedType(proto_base_enabled));

  auto rank_type_enum_type = types::RankTypeEnumType();
  ASSERT_TRUE(rank_type_enum_type.ok());
  EXPECT_TRUE(rank_type_enum_type.value()->IsSupportedType(product_external));
  EXPECT_TRUE(rank_type_enum_type.value()->IsSupportedType(product_internal));
  EXPECT_TRUE(rank_type_enum_type.value()->IsSupportedType(proto_base_enabled));
}

}  // namespace googlesql
