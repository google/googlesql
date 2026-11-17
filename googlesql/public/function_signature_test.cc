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

#include "googlesql/public/function_signature.h"

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/enum_utils.h"
#include "googlesql/common/function_signature_testutil.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/proto/function.pb.h"
#include "googlesql/public/error_location.pb.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/input_argument_type.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type_parameters.pb.h"
#include "googlesql/public/types/collation.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_deserializer.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/type_parameters.h"
#include "googlesql/public/value.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/log_severity.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql {

using testing::HasSubstr;
using testing::IsNull;
using testing::NotNull;
using absl_testing::IsOkAndHolds;
using absl_testing::StatusIs;

TEST(FunctionSignatureTests, FunctionArgumentTypeTests) {
  TypeFactory factory;
  FunctionArgumentType fixed_type_int32(factory.get_int32());
  ASSERT_FALSE(fixed_type_int32.IsConcrete());
  fixed_type_int32.set_num_occurrences(0);
  ASSERT_TRUE(fixed_type_int32.IsConcrete());
  EXPECT_FALSE(fixed_type_int32.IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
  fixed_type_int32.set_num_occurrences(2);
  ASSERT_TRUE(fixed_type_int32.IsConcrete());
  EXPECT_FALSE(fixed_type_int32.IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
  fixed_type_int32.set_num_occurrences(1);
  ASSERT_TRUE(fixed_type_int32.IsConcrete());
  GOOGLESQL_EXPECT_OK(fixed_type_int32.IsValid(ProductMode::PRODUCT_EXTERNAL));
  ASSERT_THAT(fixed_type_int32.type(), NotNull());
  ASSERT_EQ(ARG_KIND_EXPR_FIXED, fixed_type_int32.kind());
  ASSERT_FALSE(fixed_type_int32.repeated());
  ASSERT_FALSE(fixed_type_int32.optional());
  EXPECT_EQ("INT32", fixed_type_int32.UserFacingNameWithCardinality(
                         PRODUCT_INTERNAL,
                         FunctionArgumentType::NamePrintingStyle::kIfNamedOnly,
                         /*print_template_details=*/true));

  FunctionArgumentType repeating_fixed_type_int32(
      factory.get_int32(), FunctionArgumentType::REPEATED);
  ASSERT_FALSE(repeating_fixed_type_int32.IsConcrete());
  repeating_fixed_type_int32.set_num_occurrences(0);
  ASSERT_TRUE(repeating_fixed_type_int32.IsConcrete());
  GOOGLESQL_EXPECT_OK(repeating_fixed_type_int32.IsValid(ProductMode::PRODUCT_EXTERNAL));
  repeating_fixed_type_int32.IncrementNumOccurrences();
  ASSERT_TRUE(repeating_fixed_type_int32.IsConcrete());
  GOOGLESQL_EXPECT_OK(repeating_fixed_type_int32.IsValid(ProductMode::PRODUCT_EXTERNAL));
  repeating_fixed_type_int32.IncrementNumOccurrences();
  ASSERT_TRUE(repeating_fixed_type_int32.IsConcrete());
  GOOGLESQL_EXPECT_OK(repeating_fixed_type_int32.IsValid(ProductMode::PRODUCT_EXTERNAL));
  ASSERT_THAT(repeating_fixed_type_int32.type(), NotNull());
  ASSERT_EQ(ARG_KIND_EXPR_FIXED, repeating_fixed_type_int32.kind());
  ASSERT_TRUE(repeating_fixed_type_int32.repeated());
  EXPECT_EQ("[INT32, ...]",
            repeating_fixed_type_int32.UserFacingNameWithCardinality(
                PRODUCT_INTERNAL,
                FunctionArgumentType::NamePrintingStyle::kIfNamedOnly,
                /*print_template_details=*/true));

  FunctionArgumentType optional_fixed_type_int32(
      factory.get_int32(), FunctionArgumentType::OPTIONAL);
  ASSERT_FALSE(optional_fixed_type_int32.IsConcrete());
  optional_fixed_type_int32.set_num_occurrences(0);
  ASSERT_TRUE(optional_fixed_type_int32.IsConcrete());
  GOOGLESQL_EXPECT_OK(optional_fixed_type_int32.IsValid(ProductMode::PRODUCT_EXTERNAL));
  optional_fixed_type_int32.IncrementNumOccurrences();
  ASSERT_TRUE(optional_fixed_type_int32.IsConcrete());
  GOOGLESQL_EXPECT_OK(optional_fixed_type_int32.IsValid(ProductMode::PRODUCT_EXTERNAL));
  optional_fixed_type_int32.IncrementNumOccurrences();
  ASSERT_TRUE(optional_fixed_type_int32.IsConcrete());
  EXPECT_FALSE(
      optional_fixed_type_int32.IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
  optional_fixed_type_int32.set_num_occurrences(0);
  ASSERT_TRUE(optional_fixed_type_int32.IsConcrete());
  GOOGLESQL_EXPECT_OK(optional_fixed_type_int32.IsValid(ProductMode::PRODUCT_EXTERNAL));
  ASSERT_THAT(optional_fixed_type_int32.type(), NotNull());
  ASSERT_EQ(ARG_KIND_EXPR_FIXED, optional_fixed_type_int32.kind());
  ASSERT_FALSE(optional_fixed_type_int32.repeated());
  ASSERT_TRUE(optional_fixed_type_int32.optional());
  EXPECT_EQ("[INT32]",
            optional_fixed_type_int32.UserFacingNameWithCardinality(
                PRODUCT_INTERNAL,
                FunctionArgumentType::NamePrintingStyle::kIfNamedOnly,
                /*print_template_details=*/true));

  // Tests for ARG_TYPE_ANY_<K> and ARG_ARRAY_TYPE_ANY_<K>
  {
    for (const SignatureArgumentKindGroup& group :
         GetRelatedSignatureArgumentGroup()) {
      SignatureArgumentKind arg_any_kind = group.kind;
      FunctionArgumentType any_type(arg_any_kind);
      ASSERT_FALSE(any_type.IsConcrete());
      ASSERT_THAT(any_type.type(), IsNull());
      ASSERT_EQ(arg_any_kind, any_type.kind());
      ASSERT_FALSE(any_type.repeated());
    }
    for (const SignatureArgumentKindGroup& group :
         GetRelatedSignatureArgumentGroup()) {
      SignatureArgumentKind arg_array_any_kind = group.array_kind;
      FunctionArgumentType array_of_any_type(arg_array_any_kind);
      ASSERT_FALSE(array_of_any_type.IsConcrete());
      ASSERT_THAT(array_of_any_type.type(), IsNull());
      ASSERT_EQ(arg_array_any_kind, array_of_any_type.kind());
      ASSERT_FALSE(array_of_any_type.repeated());
    }
  }

  FunctionArgumentType proto_any_type(ARG_KIND_EXPR_PROTO_ANY);
  ASSERT_FALSE(proto_any_type.IsConcrete());
  ASSERT_THAT(proto_any_type.type(), IsNull());
  ASSERT_EQ(ARG_KIND_EXPR_PROTO_ANY, proto_any_type.kind());
  ASSERT_FALSE(proto_any_type.repeated());

  FunctionArgumentType struct_any_type(ARG_KIND_EXPR_STRUCT_ANY);
  ASSERT_FALSE(struct_any_type.IsConcrete());
  ASSERT_THAT(struct_any_type.type(), IsNull());
  ASSERT_EQ(ARG_KIND_EXPR_STRUCT_ANY, struct_any_type.kind());
  ASSERT_FALSE(struct_any_type.repeated());

  FunctionArgumentType enum_any_type(ARG_KIND_EXPR_ENUM_ANY);
  ASSERT_FALSE(enum_any_type.IsConcrete());
  ASSERT_THAT(enum_any_type.type(), IsNull());
  ASSERT_EQ(ARG_KIND_EXPR_ENUM_ANY, enum_any_type.kind());
  ASSERT_FALSE(enum_any_type.repeated());

  FunctionArgumentType void_type(ARG_KIND_VOID);
  ASSERT_FALSE(void_type.IsConcrete());
  void_type.set_num_occurrences(1);
  ASSERT_TRUE(void_type.IsConcrete());
  ASSERT_THAT(void_type.type(), IsNull());
  ASSERT_EQ(ARG_KIND_VOID, void_type.kind());
  ASSERT_FALSE(void_type.repeated());
}

void TestDefaultValueAfterSerialization(const FunctionArgumentType& arg_type) {
  FileDescriptorSetMap fdset_map;
  FunctionArgumentTypeProto proto;
  GOOGLESQL_EXPECT_OK(arg_type.Serialize(&fdset_map, &proto));
  TypeFactory factory;
  std::vector<const google::protobuf::DescriptorPool*> pools(fdset_map.size());
  for (const auto& pair : fdset_map) {
    pools[pair.second->descriptor_set_index] = pair.first;
  }

  std::unique_ptr<FunctionArgumentType> dummy_type =
      FunctionArgumentType::Deserialize(proto,
                                        TypeDeserializer(&factory, pools))
          .value();
  EXPECT_TRUE(
      dummy_type->GetDefault().value().Equals(arg_type.GetDefault().value()));
}

TEST(FunctionSignatureTests, FunctionArgumentTypeWithDefaultValues) {
  TypeFactory factory;
  FunctionArgumentTypeOptions invalid_required_arg_type_option =
      FunctionArgumentTypeOptions(FunctionEnums::REQUIRED)
          .set_default(values::String("abc"));
  FunctionArgumentTypeOptions valid_optional_arg_type_option =
      FunctionArgumentTypeOptions(FunctionEnums::OPTIONAL)
          .set_default(values::Int32(10086));
  FunctionArgumentTypeOptions valid_optional_arg_type_option_null =
      FunctionArgumentTypeOptions(FunctionEnums::OPTIONAL)
          .set_default(values::NullInt32());
  FunctionArgumentTypeOptions invalid_repeated_arg_type_option =
      FunctionArgumentTypeOptions(FunctionEnums::REPEATED)
          .set_default(values::Double(3.14));

  FunctionArgumentType required_fixed_type_string(
      factory.get_string(), invalid_required_arg_type_option,
      /*num_occurrences=*/1);
  EXPECT_THAT(
      required_fixed_type_string.IsValid(ProductMode::PRODUCT_EXTERNAL),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("Default value cannot be applied to a REQUIRED argument")));

  FunctionArgumentType repeated_fixed_type_double(
      factory.get_double(), invalid_repeated_arg_type_option,
      /*num_occurrences=*/1);
  EXPECT_THAT(
      repeated_fixed_type_double.IsValid(ProductMode::PRODUCT_EXTERNAL),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("Default value cannot be applied to a REPEATED argument")));

  FunctionArgumentType optional_fixed_type_bytes(factory.get_bytes(),
                                                 valid_optional_arg_type_option,
                                                 /*num_occurrences=*/1);
  EXPECT_THAT(
      optional_fixed_type_bytes.IsValid(ProductMode::PRODUCT_EXTERNAL),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("Default value type does not match the argument type")));

  FunctionArgumentType optional_fixed_type_int64(factory.get_int64(),
                                                 valid_optional_arg_type_option,
                                                 /*num_occurrences=*/1);
  EXPECT_THAT(
      optional_fixed_type_int64.IsValid(ProductMode::PRODUCT_EXTERNAL),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("Default value type does not match the argument type")));

  FunctionArgumentType bad_optional_fixed_type_int64(
      factory.get_int64(), valid_optional_arg_type_option_null,
      /*num_occurrences=*/1);
  EXPECT_THAT(
      bad_optional_fixed_type_int64.IsValid(ProductMode::PRODUCT_EXTERNAL),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("Default value type does not match the argument type")));

  FunctionArgumentType optional_fixed_type_int32(factory.get_int32(),
                                                 valid_optional_arg_type_option,
                                                 /*num_occurrences=*/1);
  EXPECT_TRUE(optional_fixed_type_int32.GetDefault().value().Equals(
      values::Int32(10086)));
  GOOGLESQL_EXPECT_OK(optional_fixed_type_int32.IsValid(ProductMode::PRODUCT_EXTERNAL));
  TestDefaultValueAfterSerialization(optional_fixed_type_int32);

  FunctionArgumentType optional_fixed_type_int32_null(
      factory.get_int32(), valid_optional_arg_type_option_null,
      /*num_occurrences=*/1);
  EXPECT_TRUE(optional_fixed_type_int32_null.GetDefault().value().Equals(
      values::NullInt32()));
  GOOGLESQL_EXPECT_OK(
      optional_fixed_type_int32_null.IsValid(ProductMode::PRODUCT_EXTERNAL));
  TestDefaultValueAfterSerialization(optional_fixed_type_int32_null);

  FunctionArgumentType templated_type_non_null(ARG_KIND_EXPR_ANY_1,
                                               valid_optional_arg_type_option);
  EXPECT_TRUE(templated_type_non_null.GetDefault().value().Equals(
      values::Int32(10086)));
  GOOGLESQL_EXPECT_OK(templated_type_non_null.IsValid(ProductMode::PRODUCT_EXTERNAL));
  TestDefaultValueAfterSerialization(templated_type_non_null);

  FunctionArgumentType templated_type_null(ARG_KIND_EXPR_ANY_1,
                                           valid_optional_arg_type_option_null);
  EXPECT_TRUE(
      templated_type_null.GetDefault().value().Equals(values::NullInt32()));
  GOOGLESQL_EXPECT_OK(templated_type_null.IsValid(ProductMode::PRODUCT_EXTERNAL));
  TestDefaultValueAfterSerialization(templated_type_null);

  FunctionArgumentType relation_type(ARG_KIND_RELATION,
                                     valid_optional_arg_type_option_null);
  EXPECT_THAT(
      relation_type.IsValid(ProductMode::PRODUCT_EXTERNAL),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("ANY TABLE argument cannot have a default value")));

  FunctionArgumentType model_type(ARG_KIND_MODEL,
                                  valid_optional_arg_type_option_null);
  EXPECT_THAT(
      model_type.IsValid(ProductMode::PRODUCT_EXTERNAL),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("ANY MODEL argument cannot have a default value")));

  FunctionArgumentType connection_type(ARG_KIND_CONNECTION,
                                       valid_optional_arg_type_option_null);
  EXPECT_THAT(
      connection_type.IsValid(ProductMode::PRODUCT_EXTERNAL),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("ANY CONNECTION argument cannot have a default value")));

  FunctionArgumentType descriptor_type(ARG_KIND_DESCRIPTOR,
                                       valid_optional_arg_type_option_null);
  EXPECT_THAT(
      descriptor_type.IsValid(ProductMode::PRODUCT_EXTERNAL),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("ANY DESCRIPTOR argument cannot have a default value")));
}

TEST(FunctionSignatureTests, FunctionArgumentTypeWithDefaultTypeModifiers) {
  TypeFactory factory;
  Collation collation = Collation::MakeScalar("und:ci");
  TypeModifiers type_modifiers =
      TypeModifiers::MakeTypeModifiers(TypeParameters(), collation);

  // Type modifiers is initialized to empty.
  FunctionArgumentType arg_type(factory.get_int32(),
                                FunctionArgumentTypeOptions(),
                                /*num_occurrences=*/1);
  ASSERT_TRUE(arg_type.type_modifiers().has_value());
  EXPECT_TRUE(arg_type.type_modifiers()->IsEmpty());
}

TEST(FunctionSignatureTests, FunctionArgumentTypeWithTypeModifiers) {
  TypeFactory factory;
  Collation collation = Collation::MakeScalar("und:ci");
  StringTypeParametersProto string_type_param_proto;
  string_type_param_proto.set_max_length(123);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      TypeParameters type_param,
      TypeParameters::MakeStringTypeParameters(string_type_param_proto));
  TypeModifiers type_modifiers =
      TypeModifiers::MakeTypeModifiers(type_param, collation);

  FunctionArgumentType arg_type(factory.get_string(),
                                FunctionArgumentTypeOptions(),
                                /*num_occurrences=*/1, type_modifiers);

  ASSERT_TRUE(arg_type.type_modifiers().has_value());
  EXPECT_TRUE(arg_type.type_modifiers()->Equals(type_modifiers));

  // Test serialization / deserialization.
  FileDescriptorSetMap fdset_map;
  FunctionArgumentTypeProto proto;
  GOOGLESQL_ASSERT_OK(arg_type.Serialize(&fdset_map, &proto));

  std::vector<const google::protobuf::DescriptorPool*> pools;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<FunctionArgumentType> deserialized_arg_type,
      FunctionArgumentType::Deserialize(proto,
                                        TypeDeserializer(&factory, pools)));

  ASSERT_TRUE(deserialized_arg_type->type_modifiers().has_value());
  EXPECT_TRUE(deserialized_arg_type->type_modifiers()->Equals(type_modifiers));
}

TEST(FunctionSignatureTests, FunctionArgumentTypeWithTypeModifiers_Validation) {
  TypeFactory factory;
  Collation collation = Collation::MakeScalar("und:ci");
  TypeModifiers type_modifiers =
      TypeModifiers::MakeTypeModifiers(TypeParameters(), collation);

  // Test that type modifiers are not allowed if kind is not
  // ARG_KIND_EXPR_FIXED.
  for (auto kind :
       googlesql_base::EnumerateEnumValues<SignatureArgumentKind>()) {
    if (kind == ARG_KIND_EXPR_FIXED ||
        // FunctionArgumentType with ARG_KIND_LAMBDA constructed directly is not
        // allowed. So skip it too.
        kind == ARG_KIND_LAMBDA) {
      continue;
    }
    FunctionArgumentType invalid_arg_type(kind, FunctionArgumentTypeOptions(),
                                          /*num_occurrences=*/1,
                                          type_modifiers);
    EXPECT_THAT(
        invalid_arg_type.IsValid(ProductMode::PRODUCT_EXTERNAL),
        StatusIs(absl::StatusCode::kInvalidArgument,
                 AllOf(HasSubstr("TypeModifiers are only applicable for kind "
                                 "ARG_KIND_EXPR_FIXED"),
                       HasSubstr(invalid_arg_type.DebugString()))));
  }

  // Test that type modifiers must be present if the kind is
  // ARG_KIND_EXPR_FIXED.
  {
    FunctionArgumentType invalid_arg_type(factory.get_int32(),
                                          FunctionArgumentTypeOptions(),
                                          /*num_occurrences=*/1, std::nullopt);
    EXPECT_THAT(
        invalid_arg_type.IsValid(ProductMode::PRODUCT_EXTERNAL),
        StatusIs(absl::StatusCode::kInvalidArgument,
                 AllOf(HasSubstr("TypeModifiers must be present for kind "
                                 "ARG_KIND_EXPR_FIXED"),
                       HasSubstr(invalid_arg_type.DebugString()))));
  }
}

TEST(FunctionSignatureTests,
     FunctionArgumentTypeWithTypeModifiers_InvalidTypeParameters) {
  TypeFactory factory;
  StringTypeParametersProto string_type_param_proto;
  string_type_param_proto.set_max_length(123);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      TypeParameters type_param,
      TypeParameters::MakeStringTypeParameters(string_type_param_proto));
  TypeModifiers type_modifiers =
      TypeModifiers::MakeTypeModifiers(type_param, Collation());

  FunctionArgumentType invalid_arg_type(factory.get_int64(),
                                        FunctionArgumentTypeOptions(),
                                        /*num_occurrences=*/1, type_modifiers);
  EXPECT_THAT(invalid_arg_type.IsValid(ProductMode::PRODUCT_EXTERNAL),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("INT64 does not support type parameters: "
                                 "INT64 type_parameters:(max_length=123)")));
}

TEST(FunctionSignatureTests,
     FunctionArgumentTypeWithTypeModifiers_InvalidCollation) {
  TypeFactory factory;
  Collation collation = Collation::MakeScalar("und:ci");
  NumericTypeParametersProto numeric_type_param_proto;
  numeric_type_param_proto.set_precision(10);
  numeric_type_param_proto.set_scale(5);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      TypeParameters type_param,
      TypeParameters::MakeNumericTypeParameters(numeric_type_param_proto));
  TypeModifiers type_modifiers =
      TypeModifiers::MakeTypeModifiers(type_param, collation);
  FunctionArgumentType invalid_arg_type(factory.get_numeric(),
                                        FunctionArgumentTypeOptions(),
                                        /*num_occurrences=*/1, type_modifiers);
  EXPECT_THAT(
      invalid_arg_type.IsValid(ProductMode::PRODUCT_EXTERNAL),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr(
                   "Collation must have compatible structure with the argument "
                   "type: NUMERIC type_parameters:(precision=10,scale=5), "
                   "collation:und:ci")));
}

TEST(FunctionSignatureTests,
     FunctionArgumentTypeWithTypeModifiers_MismatchedNumberOfFields) {
  TypeFactory factory;

  // Create type STRUCT(f1 STRING).
  const StructType* struct_type;
  GOOGLESQL_ASSERT_OK(
      factory.MakeStructType({{"f1", factory.get_string()}}, &struct_type));

  // Create type modifiers [(max_length=123),(max_length=123)].
  StringTypeParametersProto string_type_param_proto;
  string_type_param_proto.set_max_length(123);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      TypeParameters type_param,
      TypeParameters::MakeStringTypeParameters(string_type_param_proto));
  TypeModifiers type_modifiers = TypeModifiers::MakeTypeModifiers(
      TypeParameters::MakeTypeParametersWithChildList({type_param, type_param}),
      Collation());

  FunctionArgumentType invalid_arg_type(struct_type,
                                        FunctionArgumentTypeOptions(),
                                        /*num_occurrences=*/1, type_modifiers);
  EXPECT_THAT(
      invalid_arg_type.IsValid(ProductMode::PRODUCT_EXTERNAL),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          AllOf(HasSubstr("RET_CHECK failure"),
                HasSubstr(
                    "type_parameters.num_children() == num_fields() (2 vs. 1) "
                    ": STRUCT<f1 STRING> "
                    "type_parameters:[(max_length=123),(max_length=123)]"))));
}

TEST(FunctionSignatureTests, InvalidTypeModifiersInTVFSchemaColumn) {
  TypeFactory factory;
  StringTypeParametersProto string_type_param_proto;
  string_type_param_proto.set_max_length(123);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      TypeParameters type_param,
      TypeParameters::MakeStringTypeParameters(string_type_param_proto));
  TypeModifiers type_modifiers =
      TypeModifiers::MakeTypeModifiers(type_param, Collation());

  FunctionArgumentType relation_arg_type(
      googlesql::FunctionArgumentType::RelationWithSchema(
          googlesql::TVFRelation(
              {googlesql::TVFSchemaColumn("c", googlesql::types::Int64Type(),
                                          false, false, type_modifiers)}),
          /*extra_relation_input_columns_allowed=*/false));

  EXPECT_THAT(relation_arg_type.IsValid(ProductMode::PRODUCT_EXTERNAL),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Type parameters must have compatible "
                                 "structure with the column type: c "
                                 "INT64 type_parameters:(max_length=123)")));
}

TEST(FunctionSignatureTests, LambdaFunctionArgumentTypeWithTypeModifiers) {
  TypeFactory factory;
  NumericTypeParametersProto numeric_type_param_proto;
  numeric_type_param_proto.set_precision(10);
  numeric_type_param_proto.set_scale(5);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      TypeParameters type_param,
      TypeParameters::MakeNumericTypeParameters(numeric_type_param_proto));
  TypeModifiers type_modifiers =
      TypeModifiers::MakeTypeModifiers(type_param, Collation());
  FunctionArgumentType arg_type(factory.get_numeric(),
                                FunctionArgumentTypeOptions(),
                                /*num_occurrences=*/1, type_modifiers);
  EXPECT_THAT(arg_type.IsValid(ProductMode::PRODUCT_EXTERNAL),
              absl_testing::IsOk());

  // Type modifiers are not allowed in lambda argument types.
  FunctionArgumentType lambda_arg_type =
      FunctionArgumentType::Lambda({arg_type}, ARG_KIND_EXPR_ANY_1);
  EXPECT_THAT(
      lambda_arg_type.IsValid(ProductMode::PRODUCT_EXTERNAL),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("ARG_KIND_EXPR_FIXED lambda argument must have "
                         "empty type modifiers")));

  lambda_arg_type =
      FunctionArgumentType::Lambda({ARG_KIND_EXPR_ANY_1}, arg_type);
  EXPECT_THAT(
      lambda_arg_type.IsValid(ProductMode::PRODUCT_EXTERNAL),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("ARG_KIND_EXPR_FIXED lambda argument must have "
                         "empty type modifiers")));

  // Templated arguments cannot have empty type modifiers either (must be
  // std::nullopt).
  FunctionArgumentType templated_arg_with_empty_modifiers(
      ARG_KIND_EXPR_ANY_1, FunctionArgumentTypeOptions(), /*num_occurrences=*/1,
      TypeModifiers());
  lambda_arg_type = FunctionArgumentType::Lambda(
      {templated_arg_with_empty_modifiers}, ARG_KIND_EXPR_ANY_2);
  EXPECT_THAT(
      lambda_arg_type.IsValid(ProductMode::PRODUCT_EXTERNAL),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("Non ARG_KIND_EXPR_FIXED lambda argument cannot "
                         "have type modifiers")));

  lambda_arg_type = FunctionArgumentType::Lambda(
      {ARG_KIND_EXPR_ANY_2}, templated_arg_with_empty_modifiers);
  EXPECT_THAT(
      lambda_arg_type.IsValid(ProductMode::PRODUCT_EXTERNAL),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("Non ARG_KIND_EXPR_FIXED lambda argument cannot "
                         "have type modifiers")));
}

TEST(FunctionSignatureTests, FunctionSignatureWithDefaultValues) {
  TypeFactory factory;

  FunctionArgumentTypeList arguments;
  std::unique_ptr<FunctionSignature> signature;

  FunctionArgumentTypeOptions default_value_options_1 =
      FunctionArgumentTypeOptions(FunctionEnums::OPTIONAL)
          .set_default(values::String("default_value1"));
  FunctionArgumentTypeOptions default_value_options_2 =
      FunctionArgumentTypeOptions(FunctionEnums::OPTIONAL)
          .set_default(values::String("default_value2"));
  // No arg with default.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(factory.get_string()));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType(factory.get_string()), arguments, /*context_id=*/-1);
  GOOGLESQL_EXPECT_OK(signature->init_status());
  EXPECT_EQ(signature->last_arg_index_with_default(), -1);

  // One arg with default.
  arguments.clear();
  arguments.push_back(
      FunctionArgumentType(factory.get_string(), default_value_options_1));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType(factory.get_string()), arguments, /*context_id=*/-1);
  GOOGLESQL_EXPECT_OK(signature->init_status());
  EXPECT_EQ(signature->last_arg_index_with_default(), 0);

  // Two args with default.
  arguments.clear();
  arguments.push_back(
      FunctionArgumentType(factory.get_string(), default_value_options_1));
  arguments.push_back(
      FunctionArgumentType(factory.get_string(), default_value_options_2));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType(factory.get_string()), arguments, /*context_id=*/-1);
  GOOGLESQL_EXPECT_OK(signature->init_status());
  EXPECT_EQ(signature->last_arg_index_with_default(), 1);

  // One arg without default and one with default.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(factory.get_string()));
  arguments.push_back(
      FunctionArgumentType(factory.get_string(), default_value_options_1));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType(factory.get_string()), arguments, /*context_id=*/-1);
  GOOGLESQL_EXPECT_OK(signature->init_status());
  EXPECT_EQ(signature->last_arg_index_with_default(), 1);

  // Three args, last of which has default.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(factory.get_string()));
  arguments.push_back(FunctionArgumentType(factory.get_string()));
  arguments.push_back(
      FunctionArgumentType(factory.get_string(), default_value_options_1));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType(factory.get_string()), arguments, /*context_id=*/-1);
  GOOGLESQL_EXPECT_OK(signature->init_status());
  EXPECT_EQ(signature->last_arg_index_with_default(), 2);
}

TEST(FunctionSignatureTests, LambdaFunctionArgumentTypeAttributesTests) {
  TypeFactory factory;
  FunctionArgumentType lambda_zero_args =
      FunctionArgumentType::Lambda({}, ARG_KIND_EXPR_ANY_1);
  ASSERT_TRUE(lambda_zero_args.IsLambda());
  ASSERT_EQ(ARG_KIND_LAMBDA, lambda_zero_args.kind());
  ASSERT_FALSE(lambda_zero_args.IsConcrete());
  ASSERT_TRUE(lambda_zero_args.IsTemplated());
  ASSERT_FALSE(lambda_zero_args.repeated());
  ASSERT_THAT(lambda_zero_args.type(), IsNull());
  ASSERT_TRUE(lambda_zero_args.lambda().argument_types().empty());

  // Single function-type argument argument types
  FunctionArgumentType lambda_any_type =
      FunctionArgumentType::Lambda({ARG_KIND_EXPR_ANY_1}, ARG_KIND_EXPR_ANY_2);
  ASSERT_TRUE(lambda_any_type.IsLambda());
  ASSERT_EQ(ARG_KIND_LAMBDA, lambda_any_type.kind());
  ASSERT_FALSE(lambda_any_type.IsConcrete());
  ASSERT_TRUE(lambda_any_type.IsTemplated());
  ASSERT_FALSE(lambda_any_type.repeated());
  ASSERT_THAT(lambda_any_type.type(), IsNull());

  FunctionArgumentType lambda_array_any_type = FunctionArgumentType::Lambda(
      {ARG_KIND_EXPR_ARRAY_ANY_1}, ARG_KIND_EXPR_ARRAY_ANY_2);
  ASSERT_TRUE(lambda_array_any_type.IsLambda());
  ASSERT_EQ(ARG_KIND_LAMBDA, lambda_array_any_type.kind());
  ASSERT_FALSE(lambda_array_any_type.IsConcrete());
  ASSERT_TRUE(lambda_array_any_type.IsTemplated());
  ASSERT_FALSE(lambda_array_any_type.repeated());
  ASSERT_THAT(lambda_array_any_type.type(), IsNull());

  FunctionArgumentType lambda_non_templated_body_type =
      FunctionArgumentType::Lambda({ARG_KIND_EXPR_ANY_1}, factory.get_bool());
  ASSERT_TRUE(lambda_non_templated_body_type.IsLambda());
  ASSERT_EQ(ARG_KIND_LAMBDA, lambda_non_templated_body_type.kind());
  ASSERT_FALSE(lambda_non_templated_body_type.IsConcrete());
  ASSERT_TRUE(lambda_non_templated_body_type.IsTemplated());
  ASSERT_FALSE(lambda_non_templated_body_type.repeated());
  ASSERT_THAT(lambda_non_templated_body_type.type(), IsNull());

  FunctionArgumentType lambda_non_templated_arg_type =
      FunctionArgumentType::Lambda({factory.get_int64()}, ARG_KIND_EXPR_ANY_1);
  ASSERT_TRUE(lambda_non_templated_arg_type.IsLambda());
  ASSERT_EQ(ARG_KIND_LAMBDA, lambda_non_templated_arg_type.kind());
  ASSERT_FALSE(lambda_non_templated_arg_type.IsConcrete());
  ASSERT_TRUE(lambda_non_templated_arg_type.IsTemplated());
  ASSERT_FALSE(lambda_non_templated_arg_type.repeated());
  ASSERT_THAT(lambda_non_templated_arg_type.type(), IsNull());

  FunctionArgumentType lambda_non_templated_arg_body_type =
      FunctionArgumentType::Lambda({factory.get_int64()}, factory.get_bool());
  ASSERT_TRUE(lambda_non_templated_arg_body_type.IsLambda());
  ASSERT_EQ(ARG_KIND_LAMBDA, lambda_non_templated_arg_body_type.kind());
  ASSERT_FALSE(lambda_non_templated_arg_body_type.IsConcrete());
  ASSERT_FALSE(lambda_non_templated_arg_body_type.IsTemplated());
  ASSERT_FALSE(lambda_non_templated_arg_body_type.repeated());
  ASSERT_THAT(lambda_non_templated_arg_body_type.type(), IsNull());

  // Multiple function-type argument argument types
  FunctionArgumentType lambda_any_type_multi_args =
      FunctionArgumentType::Lambda(
          {
              ARG_KIND_EXPR_ANY_1,
              ARG_KIND_EXPR_ANY_2,
          },
          ARG_KIND_EXPR_ANY_2);
  ASSERT_TRUE(lambda_any_type_multi_args.IsLambda());
  ASSERT_EQ(ARG_KIND_LAMBDA, lambda_any_type_multi_args.kind());
  ASSERT_FALSE(lambda_any_type_multi_args.IsConcrete());
  ASSERT_TRUE(lambda_any_type_multi_args.IsTemplated());
  ASSERT_FALSE(lambda_any_type_multi_args.repeated());
  ASSERT_THAT(lambda_any_type_multi_args.type(), IsNull());

  FunctionArgumentType lambda_array_any_type_multi_args =
      FunctionArgumentType::Lambda(
          {
              ARG_KIND_EXPR_ARRAY_ANY_1,
              ARG_KIND_EXPR_ARRAY_ANY_2,
          },
          ARG_KIND_EXPR_ARRAY_ANY_2);
  ASSERT_TRUE(lambda_array_any_type_multi_args.IsLambda());
  ASSERT_EQ(ARG_KIND_LAMBDA, lambda_array_any_type_multi_args.kind());
  ASSERT_FALSE(lambda_array_any_type_multi_args.IsConcrete());
  ASSERT_TRUE(lambda_array_any_type_multi_args.IsTemplated());
  ASSERT_FALSE(lambda_array_any_type_multi_args.repeated());
  ASSERT_THAT(lambda_array_any_type_multi_args.type(), IsNull());

  FunctionArgumentType lambda_non_templated_body_type_multi_args =
      FunctionArgumentType::Lambda(
          {
              ARG_KIND_EXPR_ANY_1,
              ARG_KIND_EXPR_ANY_1,
          },
          factory.get_bool());
  ASSERT_TRUE(lambda_non_templated_body_type_multi_args.IsLambda());
  ASSERT_EQ(ARG_KIND_LAMBDA, lambda_non_templated_body_type_multi_args.kind());
  ASSERT_FALSE(lambda_non_templated_body_type_multi_args.IsConcrete());
  ASSERT_TRUE(lambda_non_templated_body_type_multi_args.IsTemplated());
  ASSERT_FALSE(lambda_non_templated_body_type_multi_args.repeated());
  ASSERT_THAT(lambda_non_templated_body_type_multi_args.type(), IsNull());

  FunctionArgumentType lambda_non_templated_arg_type_multi_args =
      FunctionArgumentType::Lambda(
          {
              factory.get_bool(),
              factory.get_int64(),
          },
          ARG_KIND_EXPR_ANY_1);
  ASSERT_TRUE(lambda_non_templated_arg_type_multi_args.IsLambda());
  ASSERT_EQ(ARG_KIND_LAMBDA, lambda_non_templated_arg_type_multi_args.kind());
  ASSERT_FALSE(lambda_non_templated_arg_type_multi_args.IsConcrete());
  ASSERT_TRUE(lambda_non_templated_arg_type_multi_args.IsTemplated());
  ASSERT_FALSE(lambda_non_templated_arg_type_multi_args.repeated());
  ASSERT_THAT(lambda_non_templated_arg_type_multi_args.type(), IsNull());

  FunctionArgumentType lambda_non_templated_arg_body_type_multi_args =
      FunctionArgumentType::Lambda(
          {
              factory.get_string(),
              factory.get_int64(),
          },
          factory.get_bool());
  ASSERT_TRUE(lambda_non_templated_arg_body_type_multi_args.IsLambda());
  ASSERT_EQ(ARG_KIND_LAMBDA,
            lambda_non_templated_arg_body_type_multi_args.kind());
  ASSERT_FALSE(lambda_non_templated_arg_body_type_multi_args.IsConcrete());
  ASSERT_FALSE(lambda_non_templated_arg_body_type_multi_args.IsTemplated());
  ASSERT_FALSE(lambda_non_templated_arg_body_type_multi_args.repeated());
  ASSERT_THAT(lambda_non_templated_arg_body_type_multi_args.type(), IsNull());
}

TEST(FunctionSignatureTests, LambdaFunctionArgumentTypeConcreteArgsTests) {
  // After resolving, function-type arguments are concrete.
  FunctionArgumentType lambda_concrete_arg_body_type_multi_args =
      FunctionArgumentType::Lambda(
          {
              FunctionArgumentType(types::StringType(), 1),
              FunctionArgumentType(types::Int64Type(), 1),
          },
          FunctionArgumentType(types::BoolType(), 1));
  ASSERT_TRUE(lambda_concrete_arg_body_type_multi_args.IsLambda());
  ASSERT_EQ(ARG_KIND_LAMBDA, lambda_concrete_arg_body_type_multi_args.kind());
  ASSERT_TRUE(lambda_concrete_arg_body_type_multi_args.IsConcrete());
  ASSERT_FALSE(lambda_concrete_arg_body_type_multi_args.repeated());
  ASSERT_THAT(lambda_concrete_arg_body_type_multi_args.type(), IsNull());
}

TEST(FunctionSignatureTests, LambdaFunctionWithOptionsTests) {
  FunctionArgumentTypeOptions options;
  options.set_argument_name("my_lambda", kPositionalOnly);
  FunctionArgumentType lambda_with_options = FunctionArgumentType::Lambda(
      {FunctionArgumentType(types::Int64Type(), 1)},
      FunctionArgumentType(types::Int64Type(), 1), options);
  ASSERT_TRUE(lambda_with_options.IsLambda());
  ASSERT_EQ(ARG_KIND_LAMBDA, lambda_with_options.kind());
  ASSERT_TRUE(lambda_with_options.options().has_argument_name());
  ASSERT_EQ(lambda_with_options.options().argument_name(), "my_lambda");
}

TEST(FunctionSignatureTests, LambdaWithoutOptionsSharesSimpleOptions) {
  FunctionArgumentType lambda =
      FunctionArgumentType::Lambda({FunctionArgumentType(types::Int64Type())},
                                   FunctionArgumentType(types::BoolType()));
  // FunctionArgumentType(const Type*) uses the SimpleOptions(REQUIRED)
  // singleton, so the lambda must point at the very same instance.
  FunctionArgumentType required_int64(types::Int64Type());
  EXPECT_EQ(&lambda.options(), &required_int64.options());
  EXPECT_EQ(lambda.cardinality(), FunctionArgumentType::REQUIRED);
  EXPECT_EQ(lambda.num_occurrences(), 1);
}

// Utility to test function argument type equality.
absl::Status TestFunctionArgumentTypeEq(const FunctionArgumentType& arg1,
                                        const FunctionArgumentType& arg2) {
  GOOGLESQL_RET_CHECK_EQ(arg1.kind(), arg2.kind());
  GOOGLESQL_RET_CHECK_EQ(arg1.type(), arg2.type());
  GOOGLESQL_RET_CHECK_EQ(arg1.num_occurrences(), arg2.num_occurrences());
  return absl::OkStatus();
}

void TestLambdaSerialization(const FunctionArgumentType lambda_type,
                             TypeFactory* type_factory) {
  FileDescriptorSetMap fdset_map;
  FunctionArgumentTypeProto proto;
  GOOGLESQL_ASSERT_OK(lambda_type.Serialize(&fdset_map, &proto));
  ASSERT_TRUE(fdset_map.empty());
  std::vector<const google::protobuf::DescriptorPool*> pools;
  std::unique_ptr<FunctionArgumentType> deserialized_type =
      FunctionArgumentType::Deserialize(proto,
                                        TypeDeserializer(type_factory, pools))
          .value();
  ASSERT_TRUE(deserialized_type->IsLambda());

  const auto& original_lambda = lambda_type.lambda();
  const auto& deserialized_lambda = lambda_type.lambda();
  ASSERT_EQ(original_lambda.argument_types().size(),
            deserialized_lambda.argument_types().size());
  for (int i = 0; i < original_lambda.argument_types().size(); i++) {
    GOOGLESQL_ASSERT_OK(
        TestFunctionArgumentTypeEq(original_lambda.argument_types()[i],
                                   deserialized_lambda.argument_types()[i]))
        << "Function-type argument type index " << i
        << " not the same after deserialization. Original function argument "
           "type: "
        << lambda_type.DebugString(/*verbose=*/true)
        << " deserialized function argument type: "
        << deserialized_type->DebugString(/*verbose=*/true);
  }
  GOOGLESQL_ASSERT_OK(TestFunctionArgumentTypeEq(original_lambda.body_type(),
                                       deserialized_lambda.body_type()))
      << "Function-type argument return type not the same after "
         "deserialization. Original function-type argument type: "
      << lambda_type.DebugString(/*verbose=*/true)
      << " deserialized function argument type: "
      << deserialized_type->DebugString(/*verbose=*/true);
}

TEST(FunctionSignatureTests, LambdaFunctionArgumentTypeSerializationTest) {
  TypeFactory type_factory;

  // All type are concrete type.
  TestLambdaSerialization(
      FunctionArgumentType::Lambda(
          {
              FunctionArgumentType(type_factory.get_string(), 1),
              FunctionArgumentType(type_factory.get_int64(), 1),
          },
          FunctionArgumentType(types::BoolType(), 1)),
      &type_factory);

  // Templated arg type.
  TestLambdaSerialization(
      FunctionArgumentType::Lambda(
          {
              FunctionArgumentType(ARG_KIND_EXPR_ANY_1),
              FunctionArgumentType(type_factory.get_int64()),
          },
          FunctionArgumentType(types::BoolType())),
      &type_factory);

  // Templated body type.
  TestLambdaSerialization(
      FunctionArgumentType::Lambda(
          {
              FunctionArgumentType(type_factory.get_string()),
              FunctionArgumentType(type_factory.get_int64()),
          },
          FunctionArgumentType(ARG_KIND_EXPR_ANY_1)),
      &type_factory);

  // Templated argument type and body type.
  TestLambdaSerialization(FunctionArgumentType::Lambda(
                              {
                                  FunctionArgumentType(ARG_KIND_EXPR_ANY_1),
                              },
                              FunctionArgumentType(ARG_KIND_EXPR_ANY_2)),
                          &type_factory);
}

TEST(FunctionSignatureTests,
     DeclarativeTypesAreDeduplicatedAcrossTheSignatureWhenDeserializing) {
  TypeFactory factory1;
  const Type* declarative_type;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      declarative_type,
      factory1.MakeDeclarativeType(DeclarativeTypeDescriptor()
                                       .set_type_id({"N1", "mytype"})
                                       .set_display_name("MyType")
                                       .set_backing_type(types::Int64Type())));

  FunctionArgumentTypeList arguments;
  arguments.push_back(FunctionArgumentType(declarative_type, 1));
  arguments.push_back(FunctionArgumentType(declarative_type, 1));

  FunctionSignature signature(/*result_type=*/types::BoolType(), arguments,
                              /*context_id=*/-1);

  FileDescriptorSetMap file_descriptor_set_map;
  FunctionSignatureProto proto;
  GOOGLESQL_ASSERT_OK(signature.Serialize(&file_descriptor_set_map, &proto));

  TypeFactory factory2;
  TypeDeserializer type_deserializer(&factory2);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<FunctionSignature> deserialized_signature,
      FunctionSignature::Deserialize(proto, type_deserializer));

  ASSERT_EQ(deserialized_signature->arguments().size(), 2);
  const Type* type1 = deserialized_signature->argument(0).type();
  const Type* type2 = deserialized_signature->argument(1).type();

  // Ensure the type was deduplicated.
  ASSERT_NE(type1, nullptr);
  ASSERT_NE(type2, nullptr);
  EXPECT_EQ(type1, type2);
  EXPECT_TRUE(type1->IsDeclarativeType());
  EXPECT_EQ(type1->AsDeclarativeType()->ShortTypeName(PRODUCT_INTERNAL),
            "MyType");
}

// The following helpers generate GoogleSQL function signatures for testing.
//
// Model a nullary function such as NOW()
static FunctionSignature GetNullaryFunction(TypeFactory* factory) {
  FunctionArgumentTypeList arguments;
  FunctionSignature nullary_function(
      FunctionArgumentType(factory->get_timestamp()), arguments, nullptr);
  return nullary_function;
}

// Model simple operator like '+'
static FunctionSignature GetAddFunction(TypeFactory* factory) {
  FunctionArgumentTypeList arguments;
  arguments.push_back(FunctionArgumentType(factory->get_int64()));
  arguments.push_back(FunctionArgumentType(factory->get_int64()));
  FunctionSignature add_function(FunctionArgumentType(factory->get_int64()),
                                 arguments, nullptr);
  return add_function;
}

// Model simple operator '+' for float32.
static FunctionSignature GetAddFunctionForFloat32(TypeFactory* factory) {
  FunctionArgumentTypeList arguments;
  arguments.push_back(FunctionArgumentType(factory->get_float()));
  arguments.push_back(FunctionArgumentType(factory->get_float()));
  FunctionSignature add_function(FunctionArgumentType(factory->get_float()),
                                 arguments, nullptr);
  return add_function;
}

// Model functions with function-type arguments like ARRAY_FILTER.
static FunctionSignature GetArrayFilterFunction(TypeFactory* factory) {
  FunctionArgumentTypeList arguments;
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ARRAY_ANY_1));
  arguments.push_back(FunctionArgumentType::Lambda(
      {ARG_KIND_EXPR_ANY_1}, FunctionArgumentType(factory->get_bool())));
  FunctionSignature array_filter_function(
      FunctionArgumentType(ARG_KIND_EXPR_ARRAY_ANY_1), arguments, nullptr);
  return array_filter_function;
}

// Model signature for 'IF <bool> THEN <any> ELSE <any> END'
static FunctionSignature GetIfThenFunction(TypeFactory* factory) {
  FunctionArgumentTypeList arguments;
  arguments.push_back(FunctionArgumentType(factory->get_bool()));
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ANY_1));
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ANY_1));
  FunctionSignature if_then_else_signature(
      FunctionArgumentType(ARG_KIND_EXPR_ANY_1), arguments, nullptr);
  return if_then_else_signature;
}

// Model signature for:
// CASE WHEN <x1> THEN <y1>
//      WHEN <x2> THEN <y2> ELSE <z> END
static FunctionSignature GetCaseWhenFunction(TypeFactory* factory) {
  FunctionArgumentTypeList arguments;
  arguments.push_back(FunctionArgumentType(factory->get_bool(),
                                           FunctionArgumentType::REPEATED));
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ANY_1,
                                           FunctionArgumentType::REPEATED));
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ANY_1,
                                           FunctionArgumentType::OPTIONAL));
  FunctionSignature case_when_signature(
      FunctionArgumentType(ARG_KIND_EXPR_ANY_1), arguments, nullptr);
  return case_when_signature;
}

// Model signature for:
// CASE <w> WHEN <x1> THEN <y1>
//          WHEN <x2> THEN <y2> ... ELSE <z> END
static FunctionSignature GetCaseValueFunction(
    TypeFactory* factory, FunctionArgumentTypeList* arguments) {
  arguments->clear();
  arguments->push_back(FunctionArgumentType(ARG_KIND_EXPR_ANY_1));
  arguments->push_back(FunctionArgumentType(ARG_KIND_EXPR_ANY_1,
                                            FunctionArgumentType::REPEATED));
  arguments->push_back(FunctionArgumentType(ARG_KIND_EXPR_ANY_2,
                                            FunctionArgumentType::REPEATED));
  arguments->push_back(FunctionArgumentType(ARG_KIND_EXPR_ANY_2,
                                            FunctionArgumentType::OPTIONAL));
  FunctionSignature case_value_signature(
      FunctionArgumentType(ARG_KIND_EXPR_ANY_2), *arguments,
      /*context_id=*/-1);
  return case_value_signature;
}

// Test a function with VOID return type, and some argument options.
static FunctionSignature GetVoidFunction(TypeFactory* factory) {
  FunctionSignature void_func(
      ARG_KIND_VOID,
      {{types::BoolType()},
       {types::Int64Type(),
        FunctionArgumentTypeOptions().set_is_not_aggregate()},
       {ARG_KIND_EXPR_ANY_1,
        FunctionArgumentTypeOptions().set_must_be_non_null()}},
      /*context_id=*/-1);
  return void_func;
}

static FunctionSignature GetConstantExpressionArgumentFunction(
    TypeFactory* factory) {
  FunctionSignature constant_expression_function(
      FunctionArgumentType(ARG_KIND_VOID),
      {{ARG_KIND_EXPR_ANY_1,
        FunctionArgumentTypeOptions().set_must_be_constant_expression()}},
      /*context_id=*/-1);
  return constant_expression_function;
}

TEST(FunctionSignatureTests, FunctionSignatureTestsInternalProductMode) {
  TypeFactory factory;

  // Model a nullary function such as NOW()
  FunctionSignature nullary_function = GetNullaryFunction(&factory);
  ASSERT_FALSE(nullary_function.IsConcrete());
  EXPECT_EQ("NOW() -> TIMESTAMP", nullary_function.DebugString("NOW"));
  EXPECT_EQ("() RETURNS TIMESTAMP",
            nullary_function.GetSQLDeclaration({} /* arg_names */,
                                               ProductMode::PRODUCT_INTERNAL));

  // Model simple operator like '+'
  FunctionSignature add_function = GetAddFunction(&factory);
  ASSERT_FALSE(add_function.IsConcrete());
  EXPECT_EQ("ADD(INT64, INT64) -> INT64", add_function.DebugString("ADD"));
  EXPECT_EQ("(INT64, INT64) RETURNS INT64",
            add_function.GetSQLDeclaration({} /* arg_names */,
                                           ProductMode::PRODUCT_INTERNAL));
  EXPECT_EQ("(x INT64, INT64) RETURNS INT64",
            add_function.GetSQLDeclaration({"x"} /* arg_names */,
                                           ProductMode::PRODUCT_INTERNAL));
  EXPECT_EQ("(x INT64, y INT64) RETURNS INT64",
            add_function.GetSQLDeclaration({"x", "y"} /* arg_names */,
                                           ProductMode::PRODUCT_INTERNAL));
  EXPECT_EQ("(x INT64, y INT64) RETURNS INT64",
            add_function.GetSQLDeclaration({"x", "y", "z"} /* arg_names */,
                                           ProductMode::PRODUCT_INTERNAL));

  // Model signature for 'IF <bool> THEN <any> ELSE <any> END'
  FunctionSignature if_then_else_signature = GetIfThenFunction(&factory);
  ASSERT_FALSE(if_then_else_signature.IsConcrete());
  EXPECT_EQ("IF(BOOL, <T1>, <T1>) -> <T1>",
            if_then_else_signature.DebugString("IF"));

  // Model signature for:
  // CASE WHEN <x1> THEN <y1>
  //      WHEN <x2> THEN <y2> ELSE <z> END
  FunctionSignature case_when_signature = GetCaseWhenFunction(&factory);
  ASSERT_FALSE(case_when_signature.IsConcrete());
  EXPECT_EQ("CASE(repeated BOOL, repeated <T1>, optional <T1>) -> <T1>",
            case_when_signature.DebugString("CASE"));
  EXPECT_EQ(
      "(/*repeated*/ BOOL, /*repeated*/ <T1>, /*optional*/ <T1>) "
      "RETURNS <T1>",
      case_when_signature.GetSQLDeclaration({} /* arg_names */,
                                            ProductMode::PRODUCT_INTERNAL));

  // Model signature for:
  // CASE <w> WHEN <x1> THEN <y1>
  //          WHEN <x2> THEN <y2> ... ELSE <z> END
  FunctionArgumentTypeList arguments;
  FunctionSignature case_value_signature =
      GetCaseValueFunction(&factory, &arguments);
  ASSERT_FALSE(case_value_signature.IsConcrete());
  EXPECT_EQ("CASE(<T1>, repeated <T1>, repeated <T2>, optional <T2>) -> <T2>",
            case_value_signature.DebugString("CASE"));

  // Test copying a FunctionSignature, assigning a new context.
  FunctionSignature copy1(case_value_signature, 1234 /* context_id */);
  FunctionSignature copy2(case_value_signature, &arguments /* context_ptr */);
  EXPECT_EQ(case_value_signature.DebugString("abc", true),
            copy1.DebugString("abc", true));
  EXPECT_EQ(case_value_signature.DebugString("", true),
            copy2.DebugString("", true));
  EXPECT_EQ(1234, copy1.context_id());
  EXPECT_EQ(&arguments, copy2.context_ptr());

  const int const_context = 42;
  FunctionSignature copy_const_context(case_value_signature, &const_context);
  EXPECT_EQ(&const_context, copy_const_context.context_ptr());
  FunctionSignature sig_with_const_context(
      FunctionArgumentType(factory.get_int32()), arguments, &const_context);
  EXPECT_EQ(&const_context, sig_with_const_context.context_ptr());

  // Test a function with VOID return type, and some argument options.
  FunctionSignature void_func = GetVoidFunction(&factory);
  EXPECT_EQ("(BOOL, INT64, <T1>) -> <void>", void_func.DebugString());
  EXPECT_EQ("func(BOOL, INT64 {is_not_aggregate: true}, "
            "<T1> {must_be_non_null: true}) -> <void>",
            void_func.DebugString("func", true /* verbose */));
  EXPECT_EQ("(BOOL, INT64 NOT AGGREGATE, <T1> /*must_be_non_null*/)",
            void_func.GetSQLDeclaration({} /* arg_names */,
                                        ProductMode::PRODUCT_INTERNAL));
  // With argument names, including one that will require quoting.
  EXPECT_EQ("(a BOOL, b INT64 NOT AGGREGATE, `c d` <T1> /*must_be_non_null*/)",
            void_func.GetSQLDeclaration({"a", "b", "c d"},
                                        ProductMode::PRODUCT_INTERNAL));

  // Test constant_expression declaration.
  FunctionSignature constant_expression_func =
      GetConstantExpressionArgumentFunction(&factory);
  EXPECT_EQ("(<T1>) -> <void>", constant_expression_func.DebugString());
  EXPECT_EQ("func(<T1> {must_be_constant_expression: true}) -> <void>",
            constant_expression_func.DebugString("func", true));

  // Test DebugString() for a signature with a deprecation warning.
  FreestandingDeprecationWarning warning;
  warning.set_message("foo is deprecated");
  warning.mutable_deprecation_warning()->set_kind(
      DeprecationWarning::PROTO3_FIELD_PRESENCE);
  ErrorLocation* location = warning.mutable_error_location();
  location->set_line(10);
  location->set_column(50);

  FunctionSignature func_with_deprecation_warning =
      GetNullaryFunction(&factory);
  func_with_deprecation_warning.SetAdditionalDeprecationWarnings({warning});
  EXPECT_EQ("() -> TIMESTAMP",
            func_with_deprecation_warning.DebugString(/*function_name=*/"",
                                                      /*verbose=*/false));
  EXPECT_EQ("() -> TIMESTAMP (1 deprecation warning)",
            func_with_deprecation_warning.DebugString(/*function_name=*/"",
                                                      /*verbose=*/true));

  // Model array function like ARRAY_FILTER
  FunctionSignature array_filter_function = GetArrayFilterFunction(&factory);
  ASSERT_FALSE(array_filter_function.IsConcrete());
  EXPECT_EQ("ARRAY_FILTER(<array<T1>>, FUNCTION<<T1>->BOOL>) -> <array<T1>>",
            array_filter_function.DebugString("ARRAY_FILTER"));
  EXPECT_EQ("(<array<T1>>, FUNCTION<<T1>->BOOL>) RETURNS <array<T1>>",
            array_filter_function.GetSQLDeclaration(
                /*argument_names=*/{}, ProductMode::PRODUCT_INTERNAL));
  EXPECT_EQ("(x <array<T1>>, FUNCTION<<T1>->BOOL>) RETURNS <array<T1>>",
            array_filter_function.GetSQLDeclaration(
                /*argument_names=*/{"x"}, ProductMode::PRODUCT_INTERNAL));
  EXPECT_EQ("(x <array<T1>>, y FUNCTION<<T1>->BOOL>) RETURNS <array<T1>>",
            array_filter_function.GetSQLDeclaration(
                /*argument_names=*/{"x", "y"}, ProductMode::PRODUCT_INTERNAL));
}

TEST(FunctionSignatureTests, FunctionSignatureTestsExternalProductMode) {
  TypeFactory factory;

  // Model a nullary function such as NOW()
  FunctionSignature nullary_function = GetNullaryFunction(&factory);
  EXPECT_EQ("() RETURNS TIMESTAMP",
            nullary_function.GetSQLDeclaration({} /* arg_names */,
                                               ProductMode::PRODUCT_EXTERNAL));

  // Model simple operator like '+'
  FunctionSignature add_function = GetAddFunction(&factory);
  EXPECT_EQ("(INT64, INT64) RETURNS INT64",
            add_function.GetSQLDeclaration({} /* arg_names */,
                                           ProductMode::PRODUCT_EXTERNAL));
  EXPECT_EQ("(x INT64, INT64) RETURNS INT64",
            add_function.GetSQLDeclaration({"x"} /* arg_names */,
                                           ProductMode::PRODUCT_EXTERNAL));
  EXPECT_EQ("(x INT64, y INT64) RETURNS INT64",
            add_function.GetSQLDeclaration({"x", "y"} /* arg_names */,
                                           ProductMode::PRODUCT_EXTERNAL));
  EXPECT_EQ("(x INT64, y INT64) RETURNS INT64",
            add_function.GetSQLDeclaration({"x", "y", "z"} /* arg_names */,
                                           ProductMode::PRODUCT_EXTERNAL));

  // Model signature for:
  // CASE WHEN <x1> THEN <y1>
  //      WHEN <x2> THEN <y2> ELSE <z> END
  FunctionSignature case_when_signature = GetCaseWhenFunction(&factory);
  EXPECT_EQ(
      "(/*repeated*/ BOOL, /*repeated*/ <T1>, /*optional*/ <T1>) "
      "RETURNS <T1>",
      case_when_signature.GetSQLDeclaration({} /* arg_names */,
                                            ProductMode::PRODUCT_EXTERNAL));

  // Test a function with VOID return type, and some argument options.
  FunctionSignature void_func = GetVoidFunction(&factory);
  EXPECT_EQ("(BOOL, INT64 NOT AGGREGATE, <T1> /*must_be_non_null*/)",
            void_func.GetSQLDeclaration({} /* arg_names */,
                                        ProductMode::PRODUCT_EXTERNAL));
  // With argument names, including one that will require quoting.
  EXPECT_EQ("(a BOOL, b INT64 NOT AGGREGATE, `c d` <T1> /*must_be_non_null*/)",
            void_func.GetSQLDeclaration({"a", "b", "c d"},
                                        ProductMode::PRODUCT_EXTERNAL));
  // Test constant_expression declaration.
  FunctionSignature constant_expression_func =
      GetConstantExpressionArgumentFunction(&factory);
  EXPECT_EQ("(const_arg <T1> /*must_be_constant_expression*/)",
            constant_expression_func.GetSQLDeclaration(
                {"const_arg"}, ProductMode::PRODUCT_EXTERNAL));

  // Model array function like ARRAY_FILTER
  FunctionSignature array_filter_function = GetArrayFilterFunction(&factory);
  ASSERT_FALSE(array_filter_function.IsConcrete());
  EXPECT_EQ("ARRAY_FILTER(<array<T1>>, FUNCTION<<T1>->BOOL>) -> <array<T1>>",
            array_filter_function.DebugString("ARRAY_FILTER"));
  EXPECT_EQ("(<array<T1>>, FUNCTION<<T1>->BOOL>) RETURNS <array<T1>>",
            array_filter_function.GetSQLDeclaration(
                /*argument_names=*/{}, ProductMode::PRODUCT_EXTERNAL));
  EXPECT_EQ("(x <array<T1>>, FUNCTION<<T1>->BOOL>) RETURNS <array<T1>>",
            array_filter_function.GetSQLDeclaration(
                /*argument_names=*/{"x"}, ProductMode::PRODUCT_EXTERNAL));
  EXPECT_EQ("(x <array<T1>>, y FUNCTION<<T1>->BOOL>) RETURNS <array<T1>>",
            array_filter_function.GetSQLDeclaration(
                /*argument_names=*/{"x", "y"}, ProductMode::PRODUCT_EXTERNAL));
}

TEST(FunctionSignatureTests, FunctionSignatureTestsFloat32) {
  TypeFactory factory;
  FunctionSignature float32_function = GetAddFunctionForFloat32(&factory);
  EXPECT_EQ("(FLOAT, FLOAT) RETURNS FLOAT",
            float32_function.GetSQLDeclaration({} /* arg_names */,
                                               ProductMode::PRODUCT_INTERNAL));
  EXPECT_EQ("(FLOAT32, FLOAT32) RETURNS FLOAT32",
            float32_function.GetSQLDeclaration({} /* arg_names */,
                                               ProductMode::PRODUCT_EXTERNAL));
  EXPECT_EQ("(FLOAT32, FLOAT32) RETURNS FLOAT32",
            float32_function.GetSQLDeclaration({} /* arg_names */,
                                               ProductMode::PRODUCT_EXTERNAL,
                                               /*use_external_float32=*/true));
}

TEST(FunctionSignatureTests, FunctionSignatureValidityTests) {
  TypeFactory factory;

  FunctionArgumentTypeList arguments;
  std::unique_ptr<FunctionSignature> signature;

  FunctionArgumentType::ArgumentCardinality REPEATED =
      FunctionArgumentType::REPEATED;
  FunctionArgumentType::ArgumentCardinality OPTIONAL =
      FunctionArgumentType::OPTIONAL;

  // Repeated result is invalid.
  EXPECT_DEBUG_DEATH(signature.reset(new FunctionSignature(
                         FunctionArgumentType(factory.get_int64(), REPEATED),
                         arguments, /*context_id=*/-1)),
                     "Result type cannot be repeated or optional");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  // Optional result is invalid.
  EXPECT_DEBUG_DEATH(signature.reset(new FunctionSignature(
                         FunctionArgumentType(factory.get_int64(), OPTIONAL),
                         arguments, /*context_id=*/-1)),
                     "Result type cannot be repeated or optional");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  // Optional argument that is not last is invalid.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ANY_1));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType(factory.get_int64()), arguments,
      /*context_id=*/-1);
  GOOGLESQL_EXPECT_OK(signature->IsValid(ProductMode::PRODUCT_EXTERNAL));
  GOOGLESQL_EXPECT_OK(signature->init_status());
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ANY_1, OPTIONAL));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType(factory.get_int64()), arguments,
      /*context_id=*/-1);
  GOOGLESQL_EXPECT_OK(signature->IsValid(ProductMode::PRODUCT_EXTERNAL));
  GOOGLESQL_EXPECT_OK(signature->init_status());
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ANY_1));
  EXPECT_DEBUG_DEATH(
      signature.reset(
          new FunctionSignature(FunctionArgumentType(factory.get_int64()),
                                arguments, /*context_id=*/-1)),
      "Optional arguments must be at the end of the argument list");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  // Repeated arguments must be consecutive.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ANY_1));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType(factory.get_int64()), arguments,
      /*context_id=*/-1);
  GOOGLESQL_EXPECT_OK(signature->IsValid(ProductMode::PRODUCT_EXTERNAL));
  GOOGLESQL_EXPECT_OK(signature->init_status());
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ANY_1, REPEATED));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType(factory.get_int64()), arguments,
      /*context_id=*/-1);
  GOOGLESQL_EXPECT_OK(signature->IsValid(ProductMode::PRODUCT_EXTERNAL));
  GOOGLESQL_EXPECT_OK(signature->init_status());
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ANY_1));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType(factory.get_int64()), arguments,
      /*context_id=*/-1);
  GOOGLESQL_EXPECT_OK(signature->IsValid(ProductMode::PRODUCT_EXTERNAL));
  GOOGLESQL_EXPECT_OK(signature->init_status());
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ANY_1, REPEATED));
  EXPECT_DEBUG_DEATH(signature.reset(new FunctionSignature(
                         FunctionArgumentType(factory.get_int64()), arguments,
                         /*context_id=*/-1)),
                     "Repeated arguments must be consecutive");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  // If there is at least one repeated argument, then the number of optional
  // arguments must be less than the number of repeated arguments.

  // 1 repeated, 1 optional
  arguments.clear();
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ANY_1, REPEATED));
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ANY_1, OPTIONAL));
  EXPECT_DEBUG_DEATH(
      signature.reset(
          new FunctionSignature(FunctionArgumentType(factory.get_int64()),
                                arguments, /*context_id=*/-1)),
      "The number of repeated arguments \\(1\\) must be greater than the "
      "number of optional arguments \\(1\\)");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  // 1 repeated, 2 optional
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ANY_1, OPTIONAL));
  EXPECT_DEBUG_DEATH(signature.reset(new FunctionSignature(
                         FunctionArgumentType(factory.get_int64()), arguments,
                         /*context_id=*/-1)),
                     "The number of repeated arguments");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  // 2 repeated, 2 optional
  arguments.clear();
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ANY_1, REPEATED));
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ANY_1, REPEATED));
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ANY_1, OPTIONAL));
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ANY_1, OPTIONAL));
  EXPECT_DEBUG_DEATH(signature.reset(new FunctionSignature(
                         FunctionArgumentType(factory.get_int64()), arguments,
                         /*context_id=*/-1)),
                     "The number of repeated arguments");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  // 2 repeated, 3 optional
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ANY_1, OPTIONAL));
  EXPECT_DEBUG_DEATH(signature.reset(new FunctionSignature(
                         FunctionArgumentType(factory.get_int64()), arguments,
                         /*context_id=*/-1)),
                     "The number of repeated arguments");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  // num_occurrences must be the same value for all repeated arguments.
  arguments.assign(
      {{ARG_KIND_EXPR_ANY_1, REPEATED, 2}, {ARG_KIND_EXPR_ANY_1, REPEATED, 1}});
  EXPECT_DEBUG_DEATH(signature.reset(new FunctionSignature(
                         FunctionArgumentType(factory.get_int64()), arguments,
                         /*context_id=*/-1)),
                     "num_occurrences");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  // Repeated relation argument is invalid.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(ARG_KIND_RELATION, REPEATED));
  EXPECT_DEBUG_DEATH(signature.reset(new FunctionSignature(
                         FunctionArgumentType(factory.get_int64()), arguments,
                         /*context_id=*/-1)),
                     "Repeated argument 0 must be Scalar");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  // Optional relation following any other optional argument is just fine.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ANY_1, OPTIONAL));
  arguments.push_back(FunctionArgumentType(ARG_KIND_RELATION, OPTIONAL));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType::AnyRelation(), arguments, /*context_id=*/-1);
  GOOGLESQL_EXPECT_OK(signature->IsValidForTableValuedFunction());
  GOOGLESQL_EXPECT_OK(signature->init_status());
  EXPECT_EQ(signature->last_named_arg_index(), -1);
  EXPECT_EQ(signature->last_arg_index_with_default(), -1);

  // Optional relation following a repeated argument is invalid.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(factory.get_int64(), REPEATED));
  arguments.push_back(FunctionArgumentType(factory.get_int64(), REPEATED));
  arguments.push_back(FunctionArgumentType(ARG_KIND_RELATION, OPTIONAL));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType::AnyRelation(), arguments, /*context_id=*/-1);

  EXPECT_THAT(signature->IsValidForTableValuedFunction(),
              StatusIs(absl::StatusCode::kInternal,
                       testing::HasSubstr("Relation arguments cannot follow "
                                          "repeated arguments")));

  // Required scalar following an optional relation is invalid.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(factory.get_int64()));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType(factory.get_int64()), arguments,
      /*context_id=*/-1);
  GOOGLESQL_EXPECT_OK(signature->IsValid(ProductMode::PRODUCT_EXTERNAL));
  GOOGLESQL_EXPECT_OK(signature->init_status());
  EXPECT_EQ(signature->last_named_arg_index(), -1);
  EXPECT_EQ(signature->last_arg_index_with_default(), -1);
  arguments.push_back(FunctionArgumentType(ARG_KIND_RELATION, OPTIONAL));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType(factory.get_int64()), arguments,
      /*context_id=*/-1);
  GOOGLESQL_EXPECT_OK(signature->IsValid(ProductMode::PRODUCT_EXTERNAL));
  GOOGLESQL_EXPECT_OK(signature->init_status());
  EXPECT_EQ(signature->last_named_arg_index(), -1);
  EXPECT_EQ(signature->last_arg_index_with_default(), -1);
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ANY_1));
  EXPECT_DEBUG_DEATH(
      signature.reset(
          new FunctionSignature(FunctionArgumentType(factory.get_int64()),
                                arguments, /*context_id=*/-1)),
      "Optional arguments must be at the end of the argument list");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  // Repeated relation argument following an optional scalar argument is
  // invalid.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(factory.get_int64(), OPTIONAL));
  arguments.push_back(FunctionArgumentType(ARG_KIND_RELATION, REPEATED));
  arguments.push_back(FunctionArgumentType(ARG_KIND_RELATION, REPEATED));
  EXPECT_DEBUG_DEATH(signature.reset(new FunctionSignature(
                         FunctionArgumentType(factory.get_int64()), arguments,
                         /*context_id=*/-1)),
                     "Repeated argument 1 must be Scalar");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  // Optional STRUCT before named param is allowed.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_STRUCT_ANY, OPTIONAL));
  arguments.push_back(
      FunctionArgumentType(ARG_KIND_EXPR_ARBITRARY,
                           FunctionArgumentTypeOptions()
                               .set_argument_name("foobar", kPositionalOrNamed)
                               .set_cardinality(OPTIONAL)));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType::AnyRelation(), arguments, /*context_id=*/-1);

  GOOGLESQL_EXPECT_OK(signature->IsValidForTableValuedFunction());
  GOOGLESQL_EXPECT_OK(signature->init_status());
  EXPECT_EQ(signature->last_named_arg_index(), 1);
  EXPECT_EQ(signature->last_arg_index_with_default(), -1);

  // Repeated STRUCT before named param is allowed.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_STRUCT_ANY, REPEATED));
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ARBITRARY, REPEATED));
  arguments.push_back(
      FunctionArgumentType(ARG_KIND_EXPR_ARBITRARY,
                           FunctionArgumentTypeOptions()
                               .set_argument_name("foobar", kPositionalOrNamed)
                               .set_cardinality(OPTIONAL)));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType::AnyRelation(), arguments, /*context_id=*/-1);

  GOOGLESQL_EXPECT_OK(signature->IsValidForTableValuedFunction());
  GOOGLESQL_EXPECT_OK(signature->init_status());
  EXPECT_EQ(signature->last_named_arg_index(), 2);
  EXPECT_EQ(signature->last_arg_index_with_default(), -1);

  // Optional RELATION before named param is allowed.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(ARG_KIND_RELATION, OPTIONAL));
  arguments.push_back(
      FunctionArgumentType(ARG_KIND_EXPR_ARBITRARY,
                           FunctionArgumentTypeOptions()
                               .set_argument_name("foobar", kPositionalOrNamed)
                               .set_cardinality(OPTIONAL)));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType::AnyRelation(), arguments, /*context_id=*/-1);

  GOOGLESQL_EXPECT_OK(signature->IsValidForTableValuedFunction());
  GOOGLESQL_EXPECT_OK(signature->init_status());
  EXPECT_EQ(signature->last_named_arg_index(), 1);
  EXPECT_EQ(signature->last_arg_index_with_default(), -1);

  // Named optional RELATION is fine if it's the only named param.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ARBITRARY, OPTIONAL));
  arguments.push_back(FunctionArgumentType(
      ARG_KIND_RELATION, FunctionArgumentTypeOptions()
                             .set_argument_name("foobar", kPositionalOrNamed)
                             .set_cardinality(OPTIONAL)));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType::AnyRelation(), arguments, /*context_id=*/-1);

  GOOGLESQL_EXPECT_OK(signature->IsValidForTableValuedFunction());
  GOOGLESQL_EXPECT_OK(signature->init_status());
  EXPECT_EQ(signature->last_named_arg_index(), 1);
  EXPECT_EQ(signature->last_arg_index_with_default(), -1);

  // Named optional RELATION after required RELATION is fine if it's the only
  // named param.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(ARG_KIND_RELATION));
  arguments.push_back(FunctionArgumentType(
      ARG_KIND_RELATION, FunctionArgumentTypeOptions()
                             .set_argument_name("foobar", kPositionalOrNamed)
                             .set_cardinality(OPTIONAL)));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType::AnyRelation(), arguments, /*context_id=*/-1);

  GOOGLESQL_EXPECT_OK(signature->IsValidForTableValuedFunction());
  GOOGLESQL_EXPECT_OK(signature->init_status());
  EXPECT_EQ(signature->last_named_arg_index(), 1);
  EXPECT_EQ(signature->last_arg_index_with_default(), -1);

  // Named optional RELATION after optional RELATION is allowed.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(ARG_KIND_RELATION, OPTIONAL));
  arguments.push_back(FunctionArgumentType(
      ARG_KIND_RELATION, FunctionArgumentTypeOptions()
                             .set_argument_name("foobar", kPositionalOrNamed)
                             .set_cardinality(OPTIONAL)));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType::AnyRelation(), arguments, /*context_id=*/-1);

  GOOGLESQL_EXPECT_OK(signature->IsValidForTableValuedFunction());
  GOOGLESQL_EXPECT_OK(signature->init_status());
  EXPECT_EQ(signature->last_named_arg_index(), 1);
  EXPECT_EQ(signature->last_arg_index_with_default(), -1);

  // Two named optional RELATIONS are allowed.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(
      ARG_KIND_RELATION, FunctionArgumentTypeOptions()
                             .set_argument_name("foobar", kPositionalOrNamed)
                             .set_cardinality(OPTIONAL)));
  arguments.push_back(FunctionArgumentType(
      ARG_KIND_RELATION, FunctionArgumentTypeOptions()
                             .set_argument_name("barfoo", kPositionalOrNamed)
                             .set_cardinality(OPTIONAL)));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType::AnyRelation(), arguments, /*context_id=*/-1);

  GOOGLESQL_EXPECT_OK(signature->IsValidForTableValuedFunction());
  GOOGLESQL_EXPECT_OK(signature->init_status());
  EXPECT_EQ(signature->last_named_arg_index(), 1);
  EXPECT_EQ(signature->last_arg_index_with_default(), -1);

  // Required Models not in the first position are fine.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ARBITRARY));
  arguments.push_back(FunctionArgumentType(ARG_KIND_MODEL));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType::AnyRelation(), arguments, /*context_id=*/-1);

  GOOGLESQL_EXPECT_OK(signature->IsValidForTableValuedFunction());
  GOOGLESQL_EXPECT_OK(signature->init_status());
  EXPECT_EQ(signature->last_named_arg_index(), -1);
  EXPECT_EQ(signature->last_arg_index_with_default(), -1);

  // Optional Models are fine, regardless of position.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ARBITRARY));
  arguments.push_back(FunctionArgumentType(ARG_KIND_MODEL, OPTIONAL));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType::AnyRelation(), arguments, /*context_id=*/-1);

  GOOGLESQL_EXPECT_OK(signature->IsValidForTableValuedFunction());
  GOOGLESQL_EXPECT_OK(signature->init_status());
  EXPECT_EQ(signature->last_named_arg_index(), -1);
  EXPECT_EQ(signature->last_arg_index_with_default(), -1);

  // Named optional RELATION is fine after MODEL.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ARBITRARY));
  arguments.push_back(FunctionArgumentType(ARG_KIND_MODEL));
  arguments.push_back(FunctionArgumentType(
      ARG_KIND_RELATION, FunctionArgumentTypeOptions()
                             .set_argument_name("foobar", kPositionalOrNamed)
                             .set_cardinality(OPTIONAL)));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType::AnyRelation(), arguments, /*context_id=*/-1);

  GOOGLESQL_EXPECT_OK(signature->IsValidForTableValuedFunction());
  GOOGLESQL_EXPECT_OK(signature->init_status());
  EXPECT_EQ(signature->last_named_arg_index(), 2);
  EXPECT_EQ(signature->last_arg_index_with_default(), -1);

  // Named optional RELATION is allowed after optional MODEL.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(ARG_KIND_MODEL, OPTIONAL));
  arguments.push_back(FunctionArgumentType(
      ARG_KIND_RELATION, FunctionArgumentTypeOptions()
                             .set_argument_name("barfoo", kPositionalOrNamed)
                             .set_cardinality(OPTIONAL)));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType::AnyRelation(), arguments, /*context_id=*/-1);

  GOOGLESQL_EXPECT_OK(signature->IsValidForTableValuedFunction());
  GOOGLESQL_EXPECT_OK(signature->init_status());
  EXPECT_EQ(signature->last_named_arg_index(), 1);
  EXPECT_EQ(signature->last_arg_index_with_default(), -1);

  // Mandatory named RELATION is invalid.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ARBITRARY, OPTIONAL));
  arguments.push_back(FunctionArgumentType(
      ARG_KIND_RELATION, FunctionArgumentTypeOptions()
                             .set_argument_name("foobar", kNamedOnly)
                             .set_cardinality(OPTIONAL)));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType::AnyRelation(), arguments, /*context_id=*/-1);

  GOOGLESQL_EXPECT_OK(signature->IsValidForTableValuedFunction());
  GOOGLESQL_EXPECT_OK(signature->init_status());
  EXPECT_EQ(signature->last_named_arg_index(), 1);
  EXPECT_EQ(signature->last_arg_index_with_default(), -1);

  arguments.clear();
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ARRAY_ANY_1));
  arguments.push_back(
      FunctionArgumentType::Lambda({ARG_KIND_EXPR_ANY_1}, factory.get_bool()));
  // Templated function-type related to arguments.
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType(ARG_KIND_EXPR_ANY_1), arguments,
      /*context_id=*/-1);

  // Templated function-type not related.
  EXPECT_DEBUG_DEATH(
      signature.reset(
          new FunctionSignature(FunctionArgumentType(ARG_KIND_EXPR_ANY_2),
                                arguments, /*context_id=*/-1)),
      "Result type template must match an argument type template");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  // Templated argument of function-type not related to previous arguments.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(ARG_KIND_EXPR_ARRAY_ANY_1));
  arguments.push_back(
      FunctionArgumentType::Lambda({ARG_KIND_EXPR_ANY_2}, factory.get_bool()));
  EXPECT_DEBUG_DEATH(
      signature.reset(new FunctionSignature(
          FunctionArgumentType(ARG_KIND_EXPR_ARRAY_ANY_1), arguments,
          /*context_id=*/-1)),
      "Templated argument of function-type argument type must match an "
      "argument type before the function-type argument");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  // An invalid signature like
  //   fn(optional a int32 default 1, optional b string).
  arguments.clear();
  arguments.emplace_back(factory.get_int32(),
                         FunctionArgumentTypeOptions(FunctionEnums::OPTIONAL)
                             .set_argument_name("a", kPositionalOrNamed)
                             .set_default(values::Int32(1)),
                         /*num_occurrences=*/1);
  arguments.emplace_back(factory.get_string(),
                         FunctionArgumentTypeOptions(FunctionEnums::OPTIONAL)
                             .set_argument_name("b", kPositionalOrNamed),
                         /*num_occurrences=*/1);
  EXPECT_DEBUG_DEATH(signature.reset(new FunctionSignature(
                         FunctionArgumentType(factory.get_int64()), arguments,
                         /*context_id=*/-1)),
                     "Optional arguments with default values must be at the "
                     "end of the argument list");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  // An invalid signature like
  //   fn(a int32, a string).
  arguments.clear();
  arguments.emplace_back(factory.get_int32(),
                         FunctionArgumentTypeOptions(FunctionEnums::REQUIRED)
                             .set_argument_name("a", kPositionalOrNamed),
                         /*num_occurrences=*/1);
  arguments.emplace_back(factory.get_int32(),
                         FunctionArgumentTypeOptions(FunctionEnums::REQUIRED)
                             .set_argument_name("a", kPositionalOrNamed),
                         /*num_occurrences=*/1);
  EXPECT_DEBUG_DEATH(signature.reset(new FunctionSignature(
                         FunctionArgumentType(factory.get_int64()), arguments,
                         /*context_id=*/-1)),
                     "Duplicate named argument a found in signature");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_TRUE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  // VOID argument is invalid.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(ARG_KIND_VOID));
  EXPECT_DEBUG_DEATH(signature.reset(new FunctionSignature(
                         FunctionArgumentType(factory.get_int64()), arguments,
                         /*context_id=*/-1)),
                     "Arguments cannot have type VOID");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_THAT(signature->IsValid(ProductMode::PRODUCT_EXTERNAL),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Arguments cannot have type VOID")));
    EXPECT_FALSE(signature->init_status().ok());
  }
}

TEST(FunctionSignatureTests, FunctionSignatureLambdaValidityTests) {
  TypeFactory factory;

  FunctionArgumentTypeList arguments;
  std::unique_ptr<FunctionSignature> signature;

  arguments.emplace_back(ARG_KIND_EXPR_ARRAY_ANY_1);
  // Not supported arg type for function-type arguments.
  arguments.emplace_back(FunctionArgumentType::Lambda({ARG_KIND_EXPR_ARBITRARY},
                                                      factory.get_bool()));
  EXPECT_DEBUG_DEATH(signature.reset(new FunctionSignature(
                         FunctionArgumentType(factory.get_int64()), arguments,
                         /*context_id=*/-1)),
                     "Argument kind not supported");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  arguments.clear();
  arguments.emplace_back(ARG_KIND_EXPR_ARRAY_ANY_1);
  // Not supported arg type for function-type body.
  arguments.emplace_back(FunctionArgumentType::Lambda({factory.get_bool()},
                                                      ARG_KIND_EXPR_ARBITRARY));
  EXPECT_DEBUG_DEATH(signature.reset(new FunctionSignature(
                         FunctionArgumentType(factory.get_int64()), arguments,
                         /*context_id=*/-1)),
                     "Argument kind not supported");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  // ARG_KIND_EXPR_ARRAY_ANY_1 not supported as function-type argument.
  arguments.clear();
  arguments.emplace_back(ARG_KIND_EXPR_ARRAY_ANY_1);
  // Not supported REPEATED options for function-type argument return type.
  arguments.emplace_back(FunctionArgumentType::Lambda(
      {ARG_KIND_EXPR_ARRAY_ANY_1}, ARG_KIND_EXPR_ANY_1));
  EXPECT_DEBUG_DEATH(signature.reset(new FunctionSignature(
                         FunctionArgumentType(factory.get_int64()), arguments,
                         /*context_id=*/-1)),
                     "Argument kind not supported by function-type argument");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  // ARG_KIND_EXPR_ARRAY_ANY_2 not supported as function-type argument.
  arguments.clear();
  arguments.emplace_back(ARG_KIND_EXPR_ARRAY_ANY_1);
  // Not supported REPEATED options for function-type body.
  arguments.emplace_back(FunctionArgumentType::Lambda(
      {ARG_KIND_EXPR_ARRAY_ANY_2}, ARG_KIND_EXPR_ANY_1));
  EXPECT_DEBUG_DEATH(signature.reset(new FunctionSignature(
                         FunctionArgumentType(factory.get_int64()), arguments,
                         /*context_id=*/-1)),
                     "Argument kind not supported by function-type argument");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  FunctionArgumentType::ArgumentCardinality REPEATED =
      FunctionArgumentType::REPEATED;
  FunctionArgumentType::ArgumentCardinality OPTIONAL =
      FunctionArgumentType::OPTIONAL;

  arguments.clear();
  arguments.emplace_back(ARG_KIND_EXPR_ARRAY_ANY_1);
  // Not supported REPEATED options for function-type arguments.
  arguments.emplace_back(FunctionArgumentType::Lambda(
      {FunctionArgumentType(ARG_KIND_EXPR_ANY_1, REPEATED)},
      factory.get_bool()));
  EXPECT_DEBUG_DEATH(
      signature.reset(new FunctionSignature(
          FunctionArgumentType(factory.get_int64()), arguments,
          /*context_id=*/-1)),
      "Only REQUIRED simple options are supported by function-type argument");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  arguments.clear();
  arguments.emplace_back(ARG_KIND_EXPR_ARRAY_ANY_1);
  // Not supported OPTIONAL options for function-type arguments.
  arguments.emplace_back(FunctionArgumentType::Lambda(
      {FunctionArgumentType(ARG_KIND_EXPR_ANY_1, OPTIONAL)},
      factory.get_bool()));
  EXPECT_DEBUG_DEATH(
      signature.reset(new FunctionSignature(
          FunctionArgumentType(factory.get_int64()), arguments,
          /*context_id=*/-1)),
      "Only REQUIRED simple options are supported by function-type arguments");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  arguments.clear();
  arguments.emplace_back(ARG_KIND_EXPR_ARRAY_ANY_1);
  // Not supported REPEATED options for function-type body.
  arguments.emplace_back(FunctionArgumentType::Lambda(
      {FunctionArgumentType(factory.get_bool())},
      FunctionArgumentType(ARG_KIND_EXPR_ANY_1, REPEATED)));
  EXPECT_DEBUG_DEATH(
      signature.reset(new FunctionSignature(
          FunctionArgumentType(factory.get_int64()), arguments,
          /*context_id=*/-1)),
      "Only REQUIRED simple options are supported by function-type arguments");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  EXPECT_DEBUG_DEATH(
      signature.reset(
          new FunctionSignature(ARG_KIND_EXPR_ANY_1,
                                {ARG_KIND_EXPR_ANY_1,
                                 FunctionArgumentType::Lambda(
                                     {ARG_KIND_EXPR_ANY_1}, factory.get_bool()),
                                 ARG_KIND_EXPR_ANY_1},
                                /*context_id=*/-1)),
      "Templated argument kind used by function-type argument cannot be used "
      "by "
      "arguments to the right of the function-type using it");
}

TEST(FunctionArgumentTypeTests, TestTemplatedKindIsRelated) {
  TypeFactory type_factory;
  FunctionArgumentType arg_type_fixed(type_factory.get_int32());
  FunctionArgumentType arg_type_any_1(ARG_KIND_EXPR_ANY_1);
  FunctionArgumentType arg_type_any_2(ARG_KIND_EXPR_ANY_2);
  FunctionArgumentType arg_array_type_any_1(ARG_KIND_EXPR_ARRAY_ANY_1);
  FunctionArgumentType arg_array_type_any_2(ARG_KIND_EXPR_ARRAY_ANY_2);
  FunctionArgumentType arg_proto_any(ARG_KIND_EXPR_PROTO_ANY);
  FunctionArgumentType arg_struct_any(ARG_KIND_EXPR_STRUCT_ANY);
  FunctionArgumentType arg_enum_any(ARG_KIND_EXPR_ENUM_ANY);
  FunctionArgumentType arg_type_any_1_lambda =
      FunctionArgumentType::Lambda({arg_type_any_1}, arg_type_any_1);
  FunctionArgumentType arg_type_any_2_lambda =
      FunctionArgumentType::Lambda({arg_type_any_2}, arg_type_any_2);
  FunctionArgumentType arg_array_type_any_2_lambda =
      FunctionArgumentType::Lambda({type_factory.get_int64()},
                                   ARG_KIND_EXPR_ARRAY_ANY_2);
  FunctionArgumentType arg_type_arbitrary(ARG_KIND_EXPR_ARBITRARY);

  EXPECT_FALSE(
      arg_type_arbitrary.TemplatedKindIsRelated(ARG_KIND_EXPR_ARBITRARY));

  EXPECT_FALSE(arg_type_fixed.TemplatedKindIsRelated(ARG_KIND_EXPR_FIXED));
  EXPECT_FALSE(arg_type_fixed.TemplatedKindIsRelated(ARG_KIND_EXPR_ANY_1));
  EXPECT_FALSE(
      arg_type_fixed.TemplatedKindIsRelated(ARG_KIND_EXPR_ARRAY_ANY_1));
  EXPECT_FALSE(arg_type_fixed.TemplatedKindIsRelated(ARG_KIND_EXPR_PROTO_ANY));
  EXPECT_FALSE(arg_type_fixed.TemplatedKindIsRelated(ARG_KIND_EXPR_STRUCT_ANY));
  EXPECT_FALSE(arg_type_fixed.TemplatedKindIsRelated(ARG_KIND_EXPR_ENUM_ANY));
  EXPECT_FALSE(arg_type_fixed.TemplatedKindIsRelated(ARG_KIND_EXPR_ARBITRARY));

  EXPECT_FALSE(arg_type_any_1.TemplatedKindIsRelated(ARG_KIND_EXPR_FIXED));
  EXPECT_TRUE(arg_type_any_1.TemplatedKindIsRelated(ARG_KIND_EXPR_ANY_1));
  EXPECT_TRUE(arg_type_any_1.TemplatedKindIsRelated(ARG_KIND_EXPR_ARRAY_ANY_1));
  EXPECT_FALSE(arg_type_any_1.TemplatedKindIsRelated(ARG_KIND_EXPR_ANY_2));
  EXPECT_FALSE(
      arg_type_any_1.TemplatedKindIsRelated(ARG_KIND_EXPR_ARRAY_ANY_2));
  EXPECT_FALSE(arg_type_any_1.TemplatedKindIsRelated(ARG_KIND_EXPR_PROTO_ANY));
  EXPECT_FALSE(arg_type_any_1.TemplatedKindIsRelated(ARG_KIND_EXPR_STRUCT_ANY));
  EXPECT_FALSE(arg_type_any_1.TemplatedKindIsRelated(ARG_KIND_EXPR_ENUM_ANY));
  EXPECT_FALSE(arg_type_any_1.TemplatedKindIsRelated(ARG_KIND_EXPR_ARBITRARY));

  // arg_type_any_1_lambda is has the same behavior as arg_type_any_1
  EXPECT_FALSE(
      arg_type_any_1_lambda.TemplatedKindIsRelated(ARG_KIND_EXPR_FIXED));
  EXPECT_TRUE(
      arg_type_any_1_lambda.TemplatedKindIsRelated(ARG_KIND_EXPR_ANY_1));
  EXPECT_TRUE(
      arg_type_any_1_lambda.TemplatedKindIsRelated(ARG_KIND_EXPR_ARRAY_ANY_1));
  EXPECT_FALSE(
      arg_type_any_1_lambda.TemplatedKindIsRelated(ARG_KIND_EXPR_ANY_2));
  EXPECT_FALSE(
      arg_type_any_1_lambda.TemplatedKindIsRelated(ARG_KIND_EXPR_ARRAY_ANY_2));
  EXPECT_FALSE(
      arg_type_any_1_lambda.TemplatedKindIsRelated(ARG_KIND_EXPR_PROTO_ANY));
  EXPECT_FALSE(
      arg_type_any_1_lambda.TemplatedKindIsRelated(ARG_KIND_EXPR_STRUCT_ANY));
  EXPECT_FALSE(
      arg_type_any_1_lambda.TemplatedKindIsRelated(ARG_KIND_EXPR_ENUM_ANY));
  EXPECT_FALSE(
      arg_type_any_1_lambda.TemplatedKindIsRelated(ARG_KIND_EXPR_ARBITRARY));

  EXPECT_FALSE(
      arg_array_type_any_1.TemplatedKindIsRelated(ARG_KIND_EXPR_FIXED));
  EXPECT_TRUE(arg_array_type_any_1.TemplatedKindIsRelated(ARG_KIND_EXPR_ANY_1));
  EXPECT_TRUE(
      arg_array_type_any_1.TemplatedKindIsRelated(ARG_KIND_EXPR_ARRAY_ANY_1));
  EXPECT_FALSE(
      arg_array_type_any_1.TemplatedKindIsRelated(ARG_KIND_EXPR_ANY_2));
  EXPECT_FALSE(
      arg_array_type_any_1.TemplatedKindIsRelated(ARG_KIND_EXPR_ARRAY_ANY_2));
  EXPECT_FALSE(
      arg_array_type_any_1.TemplatedKindIsRelated(ARG_KIND_EXPR_PROTO_ANY));
  EXPECT_FALSE(
      arg_array_type_any_1.TemplatedKindIsRelated(ARG_KIND_EXPR_STRUCT_ANY));
  EXPECT_FALSE(
      arg_array_type_any_1.TemplatedKindIsRelated(ARG_KIND_EXPR_ENUM_ANY));
  EXPECT_FALSE(
      arg_array_type_any_1.TemplatedKindIsRelated(ARG_KIND_EXPR_ARBITRARY));

  EXPECT_FALSE(arg_type_any_2.TemplatedKindIsRelated(ARG_KIND_EXPR_FIXED));
  EXPECT_FALSE(arg_type_any_2.TemplatedKindIsRelated(ARG_KIND_EXPR_ANY_1));
  EXPECT_FALSE(
      arg_type_any_2.TemplatedKindIsRelated(ARG_KIND_EXPR_ARRAY_ANY_1));
  EXPECT_TRUE(arg_type_any_2.TemplatedKindIsRelated(ARG_KIND_EXPR_ANY_2));
  EXPECT_TRUE(arg_type_any_2.TemplatedKindIsRelated(ARG_KIND_EXPR_ARRAY_ANY_2));
  EXPECT_FALSE(arg_type_any_2.TemplatedKindIsRelated(ARG_KIND_EXPR_PROTO_ANY));
  EXPECT_FALSE(arg_type_any_2.TemplatedKindIsRelated(ARG_KIND_EXPR_STRUCT_ANY));
  EXPECT_FALSE(arg_type_any_2.TemplatedKindIsRelated(ARG_KIND_EXPR_ENUM_ANY));
  EXPECT_FALSE(arg_type_any_2.TemplatedKindIsRelated(ARG_KIND_EXPR_ARBITRARY));

  // arg_type_any_2_lambda is has the same behavior as arg_type_any_2
  EXPECT_FALSE(
      arg_type_any_2_lambda.TemplatedKindIsRelated(ARG_KIND_EXPR_FIXED));
  EXPECT_FALSE(
      arg_type_any_2_lambda.TemplatedKindIsRelated(ARG_KIND_EXPR_ANY_1));
  EXPECT_FALSE(
      arg_type_any_2_lambda.TemplatedKindIsRelated(ARG_KIND_EXPR_ARRAY_ANY_1));
  EXPECT_TRUE(
      arg_type_any_2_lambda.TemplatedKindIsRelated(ARG_KIND_EXPR_ANY_2));
  EXPECT_TRUE(
      arg_type_any_2_lambda.TemplatedKindIsRelated(ARG_KIND_EXPR_ARRAY_ANY_2));
  EXPECT_FALSE(
      arg_type_any_2_lambda.TemplatedKindIsRelated(ARG_KIND_EXPR_PROTO_ANY));
  EXPECT_FALSE(
      arg_type_any_2_lambda.TemplatedKindIsRelated(ARG_KIND_EXPR_STRUCT_ANY));
  EXPECT_FALSE(
      arg_type_any_2_lambda.TemplatedKindIsRelated(ARG_KIND_EXPR_ENUM_ANY));
  EXPECT_FALSE(
      arg_type_any_2_lambda.TemplatedKindIsRelated(ARG_KIND_EXPR_ARBITRARY));

  EXPECT_FALSE(
      arg_array_type_any_2.TemplatedKindIsRelated(ARG_KIND_EXPR_FIXED));
  EXPECT_FALSE(
      arg_array_type_any_2.TemplatedKindIsRelated(ARG_KIND_EXPR_ANY_1));
  EXPECT_FALSE(
      arg_array_type_any_2.TemplatedKindIsRelated(ARG_KIND_EXPR_ARRAY_ANY_1));
  EXPECT_TRUE(arg_array_type_any_2.TemplatedKindIsRelated(ARG_KIND_EXPR_ANY_2));
  EXPECT_TRUE(
      arg_array_type_any_2.TemplatedKindIsRelated(ARG_KIND_EXPR_ARRAY_ANY_2));
  EXPECT_FALSE(
      arg_array_type_any_2.TemplatedKindIsRelated(ARG_KIND_EXPR_PROTO_ANY));
  EXPECT_FALSE(
      arg_array_type_any_2.TemplatedKindIsRelated(ARG_KIND_EXPR_STRUCT_ANY));
  EXPECT_FALSE(
      arg_array_type_any_2.TemplatedKindIsRelated(ARG_KIND_EXPR_ENUM_ANY));
  EXPECT_FALSE(
      arg_array_type_any_2.TemplatedKindIsRelated(ARG_KIND_EXPR_ARBITRARY));

  // arg_array_type_any_2_lambda is has the same behavior as
  // arg_array_type_any_2
  EXPECT_FALSE(
      arg_array_type_any_2_lambda.TemplatedKindIsRelated(ARG_KIND_EXPR_FIXED));
  EXPECT_FALSE(
      arg_array_type_any_2_lambda.TemplatedKindIsRelated(ARG_KIND_EXPR_ANY_1));
  EXPECT_FALSE(arg_array_type_any_2_lambda.TemplatedKindIsRelated(
      ARG_KIND_EXPR_ARRAY_ANY_1));
  EXPECT_TRUE(
      arg_array_type_any_2_lambda.TemplatedKindIsRelated(ARG_KIND_EXPR_ANY_2));
  EXPECT_TRUE(arg_array_type_any_2_lambda.TemplatedKindIsRelated(
      ARG_KIND_EXPR_ARRAY_ANY_2));
  EXPECT_FALSE(arg_array_type_any_2_lambda.TemplatedKindIsRelated(
      ARG_KIND_EXPR_PROTO_ANY));
  EXPECT_FALSE(arg_array_type_any_2_lambda.TemplatedKindIsRelated(
      ARG_KIND_EXPR_STRUCT_ANY));
  EXPECT_FALSE(arg_array_type_any_2_lambda.TemplatedKindIsRelated(
      ARG_KIND_EXPR_ENUM_ANY));
  EXPECT_FALSE(arg_array_type_any_2_lambda.TemplatedKindIsRelated(
      ARG_KIND_EXPR_ARBITRARY));

  EXPECT_FALSE(arg_enum_any.TemplatedKindIsRelated(ARG_KIND_EXPR_FIXED));
  EXPECT_FALSE(arg_enum_any.TemplatedKindIsRelated(ARG_KIND_EXPR_ANY_1));
  EXPECT_FALSE(arg_enum_any.TemplatedKindIsRelated(ARG_KIND_EXPR_ARRAY_ANY_1));
  EXPECT_FALSE(arg_enum_any.TemplatedKindIsRelated(ARG_KIND_EXPR_ANY_2));
  EXPECT_FALSE(arg_enum_any.TemplatedKindIsRelated(ARG_KIND_EXPR_ARRAY_ANY_2));
  EXPECT_FALSE(arg_enum_any.TemplatedKindIsRelated(ARG_KIND_EXPR_PROTO_ANY));
  EXPECT_FALSE(arg_enum_any.TemplatedKindIsRelated(ARG_KIND_EXPR_STRUCT_ANY));
  EXPECT_TRUE(arg_enum_any.TemplatedKindIsRelated(ARG_KIND_EXPR_ENUM_ANY));
  EXPECT_FALSE(arg_enum_any.TemplatedKindIsRelated(ARG_KIND_EXPR_ARBITRARY));
}

static void CheckConcreteArgumentType(
    const Type* expected_type,
    const std::unique_ptr<FunctionSignature>& signature, int idx) {
  if (signature->ConcreteArgument(idx).IsLambda()) {
    ASSERT_THAT(signature->ConcreteArgumentType(idx), IsNull());
    const FunctionArgumentType::ArgumentTypeLambda& concrete_lambda =
        signature->ConcreteArgument(idx).lambda();
    for (const auto& arg : concrete_lambda.argument_types()) {
      ASSERT_THAT(arg.type(), NotNull()) << arg.DebugString();
    }
    ASSERT_THAT(concrete_lambda.body_type().type(), NotNull())
        << concrete_lambda.body_type().DebugString();
  } else {
    ASSERT_THAT(signature->ConcreteArgumentType(idx), NotNull());
    EXPECT_TRUE(signature->ConcreteArgumentType(idx)->Equals(expected_type));
  }
}

TEST(FunctionSignatureTests, TestConcreteArgumentType) {
  TypeFactory factory;

  FunctionArgumentTypeList arguments;
  std::unique_ptr<FunctionSignature> signature;

  FunctionArgumentType::ArgumentCardinality REPEATED =
      FunctionArgumentType::REPEATED;
  FunctionArgumentType::ArgumentCardinality OPTIONAL =
      FunctionArgumentType::OPTIONAL;
  FunctionArgumentType::ArgumentCardinality REQUIRED =
      FunctionArgumentType::REQUIRED;

  std::unique_ptr<FunctionArgumentType> result_type;
  result_type =
      std::make_unique<FunctionArgumentType>(types::Int64Type(), REQUIRED, 0);

  // 0 arguments.
  arguments.clear();
  signature = std::make_unique<FunctionSignature>(*result_type, arguments,
                                                  /*context_id=*/-1);
  EXPECT_EQ(0, signature->NumConcreteArguments());

  // 1 required.
  arguments.push_back(FunctionArgumentType(types::Int64Type(), REQUIRED, 1));
  signature = std::make_unique<FunctionSignature>(*result_type, arguments,
                                                  /*context_id=*/-1);
  EXPECT_EQ(1, signature->NumConcreteArguments());
  CheckConcreteArgumentType(types::Int64Type(), signature, 0);

  // 2 required.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(types::Int64Type(), REQUIRED, 1));
  arguments.push_back(FunctionArgumentType(types::Int32Type(), REQUIRED, 1));
  signature = std::make_unique<FunctionSignature>(*result_type, arguments,
                                                  /*context_id=*/-1);
  EXPECT_EQ(2, signature->NumConcreteArguments());
  CheckConcreteArgumentType(types::Int64Type(), signature, 0);
  CheckConcreteArgumentType(types::Int32Type(), signature, 1);

  // 3 required - simulates IF().
  arguments.clear();
  arguments.push_back(FunctionArgumentType(types::BoolType(), REQUIRED, 1));
  arguments.push_back(FunctionArgumentType(types::Int64Type(), REQUIRED, 1));
  arguments.push_back(FunctionArgumentType(types::Int64Type(), REQUIRED, 1));
  signature = std::make_unique<FunctionSignature>(*result_type, arguments,
                                                  /*context_id=*/-1);
  EXPECT_EQ(3, signature->NumConcreteArguments());
  CheckConcreteArgumentType(types::BoolType(), signature, 0);
  CheckConcreteArgumentType(types::Int64Type(), signature, 1);
  CheckConcreteArgumentType(types::Int64Type(), signature, 2);

  // 2 repeateds (2), 1 optional (0) -
  //   CASE WHEN . THEN . WHEN . THEN . END
  arguments.clear();
  arguments.push_back(FunctionArgumentType(types::BoolType(), REPEATED, 2));
  arguments.push_back(FunctionArgumentType(types::Int64Type(), REPEATED, 2));
  arguments.push_back(FunctionArgumentType(types::Int64Type(), OPTIONAL, 0));
  signature = std::make_unique<FunctionSignature>(*result_type, arguments,
                                                  /*context_id=*/-1);
  EXPECT_EQ(4, signature->NumConcreteArguments());
  CheckConcreteArgumentType(types::BoolType(), signature, 0);
  CheckConcreteArgumentType(types::Int64Type(), signature, 1);
  CheckConcreteArgumentType(types::BoolType(), signature, 2);
  CheckConcreteArgumentType(types::Int64Type(), signature, 3);

  // 2 repeateds (2), 1 optional (1) -
  //   CASE WHEN . THEN . WHEN . THEN . ELSE . END
  arguments.clear();
  arguments.push_back(FunctionArgumentType(types::BoolType(), REPEATED, 2));
  arguments.push_back(FunctionArgumentType(types::Int64Type(), REPEATED, 2));
  arguments.push_back(FunctionArgumentType(types::Int32Type(), OPTIONAL, 1));
  signature = std::make_unique<FunctionSignature>(*result_type, arguments,
                                                  /*context_id=*/-1);
  EXPECT_EQ(5, signature->NumConcreteArguments());
  CheckConcreteArgumentType(types::BoolType(), signature, 0);
  CheckConcreteArgumentType(types::Int64Type(), signature, 1);
  CheckConcreteArgumentType(types::BoolType(), signature, 2);
  CheckConcreteArgumentType(types::Int64Type(), signature, 3);
  CheckConcreteArgumentType(types::Int32Type(), signature, 4);

  // 2 required, 3 repeateds (2), 1 required, 2 optional (0,0) -
  arguments.clear();
  arguments.push_back(FunctionArgumentType(types::BoolType(), REQUIRED, 1));
  arguments.push_back(FunctionArgumentType(types::StringType(), REQUIRED, 1));
  arguments.push_back(FunctionArgumentType(types::Uint64Type(), REPEATED, 2));
  arguments.push_back(FunctionArgumentType(types::Int64Type(), REPEATED, 2));
  arguments.push_back(FunctionArgumentType(types::BytesType(), REPEATED, 2));
  arguments.push_back(FunctionArgumentType(types::Uint32Type(), REQUIRED, 1));
  arguments.push_back(FunctionArgumentType(types::Int32Type(), OPTIONAL, 0));
  arguments.push_back(FunctionArgumentType(types::DateType(), OPTIONAL, 0));
  signature = std::make_unique<FunctionSignature>(*result_type, arguments,
                                                  /*context_id=*/-1);
  EXPECT_EQ(9, signature->NumConcreteArguments());
  CheckConcreteArgumentType(types::BoolType(), signature, 0);
  CheckConcreteArgumentType(types::StringType(), signature, 1);
  CheckConcreteArgumentType(types::Uint64Type(), signature, 2);
  CheckConcreteArgumentType(types::Int64Type(), signature, 3);
  CheckConcreteArgumentType(types::BytesType(), signature, 4);
  CheckConcreteArgumentType(types::Uint64Type(), signature, 5);
  CheckConcreteArgumentType(types::Int64Type(), signature, 6);
  CheckConcreteArgumentType(types::BytesType(), signature, 7);
  CheckConcreteArgumentType(types::Uint32Type(), signature, 8);

  // 2 required, 3 repeateds (2), 1 required, 2 optional (1,0) -
  arguments.clear();
  arguments.push_back(FunctionArgumentType(types::BoolType(), REQUIRED, 1));
  arguments.push_back(FunctionArgumentType(types::StringType(), REQUIRED, 1));
  arguments.push_back(FunctionArgumentType(types::Uint64Type(), REPEATED, 2));
  arguments.push_back(FunctionArgumentType(types::Int64Type(), REPEATED, 2));
  arguments.push_back(FunctionArgumentType(types::BytesType(), REPEATED, 2));
  arguments.push_back(FunctionArgumentType(types::Uint32Type(), REQUIRED, 1));
  arguments.push_back(FunctionArgumentType(types::Int32Type(), OPTIONAL, 1));
  arguments.push_back(FunctionArgumentType(types::DateType(), OPTIONAL, 0));
  signature = std::make_unique<FunctionSignature>(*result_type, arguments,
                                                  /*context_id=*/-1);
  EXPECT_EQ(10, signature->NumConcreteArguments());
  CheckConcreteArgumentType(types::BoolType(), signature, 0);
  CheckConcreteArgumentType(types::StringType(), signature, 1);
  CheckConcreteArgumentType(types::Uint64Type(), signature, 2);
  CheckConcreteArgumentType(types::Int64Type(), signature, 3);
  CheckConcreteArgumentType(types::BytesType(), signature, 4);
  CheckConcreteArgumentType(types::Uint64Type(), signature, 5);
  CheckConcreteArgumentType(types::Int64Type(), signature, 6);
  CheckConcreteArgumentType(types::BytesType(), signature, 7);
  CheckConcreteArgumentType(types::Uint32Type(), signature, 8);
  CheckConcreteArgumentType(types::Int32Type(), signature, 9);

  // 2 required, 2 optional (1,0) -
  arguments.clear();
  arguments.push_back(FunctionArgumentType(types::BoolType(), REQUIRED, 1));
  arguments.push_back(FunctionArgumentType(types::StringType(), REQUIRED, 1));
  arguments.push_back(FunctionArgumentType(types::Int32Type(), OPTIONAL, 1));
  arguments.push_back(FunctionArgumentType(types::DateType(), OPTIONAL, 0));
  signature = std::make_unique<FunctionSignature>(*result_type, arguments,
                                                  /*context_id=*/-1);
  EXPECT_EQ(3, signature->NumConcreteArguments());
  CheckConcreteArgumentType(types::BoolType(), signature, 0);
  CheckConcreteArgumentType(types::StringType(), signature, 1);
  CheckConcreteArgumentType(types::Int32Type(), signature, 2);

  arguments.clear();
  arguments.push_back(
      FunctionArgumentType(types::Int64ArrayType(), REQUIRED, 1));
  arguments.push_back(FunctionArgumentType::Lambda(
      {FunctionArgumentType(types::Int64Type(), REQUIRED, 1)},
      FunctionArgumentType(types::Int64Type(), REQUIRED, 1)));
  signature = std::make_unique<FunctionSignature>(*result_type, arguments,
                                                  /*context_id=*/-1);
  EXPECT_EQ(2, signature->NumConcreteArguments());
  CheckConcreteArgumentType(types::Int64ArrayType(), signature, 0);
  // The value type of function-type is the type of the body.
  CheckConcreteArgumentType(types::Int64Type(), signature, 1);
}

static std::vector<FunctionArgumentType> GetTemplatedArgumentTypes(
    TypeFactory* factory) {
  std::vector<FunctionArgumentType> templated_types;
  templated_types.push_back(FunctionArgumentType(ARG_KIND_EXPR_ANY_1));
  templated_types.push_back(FunctionArgumentType(ARG_KIND_EXPR_ANY_2));
  templated_types.push_back(FunctionArgumentType(ARG_KIND_EXPR_ARRAY_ANY_1));
  templated_types.push_back(FunctionArgumentType(ARG_KIND_EXPR_ARRAY_ANY_2));
  templated_types.push_back(FunctionArgumentType(ARG_KIND_EXPR_PROTO_ANY));
  templated_types.push_back(FunctionArgumentType(ARG_KIND_EXPR_STRUCT_ANY));
  templated_types.push_back(FunctionArgumentType(ARG_KIND_EXPR_ENUM_ANY));
  templated_types.push_back(FunctionArgumentType(ARG_KIND_EXPR_ARBITRARY));
  templated_types.push_back(FunctionArgumentType(ARG_KIND_RELATION));
  templated_types.push_back(FunctionArgumentType(ARG_KIND_MODEL));
  templated_types.push_back(FunctionArgumentType(ARG_KIND_CONNECTION));
  templated_types.push_back(FunctionArgumentType(ARG_KIND_EXPR_STRING_ANY));
  templated_types.push_back(
      FunctionArgumentType::Lambda({ARG_KIND_EXPR_ANY_1}, ARG_KIND_EXPR_ANY_2));
  templated_types.push_back(
      FunctionArgumentType::Lambda({ARG_KIND_EXPR_ANY_1}, factory->get_bool()));
  templated_types.push_back(FunctionArgumentType::Lambda({factory->get_int64()},
                                                         ARG_KIND_EXPR_ANY_1));
  templated_types.push_back(FunctionArgumentType(ARG_KIND_EXPR_ROW_ANY));
  return templated_types;
}

static std::vector<FunctionArgumentType> GetNonTemplatedArgumentTypes(
    TypeFactory* factory) {
  std::vector<FunctionArgumentType> non_templated_types;
  non_templated_types.push_back(FunctionArgumentType(ARG_KIND_VOID));
  // A few examples of ARG_KIND_EXPR_FIXED
  non_templated_types.push_back(FunctionArgumentType(factory->get_int32()));
  non_templated_types.push_back(FunctionArgumentType(factory->get_string()));
  non_templated_types.push_back(FunctionArgumentType::Lambda(
      {factory->get_int64()}, factory->get_bool()));
  return non_templated_types;
}

TEST(FunctionSignatureTests, TestIsTemplatedArgument) {
  TypeFactory factory;
  struct TestCase {
    FunctionArgumentType arg_type;
    bool expected_is_templated;
  };
  std::vector<TestCase> tests;

  // If a new enum value is added to SignatureArgumentKind then it *must*
  // be added to <templated_kinds> or <non_templated_kinds> as appropriate.
  int enum_size = 0;
  for (int i = 0; i < SignatureArgumentKind_ARRAYSIZE; ++i) {
    // SignatureArgumentKind_ARRAYSIZE doesn't account for skipped proto field
    // values.
    enum_size += SignatureArgumentKind_IsValid(i);
  }
  ASSERT_EQ(35, enum_size);

  std::set<SignatureArgumentKind> templated_kinds;
  templated_kinds.insert(ARG_KIND_EXPR_ANY_1);
  templated_kinds.insert(ARG_KIND_EXPR_ANY_2);
  templated_kinds.insert(ARG_KIND_EXPR_ANY_3);
  templated_kinds.insert(ARG_KIND_EXPR_ANY_4);
  templated_kinds.insert(ARG_KIND_EXPR_ANY_5);
  templated_kinds.insert(ARG_KIND_EXPR_ARRAY_ANY_1);
  templated_kinds.insert(ARG_KIND_EXPR_ARRAY_ANY_2);
  templated_kinds.insert(ARG_KIND_EXPR_ARRAY_ANY_3);
  templated_kinds.insert(ARG_KIND_EXPR_ARRAY_ANY_4);
  templated_kinds.insert(ARG_KIND_EXPR_ARRAY_ANY_5);
  templated_kinds.insert(ARG_KIND_EXPR_PROTO_MAP_ANY);
  templated_kinds.insert(ARG_KIND_EXPR_PROTO_MAP_KEY_ANY);
  templated_kinds.insert(ARG_KIND_EXPR_PROTO_MAP_VALUE_ANY);
  templated_kinds.insert(ARG_KIND_EXPR_MAP_ANY_1_2);
  templated_kinds.insert(ARG_KIND_EXPR_PROTO_ANY);
  templated_kinds.insert(ARG_KIND_EXPR_STRUCT_ANY);
  templated_kinds.insert(ARG_KIND_EXPR_ENUM_ANY);
  templated_kinds.insert(ARG_KIND_EXPR_ARBITRARY);
  templated_kinds.insert(ARG_KIND_RELATION);
  templated_kinds.insert(ARG_KIND_MODEL);
  templated_kinds.insert(ARG_KIND_CONNECTION);
  templated_kinds.insert(ARG_KIND_DESCRIPTOR);
  templated_kinds.insert(ARG_KIND_LAMBDA);
  templated_kinds.insert(ARG_KIND_EXPR_RANGE_ANY_1);
  templated_kinds.insert(ARG_KIND_EXPR_GRAPH_NODE);
  templated_kinds.insert(ARG_KIND_EXPR_GRAPH_EDGE);
  templated_kinds.insert(ARG_KIND_EXPR_GRAPH_PATH);
  templated_kinds.insert(ARG_KIND_SEQUENCE);
  templated_kinds.insert(ARG_KIND_EXPR_MEASURE_ANY_1);
  templated_kinds.insert(ARG_KIND_EXPR_STRING_ANY);
  templated_kinds.insert(ARG_KIND_EXPR_ROW_ANY);

  std::set<SignatureArgumentKind> non_templated_kinds;
  non_templated_kinds.insert(ARG_KIND_EXPR_FIXED);
  non_templated_kinds.insert(ARG_KIND_VOID);
  templated_kinds.insert(ARG_KIND_GRAPH);

  for (const FunctionArgumentType& type : GetTemplatedArgumentTypes(&factory)) {
    tests.push_back({type, true});
  }

  for (const FunctionArgumentType& type :
           GetNonTemplatedArgumentTypes(&factory)) {
    tests.push_back({type, false});
  }

  // Relation type arguments that have a relation schema defined (in options)
  // are non-templated.
  TVFRelation tvf_relation({});
  FunctionArgumentType arg_type =
      FunctionArgumentType::RelationWithSchema(
          tvf_relation, /*extra_relation_input_columns_allowed=*/false);
  tests.push_back({arg_type, false});

  arg_type =
      FunctionArgumentType::RelationWithSchema(
          tvf_relation, /*extra_relation_input_columns_allowed=*/true);
  tests.push_back({arg_type, false});

  for (const auto& test : tests) {
    EXPECT_EQ(test.expected_is_templated,
              test.arg_type.IsTemplated()) << test.arg_type.DebugString();
  }
}

TEST(FunctionSignatureTests, TestIsTemplatedSignature) {
  TypeFactory factory;
  struct TestCase {
    FunctionSignature signature;
    bool expected_is_templated;
  };
  std::vector<TestCase> tests;

  FunctionArgumentTypeList arguments;
  tests.push_back({FunctionSignature(FunctionArgumentType(factory.get_int32()),
                                     arguments,
                                     /*context_ptr=*/nullptr),
                   /*expected_is_templated=*/false});

  arguments.push_back(FunctionArgumentType(factory.get_int32()));
  arguments.push_back(FunctionArgumentType(factory.get_int64()));
  arguments.push_back(FunctionArgumentType(factory.get_bytes()));
  tests.push_back({FunctionSignature(FunctionArgumentType(factory.get_string()),
                                     arguments,
                                     /*context_ptr=*/nullptr),
                   /*expected_is_templated=*/false});

  for (const FunctionArgumentType& type : GetTemplatedArgumentTypes(&factory)) {
    // A signature with a single argument of this templated type.
    tests.push_back(
        {FunctionSignature(FunctionArgumentType(factory.get_int32()),
                           {type},
                           /*context_ptr=*/nullptr),
         /*expected_is_templated=*/true});
    // A signature with templated function-type requires corresponding templated
    // argument, which negates this test.
    if (type.IsLambda()) {
      continue;
    }
    // A signature with a some fixed arguments and also this templated type.
    FunctionArgumentTypeList arguments_with_template = arguments;
    arguments_with_template.push_back(type);
    tests.push_back(
        {FunctionSignature(FunctionArgumentType(factory.get_int32()),
                           arguments_with_template,
                           /*context_ptr=*/nullptr),
         /*expected_is_templated=*/true});
  }

  tests.push_back(
      {GetArrayFilterFunction(&factory), /*expected_is_templated=*/true});
  {
    FunctionArgumentTypeList arguments;
    arguments.push_back(FunctionArgumentType(factory.get_int64()));
    arguments.push_back(FunctionArgumentType::Lambda(
        {factory.get_int64()}, FunctionArgumentType(factory.get_bool())));
    const FunctionSignature non_templated_lambda(
        FunctionArgumentType(factory.get_int64()), arguments, nullptr);
    tests.push_back({non_templated_lambda, /*expected_is_templated=*/false});
  }

  for (const auto& test : tests) {
    EXPECT_EQ(test.expected_is_templated,
              test.signature.IsTemplated()) << test.signature.DebugString();
  }
}

TEST(FunctionSignatureTests, TestIsDescriptorTableOffsetArgumentValid) {
  TypeFactory factory;
  std::unique_ptr<FunctionSignature> signature;
  TVFRelation tvf_relation({});
  FunctionArgumentType arg_type = FunctionArgumentType::RelationWithSchema(
      tvf_relation, /*extra_relation_input_columns_allowed=*/false);
  FunctionArgumentType retuneType = FunctionArgumentType(factory.get_int32());
  signature.reset(new FunctionSignature(
      retuneType, {arg_type, FunctionArgumentType::AnyDescriptor(0)}, -1));

  GOOGLESQL_EXPECT_OK(signature->IsValid(ProductMode::PRODUCT_EXTERNAL));
  GOOGLESQL_EXPECT_OK(signature->init_status());
  EXPECT_EQ(signature->last_named_arg_index(), -1);
  EXPECT_EQ(signature->last_arg_index_with_default(), -1);

  EXPECT_DEBUG_DEATH(
      signature.reset(new FunctionSignature(
          retuneType, {FunctionArgumentType::AnyDescriptor(3), arg_type}, -1)),
      "should point to a valid table argument");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  EXPECT_DEBUG_DEATH(
      signature.reset(new FunctionSignature(
          retuneType, {arg_type, FunctionArgumentType::AnyDescriptor(1)}, -1)),
      "should point to a valid table argument");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }
}

TEST(FunctionSignatureTests, FunctionSignatureOptionTests) {
  FunctionSignature signature{
      types::Int64Type(),
      {types::StringType()},
      /* context_id = */ -1,
      FunctionSignatureOptions().AddRequiredLanguageFeature(
          FEATURE_EXTENDED_TYPES)};
  EXPECT_TRUE(signature.options().CheckAllRequiredFeaturesAreEnabled(
      {FEATURE_EXTENDED_TYPES, FEATURE_CIVIL_TIME}));
  EXPECT_FALSE(signature.options().CheckAllRequiredFeaturesAreEnabled(
      {FEATURE_NUMERIC_TYPE, FEATURE_CIVIL_TIME}));
}

TEST(FunctionSignatureTests, FunctionSignatureRewriteOptionsSerialization) {
  FunctionSignatureRewriteOptions opts1, opts2;
  opts1.set_enabled(false)
      .set_rewriter(REWRITE_PIVOT)
      .set_sql("false")
      .set_allow_table_references(true)
      .set_allowed_function_groups({"test1", "test2"});
  opts2.set_enabled(true)
      .set_rewriter(REWRITE_FLATTEN)
      .set_sql("true")
      .set_allow_table_references(false)
      .set_allowed_function_groups({});
  for (const auto& opts : {opts1, opts2}) {
    FunctionSignatureRewriteOptionsProto opts_proto;
    opts.Serialize(&opts_proto);
    FunctionSignatureRewriteOptions deserialized;
    GOOGLESQL_EXPECT_OK(
        FunctionSignatureRewriteOptions::Deserialize(opts_proto, deserialized));
    EXPECT_EQ(opts.enabled(), deserialized.enabled());
    EXPECT_EQ(opts.rewriter(), deserialized.rewriter());
    EXPECT_EQ(opts.sql(), deserialized.sql());
    EXPECT_EQ(opts.allow_table_references(),
              deserialized.allow_table_references());
    EXPECT_EQ(opts.allowed_function_groups(),
              deserialized.allowed_function_groups());
  }
}

TEST(FunctionSignatureTests, TestArgumentConstraints) {
  auto noop_constraints_callback =
      [](const FunctionSignature& signature,
         absl::Span<const InputArgumentType> arguments) { return ""; };
  FunctionSignature nonconcrete_signature(
      types::Int64Type(), {{ARG_KIND_EXPR_ANY_1, /*num_occurrences=*/1}},
      /*context_id=*/-1,
      FunctionSignatureOptions().set_constraints(noop_constraints_callback));
  // Calling the argument constraint callback on a non-concrete signature should
  // result in a ABSL_DCHECK failure.
  EXPECT_THAT(nonconcrete_signature.CheckArgumentConstraints(/*arguments=*/{}),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("FunctionSignatureArgumentConstraintsCallback "
                                 "must be called with concrete arguments")));

  FunctionSignature concrete_signature(
      {types::Int64Type(), FunctionArgumentType::REQUIRED,
       /*num_occurrences=*/1},
      {{types::StringType(), FunctionArgumentType::OPTIONAL,
        /*num_occurrences=*/1}},
      /*context_id=*/-1,
      FunctionSignatureOptions().set_constraints(noop_constraints_callback));
  EXPECT_THAT(concrete_signature.CheckArgumentConstraints(
                  {InputArgumentType::UntypedNull()}),
              IsOkAndHolds(""));

  auto nonnull_constraints_callback =
      [](const FunctionSignature& signature,
         absl::Span<const InputArgumentType> arguments) -> std::string {
    if (signature.NumConcreteArguments() != arguments.size()) {
      return absl::StrCat("Expecting ", signature.NumConcreteArguments(),
                          " arguments, but got ", arguments.size());
    }
    for (int i = 0; i < arguments.size(); ++i) {
      const InputArgumentType& arg_type = arguments[i];
      if (arg_type.is_null()) {
        return absl::StrCat("Argument ", i + 1, ": NULL cannot be provided");
      }
    }
    return "";
  };

  FunctionSignature concrete_signature2(
      {types::Int64Type(), FunctionArgumentType::REQUIRED,
       /*num_occurrences=*/1},
      {{types::StringType(), FunctionArgumentType::OPTIONAL,
        /*num_occurrences=*/1}},
      /*context_id=*/-1,
      FunctionSignatureOptions().set_constraints(nonnull_constraints_callback));
  EXPECT_THAT(concrete_signature2.CheckArgumentConstraints(
                  {InputArgumentType::UntypedNull()}),
              IsOkAndHolds("Argument 1: NULL cannot be provided"));
  EXPECT_THAT(concrete_signature2.CheckArgumentConstraints(
                  {InputArgumentType{types::StringType()}}),
              IsOkAndHolds(""));

  FunctionSignature concrete_signature3(
      {types::Int64Type(), FunctionArgumentType::REQUIRED,
       /*num_occurrences=*/1},
      {{types::StringType(), FunctionArgumentType::OPTIONAL,
        /*num_occurrences=*/1},
       {types::StringType(), FunctionArgumentType::OPTIONAL,
        /*num_occurrences=*/0}},
      /*context_id=*/-1,
      FunctionSignatureOptions().set_constraints(nonnull_constraints_callback));
  EXPECT_THAT(concrete_signature3.CheckArgumentConstraints(
                  {InputArgumentType::UntypedNull()}),
              IsOkAndHolds("Argument 1: NULL cannot be provided"));
  EXPECT_THAT(concrete_signature3.CheckArgumentConstraints(
                  {InputArgumentType{types::StringType()}}),
              IsOkAndHolds(""));
}

void TestArgumentTypeOptionsSerialization(
    const FunctionArgumentType& arg_type) {
  FileDescriptorSetMap fdset_map;
  FunctionArgumentTypeProto proto;
  GOOGLESQL_EXPECT_OK(arg_type.Serialize(&fdset_map, &proto));
  TypeFactory factory;
  std::vector<const google::protobuf::DescriptorPool*> pools(fdset_map.size());
  for (const auto& pair : fdset_map) {
    pools[pair.second->descriptor_set_index] = pair.first;
  }

  std::unique_ptr<FunctionArgumentType> dummy_type =
      FunctionArgumentType::Deserialize(proto,
                                        TypeDeserializer(&factory, pools))
          .value();

  EXPECT_TRUE(dummy_type->options().must_support_ordering() ==
              arg_type.options().must_support_ordering());
  EXPECT_TRUE(dummy_type->options().must_support_equality() ==
              arg_type.options().must_support_equality());
  EXPECT_TRUE(dummy_type->options().must_support_grouping() ==
              arg_type.options().must_support_grouping());
  EXPECT_TRUE(dummy_type->options().array_element_must_support_ordering() ==
              arg_type.options().array_element_must_support_ordering());
  EXPECT_TRUE(dummy_type->options().array_element_must_support_equality() ==
              arg_type.options().array_element_must_support_equality());
  EXPECT_TRUE(dummy_type->options().array_element_must_support_grouping() ==
              arg_type.options().array_element_must_support_grouping());
  EXPECT_TRUE(dummy_type->options().supports_order_by() ==
              arg_type.options().supports_order_by());
}

TEST(FunctionSignatureTests, TestFunctionArgumentTypeOptionsConstraint) {
  FunctionSignature orderable_element_signature(
      FunctionArgumentType(ARG_KIND_EXPR_ANY_1),
      {{ARG_KIND_EXPR_ANY_1,
        FunctionArgumentTypeOptions().set_must_support_ordering()}},
      /*context_id=*/-1);

  EXPECT_TRUE(orderable_element_signature.argument(0)
                  .options()
                  .must_support_ordering());
  EXPECT_FALSE(orderable_element_signature.argument(0)
                   .options()
                   .must_support_equality());
  EXPECT_FALSE(orderable_element_signature.argument(0)
                   .options()
                   .must_support_grouping());

  FunctionSignature equatable_element_signature(
      FunctionArgumentType(ARG_KIND_EXPR_ANY_1),
      {{ARG_KIND_EXPR_ANY_1,
        FunctionArgumentTypeOptions().set_must_support_equality()}},
      /*context_id=*/-1);

  EXPECT_FALSE(equatable_element_signature.argument(0)
                   .options()
                   .must_support_ordering());
  EXPECT_TRUE(equatable_element_signature.argument(0)
                  .options()
                  .must_support_equality());
  EXPECT_FALSE(equatable_element_signature.argument(0)
                   .options()
                   .must_support_grouping());

  FunctionSignature groupable_element_signature(
      FunctionArgumentType(ARG_KIND_EXPR_ANY_1),
      {{ARG_KIND_EXPR_ANY_1,
        FunctionArgumentTypeOptions().set_must_support_grouping()}},
      /*context_id=*/-1);

  EXPECT_FALSE(groupable_element_signature.argument(0)
                   .options()
                   .must_support_ordering());
  EXPECT_FALSE(groupable_element_signature.argument(0)
                   .options()
                   .must_support_equality());
  EXPECT_TRUE(groupable_element_signature.argument(0)
                  .options()
                  .must_support_grouping());

  TestArgumentTypeOptionsSerialization(orderable_element_signature.argument(0));
  TestArgumentTypeOptionsSerialization(equatable_element_signature.argument(0));
  TestArgumentTypeOptionsSerialization(groupable_element_signature.argument(0));
}

TEST(FunctionSignatureTests, FunctionArgumentNamesSerialization) {
  TypeFactory type_factory;
  TypeDeserializer type_deserializor(&type_factory,
                                     /*extended_type_deserializer=*/{});
  SignatureArgumentKind arg_kind = ARG_KIND_VOID;
  const Type* arg_type = nullptr;

  for (auto [named_kind, is_mandatory] :
       {std::make_pair(kPositionalOnly, false),
        std::make_pair(kPositionalOrNamed, false),
        std::make_pair(kNamedOnly, true)}) {
    auto arg_opts =
        FunctionArgumentTypeOptions().set_argument_name("arg", named_kind);
    FunctionArgumentTypeOptionsProto arg_opts_proto;
    GOOGLESQL_EXPECT_OK(arg_opts.Serialize(arg_type, &arg_opts_proto,
                                 /*file_descriptor_set_map=*/{}));
    EXPECT_TRUE(arg_opts_proto.has_argument_name());
    EXPECT_EQ(arg_opts_proto.argument_name(), "arg");
    // We don't set this field unless its true. It defaults to false.
    EXPECT_EQ(arg_opts_proto.has_argument_name_is_mandatory(), is_mandatory);
    EXPECT_EQ(arg_opts_proto.argument_name_is_mandatory(), is_mandatory);
    EXPECT_TRUE(arg_opts_proto.has_named_argument_kind());
    EXPECT_EQ(arg_opts_proto.named_argument_kind(), named_kind);
    FunctionArgumentTypeOptions arg_opts_deserialized;
    GOOGLESQL_EXPECT_OK(FunctionArgumentTypeOptions::Deserialize(
        arg_opts_proto, type_deserializor, arg_kind, arg_type,
        &arg_opts_deserialized));
    EXPECT_EQ(arg_opts.argument_name(), arg_opts_deserialized.argument_name());
    EXPECT_EQ(arg_opts.argument_name(), arg_opts_deserialized.argument_name());
    EXPECT_EQ(arg_opts.named_argument_kind(),
              arg_opts_deserialized.named_argument_kind());

    // Serializations from older binaries will not have named_argument_kind.
    arg_opts_proto.clear_named_argument_kind();
    FunctionArgumentTypeOptions arg_opts_deserialized_compat;
    GOOGLESQL_EXPECT_OK(FunctionArgumentTypeOptions::Deserialize(
        arg_opts_proto, type_deserializor, arg_kind, arg_type,
        &arg_opts_deserialized_compat));
    EXPECT_EQ(arg_opts.argument_name(),
              arg_opts_deserialized_compat.argument_name());
    if (is_mandatory) {
      EXPECT_EQ(arg_opts_deserialized_compat.named_argument_kind(), kNamedOnly);
    } else {
      EXPECT_EQ(arg_opts_deserialized_compat.named_argument_kind(),
                kPositionalOrNamed);
    }
  }
}

TEST(FunctionSignatureTests,
     TestFunctionArgumentTypeOptionsArrayElementConstraint) {
  FunctionArgumentType orderable_array_arg(
      ARG_KIND_EXPR_ARRAY_ANY_1,
      FunctionArgumentTypeOptions().set_array_element_must_support_ordering());
  EXPECT_TRUE(
      orderable_array_arg.options().array_element_must_support_ordering());
  EXPECT_FALSE(
      orderable_array_arg.options().array_element_must_support_grouping());
  EXPECT_FALSE(
      orderable_array_arg.options().array_element_must_support_equality());
  TestArgumentTypeOptionsSerialization(orderable_array_arg);

  FunctionArgumentType groupable_array_arg(
      ARG_KIND_EXPR_ARRAY_ANY_1,
      FunctionArgumentTypeOptions().set_array_element_must_support_grouping());
  EXPECT_FALSE(
      groupable_array_arg.options().array_element_must_support_ordering());
  EXPECT_TRUE(
      groupable_array_arg.options().array_element_must_support_grouping());
  EXPECT_FALSE(
      groupable_array_arg.options().array_element_must_support_equality());
  TestArgumentTypeOptionsSerialization(groupable_array_arg);

  FunctionArgumentType equatable_array_arg(
      ARG_KIND_EXPR_ARRAY_ANY_1,
      FunctionArgumentTypeOptions().set_array_element_must_support_equality());
  EXPECT_FALSE(
      equatable_array_arg.options().array_element_must_support_ordering());
  EXPECT_FALSE(
      equatable_array_arg.options().array_element_must_support_grouping());
  EXPECT_TRUE(
      equatable_array_arg.options().array_element_must_support_equality());
  TestArgumentTypeOptionsSerialization(equatable_array_arg);
}

TEST(FunctionSignatureTests, GraphFunctionArgumentType) {
  FunctionArgumentType node_arg(ARG_KIND_EXPR_GRAPH_NODE),
      edge_arg(ARG_KIND_EXPR_GRAPH_EDGE),
      element_arg(ARG_KIND_EXPR_GRAPH_ELEMENT),
      path_arg(ARG_KIND_EXPR_GRAPH_PATH);
  EXPECT_TRUE(node_arg.IsScalar());
  EXPECT_TRUE(edge_arg.IsScalar());
  EXPECT_TRUE(element_arg.IsScalar());
  EXPECT_TRUE(path_arg.IsScalar());
  EXPECT_EQ(node_arg.UserFacingName(PRODUCT_INTERNAL), "GRAPH_NODE");
  EXPECT_EQ(edge_arg.UserFacingName(PRODUCT_INTERNAL), "GRAPH_EDGE");
  EXPECT_EQ(element_arg.UserFacingName(PRODUCT_INTERNAL), "GRAPH_ELEMENT");
  EXPECT_EQ(path_arg.UserFacingName(PRODUCT_INTERNAL), "GRAPH_PATH");
  EXPECT_EQ(node_arg.DebugString(), "<graph_node>");
  EXPECT_EQ(edge_arg.DebugString(), "<graph_edge>");
  EXPECT_EQ(element_arg.DebugString(), "<graph_element>");
  EXPECT_EQ(path_arg.DebugString(), "<graph_path>");
}

TEST(FunctionSignatureTests, LambdaArgumentTypeConstructedDirectlyIsInvalid) {
  FunctionArgumentType lambda(ARG_KIND_LAMBDA);
  EXPECT_THAT(
      lambda.IsValid(PRODUCT_INTERNAL),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("constructed directly")));
}

TEST(FunctionSignatureTests, SignatureSupportsArgumentAlias) {
  FunctionSignature support_alias(
      FunctionArgumentType(ARG_KIND_EXPR_ANY_1),
      {{ARG_KIND_EXPR_ANY_1,
        FunctionArgumentTypeOptions().set_argument_alias_kind(
            FunctionEnums::ARGUMENT_ALIASED)},
       {ARG_KIND_EXPR_ANY_1,
        FunctionArgumentTypeOptions().set_argument_alias_kind(
            FunctionEnums::ARGUMENT_NON_ALIASED)}},
      /*context_id=*/-1);
  EXPECT_TRUE(SignatureSupportsArgumentAliases(support_alias));

  FunctionSignature unsupport_alias(
      FunctionArgumentType(ARG_KIND_EXPR_ANY_1),
      {{ARG_KIND_EXPR_ANY_1,
        FunctionArgumentTypeOptions().set_argument_alias_kind(
            FunctionEnums::ARGUMENT_NON_ALIASED)},
       {ARG_KIND_EXPR_ANY_1,
        FunctionArgumentTypeOptions().set_argument_alias_kind(
            FunctionEnums::ARGUMENT_NON_ALIASED)}},
      /*context_id=*/-1);
  EXPECT_FALSE(SignatureSupportsArgumentAliases(unsupport_alias));
}

TEST(FunctionSignatureTests, SetConcreteResultTypePreservesArgumentOptions) {
  FunctionSignature result_arg_has_options(
      FunctionArgumentType(
          ARG_KIND_EXPR_ANY_1,
          FunctionArgumentTypeOptions().set_uses_array_element_for_collation()),
      {ARG_KIND_EXPR_ANY_1, ARG_KIND_EXPR_ANY_1},
      /*context_id=*/-1);
  result_arg_has_options.SetConcreteResultType(
      types::Int64Type(), SignatureArgumentKind::ARG_KIND_EXPR_FIXED);
  EXPECT_TRUE(result_arg_has_options.result_type()
                  .options()
                  .uses_array_element_for_collation());
}

struct FunctionSignatureInvalidTestParams {
  const FunctionArgumentType result_type;
  const std::vector<FunctionArgumentType> arguments;
};

class FunctionSignatureInvalidTest
    : public ::testing::TestWithParam<FunctionSignatureInvalidTestParams> {};

TEST_P(FunctionSignatureInvalidTest,
       SignatureInvalidWhenTemplatedTypeIsNotFullyResolved) {
  std::unique_ptr<FunctionSignature> signature;
  auto& [result_type, arguments] = GetParam();

  EXPECT_DEBUG_DEATH(
      signature.reset(new FunctionSignature(
          GetParam().result_type, GetParam().arguments, /*context_id=*/-1)),
      "template must match an argument type template")
      << "Expected invalid for signature with result type "
      << result_type.DebugString(/*verbose=*/true) << " and args "
      << absl::StrJoin(
             arguments, ", ",
             [](std::string* out, const FunctionArgumentType& element) {
               absl::StrAppend(out, element.DebugString());
             });
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
  }
}

// Since templated types consist of other argument types, ensure that signatures
// without enough information for the templated type to derive are not valid.
// This could happen when a function returns a templated type, since there is no
// value provided in the arguments list to derive from.
INSTANTIATE_TEST_SUITE_P(
    FunctionSignatureTests, FunctionSignatureInvalidTest,
    ::testing::ValuesIn<FunctionSignatureInvalidTestParams>({
        // Templated array type should fail if corresponding ANY type is not
        // present.
        {.result_type = {ARG_KIND_EXPR_ARRAY_ANY_1}, .arguments = {}},
        {.result_type = {ARG_KIND_EXPR_ARRAY_ANY_1},
         .arguments = {ARG_KIND_EXPR_ANY_2}},
        {.result_type = {ARG_KIND_EXPR_ANY_2},
         .arguments = {ARG_KIND_EXPR_ARRAY_ANY_1}},
        // Templated range type should fail if corresponding ANY type is not
        // present.
        {.result_type = {ARG_KIND_EXPR_RANGE_ANY_1}, .arguments = {}},
        {.result_type = {ARG_KIND_EXPR_RANGE_ANY_1},
         .arguments = {ARG_KIND_EXPR_ANY_2}},
        {.result_type = {ARG_KIND_EXPR_ANY_2},
         .arguments = {ARG_KIND_EXPR_RANGE_ANY_1}},
        // Templated measure type should fail if corresponding ANY type is not
        // present.
        {.result_type = {ARG_KIND_EXPR_MEASURE_ANY_1}, .arguments = {}},
        {.result_type = {ARG_KIND_EXPR_MEASURE_ANY_1},
         .arguments = {ARG_KIND_EXPR_ANY_2}},
        {.result_type = {ARG_KIND_EXPR_ANY_2},
         .arguments = {ARG_KIND_EXPR_MEASURE_ANY_1}},
        // Templated map result type should fail if key and value are not both
        // present.
        {.result_type = {ARG_KIND_EXPR_MAP_ANY_1_2}, .arguments = {}},
        {.result_type = {ARG_KIND_EXPR_MAP_ANY_1_2},
         .arguments = {ARG_KIND_EXPR_ANY_1}},
        {.result_type = {ARG_KIND_EXPR_MAP_ANY_1_2},
         .arguments = {ARG_KIND_EXPR_ANY_2}},
        {.result_type = {ARG_KIND_EXPR_MAP_ANY_1_2},
         .arguments = {ARG_KIND_EXPR_ANY_1, ARG_KIND_EXPR_ANY_3}},
        {.result_type = {ARG_KIND_EXPR_MAP_ANY_1_2},
         .arguments = {ARG_KIND_EXPR_ANY_1, ARG_KIND_EXPR_ARRAY_ANY_1}},
        {.result_type = {ARG_KIND_EXPR_MAP_ANY_1_2},
         .arguments = {FunctionArgumentType::Lambda({ARG_KIND_EXPR_ANY_1},
                                                    ARG_KIND_EXPR_ANY_1)}},
        {.result_type = {ARG_KIND_EXPR_MAP_ANY_1_2},
         .arguments = {FunctionArgumentType::Lambda({ARG_KIND_EXPR_ANY_2},
                                                    ARG_KIND_EXPR_ANY_2)}},
        // Templated proto map type should fail if key and value are not
        // present.
        {.result_type = {ARG_KIND_EXPR_PROTO_MAP_ANY}, .arguments = {}},

        // These probably should fail, but don't currently. Since
        // ARG_KIND_EXPR_PROTO_MAP_ANY is not used as a return type
        // anywhere in
        // our
        // catalog, nor do we expect it to be in the future, this is low
        // priority.
        //
        // {.result_type = {ARG_KIND_EXPR_PROTO_MAP_ANY},
        //  .arguments = {ARG_KIND_EXPR_PROTO_MAP_KEY_ANY}},
        // {.result_type = {ARG_KIND_EXPR_PROTO_MAP_ANY},
        //  .arguments = {ARG_KIND_EXPR_PROTO_MAP_VALUE_ANY}},
    }));

struct FunctionSignatureValidTestParams {
  const FunctionArgumentType result_type;
  const std::vector<FunctionArgumentType> arguments;
};

class FunctionSignatureValidTest
    : public ::testing::TestWithParam<FunctionSignatureValidTestParams> {};

TEST_P(FunctionSignatureValidTest,
       SignatureValidWhenTemplatedTypeIsFullyResolved) {
  std::unique_ptr<FunctionSignature> signature;
  signature = std::make_unique<FunctionSignature>(
      GetParam().result_type, GetParam().arguments, /*context_id=*/-1);

  GOOGLESQL_EXPECT_OK(signature->IsValid(ProductMode::PRODUCT_EXTERNAL));
}

INSTANTIATE_TEST_SUITE_P(
    FunctionSignatureTests, FunctionSignatureValidTest,
    ::testing::ValuesIn<FunctionSignatureValidTestParams>({
        // Templated map type should succeed if key and value are both present
        // or inferrable.
        {.result_type = {ARG_KIND_EXPR_MAP_ANY_1_2},
         .arguments = {ARG_KIND_EXPR_ANY_1, ARG_KIND_EXPR_ANY_2}},
        {.result_type = {ARG_KIND_EXPR_MAP_ANY_1_2},
         .arguments = {ARG_KIND_EXPR_ARRAY_ANY_1, ARG_KIND_EXPR_ANY_2}},
        {.result_type = {ARG_KIND_EXPR_MAP_ANY_1_2},
         .arguments = {ARG_KIND_EXPR_ARRAY_ANY_1, ARG_KIND_EXPR_ARRAY_ANY_2}},
        {.result_type = {ARG_KIND_EXPR_MAP_ANY_1_2},
         .arguments = {ARG_KIND_EXPR_RANGE_ANY_1, ARG_KIND_EXPR_ANY_2}},
        {.result_type = {ARG_KIND_EXPR_MAP_ANY_1_2},
         .arguments = {ARG_KIND_EXPR_RANGE_ANY_1, ARG_KIND_EXPR_ARRAY_ANY_2}},
        {.result_type = {ARG_KIND_EXPR_MAP_ANY_1_2},
         .arguments = {FunctionArgumentType::Lambda({ARG_KIND_EXPR_ANY_1},
                                                    ARG_KIND_EXPR_ANY_2)}},
        {.result_type = {types::BoolType()},
         .arguments = {ARG_KIND_EXPR_MAP_ANY_1_2, ARG_KIND_EXPR_ANY_2,
                       FunctionArgumentType::Lambda({ARG_KIND_EXPR_ANY_1},
                                                    ARG_KIND_EXPR_ANY_1)}},
    }));

TEST(FunctionSignatureTests, GetSQLDeclarationWithTypeModifiers) {
  TypeFactory factory;
  Collation collation = Collation::MakeScalar("und:ci");
  StringTypeParametersProto string_type_param_proto;
  string_type_param_proto.set_max_length(123);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      TypeParameters type_param,
      TypeParameters::MakeStringTypeParameters(string_type_param_proto));
  TypeModifiers type_modifiers =
      TypeModifiers::MakeTypeModifiers(type_param, collation);

  FunctionArgumentType arg_type(factory.get_string(),
                                FunctionArgumentTypeOptions(),
                                /*num_occurrences=*/1, type_modifiers);
  FunctionArgumentType arg_type_no_modifiers(factory.get_string(),
                                             FunctionArgumentTypeOptions(),
                                             /*num_occurrences=*/1);

  // Test FunctionArgumentType::GetSQLDeclaration
  EXPECT_EQ("STRING(123) COLLATE 'und:ci'",
            arg_type.GetSQLDeclaration(ProductMode::PRODUCT_INTERNAL,
                                       /*use_external_float32=*/false));
  EXPECT_EQ("STRING", arg_type_no_modifiers.GetSQLDeclaration(
                          ProductMode::PRODUCT_INTERNAL,
                          /*use_external_float32=*/false));

  // Test FunctionSignature::GetSQLDeclaration
  FunctionArgumentTypeList arguments;
  arguments.push_back(arg_type);
  FunctionSignature signature(factory.get_int64(), arguments,
                              /*context_id=*/-1);

  FunctionArgumentTypeList arguments_no_modifiers;
  arguments_no_modifiers.push_back(arg_type_no_modifiers);
  FunctionSignature signature_no_modifiers(
      factory.get_int64(), arguments_no_modifiers, /*context_id=*/-1);

  EXPECT_EQ(signature.GetSQLDeclaration({} /* argument_names */,
                                        ProductMode::PRODUCT_INTERNAL,
                                        /*use_external_float32=*/false),
            "(STRING(123) COLLATE 'und:ci') RETURNS INT64");
  EXPECT_EQ(signature_no_modifiers.GetSQLDeclaration(
                {} /* argument_names */, ProductMode::PRODUCT_INTERNAL,
                /*use_external_float32=*/false),
            "(STRING) RETURNS INT64");
}

TEST(FunctionSignatureTests, GetSQLDeclarationLambdaKeepsArgumentOptions) {
  TypeFactory factory;
  FunctionArgumentType unary_lambda = FunctionArgumentType::Lambda(
      {factory.get_int64()}, factory.get_int64(),
      FunctionArgumentTypeOptions().set_is_not_aggregate());
  FunctionArgumentType binary_lambda = FunctionArgumentType::Lambda(
      {factory.get_int64(), factory.get_string()}, factory.get_bool(),
      FunctionArgumentTypeOptions().set_is_not_aggregate());
  FunctionArgumentType nullary_lambda_no_options =
      FunctionArgumentType::Lambda({}, factory.get_int64());

  EXPECT_EQ("FUNCTION<INT64->INT64> NOT AGGREGATE",
            unary_lambda.GetSQLDeclaration(ProductMode::PRODUCT_INTERNAL));
  EXPECT_EQ("FUNCTION<(INT64, STRING)->BOOL> NOT AGGREGATE",
            binary_lambda.GetSQLDeclaration(ProductMode::PRODUCT_INTERNAL));
  EXPECT_EQ("FUNCTION<()->INT64>", nullary_lambda_no_options.GetSQLDeclaration(
                                       ProductMode::PRODUCT_INTERNAL));
  // Cardinality comments are also emitted for lambda arguments.
  FunctionArgumentType optional_lambda = FunctionArgumentType::Lambda(
      {factory.get_int64()}, factory.get_bool(),
      FunctionArgumentTypeOptions(FunctionArgumentType::OPTIONAL));
  EXPECT_EQ("/*optional*/ FUNCTION<INT64->BOOL>",
            optional_lambda.GetSQLDeclaration(ProductMode::PRODUCT_INTERNAL));

  // NOT AGGREGATE on a function-typed parameter round-trips through
  // FunctionSignature::GetSQLDeclaration, e.g. for SQL UDA declarations.
  FunctionSignature signature(
      factory.get_int64(),
      {FunctionArgumentType(factory.get_int64()), unary_lambda},
      /*context_id=*/-1);
  EXPECT_EQ(
      "(v INT64, f FUNCTION<INT64->INT64> NOT AGGREGATE) RETURNS INT64",
      signature.GetSQLDeclaration({"v", "f"}, ProductMode::PRODUCT_INTERNAL));
}

TEST(FunctionSignatureTests, GetSQLDeclarationWithTypeModifiersError) {
  TypeFactory factory;
  Collation collation = Collation::MakeScalar("und:ci");
  TypeModifiers type_modifiers =
      TypeModifiers::MakeTypeModifiers(TypeParameters(), collation);

  // Collation is not compatible with INT64, so TypeNameWithModifiers should
  // fail.
  FunctionArgumentType arg_type(factory.get_int64(),
                                FunctionArgumentTypeOptions(),
                                /*num_occurrences=*/1, type_modifiers);

  std::string decl = arg_type.GetSQLDeclaration(ProductMode::PRODUCT_INTERNAL,
                                                /*use_external_float32=*/false);
  EXPECT_EQ("ERROR: Input collation und:ci is not compatible with type INT64",
            decl);
}

TEST(FunctionSignatureTest, ValidateSignatureConstraints) {
  TypeFactory factory;
  const Type* int32_type = factory.get_int32();

  auto opt_with_kind = [](FunctionEnums::NamedArgumentKind kind,
                          absl::string_view name = "") {
    FunctionArgumentTypeOptions opt;
    if (!name.empty() || kind != FunctionEnums::POSITIONAL_ONLY) {
      opt.set_argument_name(name, kind);
    }
    return opt;
  };

  // Helper to expect invalid signature.
  auto expect_invalid = [&](auto signature_creator,
                            const std::string& error_message) {
    std::unique_ptr<FunctionSignature> signature;
    EXPECT_DEBUG_DEATH(signature.reset(signature_creator()), error_message);
    if (!GOOGLESQL_DEBUG_MODE) {
      signature.reset(signature_creator());
      EXPECT_THAT(signature->IsValid(ProductMode::PRODUCT_EXTERNAL),
                  StatusIs(absl::StatusCode::kInvalidArgument,
                           HasSubstr(error_message)));
    }
  };

  // Valid: POSITIONAL_ONLY -> POSITIONAL_OR_NAMED -> NAMED_ONLY
  {
    FunctionSignature signature(
        FunctionArgumentType(int32_type),
        {
            FunctionArgumentType(int32_type,
                                 opt_with_kind(FunctionEnums::POSITIONAL_ONLY)),
            FunctionArgumentType(
                int32_type,
                opt_with_kind(FunctionEnums::POSITIONAL_OR_NAMED, "a")),
            FunctionArgumentType(int32_type,
                                 opt_with_kind(FunctionEnums::NAMED_ONLY, "b")),
        },
        /*context_ptr=*/nullptr);
    GOOGLESQL_EXPECT_OK(signature.IsValid(ProductMode::PRODUCT_EXTERNAL));
  }

  // Invalid ordering: POSITIONAL_OR_NAMED -> REQUIRED POSITIONAL_ONLY
  expect_invalid(
      [&]() {
        return new FunctionSignature(
            FunctionArgumentType(int32_type),
            {
                FunctionArgumentType(
                    int32_type,
                    opt_with_kind(FunctionEnums::POSITIONAL_OR_NAMED, "a")),
                FunctionArgumentType(
                    int32_type, opt_with_kind(FunctionEnums::POSITIONAL_ONLY)),
            },
            /*context_ptr=*/nullptr);
      },
      "follows an argument with a more restrictive kind");

  // Valid ordering: POSITIONAL_OR_NAMED -> REPEATED POSITIONAL_ONLY
  {
    FunctionSignature signature(
        FunctionArgumentType(int32_type),
        {
            FunctionArgumentType(
                int32_type,
                opt_with_kind(FunctionEnums::POSITIONAL_OR_NAMED, "a")),
            FunctionArgumentType(int32_type,
                                 opt_with_kind(FunctionEnums::POSITIONAL_ONLY)
                                     .set_cardinality(FunctionEnums::REPEATED)),
        },
        /*context_ptr=*/nullptr);
    GOOGLESQL_EXPECT_OK(signature.IsValid(ProductMode::PRODUCT_EXTERNAL));
  }

  // Valid ordering: POSITIONAL_OR_NAMED -> OPTIONAL POSITIONAL_ONLY
  {
    FunctionSignature signature(
        FunctionArgumentType(int32_type),
        {
            FunctionArgumentType(
                int32_type,
                opt_with_kind(FunctionEnums::POSITIONAL_OR_NAMED, "a")),
            FunctionArgumentType(int32_type,
                                 opt_with_kind(FunctionEnums::POSITIONAL_ONLY)
                                     .set_cardinality(FunctionEnums::OPTIONAL)),
        },
        /*context_ptr=*/nullptr);
    GOOGLESQL_EXPECT_OK(signature.IsValid(ProductMode::PRODUCT_EXTERNAL));
  }

  // Valid ordering: POSITIONAL_OR_NAMED -> Lambda POSITIONAL_ONLY
  {
    FunctionSignature signature(
        FunctionArgumentType(int32_type),
        {
            FunctionArgumentType(
                int32_type,
                opt_with_kind(FunctionEnums::POSITIONAL_OR_NAMED, "a")),
            FunctionArgumentType::Lambda({FunctionArgumentType(int32_type)},
                                         FunctionArgumentType(int32_type)),
        },
        /*context_ptr=*/nullptr);
    GOOGLESQL_EXPECT_OK(signature.IsValid(ProductMode::PRODUCT_EXTERNAL));
  }

  // Invalid ordering: NAMED_ONLY -> POSITIONAL_OR_NAMED
  expect_invalid(
      [&]() {
        return new FunctionSignature(
            FunctionArgumentType(int32_type),
            {
                FunctionArgumentType(
                    int32_type, opt_with_kind(FunctionEnums::NAMED_ONLY, "a")),
                FunctionArgumentType(
                    int32_type,
                    opt_with_kind(FunctionEnums::POSITIONAL_OR_NAMED, "b")),
            },
            /*context_ptr=*/nullptr);
      },
      "follows an argument with a more restrictive kind");

  // Invalid: NAMED_ONLY without name
  expect_invalid(
      [&]() {
        return new FunctionSignature(
            FunctionArgumentType(int32_type),
            {
                FunctionArgumentType(
                    int32_type, opt_with_kind(FunctionEnums::NAMED_ONLY, "")),
            },
            /*context_ptr=*/nullptr);
      },
      "is NAMED_ONLY but has no name");

  // Invalid: POSITIONAL_OR_NAMED without name
  expect_invalid(
      [&]() {
        return new FunctionSignature(
            FunctionArgumentType(int32_type),
            {
                FunctionArgumentType(
                    int32_type,
                    opt_with_kind(FunctionEnums::POSITIONAL_OR_NAMED, "")),
            },
            /*context_ptr=*/nullptr);
      },
      "is POSITIONAL_OR_NAMED but has no name");

  // Valid: Repeated, POSITIONAL_ONLY, Scalar
  {
    FunctionSignature signature(
        FunctionArgumentType(int32_type),
        {
            FunctionArgumentType(int32_type, FunctionArgumentType::REPEATED),
        },
        /*context_ptr=*/nullptr);
    GOOGLESQL_EXPECT_OK(signature.IsValid(ProductMode::PRODUCT_EXTERNAL));
  }

  // Invalid: Repeated, POSITIONAL_OR_NAMED
  expect_invalid(
      [&]() {
        FunctionArgumentTypeOptions opt =
            opt_with_kind(FunctionEnums::POSITIONAL_OR_NAMED, "a");
        opt.set_cardinality(FunctionArgumentType::REPEATED);
        return new FunctionSignature(FunctionArgumentType(int32_type),
                                     {
                                         FunctionArgumentType(int32_type, opt),
                                     },
                                     /*context_ptr=*/nullptr);
      },
      "must be POSITIONAL_ONLY");

  // Invalid: Repeated, non-Scalar (Relation)
  expect_invalid(
      [&]() {
        FunctionArgumentTypeOptions opt;
        opt.set_cardinality(FunctionArgumentType::REPEATED);
        return new FunctionSignature(
            FunctionArgumentType(int32_type),
            {
                FunctionArgumentType(ARG_KIND_RELATION, opt),
            },
            /*context_ptr=*/nullptr);
      },
      "must be Scalar");
}

TEST(FunctionSignatureTests, SupportsOrderByRelationValid) {
  TypeFactory factory;
  TVFRelation schema({{"a", factory.get_string()}});
  FunctionArgumentTypeOptions options;
  options.set_supports_order_by(true);
  FunctionArgumentType rel_arg = FunctionArgumentType::RelationWithSchema(
      schema, /*extra_relation_input_columns_allowed=*/false, options);
  EXPECT_TRUE(rel_arg.options().supports_order_by());
  GOOGLESQL_EXPECT_OK(rel_arg.IsValid(PRODUCT_EXTERNAL));
  TestArgumentTypeOptionsSerialization(rel_arg);

  FunctionArgumentType any_rel_arg = FunctionArgumentType::AnyRelation(options);
  EXPECT_TRUE(any_rel_arg.options().supports_order_by());
  GOOGLESQL_EXPECT_OK(any_rel_arg.IsValid(PRODUCT_EXTERNAL));
  TestArgumentTypeOptionsSerialization(any_rel_arg);
}

TEST(FunctionSignatureTests, SupportsOrderByNonRelationFails) {
  TypeFactory factory;
  FunctionArgumentTypeOptions options;
  options.set_supports_order_by(true);

  // Scalar argument
  FunctionArgumentType scalar_arg(factory.get_int64(), options);
  EXPECT_THAT(
      scalar_arg.IsValid(PRODUCT_EXTERNAL),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Option supports_order_by can only be set on relation "
                         "arguments")));

  // Model argument
  FunctionArgumentType model_arg(ARG_KIND_MODEL, options);
  EXPECT_THAT(
      model_arg.IsValid(PRODUCT_EXTERNAL),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Option supports_order_by can only be set on relation "
                         "arguments")));

  // Connection argument
  FunctionArgumentType connection_arg(ARG_KIND_CONNECTION, options);
  EXPECT_THAT(
      connection_arg.IsValid(PRODUCT_EXTERNAL),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Option supports_order_by can only be set on relation "
                         "arguments")));

  // Descriptor argument
  FunctionArgumentType descriptor_arg(ARG_KIND_DESCRIPTOR, options);
  EXPECT_THAT(
      descriptor_arg.IsValid(PRODUCT_EXTERNAL),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Option supports_order_by can only be set on relation "
                         "arguments")));
}

}  // namespace googlesql
