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

#include "googlesql/public/types/declarative_type.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/type_parameters.pb.h"
#include "googlesql/public/types/builtin_declarative_types.h"
#include "googlesql/public/types/collation.h"
#include "googlesql/public/types/simple_value.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_deserializer.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/type_parameters.h"
#include "googlesql/public/value.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "testing/base/public/malloc_counter.h"
#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"

using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;

namespace googlesql {

TEST(DeclarativeTypeTest, MinimalTypeCreation) {
  TypeFactory type_factory;
  const Type* t1 = nullptr;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(t1,
                       type_factory.MakeDeclarativeType(
                           DeclarativeTypeDescriptor()
                               .set_type_id({"NS", "T1"})
                               .set_display_name("t1")
                               .set_backing_type(type_factory.get_int64())));

  EXPECT_EQ(t1->TypeName(ProductMode::PRODUCT_INTERNAL), "t1");
  EXPECT_EQ(t1->kind(), TYPE_DECLARATIVE);
  EXPECT_TRUE(t1->IsDeclarativeType());
  EXPECT_EQ(t1->AsDeclarativeType(), t1);

  EXPECT_THAT(
      t1->TypeNameWithModifiers(TypeModifiers(), ProductMode::PRODUCT_INTERNAL),
      IsOkAndHolds("t1"));
}

TEST(DeclarativeTypeTest, TypeNameWithModifiersSupportsTypeModifiers) {
  TypeFactory type_factory;
  const Type* t1 = nullptr;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(t1,
                       type_factory.MakeDeclarativeType(
                           DeclarativeTypeDescriptor()
                               .set_type_id({"NS", "T1"})
                               .set_display_name("t1")
                               .set_backing_type(type_factory.get_int64())));

  EXPECT_THAT(
      t1->TypeNameWithModifiers(TypeModifiers(), ProductMode::PRODUCT_INTERNAL),
      IsOkAndHolds("t1"));

  NumericTypeParametersProto numeric_type_params_proto;
  numeric_type_params_proto.set_precision(5);
  numeric_type_params_proto.set_scale(3);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters numeric_type_parameters,
                       TypeParameters::MakeNumericTypeParameters(
                           std::move(numeric_type_params_proto)));

  EXPECT_THAT(t1->TypeNameWithModifiers(
                  TypeModifiers::MakeTypeModifiers(
                      std::move(numeric_type_parameters), Collation()),
                  ProductMode::PRODUCT_INTERNAL),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(DeclarativeTypeTest, DeclarativeTypeDisallowingReturning) {
  TypeFactory type_factory;
  LanguageOptions language_options;

  const Type* backing_type = types::Int64Type();

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* t1,
      type_factory.MakeDeclarativeType(
          DeclarativeTypeDescriptor()
              .set_type_id({"NS", "DeclType"})
              .set_display_name("DeclType")
              .set_backing_type(backing_type)
              .set_coercion_from_backing_type(
                  DeclarativeTypeDescriptor::AllowCoercionMode::
                      kAllowAllCoercion)
              .set_coercion_to_backing_type(
                  DeclarativeTypeDescriptor::AllowCoercionMode::kExplicitOnly)
              .set_returning_strategy(
                  DeclarativeTypeDescriptor::ReturningDisallowed{})));

  // ReturningDisallowed ensures that SupportsReturning() returns false, even
  // though the backing type does support returning.
  EXPECT_TRUE(backing_type->SupportsReturning(language_options));

  std::string type_description;
  EXPECT_FALSE(t1->SupportsReturning(language_options, &type_description));
  EXPECT_EQ(type_description, "DeclType");

  const DeclarativeType* decl_type = t1->AsDeclarativeType();
  ASSERT_NE(decl_type, nullptr);

  EXPECT_TRUE(decl_type->CanCoerceTo(backing_type, /*is_explicit=*/true));
  EXPECT_FALSE(decl_type->CanCoerceTo(backing_type,
                                      /*is_explicit=*/false));

  EXPECT_TRUE(decl_type->CanCoerceFrom(backing_type,
                                       /*is_explicit=*/true));
  EXPECT_TRUE(decl_type->CanCoerceFrom(backing_type,
                                       /*is_explicit=*/false));
}

TEST(DeclarativeTypeTest, DeclarativeTypeDisallowingEquality) {
  TypeFactory type_factory;
  LanguageOptions language_options;

  const Type* backing_type = types::Int64Type();

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* decl_type,
      type_factory.MakeDeclarativeType(
          DeclarativeTypeDescriptor()
              .set_type_id({"NS", "DeclType"})
              .set_display_name("DeclType")
              .set_backing_type(backing_type)
              .set_equality_strategy(
                  DeclarativeTypeDescriptor::EqualityDisallowed{})));

  EXPECT_TRUE(backing_type->SupportsEquality());
  EXPECT_FALSE(decl_type->SupportsEquality());

  EXPECT_TRUE(backing_type->SupportsGrouping(language_options));
  std::string type_description;
  EXPECT_FALSE(
      decl_type->SupportsGrouping(language_options, &type_description));
  EXPECT_EQ(type_description, "DeclType");

  EXPECT_TRUE(backing_type->SupportsPartitioning(language_options));
  std::string type_description2;
  EXPECT_FALSE(
      decl_type->SupportsPartitioning(language_options, &type_description2));
  EXPECT_EQ(type_description2, "DeclType");
}

TEST(DeclarativeTypeTest, DeclarativeTypeDelegatingEqualityToSupported) {
  TypeFactory type_factory;
  LanguageOptions language_options;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* t1,
      type_factory.MakeDeclarativeType(
          DeclarativeTypeDescriptor()
              .set_type_id({"NS", "T1"})
              .set_display_name("t1")
              .set_backing_type(type_factory.get_int64())
              .set_equality_strategy(
                  DeclarativeTypeDescriptor::EqualityDelegated{})));

  EXPECT_TRUE(t1->SupportsEquality());
  EXPECT_TRUE(t1->SupportsGrouping(language_options));
  EXPECT_TRUE(t1->SupportsPartitioning(language_options));
}

TEST(DeclarativeTypeTest, DeclarativeTypeEqualityDelegatedIsNotSupported) {
  TypeFactory type_factory;
  LanguageOptions language_options;
  language_options.DisableLanguageFeature(FEATURE_JSON_TYPE_COMPARISON);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* t1,
      type_factory.MakeDeclarativeType(
          DeclarativeTypeDescriptor()
              .set_type_id({"NS", "T1"})
              .set_display_name("t1")
              .set_backing_type(types::JsonType())
              .set_equality_strategy(
                  DeclarativeTypeDescriptor::EqualityDelegated{})));

  EXPECT_TRUE(t1->SupportsEquality());
  std::string type_description;
  ASSERT_FALSE(t1->SupportsGrouping(language_options, &type_description));
  EXPECT_EQ(type_description, "t1");

  std::string type_description2;
  ASSERT_FALSE(t1->SupportsPartitioning(language_options, &type_description2));
  EXPECT_EQ(type_description2, "t1");
}

// For this test case, we create a declarative type with a backing type of
// ARRAY<Geography>, to ensure that it still reports itself as the problematic
// type. Furthermore, we actually test a complex type containing that
// declarative type, to make sure that it is reported correctly, and not just
// with the opaque kind "DECLARATIVE".
TEST(DeclarativeTypeTest,
     DeclarativeTypeEqualityDelegatedToUnsupportedReportsCorrectTypeName) {
  TypeFactory type_factory;
  LanguageOptions language_options;
  language_options.EnableLanguageFeature(FEATURE_GROUP_BY_ARRAY);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* t1,
      type_factory.MakeDeclarativeType(
          DeclarativeTypeDescriptor()
              .set_type_id({"NS", "T1"})
              .set_display_name("t1")
              .set_backing_type(types::GeographyArrayType())
              .set_equality_strategy(
                  DeclarativeTypeDescriptor::EqualityDelegated{})));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* t1_array, type_factory.MakeArrayType(t1));

  EXPECT_FALSE(t1_array->SupportsEquality());

  std::string type_description;
  EXPECT_FALSE(t1_array->SupportsGrouping(language_options, &type_description));
  EXPECT_EQ(type_description, "ARRAY containing t1");

  type_description = "";
  EXPECT_FALSE(
      t1_array->SupportsPartitioning(language_options, &type_description));
  EXPECT_EQ(type_description, "ARRAY containing t1");
}

TEST(DeclarativeTypeTest, DeclarativeTypeReturningDelegated) {
  TypeFactory type_factory;
  LanguageOptions language_options;

  const Type* t1 = nullptr;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      t1, type_factory.MakeDeclarativeType(
              DeclarativeTypeDescriptor()
                  .set_type_id({"NS", "T1"})
                  .set_display_name("t1")
                  .set_backing_type(type_factory.get_int64())
                  .set_returning_strategy(
                      DeclarativeTypeDescriptor::ReturningDelegated{})));
  std::string type_description = "";
  EXPECT_TRUE(t1->SupportsReturning(language_options, &type_description));
  EXPECT_EQ(type_description, "");

  const Type* t_disallowed = nullptr;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      t_disallowed,
      type_factory.MakeDeclarativeType(
          DeclarativeTypeDescriptor()
              .set_type_id({"NS", "T_Disallowed"})
              .set_display_name("T_Disallowed")
              .set_backing_type(type_factory.get_int64())
              .set_returning_strategy(
                  DeclarativeTypeDescriptor::ReturningDisallowed{})));
  EXPECT_FALSE(
      t_disallowed->SupportsReturning(language_options, &type_description));
  EXPECT_EQ(type_description, "T_Disallowed");

  // ARRAY<T_Disallowed> reports the correct type description for not returning.
  type_description = "";
  const Type* t_disallowed_array = nullptr;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(t_disallowed_array,
                       type_factory.MakeArrayType(t_disallowed));
  EXPECT_FALSE(t_disallowed_array->SupportsReturning(language_options,
                                                     &type_description));
  EXPECT_EQ(type_description, "T_Disallowed");

  // DeclType based on ARRAY<T_Disallowed>, delegates returnability so will also
  // be non-returnable. Note that it reports itself as the reason.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* transitive,
      type_factory.MakeDeclarativeType(
          DeclarativeTypeDescriptor()
              .set_type_id({"NS", "T_Transitive"})
              .set_display_name("T_Transitive")
              .set_backing_type(t_disallowed_array)
              .set_returning_strategy(
                  DeclarativeTypeDescriptor::ReturningDelegated{})));
  type_description = "";
  EXPECT_FALSE(
      transitive->SupportsReturning(language_options, &type_description));
  EXPECT_EQ(type_description, "T_Transitive");

  // Even if returning_strategy = ReturningDelegated for the declarative type,
  // the backing type for a declarative type still must be returnable for the
  // declarative type itself to be returnable.
  const Type* t2 = nullptr;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      t2, type_factory.MakeDeclarativeType(
              DeclarativeTypeDescriptor()
                  .set_type_id({"NS", "T2"})
                  .set_display_name("t2")
                  .set_backing_type(t_disallowed)
                  .set_returning_strategy(
                      DeclarativeTypeDescriptor::ReturningDelegated{})));
  type_description = "";
  EXPECT_FALSE(t2->SupportsReturning(language_options, &type_description));
  EXPECT_EQ(type_description, "t2");

  // STRUCT<t2> reports the correct type description for not returning.
  type_description = "";
  const Type* t2_struct = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory.MakeStructType({{"a", t2}}, &t2_struct));
  EXPECT_FALSE(
      t2_struct->SupportsReturning(language_options, &type_description));
  EXPECT_EQ(type_description, "t2");

  // DeclType based on STRUCT<T_Disallowed>, delegates returnability so will
  // also be non-returnable. Note that it reports itself as the reason.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const Type* transitive2,
      type_factory.MakeDeclarativeType(
          DeclarativeTypeDescriptor()
              .set_type_id({"NS", "T_Transitive2"})
              .set_display_name("T_Transitive2")
              .set_backing_type(t2_struct)
              .set_returning_strategy(
                  DeclarativeTypeDescriptor::ReturningDelegated{})));
  type_description = "";
  EXPECT_FALSE(
      transitive2->SupportsReturning(language_options, &type_description));
  EXPECT_EQ(type_description, "T_Transitive2");
}

TEST(DeclarativeTypeTest, NoComponentTypes) {
  TypeFactory type_factory;

  const Type* struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory.MakeStructType({{"a", type_factory.get_int32()}},
                                        &struct_type));

  const Type* decl_struct_type = nullptr;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      decl_struct_type,
      type_factory.MakeDeclarativeType(DeclarativeTypeDescriptor()
                                           .set_type_id({"NS", "T_STRUCT"})
                                           .set_display_name("t_struct")
                                           .set_backing_type(struct_type)));

  EXPECT_THAT(decl_struct_type->ComponentTypes(), IsEmpty());
}

TEST(DeclarativeTypeTest, DeclarativeTypeCounterEquality) {
  TypeFactory type_factory;
  const Type* t1 = nullptr;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      t1, type_factory.MakeDeclarativeType(
              DeclarativeTypeDescriptor()
                  .set_type_id(DeclarativeTypeId{
                      .name_space = "NS", .local_id = "T1", .version_id = "v1"})
                  .set_display_name("t1")
                  .set_backing_type(type_factory.get_int64())));

  const Type* t2 = nullptr;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      t2, type_factory.MakeDeclarativeType(
              DeclarativeTypeDescriptor()
                  .set_type_id(DeclarativeTypeId{
                      .name_space = "NS", .local_id = "T1", .version_id = "v2"})
                  .set_display_name("t1")
                  .set_backing_type(type_factory.get_int64())));

  EXPECT_FALSE(t1->Equals(t2));

  absl::Hash<DeclarativeTypeId> type_id_hasher;
  EXPECT_NE(type_id_hasher(t1->AsDeclarativeType()->id()),
            type_id_hasher(t2->AsDeclarativeType()->id()));
}

TEST(DeclarativeTypeTest, DeclarativeTypeProtoFieldCount) {
  TypeFactory type_factory;

  const google::protobuf::Descriptor* descriptor = DeclarativeTypeProto::descriptor();
  EXPECT_EQ(descriptor->field_count(), 10)
      << "DeclarativeTypeProto field count has changed. If you added a new "
         "field to DeclarativeTypeProto, please make sure to update the "
         "descriptor structure, the serializer "
         "(SerializeToProtoAndDistinctFileDescriptorsImpl in "
         "declarative_type.cc), "
         "the deserializer "
         "(DeserializeDeclarativeTypeDescriptor in type_deserializer.cc), "
         "IsIdenticalTo() in declarative_type.cc, "
         "DeclarativeTypeDescriptor::GetEstimatedOwnedMemoryBytesSize(), "
         "and update this expected count.";
}

TEST(DeclarativeTypeTest, DeclarativeTypeIsSupportedType) {
  TypeFactory type_factory;
  const Type* t1 = nullptr;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(t1, type_factory.MakeDeclarativeType(
                               DeclarativeTypeDescriptor()
                                   .set_type_id({"NS", "T1"})
                                   .set_display_name("t1")
                                   .set_backing_type(type_factory.get_json())
                                   .set_additional_required_language_features(
                                       {FEATURE_NUMERIC_TYPE})));

  // Case 1: All options disabled.
  LanguageOptions options_all_disabled;
  EXPECT_FALSE(t1->IsSupportedType(options_all_disabled));

  // Case 2a: Missing FEATURE_JSON_TYPE (required for Json backing type)
  LanguageOptions options_missing_json;
  options_missing_json.EnableLanguageFeature(FEATURE_NUMERIC_TYPE);
  EXPECT_FALSE(t1->IsSupportedType(options_missing_json));

  // Case 2b: Missing FEATURE_NUMERIC_TYPE (additional required feature)
  LanguageOptions options_missing_numeric;
  options_missing_numeric.EnableLanguageFeature(FEATURE_JSON_TYPE);
  EXPECT_FALSE(t1->IsSupportedType(options_missing_numeric));

  // Case 3: All enabled
  LanguageOptions options_all_enabled;
  options_all_enabled.EnableLanguageFeature(FEATURE_JSON_TYPE);
  options_all_enabled.EnableLanguageFeature(FEATURE_NUMERIC_TYPE);
  EXPECT_TRUE(t1->IsSupportedType(options_all_enabled));
}

TEST(DeclarativeTypeTest,
     DeclarativeBuiltinTypeIsDeterminedBasedOnNamespaceInTypeId) {
  TypeFactory type_factory;
  auto descriptor =
      DeclarativeTypeDescriptor()
          .set_type_id({std::string(DeclarativeTypeId::kGoogleSqlNamespace),
                        "MY_BUILTIN_TYPE"})
          .set_display_name("my_builtin")
          .set_backing_type(types::Int32Type());

  ASSERT_TRUE(descriptor.type_id().IsGoogleSQLBuiltin());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* builtin_type,
                       type_factory.MakeDeclarativeType(descriptor));
  EXPECT_TRUE(builtin_type->AsDeclarativeType()->IsGoogleSQLBuiltin());

  // Non-builtin type.
  descriptor.set_type_id({"CustomNamespace", "MY_CUSTOM"});

  ASSERT_FALSE(descriptor.type_id().IsGoogleSQLBuiltin());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* non_builtin_type,
                       type_factory.MakeDeclarativeType(descriptor));
  EXPECT_FALSE(non_builtin_type->AsDeclarativeType()->IsGoogleSQLBuiltin());
}

TEST(DeclarativeTypeTest, DescriptorValidation) {
  TypeFactory type_factory;

  // Valid descriptor
  GOOGLESQL_EXPECT_OK(type_factory.MakeDeclarativeType(
      DeclarativeTypeDescriptor()
          .set_type_id(
              DeclarativeTypeId{.name_space = "NS", .local_id = "local_id"})
          .set_display_name("t1")
          .set_backing_type(type_factory.get_int64())));

  // Empty Namespace
  EXPECT_THAT(type_factory.MakeDeclarativeType(
                  DeclarativeTypeDescriptor()
                      .set_type_id(DeclarativeTypeId{.name_space = "",
                                                     .local_id = "local_id"})
                      .set_display_name("t1")
                      .set_backing_type(type_factory.get_int64())),
              StatusIs(absl::StatusCode::kInternal, HasSubstr("name_space")));

  // Empty Local ID
  EXPECT_THAT(type_factory.MakeDeclarativeType(
                  DeclarativeTypeDescriptor()
                      .set_type_id(
                          DeclarativeTypeId{.name_space = "NS", .local_id = ""})
                      .set_display_name("t1")
                      .set_backing_type(type_factory.get_int64())),
              StatusIs(absl::StatusCode::kInternal, HasSubstr("local_id")));

  // Non-empty version_id for GoogleSQL built-in type
  EXPECT_THAT(type_factory.MakeDeclarativeType(
                  DeclarativeTypeDescriptor()
                      .set_type_id(DeclarativeTypeId{
                          .name_space = std::string(
                              DeclarativeTypeId::kGoogleSqlNamespace),
                          .local_id = "local_id",
                          .version_id = "v1"})
                      .set_display_name("t1")
                      .set_backing_type(type_factory.get_int64())),
              StatusIs(absl::StatusCode::kInternal, HasSubstr("version_id")));

  // Empty display_name
  EXPECT_THAT(type_factory.MakeDeclarativeType(
                  DeclarativeTypeDescriptor()
                      .set_type_id(DeclarativeTypeId{.name_space = "NS",
                                                     .local_id = "local_id"})
                      .set_display_name("")
                      .set_backing_type(type_factory.get_int64())),
              StatusIs(absl::StatusCode::kInternal, HasSubstr("display_name")));

  // Nullptr backing_type
  EXPECT_THAT(
      type_factory.MakeDeclarativeType(
          DeclarativeTypeDescriptor()
              .set_type_id(DeclarativeTypeId{
                  .name_space = "NS", .local_id = "local_id", .version_id = ""})
              .set_display_name("t1")
              .set_backing_type(nullptr)),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("backing_type")));
}

TEST(DeclarativeTypeTest, ValidationOfRequiredFieldsUponDeserialization) {
  TypeFactory type_factory;

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* declarative_type,
                       type_factory.MakeDeclarativeType(
                           DeclarativeTypeDescriptor()
                               .set_type_id({"CustomNamespace", "MY_CUSTOM"})
                               .set_display_name("my_custom")
                               .set_backing_type(types::Int32Type())));

  TypeProto type_proto;
  FileDescriptorSetMap file_descriptor_set_map;
  GOOGLESQL_ASSERT_OK(declarative_type->SerializeToProtoAndDistinctFileDescriptors(
      &type_proto, &file_descriptor_set_map));

  TypeDeserializer deserializer(&type_factory);
  {
    // Test missing namespace.
    TypeProto empty_namespace_proto = type_proto;
    auto* mutable_id =
        empty_namespace_proto.mutable_declarative_type()->mutable_type_id();

    mutable_id->clear_name_space();
    EXPECT_THAT(deserializer.Deserialize(empty_namespace_proto),
                StatusIs(absl::StatusCode::kInternal, HasSubstr("name_space")));

    // Test empty string namespace.
    mutable_id->set_name_space("");
    EXPECT_THAT(deserializer.Deserialize(empty_namespace_proto),
                StatusIs(absl::StatusCode::kInternal, HasSubstr("name_space")));
  }

  {
    // Test empty local_id.
    TypeProto empty_local_id_proto = type_proto;
    auto* mutable_id =
        empty_local_id_proto.mutable_declarative_type()->mutable_type_id();

    mutable_id->clear_local_id();
    EXPECT_THAT(deserializer.Deserialize(empty_local_id_proto),
                StatusIs(absl::StatusCode::kInternal, HasSubstr("local_id")));

    // Test empty string local_id.
    mutable_id->set_local_id("");
    EXPECT_THAT(deserializer.Deserialize(empty_local_id_proto),
                StatusIs(absl::StatusCode::kInternal, HasSubstr("local_id")));
  }

  {
    // Test empty display name.
    TypeProto empty_display_name_proto = type_proto;
    auto* mutable_decl_type =
        empty_display_name_proto.mutable_declarative_type();
    mutable_decl_type->clear_display_name();
    EXPECT_THAT(
        deserializer.Deserialize(empty_display_name_proto),
        StatusIs(absl::StatusCode::kInternal, HasSubstr("display_name")));

    // Test empty string display name.
    mutable_decl_type->set_display_name("");
    EXPECT_THAT(
        deserializer.Deserialize(empty_display_name_proto),
        StatusIs(absl::StatusCode::kInternal, HasSubstr("display_name")));
  }

  {
    // Test missing backing type.
    TypeProto missing_backing_type_proto = type_proto;
    missing_backing_type_proto.mutable_declarative_type()->clear_backing_type();
    EXPECT_THAT(
        deserializer.Deserialize(missing_backing_type_proto),
        StatusIs(absl::StatusCode::kInternal, HasSubstr("backing_type")));
  }
}

TEST(DeclarativeTypeTest, DeclarativeTypeVersionIdCannotBeSetForBuiltinTypes) {
  TypeFactory type_factory;

  auto descriptor =
      DeclarativeTypeDescriptor()
          .set_type_id(
              {std::string(DeclarativeTypeId::kGoogleSqlNamespace), "BT", "v1"})
          .set_display_name("builtin_type")
          .set_backing_type(types::Int32Type());

  EXPECT_THAT(type_factory.MakeDeclarativeType(descriptor),
              StatusIs(absl::StatusCode::kInternal, HasSubstr("version_id")));

  // Use a proper descriptor with an empty version_id to generate a valid proto.
  descriptor.set_type_id(
      {std::string(DeclarativeTypeId::kGoogleSqlNamespace), "BT", ""});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* declarative_type,
                       type_factory.MakeDeclarativeType(descriptor));
  TypeProto type_proto;
  FileDescriptorSetMap file_descriptor_set_map;
  GOOGLESQL_ASSERT_OK(declarative_type->SerializeToProtoAndDistinctFileDescriptors(
      &type_proto, &file_descriptor_set_map));

  type_proto.mutable_declarative_type()->mutable_type_id()->set_version_id(
      "v1");

  TypeDeserializer deserializer(&type_factory);
  EXPECT_THAT(deserializer.Deserialize(type_proto),
              StatusIs(absl::StatusCode::kInternal, HasSubstr("version_id")));
}

TEST(DeclarativeTypeTest,
     UsesStaticFactoryForGoogleSQLBuiltinTypesBackedByTypesInStaticFactory) {
  auto descriptor =
      DeclarativeTypeDescriptor()
          .set_type_id(
              {std::string(DeclarativeTypeId::kGoogleSqlNamespace), "T1"})
          .set_display_name("t1")
          .set_backing_type(types::Int64Type());

  ASSERT_TRUE(descriptor.type_id().IsGoogleSQLBuiltin());

  // Create t1 from factory1.
  TypeFactory factory1;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* t1,
                       factory1.MakeDeclarativeType(descriptor));

  // Create t2 from factory2.
  TypeFactory factory2;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* t2,
                       factory2.MakeDeclarativeType(descriptor));
  EXPECT_EQ(t1, t2);

  // Same for a transitive builtin DeclarativeType
  descriptor
      .set_type_id({std::string(DeclarativeTypeId::kGoogleSqlNamespace), "T2"})
      .set_display_name("t2")
      .set_backing_type(t1);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* t3,
                       factory1.MakeDeclarativeType(descriptor));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* t4,
                       factory2.MakeDeclarativeType(descriptor));
  EXPECT_EQ(t3, t4);
}

TEST(DeclarativeTypeTest, BuiltinTypesBackedByTypesNotInStaticFactory) {
  auto descriptor =
      DeclarativeTypeDescriptor()
          .set_type_id(
              {std::string(DeclarativeTypeId::kGoogleSqlNamespace), "T1"})
          .set_display_name("t1");

  ASSERT_TRUE(descriptor.type_id().IsGoogleSQLBuiltin());

  const auto* enum_descriptor = googlesql_test::TestEnum_descriptor();

  // Create t1 from factory1.
  TypeFactory factory1;
  const EnumType* enum_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory1.MakeEnumType(enum_descriptor, &enum_type));

  descriptor.set_backing_type(enum_type);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* t1,
                       factory1.MakeDeclarativeType(descriptor));

  // Create t2 from factory2.
  TypeFactory factory2;
  enum_type = nullptr;
  GOOGLESQL_ASSERT_OK(factory2.MakeEnumType(enum_descriptor, &enum_type));

  descriptor.set_backing_type(enum_type);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* t2,
                       factory2.MakeDeclarativeType(descriptor));

  // Pointers are not equal, but the type still compares equal.
  EXPECT_NE(t1, t2);
  EXPECT_TRUE(t1->Equals(t2));
}

TEST(DeclarativeTypeTest, DoesNotUseStaticFactoryForNonBuiltinTypes) {
  auto descriptor = DeclarativeTypeDescriptor()
                        .set_type_id({"NS", "T1"})
                        .set_display_name("t1")
                        .set_backing_type(types::Int64Type());

  ASSERT_FALSE(descriptor.type_id().IsGoogleSQLBuiltin());

  // Create t1 from factory1.
  TypeFactory factory1;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* t1,
                       factory1.MakeDeclarativeType(descriptor));

  // Create t2 from factory2.
  TypeFactory factory2;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* t2,
                       factory2.MakeDeclarativeType(descriptor));
  // Pointers are not equal, but the type still compares equal.
  EXPECT_NE(t1, t2);
  EXPECT_TRUE(t1->Equals(t2));
}

TEST(TypeTest, TypeFactoryDoesNotAllowConflictingDeclarativeTypes) {
  TypeFactory factory;

  auto descriptor = DeclarativeTypeDescriptor()
                        .set_type_id({"NS", "T1"})
                        .set_display_name("t1")
                        .set_backing_type(types::StringType());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* t1,
                       (factory.MakeDeclarativeType(descriptor)));
  EXPECT_TRUE(t1 != nullptr);

  // Change the type descriptor, maintaining the same ID.
  descriptor.set_backing_type(types::Int64Type());

  EXPECT_THAT(factory.MakeDeclarativeType(descriptor),
              StatusIs(absl::StatusCode::kInternal, HasSubstr("Conflicting")));
}

static absl::StatusOr<TypeParameters> ResolveEmptyTypeParameters1(
    const std::vector<TypeParameterValue>&, ProductMode) {
  return TypeParameters();
}

static absl::StatusOr<TypeParameters> ResolveEmptyTypeParameters2(
    const std::vector<TypeParameterValue>&, ProductMode) {
  return TypeParameters();
}

static absl::Status ValidateOkTypeParameters(const TypeParameters&,
                                             ProductMode) {
  return absl::OkStatus();
}

TEST(DeclarativeTypeTest, TypeParameterHandlersCreateNullCallbacksFails) {
  EXPECT_THAT(TypeParameterHandlers::Create(nullptr, nullptr),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(
      TypeParameterHandlers::Create(&ResolveEmptyTypeParameters1, nullptr),
      StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(TypeParameterHandlers::Create(nullptr, &ValidateOkTypeParameters),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(DeclarativeTypeTest, TypeParameterHandlersNonBuiltinTypeRejected) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const TypeParameterHandlers handlers,
      TypeParameterHandlers::Create(&ResolveEmptyTypeParameters1,
                                    &ValidateOkTypeParameters));

  auto descriptor = DeclarativeTypeDescriptor()
                        .set_type_id({"CUSTOM_NS", "VEC"})
                        .set_display_name("VEC")
                        .set_backing_type(types::BytesType())
                        .set_type_params_strategy(handlers);

  ASSERT_FALSE(descriptor.type_id().IsGoogleSQLBuiltin());

  DeclarativeTypeId type_id = descriptor.type_id();

  // Create t1 from factory1.
  TypeFactory factory1;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* t1,
                       factory1.MakeDeclarativeType(descriptor));

  // Create t2 from factory2.
  TypeFactory factory2;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* t2,
                       factory2.MakeDeclarativeType(descriptor));
  // Pointers are not equal, but the type still compares equal.
  EXPECT_NE(t1, t2);
  EXPECT_TRUE(t1->Equals(t2));

  // Round-trip through serialization and ensure deserialization rehydrates it
  // correctly by adding the correct type parameter handlers.
  TypeProto type_proto;
  GOOGLESQL_ASSERT_OK(t1->SerializeToSelfContainedProto(&type_proto));

  {
    // Deserializing without the custom handlers will fail GOOGLESQL_RET_CHECK during
    // descriptor deserialization.
    TypeDeserializer incomplete_deserializer(&factory1);
    EXPECT_THAT(incomplete_deserializer.Deserialize(type_proto),
                StatusIs(absl::StatusCode::kInternal));
  }

  {
    // Providing different handlers also leads to conflict.
    const auto get_decl_type_param_handlers_cb_wrong =
        [](const DeclarativeTypeId& id)
        -> absl::StatusOr<std::optional<TypeParameterHandlers>> {
      return TypeParameterHandlers::Create(&ResolveEmptyTypeParameters2,
                                           &ValidateOkTypeParameters);
    };
    DeclarativeTypeCallbacksRegistry registry(
        get_decl_type_param_handlers_cb_wrong);
    TypeDeserializer incorrect_deserializer(
        &factory1, /*descriptor_pools=*/{},
        /*extended_type_deserializer=*/nullptr, registry);
    EXPECT_THAT(incorrect_deserializer.Deserialize(type_proto),
                StatusIs(absl::StatusCode::kInternal,
                         HasSubstr("Conflicting declarative types")));
  }

  {
    // Providing the correct handlers succeeds.
    const auto get_decl_type_param_handlers_cb = [](const DeclarativeTypeId& id)
        -> absl::StatusOr<std::optional<TypeParameterHandlers>> {
      return TypeParameterHandlers::Create(&ResolveEmptyTypeParameters1,
                                           &ValidateOkTypeParameters);
    };
    DeclarativeTypeCallbacksRegistry registry(get_decl_type_param_handlers_cb);

    TypeDeserializer deserializer(&factory1, /*descriptor_pools=*/{},
                                  /*extended_type_deserializer=*/nullptr,
                                  registry);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* deserialized_type,
                         deserializer.Deserialize(type_proto));
    EXPECT_EQ(deserialized_type, t1);
  }
}

static absl::StatusOr<TypeParameters> ResolveMyVecTypeParameters(
    const std::vector<TypeParameterValue>& values, ProductMode) {
  if (values.size() != 1 || !values[0].GetValue().has_int64_value()) {
    return absl::InvalidArgumentError("Expected 1 int64 parameter");
  }
  StringTypeParametersProto proto;
  proto.set_max_length(values[0].GetValue().int64_value());
  return TypeParameters::MakeStringTypeParameters(proto);
}

static absl::Status ValidateMyVecTypeParameters(const TypeParameters& params,
                                                ProductMode) {
  if (params.IsStringTypeParameters() &&
      params.string_type_parameters().max_length() == 999) {
    return absl::InvalidArgumentError("Invalid dimension 999");
  }
  return absl::OkStatus();
}

TEST(DeclarativeTypeTest, TypeParameterHandlersBuiltinTypeSuccessAndExecution) {
  TypeFactory factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      TypeParameterHandlers handlers,
      TypeParameterHandlers::Create(&ResolveMyVecTypeParameters,
                                    &ValidateMyVecTypeParameters));

  auto builtin_descriptor =
      DeclarativeTypeDescriptor()
          .set_type_id(
              {std::string(DeclarativeTypeId::kGoogleSqlNamespace), "MY_VEC"})
          .set_display_name("MY_VEC")
          .set_backing_type(types::BytesType())
          .set_type_params_strategy(std::move(handlers));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* my_vec,
                       factory.MakeDeclarativeType(builtin_descriptor));

  std::vector<TypeParameterValue> params = {
      TypeParameterValue(SimpleValue::Int64(128))};
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      TypeParameters resolved_params,
      my_vec->ValidateAndResolveTypeParameters(params, PRODUCT_INTERNAL));
  EXPECT_EQ(resolved_params.DebugString(), "(max_length=128)");

  GOOGLESQL_EXPECT_OK(my_vec->ValidateResolvedTypeParameters(resolved_params,
                                                   PRODUCT_INTERNAL));
  StringTypeParametersProto bad_proto;
  bad_proto.set_max_length(999);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(TypeParameters bad_params,
                       TypeParameters::MakeStringTypeParameters(bad_proto));
  EXPECT_THAT(
      my_vec->ValidateResolvedTypeParameters(bad_params, PRODUCT_INTERNAL),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Invalid dimension 999")));

  EXPECT_THAT(my_vec->TypeNameWithModifiers(TypeModifiers::MakeTypeModifiers(
                                                resolved_params, Collation()),
                                            PRODUCT_INTERNAL),
              IsOkAndHolds("MY_VEC(128)"));
  EXPECT_THAT(my_vec->TypeNameWithModifiers(
                  TypeModifiers::MakeTypeModifiers(bad_params, Collation()),
                  PRODUCT_INTERNAL),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid dimension 999")));
}

TEST(DeclarativeTypeTest, TypeParameterHandlersIsIdenticalTo) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      TypeParameterHandlers handlers1,
      TypeParameterHandlers::Create(&ResolveEmptyTypeParameters1,
                                    &ValidateOkTypeParameters));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      TypeParameterHandlers handlers2,
      TypeParameterHandlers::Create(&ResolveEmptyTypeParameters2,
                                    &ValidateOkTypeParameters));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      TypeParameterHandlers handlers1_copy,
      TypeParameterHandlers::Create(&ResolveEmptyTypeParameters1,
                                    &ValidateOkTypeParameters));

  auto desc_with_handlers1 =
      DeclarativeTypeDescriptor()
          .set_type_id(
              {std::string(DeclarativeTypeId::kGoogleSqlNamespace), "VEC"})
          .set_display_name("VEC")
          .set_backing_type(types::BytesType())
          .set_type_params_strategy(std::move(handlers1));
  auto desc_with_handlers2 = desc_with_handlers1;
  desc_with_handlers2.set_type_params_strategy(std::move(handlers2));
  auto desc_with_handlers1_copy = desc_with_handlers1;
  desc_with_handlers1_copy.set_type_params_strategy(std::move(handlers1_copy));
  auto desc_without_handlers =
      DeclarativeTypeDescriptor()
          .set_type_id(
              {std::string(DeclarativeTypeId::kGoogleSqlNamespace), "VEC"})
          .set_display_name("VEC")
          .set_backing_type(types::BytesType());

  EXPECT_TRUE(desc_with_handlers1.IsIdenticalTo(desc_with_handlers1_copy));
  EXPECT_FALSE(desc_with_handlers1.IsIdenticalTo(desc_with_handlers2));
  EXPECT_FALSE(desc_with_handlers1.IsIdenticalTo(desc_without_handlers));
  EXPECT_FALSE(desc_without_handlers.IsIdenticalTo(desc_with_handlers1));
}

TEST(DeclarativeTypeTest, TypeFactoryRejectsConflictingTypeParameterHandlers) {
  TypeFactory factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      TypeParameterHandlers handlers1,
      TypeParameterHandlers::Create(&ResolveEmptyTypeParameters1,
                                    &ValidateOkTypeParameters));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      TypeParameterHandlers handlers2,
      TypeParameterHandlers::Create(&ResolveEmptyTypeParameters2,
                                    &ValidateOkTypeParameters));

  auto desc1 =
      DeclarativeTypeDescriptor()
          .set_type_id({std::string(DeclarativeTypeId::kGoogleSqlNamespace),
                        "MY_VEC_COPY"})
          .set_display_name("MY_VEC_COPY")
          .set_backing_type(types::BytesType())
          .set_type_params_strategy(std::move(handlers1));
  auto desc2 = desc1;
  desc2.set_type_params_strategy(std::move(handlers2));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* t1, factory.MakeDeclarativeType(desc1));
  EXPECT_TRUE(t1 != nullptr);
  EXPECT_THAT(factory.MakeDeclarativeType(desc2),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Conflicting declarative types")));
}

using FormatOptions =
    DeclarativeTypeDescriptor::FormattingCustom::FormatOptions;

static std::string MyFormatCallback(const ValueContent& value,
                                    const FormatOptions& options) {
  int64_t v = value.GetAs<int64_t>();
  switch (options.mode) {
    case FormatOptions::Mode::kDebug:
      return absl::StrCat("DEBUG(", v, ")");
    case FormatOptions::Mode::kSQLLiteral:
      return absl::StrCat("LITERAL(", v, ")");
    case FormatOptions::Mode::kSQLExpression:
      return absl::StrCat("CAST(", v, " AS foo_type)");
  }
}

static std::string Formatter1(const ValueContent&, const FormatOptions&) {
  return "fmt1";
}

static std::string Formatter2(const ValueContent&, const FormatOptions&) {
  return "fmt2";
}

TEST(DeclarativeTypeTest, FormattingCustomExecution) {
  TypeFactory factory;
  auto descriptor =
      DeclarativeTypeDescriptor()
          .set_type_id({"CUSTOM_NS", "FOO_TYPE"})
          .set_display_name("foo_type")
          .set_backing_type(types::Int64Type())
          .set_formatting_strategy(
              DeclarativeTypeDescriptor::FormattingCustom(&MyFormatCallback));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* foo_type,
                       factory.MakeDeclarativeType(descriptor));
  const DeclarativeType* decl_type = foo_type->AsDeclarativeType();
  ASSERT_NE(decl_type, nullptr);
  EXPECT_TRUE(decl_type->SupportsFormatting());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value value,
                       Value::Declarative(decl_type, Value::Int64(42)));

  // DebugString / Format (Mode::kDebug)
  EXPECT_EQ(value.DebugString(), "DEBUG(42)");
  EXPECT_EQ(value.Format(/*print_top_level_type=*/false), "DEBUG(42)");

  // GetSQLLiteral (Mode::kSQLLiteral)
  EXPECT_EQ(value.GetSQLLiteral(), "LITERAL(42)");

  // GetSQL (Mode::kSQLExpression)
  EXPECT_EQ(value.GetSQL(), "CAST(42 AS foo_type)");
}

TEST(DeclarativeTypeTest, FormattingDisallowedExecution) {
  TypeFactory factory;
  auto descriptor = DeclarativeTypeDescriptor()
                        .set_type_id({"CUSTOM_NS", "DISALLOWED_TYPE"})
                        .set_display_name("disallowed_type")
                        .set_backing_type(types::Int64Type())
                        .set_formatting_strategy(
                            DeclarativeTypeDescriptor::FormattingDisallowed{});

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* type,
                       factory.MakeDeclarativeType(descriptor));
  const DeclarativeType* decl_type = type->AsDeclarativeType();
  ASSERT_NE(decl_type, nullptr);
  EXPECT_FALSE(decl_type->SupportsFormatting());

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value value,
                       Value::Declarative(decl_type, Value::Int64(42)));

  EXPECT_EQ(value.DebugString(), "ERROR('Unimplemented')");
  EXPECT_EQ(value.Format(/*print_top_level_type=*/false),
            "ERROR('Unimplemented')");
  EXPECT_EQ(value.GetSQLLiteral(), "ERROR('Unimplemented')");
  EXPECT_EQ(value.GetSQL(), "ERROR('Unimplemented')");
}

TEST(DeclarativeTypeTest, FormattingStrategyIsIdenticalTo) {
  auto base_desc = DeclarativeTypeDescriptor()
                       .set_type_id({"NS", "T_FMT"})
                       .set_display_name("t_fmt")
                       .set_backing_type(types::Int64Type());

  auto desc_disallowed1 = base_desc;
  desc_disallowed1.set_formatting_strategy(
      DeclarativeTypeDescriptor::FormattingDisallowed{});
  auto desc_disallowed2 = base_desc;
  desc_disallowed2.set_formatting_strategy(
      DeclarativeTypeDescriptor::FormattingDisallowed{});

  auto desc_cb1 = base_desc;
  desc_cb1.set_formatting_strategy(
      DeclarativeTypeDescriptor::FormattingCustom(&Formatter1));
  auto desc_cb1_copy = base_desc;
  desc_cb1_copy.set_formatting_strategy(
      DeclarativeTypeDescriptor::FormattingCustom(&Formatter1));
  auto desc_cb2 = base_desc;
  desc_cb2.set_formatting_strategy(
      DeclarativeTypeDescriptor::FormattingCustom(&Formatter2));

  EXPECT_TRUE(desc_disallowed1.IsIdenticalTo(desc_disallowed2));
  EXPECT_TRUE(desc_cb1.IsIdenticalTo(desc_cb1_copy));
  EXPECT_FALSE(desc_cb1.IsIdenticalTo(desc_cb2));
  EXPECT_FALSE(desc_cb1.IsIdenticalTo(desc_disallowed1));
}

TEST(DeclarativeTypeTest, TypeFactoryRejectsConflictingFormattingCallbacks) {
  TypeFactory factory;
  auto desc1 =
      DeclarativeTypeDescriptor()
          .set_type_id({"NS", "T_CONFLICT"})
          .set_display_name("t_conflict")
          .set_backing_type(types::Int64Type())
          .set_formatting_strategy(
              DeclarativeTypeDescriptor::FormattingCustom(&Formatter1));
  auto desc2 = desc1;
  desc2.set_formatting_strategy(
      DeclarativeTypeDescriptor::FormattingCustom(&Formatter2));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* t1, factory.MakeDeclarativeType(desc1));
  EXPECT_TRUE(t1 != nullptr);
  EXPECT_THAT(factory.MakeDeclarativeType(desc2),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Conflicting declarative types")));
}

TEST(DeclarativeTypeTest, FormattingCustomDeserialization) {
  auto descriptor =
      DeclarativeTypeDescriptor()
          .set_type_id({"ENGINE_NS", "ENGINE_TYPE"})
          .set_display_name("engine_type")
          .set_backing_type(types::Int64Type())
          .set_formatting_strategy(
              DeclarativeTypeDescriptor::FormattingCustom(&MyFormatCallback));

  TypeFactory factory1;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* t1,
                       factory1.MakeDeclarativeType(descriptor));

  TypeProto type_proto;
  GOOGLESQL_ASSERT_OK(t1->SerializeToSelfContainedProto(&type_proto));
  EXPECT_EQ(type_proto.declarative_type().formatting_strategy(),
            DeclarativeTypeProto::FORMATTING_CUSTOM);

  // Deserializing without custom callback hook fails GOOGLESQL_RET_CHECK during
  // descriptor deserialization.
  {
    TypeDeserializer incomplete_deserializer(&factory1);
    EXPECT_THAT(incomplete_deserializer.Deserialize(type_proto),
                StatusIs(absl::StatusCode::kInternal));
  }

  // Deserializing with the wrong callback hook also leads to conflict.
  {
    const auto get_wrong_fmt_cb = [](const DeclarativeTypeId& id)
        -> absl::StatusOr<std::optional<
            DeclarativeTypeDescriptor::FormattingCustom::Callback>> {
      return &Formatter2;
    };
    DeclarativeTypeCallbacksRegistry registry(nullptr, get_wrong_fmt_cb);
    TypeDeserializer incorrect_deserializer(
        &factory1, /*descriptor_pools=*/{},
        /*extended_type_deserializer=*/nullptr, registry);
    EXPECT_THAT(incorrect_deserializer.Deserialize(type_proto),
                StatusIs(absl::StatusCode::kInternal,
                         HasSubstr("Conflicting declarative types")));
  }

  // Deserializing with the correct callback hook rehydrates the callback and
  // succeeds.
  {
    const auto get_fmt_cb = [](const DeclarativeTypeId& id)
        -> absl::StatusOr<std::optional<
            DeclarativeTypeDescriptor::FormattingCustom::Callback>> {
      return &MyFormatCallback;
    };
    DeclarativeTypeCallbacksRegistry registry(nullptr, get_fmt_cb);
    TypeDeserializer deserializer(&factory1, /*descriptor_pools=*/{},
                                  /*extended_type_deserializer=*/nullptr,
                                  registry);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* deserialized_type,
                         deserializer.Deserialize(type_proto));
    EXPECT_EQ(deserialized_type, t1);
  }
}

TEST(DeclarativeTypeTest, FormattingDisallowedSerialization) {
  TypeFactory factory;
  auto desc_disallowed =
      DeclarativeTypeDescriptor()
          .set_type_id({"NS", "T_DISALLOWED"})
          .set_display_name("t_disallowed")
          .set_backing_type(types::Int64Type())
          .set_formatting_strategy(
              DeclarativeTypeDescriptor::FormattingDisallowed{});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* t_dis,
                       factory.MakeDeclarativeType(desc_disallowed));

  TypeProto proto_dis;
  GOOGLESQL_ASSERT_OK(t_dis->SerializeToSelfContainedProto(&proto_dis));
  EXPECT_EQ(proto_dis.declarative_type().formatting_strategy(),
            DeclarativeTypeProto::FORMATTING_DISALLOWED);

  TypeDeserializer deserializer(&factory);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* deserialized_dis,
                       deserializer.Deserialize(proto_dis));
  EXPECT_EQ(deserialized_dis, t_dis);
}

TEST(DeclarativeTypeTest, BuiltinDeclarativeTypesOpaqueCallbacksLookup) {
  EXPECT_FALSE(GetBuiltinTypeParameterHandlers("NONEXISTENT_TYPE").has_value());
  EXPECT_FALSE(
      GetBuiltinCustomFormattingCallback("NONEXISTENT_TYPE").has_value());

  EXPECT_TRUE(GetBuiltinTypeParameterHandlers("VECTOR").has_value());
  EXPECT_TRUE(GetBuiltinCustomFormattingCallback("VECTOR").has_value());
}

TEST(DeclarativeTypeTest,
     BuiltinDeclarativeTypeDeserializationWithCustomFormat) {
  // Simulate a GoogleSQL built-in declarative type serialized with
  // FORMATTING_CUSTOM.
  TypeFactory factory;
  auto builtin_desc =
      DeclarativeTypeDescriptor()
          .set_type_id({std::string(DeclarativeTypeId::kGoogleSqlNamespace),
                        "BUILTIN_FORMAT_TEST"})
          .set_display_name("builtin_format_test")
          .set_backing_type(types::Int64Type())
          .set_formatting_strategy(
              DeclarativeTypeDescriptor::FormattingCustom(&MyFormatCallback));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const Type* t1,
                       factory.MakeDeclarativeType(builtin_desc));

  TypeProto proto;
  GOOGLESQL_ASSERT_OK(t1->SerializeToSelfContainedProto(&proto));
  EXPECT_EQ(proto.declarative_type().formatting_strategy(),
            DeclarativeTypeProto::FORMATTING_CUSTOM);

  // Deserializing without a registered built-in callback in
  // builtin_declarative_types will fail GOOGLESQL_RET_CHECK during descriptor
  // deserialization.
  TypeDeserializer deserializer(&factory);
  EXPECT_THAT(deserializer.Deserialize(proto),
              StatusIs(absl::StatusCode::kInternal));
}

}  // namespace googlesql
