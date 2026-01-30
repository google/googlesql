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
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/function_signature_testutil.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/proto/function.pb.h"
#include "googlesql/public/error_location.pb.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/input_argument_type.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_deserializer.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/log_severity.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/types/span.h"
#include "googlesql/base/status.h"

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
  ASSERT_EQ(ARG_TYPE_FIXED, fixed_type_int32.kind());
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
  ASSERT_EQ(ARG_TYPE_FIXED, repeating_fixed_type_int32.kind());
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
  ASSERT_EQ(ARG_TYPE_FIXED, optional_fixed_type_int32.kind());
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

  FunctionArgumentType proto_any_type(ARG_PROTO_ANY);
  ASSERT_FALSE(proto_any_type.IsConcrete());
  ASSERT_THAT(proto_any_type.type(), IsNull());
  ASSERT_EQ(ARG_PROTO_ANY, proto_any_type.kind());
  ASSERT_FALSE(proto_any_type.repeated());

  FunctionArgumentType struct_any_type(ARG_STRUCT_ANY);
  ASSERT_FALSE(struct_any_type.IsConcrete());
  ASSERT_THAT(struct_any_type.type(), IsNull());
  ASSERT_EQ(ARG_STRUCT_ANY, struct_any_type.kind());
  ASSERT_FALSE(struct_any_type.repeated());

  FunctionArgumentType enum_any_type(ARG_ENUM_ANY);
  ASSERT_FALSE(enum_any_type.IsConcrete());
  ASSERT_THAT(enum_any_type.type(), IsNull());
  ASSERT_EQ(ARG_ENUM_ANY, enum_any_type.kind());
  ASSERT_FALSE(enum_any_type.repeated());

  FunctionArgumentType void_type(ARG_TYPE_VOID);
  ASSERT_FALSE(void_type.IsConcrete());
  void_type.set_num_occurrences(1);
  ASSERT_TRUE(void_type.IsConcrete());
  ASSERT_THAT(void_type.type(), IsNull());
  ASSERT_EQ(ARG_TYPE_VOID, void_type.kind());
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

  FunctionArgumentType templated_type_non_null(ARG_TYPE_ANY_1,
                                               valid_optional_arg_type_option);
  EXPECT_TRUE(templated_type_non_null.GetDefault().value().Equals(
      values::Int32(10086)));
  GOOGLESQL_EXPECT_OK(templated_type_non_null.IsValid(ProductMode::PRODUCT_EXTERNAL));
  TestDefaultValueAfterSerialization(templated_type_non_null);

  FunctionArgumentType templated_type_null(ARG_TYPE_ANY_1,
                                           valid_optional_arg_type_option_null);
  EXPECT_TRUE(
      templated_type_null.GetDefault().value().Equals(values::NullInt32()));
  GOOGLESQL_EXPECT_OK(templated_type_null.IsValid(ProductMode::PRODUCT_EXTERNAL));
  TestDefaultValueAfterSerialization(templated_type_null);

  FunctionArgumentType relation_type(ARG_TYPE_RELATION,
                                     valid_optional_arg_type_option_null);
  EXPECT_THAT(
      relation_type.IsValid(ProductMode::PRODUCT_EXTERNAL),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("ANY TABLE argument cannot have a default value")));

  FunctionArgumentType model_type(ARG_TYPE_MODEL,
                                  valid_optional_arg_type_option_null);
  EXPECT_THAT(
      model_type.IsValid(ProductMode::PRODUCT_EXTERNAL),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("ANY MODEL argument cannot have a default value")));

  FunctionArgumentType connection_type(ARG_TYPE_CONNECTION,
                                       valid_optional_arg_type_option_null);
  EXPECT_THAT(
      connection_type.IsValid(ProductMode::PRODUCT_EXTERNAL),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("ANY CONNECTION argument cannot have a default value")));

  FunctionArgumentType descriptor_type(ARG_TYPE_DESCRIPTOR,
                                       valid_optional_arg_type_option_null);
  EXPECT_THAT(
      descriptor_type.IsValid(ProductMode::PRODUCT_EXTERNAL),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("ANY DESCRIPTOR argument cannot have a default value")));
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
      FunctionArgumentType::Lambda({}, ARG_TYPE_ANY_1);
  ASSERT_TRUE(lambda_zero_args.IsLambda());
  ASSERT_EQ(ARG_TYPE_LAMBDA, lambda_zero_args.kind());
  ASSERT_FALSE(lambda_zero_args.IsConcrete());
  ASSERT_TRUE(lambda_zero_args.IsTemplated());
  ASSERT_FALSE(lambda_zero_args.repeated());
  ASSERT_THAT(lambda_zero_args.type(), IsNull());
  ASSERT_TRUE(lambda_zero_args.lambda().argument_types().empty());

  // Single function-type argument argument types
  FunctionArgumentType lambda_any_type =
      FunctionArgumentType::Lambda({ARG_TYPE_ANY_1}, ARG_TYPE_ANY_2);
  ASSERT_TRUE(lambda_any_type.IsLambda());
  ASSERT_EQ(ARG_TYPE_LAMBDA, lambda_any_type.kind());
  ASSERT_FALSE(lambda_any_type.IsConcrete());
  ASSERT_TRUE(lambda_any_type.IsTemplated());
  ASSERT_FALSE(lambda_any_type.repeated());
  ASSERT_THAT(lambda_any_type.type(), IsNull());

  FunctionArgumentType lambda_array_any_type = FunctionArgumentType::Lambda(
      {ARG_ARRAY_TYPE_ANY_1}, ARG_ARRAY_TYPE_ANY_2);
  ASSERT_TRUE(lambda_array_any_type.IsLambda());
  ASSERT_EQ(ARG_TYPE_LAMBDA, lambda_array_any_type.kind());
  ASSERT_FALSE(lambda_array_any_type.IsConcrete());
  ASSERT_TRUE(lambda_array_any_type.IsTemplated());
  ASSERT_FALSE(lambda_array_any_type.repeated());
  ASSERT_THAT(lambda_array_any_type.type(), IsNull());

  FunctionArgumentType lambda_non_templated_body_type =
      FunctionArgumentType::Lambda({ARG_TYPE_ANY_1}, factory.get_bool());
  ASSERT_TRUE(lambda_non_templated_body_type.IsLambda());
  ASSERT_EQ(ARG_TYPE_LAMBDA, lambda_non_templated_body_type.kind());
  ASSERT_FALSE(lambda_non_templated_body_type.IsConcrete());
  ASSERT_TRUE(lambda_non_templated_body_type.IsTemplated());
  ASSERT_FALSE(lambda_non_templated_body_type.repeated());
  ASSERT_THAT(lambda_non_templated_body_type.type(), IsNull());

  FunctionArgumentType lambda_non_templated_arg_type =
      FunctionArgumentType::Lambda({factory.get_int64()}, ARG_TYPE_ANY_1);
  ASSERT_TRUE(lambda_non_templated_arg_type.IsLambda());
  ASSERT_EQ(ARG_TYPE_LAMBDA, lambda_non_templated_arg_type.kind());
  ASSERT_FALSE(lambda_non_templated_arg_type.IsConcrete());
  ASSERT_TRUE(lambda_non_templated_arg_type.IsTemplated());
  ASSERT_FALSE(lambda_non_templated_arg_type.repeated());
  ASSERT_THAT(lambda_non_templated_arg_type.type(), IsNull());

  FunctionArgumentType lambda_non_templated_arg_body_type =
      FunctionArgumentType::Lambda({factory.get_int64()}, factory.get_bool());
  ASSERT_TRUE(lambda_non_templated_arg_body_type.IsLambda());
  ASSERT_EQ(ARG_TYPE_LAMBDA, lambda_non_templated_arg_body_type.kind());
  ASSERT_FALSE(lambda_non_templated_arg_body_type.IsConcrete());
  ASSERT_FALSE(lambda_non_templated_arg_body_type.IsTemplated());
  ASSERT_FALSE(lambda_non_templated_arg_body_type.repeated());
  ASSERT_THAT(lambda_non_templated_arg_body_type.type(), IsNull());

  // Multiple function-type argument argument types
  FunctionArgumentType lambda_any_type_multi_args =
      FunctionArgumentType::Lambda(
          {
              ARG_TYPE_ANY_1,
              ARG_TYPE_ANY_2,
          },
          ARG_TYPE_ANY_2);
  ASSERT_TRUE(lambda_any_type_multi_args.IsLambda());
  ASSERT_EQ(ARG_TYPE_LAMBDA, lambda_any_type_multi_args.kind());
  ASSERT_FALSE(lambda_any_type_multi_args.IsConcrete());
  ASSERT_TRUE(lambda_any_type_multi_args.IsTemplated());
  ASSERT_FALSE(lambda_any_type_multi_args.repeated());
  ASSERT_THAT(lambda_any_type_multi_args.type(), IsNull());

  FunctionArgumentType lambda_array_any_type_multi_args =
      FunctionArgumentType::Lambda(
          {
              ARG_ARRAY_TYPE_ANY_1,
              ARG_ARRAY_TYPE_ANY_2,
          },
          ARG_ARRAY_TYPE_ANY_2);
  ASSERT_TRUE(lambda_array_any_type_multi_args.IsLambda());
  ASSERT_EQ(ARG_TYPE_LAMBDA, lambda_array_any_type_multi_args.kind());
  ASSERT_FALSE(lambda_array_any_type_multi_args.IsConcrete());
  ASSERT_TRUE(lambda_array_any_type_multi_args.IsTemplated());
  ASSERT_FALSE(lambda_array_any_type_multi_args.repeated());
  ASSERT_THAT(lambda_array_any_type_multi_args.type(), IsNull());

  FunctionArgumentType lambda_non_templated_body_type_multi_args =
      FunctionArgumentType::Lambda(
          {
              ARG_TYPE_ANY_1,
              ARG_TYPE_ANY_1,
          },
          factory.get_bool());
  ASSERT_TRUE(lambda_non_templated_body_type_multi_args.IsLambda());
  ASSERT_EQ(ARG_TYPE_LAMBDA, lambda_non_templated_body_type_multi_args.kind());
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
          ARG_TYPE_ANY_1);
  ASSERT_TRUE(lambda_non_templated_arg_type_multi_args.IsLambda());
  ASSERT_EQ(ARG_TYPE_LAMBDA, lambda_non_templated_arg_type_multi_args.kind());
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
  ASSERT_EQ(ARG_TYPE_LAMBDA,
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
  ASSERT_EQ(ARG_TYPE_LAMBDA, lambda_concrete_arg_body_type_multi_args.kind());
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
  ASSERT_EQ(ARG_TYPE_LAMBDA, lambda_with_options.kind());
  ASSERT_TRUE(lambda_with_options.options().has_argument_name());
  ASSERT_EQ(lambda_with_options.options().argument_name(), "my_lambda");
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
              FunctionArgumentType(ARG_TYPE_ANY_1),
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
          FunctionArgumentType(ARG_TYPE_ANY_1)),
      &type_factory);

  // Templated argument type and body type.
  TestLambdaSerialization(FunctionArgumentType::Lambda(
                              {
                                  FunctionArgumentType(ARG_TYPE_ANY_1),
                              },
                              FunctionArgumentType(ARG_TYPE_ANY_2)),
                          &type_factory);
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
  arguments.push_back(FunctionArgumentType(ARG_ARRAY_TYPE_ANY_1));
  arguments.push_back(FunctionArgumentType::Lambda(
      {ARG_TYPE_ANY_1}, FunctionArgumentType(factory->get_bool())));
  FunctionSignature array_filter_function(
      FunctionArgumentType(ARG_ARRAY_TYPE_ANY_1), arguments, nullptr);
  return array_filter_function;
}

// Model signature for 'IF <bool> THEN <any> ELSE <any> END'
static FunctionSignature GetIfThenFunction(TypeFactory* factory) {
  FunctionArgumentTypeList arguments;
  arguments.push_back(FunctionArgumentType(factory->get_bool()));
  arguments.push_back(FunctionArgumentType(ARG_TYPE_ANY_1));
  arguments.push_back(FunctionArgumentType(ARG_TYPE_ANY_1));
  FunctionSignature if_then_else_signature(
      FunctionArgumentType(ARG_TYPE_ANY_1), arguments, nullptr);
  return if_then_else_signature;
}

// Model signature for:
// CASE WHEN <x1> THEN <y1>
//      WHEN <x2> THEN <y2> ELSE <z> END
static FunctionSignature GetCaseWhenFunction(TypeFactory* factory) {
  FunctionArgumentTypeList arguments;
  arguments.push_back(FunctionArgumentType(factory->get_bool(),
                                           FunctionArgumentType::REPEATED));
  arguments.push_back(
      FunctionArgumentType(ARG_TYPE_ANY_1, FunctionArgumentType::REPEATED));
  arguments.push_back(
      FunctionArgumentType(ARG_TYPE_ANY_1, FunctionArgumentType::OPTIONAL));
  FunctionSignature case_when_signature(FunctionArgumentType(ARG_TYPE_ANY_1),
                                        arguments, nullptr);
  return case_when_signature;
}

// Model signature for:
// CASE <w> WHEN <x1> THEN <y1>
//          WHEN <x2> THEN <y2> ... ELSE <z> END
static FunctionSignature GetCaseValueFunction(
    TypeFactory* factory, FunctionArgumentTypeList* arguments) {
  arguments->clear();
  arguments->push_back(FunctionArgumentType(ARG_TYPE_ANY_1));
  arguments->push_back(FunctionArgumentType(ARG_TYPE_ANY_1,
                                            FunctionArgumentType::REPEATED));
  arguments->push_back(FunctionArgumentType(ARG_TYPE_ANY_2,
                                            FunctionArgumentType::REPEATED));
  arguments->push_back(FunctionArgumentType(ARG_TYPE_ANY_2,
                                            FunctionArgumentType::OPTIONAL));
  FunctionSignature case_value_signature(FunctionArgumentType(ARG_TYPE_ANY_2),
                                         *arguments, /*context_id=*/-1);
  return case_value_signature;
}

// Test a function with VOID return type, and some argument options.
static FunctionSignature GetVoidFunction(TypeFactory* factory) {
  FunctionSignature void_func(
      ARG_TYPE_VOID,
      {{types::BoolType()},
       {types::Int64Type(),
        FunctionArgumentTypeOptions().set_is_not_aggregate()},
       {ARG_TYPE_ANY_1, FunctionArgumentTypeOptions().set_must_be_non_null()}},
      /*context_id=*/-1);
  return void_func;
}

static FunctionSignature GetConstantExpressionArgumentFunction(
    TypeFactory* factory) {
  FunctionSignature constant_expression_function(
      FunctionArgumentType(ARG_TYPE_VOID),
      {{ARG_TYPE_ANY_1,
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
  arguments.push_back(FunctionArgumentType(ARG_TYPE_ANY_1));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType(factory.get_int64()), arguments,
      /*context_id=*/-1);
  GOOGLESQL_EXPECT_OK(signature->IsValid(ProductMode::PRODUCT_EXTERNAL));
  GOOGLESQL_EXPECT_OK(signature->init_status());
  arguments.push_back(FunctionArgumentType(ARG_TYPE_ANY_1, OPTIONAL));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType(factory.get_int64()), arguments,
      /*context_id=*/-1);
  GOOGLESQL_EXPECT_OK(signature->IsValid(ProductMode::PRODUCT_EXTERNAL));
  GOOGLESQL_EXPECT_OK(signature->init_status());
  arguments.push_back(FunctionArgumentType(ARG_TYPE_ANY_1));
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
  arguments.push_back(FunctionArgumentType(ARG_TYPE_ANY_1));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType(factory.get_int64()), arguments,
      /*context_id=*/-1);
  GOOGLESQL_EXPECT_OK(signature->IsValid(ProductMode::PRODUCT_EXTERNAL));
  GOOGLESQL_EXPECT_OK(signature->init_status());
  arguments.push_back(FunctionArgumentType(ARG_TYPE_ANY_1, REPEATED));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType(factory.get_int64()), arguments,
      /*context_id=*/-1);
  GOOGLESQL_EXPECT_OK(signature->IsValid(ProductMode::PRODUCT_EXTERNAL));
  GOOGLESQL_EXPECT_OK(signature->init_status());
  arguments.push_back(FunctionArgumentType(ARG_TYPE_ANY_1));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType(factory.get_int64()), arguments,
      /*context_id=*/-1);
  GOOGLESQL_EXPECT_OK(signature->IsValid(ProductMode::PRODUCT_EXTERNAL));
  GOOGLESQL_EXPECT_OK(signature->init_status());
  arguments.push_back(FunctionArgumentType(ARG_TYPE_ANY_1, REPEATED));
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
  arguments.push_back(FunctionArgumentType(ARG_TYPE_ANY_1, REPEATED));
  arguments.push_back(FunctionArgumentType(ARG_TYPE_ANY_1, OPTIONAL));
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
  arguments.push_back(FunctionArgumentType(ARG_TYPE_ANY_1, OPTIONAL));
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
  arguments.push_back(FunctionArgumentType(ARG_TYPE_ANY_1, REPEATED));
  arguments.push_back(FunctionArgumentType(ARG_TYPE_ANY_1, REPEATED));
  arguments.push_back(FunctionArgumentType(ARG_TYPE_ANY_1, OPTIONAL));
  arguments.push_back(FunctionArgumentType(ARG_TYPE_ANY_1, OPTIONAL));
  EXPECT_DEBUG_DEATH(signature.reset(new FunctionSignature(
                         FunctionArgumentType(factory.get_int64()), arguments,
                         /*context_id=*/-1)),
                     "The number of repeated arguments");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  // 2 repeated, 3 optional
  arguments.push_back(FunctionArgumentType(ARG_TYPE_ANY_1, OPTIONAL));
  EXPECT_DEBUG_DEATH(signature.reset(new FunctionSignature(
                         FunctionArgumentType(factory.get_int64()), arguments,
                         /*context_id=*/-1)),
                     "The number of repeated arguments");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  // num_occurrences must be the same value for all repeated arguments.
  arguments.assign({{ARG_TYPE_ANY_1, REPEATED, 2},
                    {ARG_TYPE_ANY_1, REPEATED, 1}});
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
  arguments.push_back(FunctionArgumentType(ARG_TYPE_RELATION, REPEATED));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType(factory.get_int64()), arguments,
      /*context_id=*/-1);

  EXPECT_THAT(signature->IsValidForTableValuedFunction(),
              StatusIs(absl::StatusCode::kInternal,
                       testing::HasSubstr(
                           "Repeated relation argument is not supported")));

  // Optional relation following any other optional argument is just fine.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(ARG_TYPE_ANY_1, OPTIONAL));
  arguments.push_back(FunctionArgumentType(ARG_TYPE_RELATION, OPTIONAL));
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
  arguments.push_back(FunctionArgumentType(ARG_TYPE_RELATION, OPTIONAL));
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
  arguments.push_back(FunctionArgumentType(ARG_TYPE_RELATION, OPTIONAL));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType(factory.get_int64()), arguments,
      /*context_id=*/-1);
  GOOGLESQL_EXPECT_OK(signature->IsValid(ProductMode::PRODUCT_EXTERNAL));
  GOOGLESQL_EXPECT_OK(signature->init_status());
  EXPECT_EQ(signature->last_named_arg_index(), -1);
  EXPECT_EQ(signature->last_arg_index_with_default(), -1);
  arguments.push_back(FunctionArgumentType(ARG_TYPE_ANY_1));
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
  arguments.push_back(FunctionArgumentType(ARG_TYPE_RELATION, REPEATED));
  arguments.push_back(FunctionArgumentType(ARG_TYPE_RELATION, REPEATED));
  EXPECT_DEBUG_DEATH(
      signature.reset(
          new FunctionSignature(FunctionArgumentType(factory.get_int64()),
                                arguments, /*context_id=*/-1)),
      "Optional arguments must be at the end of the argument list");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  // Optional STRUCT before named param is allowed.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(ARG_STRUCT_ANY, OPTIONAL));
  arguments.push_back(FunctionArgumentType(
      ARG_TYPE_ARBITRARY, FunctionArgumentTypeOptions()
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
  arguments.push_back(FunctionArgumentType(ARG_STRUCT_ANY, REPEATED));
  arguments.push_back(FunctionArgumentType(ARG_TYPE_ARBITRARY, REPEATED));
  arguments.push_back(FunctionArgumentType(
      ARG_TYPE_ARBITRARY, FunctionArgumentTypeOptions()
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
  arguments.push_back(FunctionArgumentType(ARG_TYPE_RELATION, OPTIONAL));
  arguments.push_back(FunctionArgumentType(
      ARG_TYPE_ARBITRARY, FunctionArgumentTypeOptions()
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
  arguments.push_back(FunctionArgumentType(ARG_TYPE_ARBITRARY, OPTIONAL));
  arguments.push_back(FunctionArgumentType(
      ARG_TYPE_RELATION, FunctionArgumentTypeOptions()
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
  arguments.push_back(FunctionArgumentType(ARG_TYPE_RELATION));
  arguments.push_back(FunctionArgumentType(
      ARG_TYPE_RELATION, FunctionArgumentTypeOptions()
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
  arguments.push_back(FunctionArgumentType(ARG_TYPE_RELATION, OPTIONAL));
  arguments.push_back(FunctionArgumentType(
      ARG_TYPE_RELATION, FunctionArgumentTypeOptions()
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
      ARG_TYPE_RELATION, FunctionArgumentTypeOptions()
                             .set_argument_name("foobar", kPositionalOrNamed)
                             .set_cardinality(OPTIONAL)));
  arguments.push_back(FunctionArgumentType(
      ARG_TYPE_RELATION, FunctionArgumentTypeOptions()
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
  arguments.push_back(FunctionArgumentType(ARG_TYPE_ARBITRARY));
  arguments.push_back(FunctionArgumentType(ARG_TYPE_MODEL));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType::AnyRelation(), arguments, /*context_id=*/-1);

  GOOGLESQL_EXPECT_OK(signature->IsValidForTableValuedFunction());
  GOOGLESQL_EXPECT_OK(signature->init_status());
  EXPECT_EQ(signature->last_named_arg_index(), -1);
  EXPECT_EQ(signature->last_arg_index_with_default(), -1);

  // Optional Models are fine, regardless of position.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(ARG_TYPE_ARBITRARY));
  arguments.push_back(FunctionArgumentType(ARG_TYPE_MODEL, OPTIONAL));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType::AnyRelation(), arguments, /*context_id=*/-1);

  GOOGLESQL_EXPECT_OK(signature->IsValidForTableValuedFunction());
  GOOGLESQL_EXPECT_OK(signature->init_status());
  EXPECT_EQ(signature->last_named_arg_index(), -1);
  EXPECT_EQ(signature->last_arg_index_with_default(), -1);

  // Named optional RELATION is fine after MODEL.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(ARG_TYPE_ARBITRARY));
  arguments.push_back(FunctionArgumentType(ARG_TYPE_MODEL));
  arguments.push_back(FunctionArgumentType(
      ARG_TYPE_RELATION, FunctionArgumentTypeOptions()
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
  arguments.push_back(FunctionArgumentType(ARG_TYPE_MODEL, OPTIONAL));
  arguments.push_back(FunctionArgumentType(
      ARG_TYPE_RELATION, FunctionArgumentTypeOptions()
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
  arguments.push_back(FunctionArgumentType(ARG_TYPE_ARBITRARY, OPTIONAL));
  arguments.push_back(FunctionArgumentType(
      ARG_TYPE_RELATION, FunctionArgumentTypeOptions()
                             .set_argument_name("foobar", kNamedOnly)
                             .set_cardinality(OPTIONAL)));
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType::AnyRelation(), arguments, /*context_id=*/-1);

  GOOGLESQL_EXPECT_OK(signature->IsValidForTableValuedFunction());
  GOOGLESQL_EXPECT_OK(signature->init_status());
  EXPECT_EQ(signature->last_named_arg_index(), 1);
  EXPECT_EQ(signature->last_arg_index_with_default(), -1);

  arguments.clear();
  arguments.push_back(FunctionArgumentType(ARG_ARRAY_TYPE_ANY_1));
  arguments.push_back(
      FunctionArgumentType::Lambda({ARG_TYPE_ANY_1}, factory.get_bool()));
  // Templated function-type related to arguments.
  signature = std::make_unique<FunctionSignature>(
      FunctionArgumentType(ARG_TYPE_ANY_1), arguments, /*context_id=*/-1);

  // Templated function-type not related.
  EXPECT_DEBUG_DEATH(
      signature.reset(new FunctionSignature(
          FunctionArgumentType(ARG_TYPE_ANY_2), arguments, /*context_id=*/-1)),
      "Result type template must match an argument type template");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  // Templated argument of function-type not related to previous arguments.
  arguments.clear();
  arguments.push_back(FunctionArgumentType(ARG_ARRAY_TYPE_ANY_1));
  arguments.push_back(
      FunctionArgumentType::Lambda({ARG_TYPE_ANY_2}, factory.get_bool()));
  EXPECT_DEBUG_DEATH(
      signature.reset(new FunctionSignature(
          FunctionArgumentType(ARG_ARRAY_TYPE_ANY_1), arguments,
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
  arguments.push_back(FunctionArgumentType(ARG_TYPE_VOID));
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

  arguments.emplace_back(ARG_ARRAY_TYPE_ANY_1);
  // Not supported arg type for function-type arguments.
  arguments.emplace_back(
      FunctionArgumentType::Lambda({ARG_TYPE_ARBITRARY}, factory.get_bool()));
  EXPECT_DEBUG_DEATH(signature.reset(new FunctionSignature(
                         FunctionArgumentType(factory.get_int64()), arguments,
                         /*context_id=*/-1)),
                     "Argument kind not supported");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  arguments.clear();
  arguments.emplace_back(ARG_ARRAY_TYPE_ANY_1);
  // Not supported arg type for function-type body.
  arguments.emplace_back(
      FunctionArgumentType::Lambda({factory.get_bool()}, ARG_TYPE_ARBITRARY));
  EXPECT_DEBUG_DEATH(signature.reset(new FunctionSignature(
                         FunctionArgumentType(factory.get_int64()), arguments,
                         /*context_id=*/-1)),
                     "Argument kind not supported");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  // ARG_ARRAY_TYPE_ANY_1 not supported as function-type argument.
  arguments.clear();
  arguments.emplace_back(ARG_ARRAY_TYPE_ANY_1);
  // Not supported REPEATED options for function-type argument return type.
  arguments.emplace_back(
      FunctionArgumentType::Lambda({ARG_ARRAY_TYPE_ANY_1}, ARG_TYPE_ANY_1));
  EXPECT_DEBUG_DEATH(signature.reset(new FunctionSignature(
                         FunctionArgumentType(factory.get_int64()), arguments,
                         /*context_id=*/-1)),
                     "Argument kind not supported by function-type argument");
  if (!GOOGLESQL_DEBUG_MODE) {
    EXPECT_FALSE(signature->IsValid(ProductMode::PRODUCT_EXTERNAL).ok());
    EXPECT_FALSE(signature->init_status().ok());
  }

  // ARG_ARRAY_TYPE_ANY_2 not supported as function-type argument.
  arguments.clear();
  arguments.emplace_back(ARG_ARRAY_TYPE_ANY_1);
  // Not supported REPEATED options for function-type body.
  arguments.emplace_back(
      FunctionArgumentType::Lambda({ARG_ARRAY_TYPE_ANY_2}, ARG_TYPE_ANY_1));
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
  arguments.emplace_back(ARG_ARRAY_TYPE_ANY_1);
  // Not supported REPEATED options for function-type arguments.
  arguments.emplace_back(FunctionArgumentType::Lambda(
      {FunctionArgumentType(ARG_TYPE_ANY_1, REPEATED)}, factory.get_bool()));
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
  arguments.emplace_back(ARG_ARRAY_TYPE_ANY_1);
  // Not supported OPTIONAL options for function-type arguments.
  arguments.emplace_back(FunctionArgumentType::Lambda(
      {FunctionArgumentType(ARG_TYPE_ANY_1, OPTIONAL)}, factory.get_bool()));
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
  arguments.emplace_back(ARG_ARRAY_TYPE_ANY_1);
  // Not supported REPEATED options for function-type body.
  arguments.emplace_back(FunctionArgumentType::Lambda(
      {FunctionArgumentType(factory.get_bool())},
      FunctionArgumentType(ARG_TYPE_ANY_1, REPEATED)));
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
      signature.reset(new FunctionSignature(
          ARG_TYPE_ANY_1,
          {ARG_TYPE_ANY_1,
           FunctionArgumentType::Lambda({ARG_TYPE_ANY_1}, factory.get_bool()),
           ARG_TYPE_ANY_1},
          /*context_id=*/-1)),
      "Templated argument kind used by function-type argument cannot be used "
      "by "
      "arguments to the right of the function-type using it");
}

TEST(FunctionArgumentTypeTests, TestTemplatedKindIsRelated) {
  TypeFactory type_factory;
  FunctionArgumentType arg_type_fixed(type_factory.get_int32());
  FunctionArgumentType arg_type_any_1(ARG_TYPE_ANY_1);
  FunctionArgumentType arg_type_any_2(ARG_TYPE_ANY_2);
  FunctionArgumentType arg_array_type_any_1(ARG_ARRAY_TYPE_ANY_1);
  FunctionArgumentType arg_array_type_any_2(ARG_ARRAY_TYPE_ANY_2);
  FunctionArgumentType arg_proto_any(ARG_PROTO_ANY);
  FunctionArgumentType arg_struct_any(ARG_STRUCT_ANY);
  FunctionArgumentType arg_enum_any(ARG_ENUM_ANY);
  FunctionArgumentType arg_type_any_1_lambda =
      FunctionArgumentType::Lambda({arg_type_any_1}, arg_type_any_1);
  FunctionArgumentType arg_type_any_2_lambda =
      FunctionArgumentType::Lambda({arg_type_any_2}, arg_type_any_2);
  FunctionArgumentType arg_array_type_any_2_lambda =
      FunctionArgumentType::Lambda({type_factory.get_int64()},
                                   ARG_ARRAY_TYPE_ANY_2);
  FunctionArgumentType arg_type_arbitrary(ARG_TYPE_ARBITRARY);

  EXPECT_FALSE(arg_type_arbitrary.TemplatedKindIsRelated(ARG_TYPE_ARBITRARY));

  EXPECT_FALSE(arg_type_fixed.TemplatedKindIsRelated(ARG_TYPE_FIXED));
  EXPECT_FALSE(arg_type_fixed.TemplatedKindIsRelated(ARG_TYPE_ANY_1));
  EXPECT_FALSE(arg_type_fixed.TemplatedKindIsRelated(ARG_ARRAY_TYPE_ANY_1));
  EXPECT_FALSE(arg_type_fixed.TemplatedKindIsRelated(ARG_PROTO_ANY));
  EXPECT_FALSE(arg_type_fixed.TemplatedKindIsRelated(ARG_STRUCT_ANY));
  EXPECT_FALSE(arg_type_fixed.TemplatedKindIsRelated(ARG_ENUM_ANY));
  EXPECT_FALSE(arg_type_fixed.TemplatedKindIsRelated(ARG_TYPE_ARBITRARY));

  EXPECT_FALSE(arg_type_any_1.TemplatedKindIsRelated(ARG_TYPE_FIXED));
  EXPECT_TRUE(arg_type_any_1.TemplatedKindIsRelated(ARG_TYPE_ANY_1));
  EXPECT_TRUE(arg_type_any_1.TemplatedKindIsRelated(ARG_ARRAY_TYPE_ANY_1));
  EXPECT_FALSE(arg_type_any_1.TemplatedKindIsRelated(ARG_TYPE_ANY_2));
  EXPECT_FALSE(arg_type_any_1.TemplatedKindIsRelated(ARG_ARRAY_TYPE_ANY_2));
  EXPECT_FALSE(arg_type_any_1.TemplatedKindIsRelated(ARG_PROTO_ANY));
  EXPECT_FALSE(arg_type_any_1.TemplatedKindIsRelated(ARG_STRUCT_ANY));
  EXPECT_FALSE(arg_type_any_1.TemplatedKindIsRelated(ARG_ENUM_ANY));
  EXPECT_FALSE(arg_type_any_1.TemplatedKindIsRelated(ARG_TYPE_ARBITRARY));

  // arg_type_any_1_lambda is has the same behavior as arg_type_any_1
  EXPECT_FALSE(arg_type_any_1_lambda.TemplatedKindIsRelated(ARG_TYPE_FIXED));
  EXPECT_TRUE(arg_type_any_1_lambda.TemplatedKindIsRelated(ARG_TYPE_ANY_1));
  EXPECT_TRUE(
      arg_type_any_1_lambda.TemplatedKindIsRelated(ARG_ARRAY_TYPE_ANY_1));
  EXPECT_FALSE(arg_type_any_1_lambda.TemplatedKindIsRelated(ARG_TYPE_ANY_2));
  EXPECT_FALSE(
      arg_type_any_1_lambda.TemplatedKindIsRelated(ARG_ARRAY_TYPE_ANY_2));
  EXPECT_FALSE(arg_type_any_1_lambda.TemplatedKindIsRelated(ARG_PROTO_ANY));
  EXPECT_FALSE(arg_type_any_1_lambda.TemplatedKindIsRelated(ARG_STRUCT_ANY));
  EXPECT_FALSE(arg_type_any_1_lambda.TemplatedKindIsRelated(ARG_ENUM_ANY));
  EXPECT_FALSE(
      arg_type_any_1_lambda.TemplatedKindIsRelated(ARG_TYPE_ARBITRARY));

  EXPECT_FALSE(arg_array_type_any_1.TemplatedKindIsRelated(ARG_TYPE_FIXED));
  EXPECT_TRUE(arg_array_type_any_1.TemplatedKindIsRelated(ARG_TYPE_ANY_1));
  EXPECT_TRUE(
      arg_array_type_any_1.TemplatedKindIsRelated(ARG_ARRAY_TYPE_ANY_1));
  EXPECT_FALSE(arg_array_type_any_1.TemplatedKindIsRelated(ARG_TYPE_ANY_2));
  EXPECT_FALSE(
      arg_array_type_any_1.TemplatedKindIsRelated(ARG_ARRAY_TYPE_ANY_2));
  EXPECT_FALSE(arg_array_type_any_1.TemplatedKindIsRelated(ARG_PROTO_ANY));
  EXPECT_FALSE(arg_array_type_any_1.TemplatedKindIsRelated(ARG_STRUCT_ANY));
  EXPECT_FALSE(arg_array_type_any_1.TemplatedKindIsRelated(ARG_ENUM_ANY));
  EXPECT_FALSE(arg_array_type_any_1.TemplatedKindIsRelated(ARG_TYPE_ARBITRARY));

  EXPECT_FALSE(arg_type_any_2.TemplatedKindIsRelated(ARG_TYPE_FIXED));
  EXPECT_FALSE(arg_type_any_2.TemplatedKindIsRelated(ARG_TYPE_ANY_1));
  EXPECT_FALSE(arg_type_any_2.TemplatedKindIsRelated(ARG_ARRAY_TYPE_ANY_1));
  EXPECT_TRUE(arg_type_any_2.TemplatedKindIsRelated(ARG_TYPE_ANY_2));
  EXPECT_TRUE(arg_type_any_2.TemplatedKindIsRelated(ARG_ARRAY_TYPE_ANY_2));
  EXPECT_FALSE(arg_type_any_2.TemplatedKindIsRelated(ARG_PROTO_ANY));
  EXPECT_FALSE(arg_type_any_2.TemplatedKindIsRelated(ARG_STRUCT_ANY));
  EXPECT_FALSE(arg_type_any_2.TemplatedKindIsRelated(ARG_ENUM_ANY));
  EXPECT_FALSE(arg_type_any_2.TemplatedKindIsRelated(ARG_TYPE_ARBITRARY));

  // arg_type_any_2_lambda is has the same behavior as arg_type_any_2
  EXPECT_FALSE(arg_type_any_2_lambda.TemplatedKindIsRelated(ARG_TYPE_FIXED));
  EXPECT_FALSE(arg_type_any_2_lambda.TemplatedKindIsRelated(ARG_TYPE_ANY_1));
  EXPECT_FALSE(
      arg_type_any_2_lambda.TemplatedKindIsRelated(ARG_ARRAY_TYPE_ANY_1));
  EXPECT_TRUE(arg_type_any_2_lambda.TemplatedKindIsRelated(ARG_TYPE_ANY_2));
  EXPECT_TRUE(
      arg_type_any_2_lambda.TemplatedKindIsRelated(ARG_ARRAY_TYPE_ANY_2));
  EXPECT_FALSE(arg_type_any_2_lambda.TemplatedKindIsRelated(ARG_PROTO_ANY));
  EXPECT_FALSE(arg_type_any_2_lambda.TemplatedKindIsRelated(ARG_STRUCT_ANY));
  EXPECT_FALSE(arg_type_any_2_lambda.TemplatedKindIsRelated(ARG_ENUM_ANY));
  EXPECT_FALSE(
      arg_type_any_2_lambda.TemplatedKindIsRelated(ARG_TYPE_ARBITRARY));

  EXPECT_FALSE(arg_array_type_any_2.TemplatedKindIsRelated(ARG_TYPE_FIXED));
  EXPECT_FALSE(arg_array_type_any_2.TemplatedKindIsRelated(ARG_TYPE_ANY_1));
  EXPECT_FALSE(
      arg_array_type_any_2.TemplatedKindIsRelated(ARG_ARRAY_TYPE_ANY_1));
  EXPECT_TRUE(arg_array_type_any_2.TemplatedKindIsRelated(ARG_TYPE_ANY_2));
  EXPECT_TRUE(
      arg_array_type_any_2.TemplatedKindIsRelated(ARG_ARRAY_TYPE_ANY_2));
  EXPECT_FALSE(arg_array_type_any_2.TemplatedKindIsRelated(ARG_PROTO_ANY));
  EXPECT_FALSE(arg_array_type_any_2.TemplatedKindIsRelated(ARG_STRUCT_ANY));
  EXPECT_FALSE(arg_array_type_any_2.TemplatedKindIsRelated(ARG_ENUM_ANY));
  EXPECT_FALSE(arg_array_type_any_2.TemplatedKindIsRelated(ARG_TYPE_ARBITRARY));

  // arg_array_type_any_2_lambda is has the same behavior as
  // arg_array_type_any_2
  EXPECT_FALSE(
      arg_array_type_any_2_lambda.TemplatedKindIsRelated(ARG_TYPE_FIXED));
  EXPECT_FALSE(
      arg_array_type_any_2_lambda.TemplatedKindIsRelated(ARG_TYPE_ANY_1));
  EXPECT_FALSE(
      arg_array_type_any_2_lambda.TemplatedKindIsRelated(ARG_ARRAY_TYPE_ANY_1));
  EXPECT_TRUE(
      arg_array_type_any_2_lambda.TemplatedKindIsRelated(ARG_TYPE_ANY_2));
  EXPECT_TRUE(
      arg_array_type_any_2_lambda.TemplatedKindIsRelated(ARG_ARRAY_TYPE_ANY_2));
  EXPECT_FALSE(
      arg_array_type_any_2_lambda.TemplatedKindIsRelated(ARG_PROTO_ANY));
  EXPECT_FALSE(
      arg_array_type_any_2_lambda.TemplatedKindIsRelated(ARG_STRUCT_ANY));
  EXPECT_FALSE(
      arg_array_type_any_2_lambda.TemplatedKindIsRelated(ARG_ENUM_ANY));
  EXPECT_FALSE(
      arg_array_type_any_2_lambda.TemplatedKindIsRelated(ARG_TYPE_ARBITRARY));

  EXPECT_FALSE(arg_enum_any.TemplatedKindIsRelated(ARG_TYPE_FIXED));
  EXPECT_FALSE(arg_enum_any.TemplatedKindIsRelated(ARG_TYPE_ANY_1));
  EXPECT_FALSE(arg_enum_any.TemplatedKindIsRelated(ARG_ARRAY_TYPE_ANY_1));
  EXPECT_FALSE(arg_enum_any.TemplatedKindIsRelated(ARG_TYPE_ANY_2));
  EXPECT_FALSE(arg_enum_any.TemplatedKindIsRelated(ARG_ARRAY_TYPE_ANY_2));
  EXPECT_FALSE(arg_enum_any.TemplatedKindIsRelated(ARG_PROTO_ANY));
  EXPECT_FALSE(arg_enum_any.TemplatedKindIsRelated(ARG_STRUCT_ANY));
  EXPECT_TRUE(arg_enum_any.TemplatedKindIsRelated(ARG_ENUM_ANY));
  EXPECT_FALSE(arg_enum_any.TemplatedKindIsRelated(ARG_TYPE_ARBITRARY));
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
  templated_types.push_back(FunctionArgumentType(ARG_TYPE_ANY_1));
  templated_types.push_back(FunctionArgumentType(ARG_TYPE_ANY_2));
  templated_types.push_back(FunctionArgumentType(ARG_ARRAY_TYPE_ANY_1));
  templated_types.push_back(FunctionArgumentType(ARG_ARRAY_TYPE_ANY_2));
  templated_types.push_back(FunctionArgumentType(ARG_PROTO_ANY));
  templated_types.push_back(FunctionArgumentType(ARG_STRUCT_ANY));
  templated_types.push_back(FunctionArgumentType(ARG_ENUM_ANY));
  templated_types.push_back(FunctionArgumentType(ARG_TYPE_ARBITRARY));
  templated_types.push_back(FunctionArgumentType(ARG_TYPE_RELATION));
  templated_types.push_back(FunctionArgumentType(ARG_TYPE_MODEL));
  templated_types.push_back(FunctionArgumentType(ARG_TYPE_CONNECTION));
  templated_types.push_back(
      FunctionArgumentType::Lambda({ARG_TYPE_ANY_1}, ARG_TYPE_ANY_2));
  templated_types.push_back(
      FunctionArgumentType::Lambda({ARG_TYPE_ANY_1}, factory->get_bool()));
  templated_types.push_back(
      FunctionArgumentType::Lambda({factory->get_int64()}, ARG_TYPE_ANY_1));
  return templated_types;
}

static std::vector<FunctionArgumentType> GetNonTemplatedArgumentTypes(
    TypeFactory* factory) {
  std::vector<FunctionArgumentType> non_templated_types;
  non_templated_types.push_back(FunctionArgumentType(ARG_TYPE_VOID));
  // A few examples of ARG_TYPE_FIXED
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
  ASSERT_EQ(33, enum_size);

  std::set<SignatureArgumentKind> templated_kinds;
  templated_kinds.insert(ARG_TYPE_ANY_1);
  templated_kinds.insert(ARG_TYPE_ANY_2);
  templated_kinds.insert(ARG_TYPE_ANY_3);
  templated_kinds.insert(ARG_TYPE_ANY_4);
  templated_kinds.insert(ARG_TYPE_ANY_5);
  templated_kinds.insert(ARG_ARRAY_TYPE_ANY_1);
  templated_kinds.insert(ARG_ARRAY_TYPE_ANY_2);
  templated_kinds.insert(ARG_ARRAY_TYPE_ANY_3);
  templated_kinds.insert(ARG_ARRAY_TYPE_ANY_4);
  templated_kinds.insert(ARG_ARRAY_TYPE_ANY_5);
  templated_kinds.insert(ARG_PROTO_MAP_ANY);
  templated_kinds.insert(ARG_PROTO_MAP_KEY_ANY);
  templated_kinds.insert(ARG_PROTO_MAP_VALUE_ANY);
  templated_kinds.insert(ARG_MAP_TYPE_ANY_1_2);
  templated_kinds.insert(ARG_PROTO_ANY);
  templated_kinds.insert(ARG_STRUCT_ANY);
  templated_kinds.insert(ARG_ENUM_ANY);
  templated_kinds.insert(ARG_TYPE_ARBITRARY);
  templated_kinds.insert(ARG_TYPE_RELATION);
  templated_kinds.insert(ARG_TYPE_MODEL);
  templated_kinds.insert(ARG_TYPE_CONNECTION);
  templated_kinds.insert(ARG_TYPE_DESCRIPTOR);
  templated_kinds.insert(ARG_TYPE_LAMBDA);
  templated_kinds.insert(ARG_RANGE_TYPE_ANY_1);
  templated_kinds.insert(ARG_TYPE_GRAPH_NODE);
  templated_kinds.insert(ARG_TYPE_GRAPH_EDGE);
  templated_kinds.insert(ARG_TYPE_GRAPH_PATH);
  templated_kinds.insert(ARG_TYPE_SEQUENCE);
  templated_kinds.insert(ARG_MEASURE_TYPE_ANY_1);

  std::set<SignatureArgumentKind> non_templated_kinds;
  non_templated_kinds.insert(ARG_TYPE_FIXED);
  non_templated_kinds.insert(ARG_TYPE_VOID);
  templated_kinds.insert(ARG_TYPE_GRAPH);

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
      types::Int64Type(), {{ARG_TYPE_ANY_1, /*num_occurrences=*/1}},
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
}

TEST(FunctionSignatureTests, TestFunctionArgumentTypeOptionsConstraint) {
  FunctionSignature orderable_element_signature(
      FunctionArgumentType(ARG_TYPE_ANY_1),
      {{ARG_TYPE_ANY_1,
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
      FunctionArgumentType(ARG_TYPE_ANY_1),
      {{ARG_TYPE_ANY_1,
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
      FunctionArgumentType(ARG_TYPE_ANY_1),
      {{ARG_TYPE_ANY_1,
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
  SignatureArgumentKind arg_kind = ARG_TYPE_VOID;
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
      ARG_ARRAY_TYPE_ANY_1,
      FunctionArgumentTypeOptions().set_array_element_must_support_ordering());
  EXPECT_TRUE(
      orderable_array_arg.options().array_element_must_support_ordering());
  EXPECT_FALSE(
      orderable_array_arg.options().array_element_must_support_grouping());
  EXPECT_FALSE(
      orderable_array_arg.options().array_element_must_support_equality());
  TestArgumentTypeOptionsSerialization(orderable_array_arg);

  FunctionArgumentType groupable_array_arg(
      ARG_ARRAY_TYPE_ANY_1,
      FunctionArgumentTypeOptions().set_array_element_must_support_grouping());
  EXPECT_FALSE(
      groupable_array_arg.options().array_element_must_support_ordering());
  EXPECT_TRUE(
      groupable_array_arg.options().array_element_must_support_grouping());
  EXPECT_FALSE(
      groupable_array_arg.options().array_element_must_support_equality());
  TestArgumentTypeOptionsSerialization(groupable_array_arg);

  FunctionArgumentType equatable_array_arg(
      ARG_ARRAY_TYPE_ANY_1,
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
  FunctionArgumentType node_arg(ARG_TYPE_GRAPH_NODE),
      edge_arg(ARG_TYPE_GRAPH_EDGE), element_arg(ARG_TYPE_GRAPH_ELEMENT),
      path_arg(ARG_TYPE_GRAPH_PATH);
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
  FunctionArgumentType lambda(ARG_TYPE_LAMBDA);
  EXPECT_THAT(
      lambda.IsValid(PRODUCT_INTERNAL),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("constructed directly")));
}

TEST(FunctionSignatureTests, SignatureSupportsArgumentAlias) {
  FunctionSignature support_alias(
      FunctionArgumentType(ARG_TYPE_ANY_1),
      {{ARG_TYPE_ANY_1, FunctionArgumentTypeOptions().set_argument_alias_kind(
                            FunctionEnums::ARGUMENT_ALIASED)},
       {ARG_TYPE_ANY_1, FunctionArgumentTypeOptions().set_argument_alias_kind(
                            FunctionEnums::ARGUMENT_NON_ALIASED)}},
      /*context_id=*/-1);
  EXPECT_TRUE(SignatureSupportsArgumentAliases(support_alias));

  FunctionSignature unsupport_alias(
      FunctionArgumentType(ARG_TYPE_ANY_1),
      {{ARG_TYPE_ANY_1, FunctionArgumentTypeOptions().set_argument_alias_kind(
                            FunctionEnums::ARGUMENT_NON_ALIASED)},
       {ARG_TYPE_ANY_1, FunctionArgumentTypeOptions().set_argument_alias_kind(
                            FunctionEnums::ARGUMENT_NON_ALIASED)}},
      /*context_id=*/-1);
  EXPECT_FALSE(SignatureSupportsArgumentAliases(unsupport_alias));
}

TEST(FunctionSignatureTests, SetConcreteResultTypePreservesArgumentOptions) {
  FunctionSignature result_arg_has_options(
      FunctionArgumentType(
          ARG_TYPE_ANY_1,
          FunctionArgumentTypeOptions().set_uses_array_element_for_collation()),
      {ARG_TYPE_ANY_1, ARG_TYPE_ANY_1},
      /*context_id=*/-1);
  result_arg_has_options.SetConcreteResultType(
      types::Int64Type(), SignatureArgumentKind::ARG_TYPE_FIXED);
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
        {.result_type = {ARG_ARRAY_TYPE_ANY_1}, .arguments = {}},
        {.result_type = {ARG_ARRAY_TYPE_ANY_1}, .arguments = {ARG_TYPE_ANY_2}},
        {.result_type = {ARG_TYPE_ANY_2}, .arguments = {ARG_ARRAY_TYPE_ANY_1}},
        // Templated range type should fail if corresponding ANY type is not
        // present.
        {.result_type = {ARG_RANGE_TYPE_ANY_1}, .arguments = {}},
        {.result_type = {ARG_RANGE_TYPE_ANY_1}, .arguments = {ARG_TYPE_ANY_2}},
        {.result_type = {ARG_TYPE_ANY_2}, .arguments = {ARG_RANGE_TYPE_ANY_1}},
        // Templated measure type should fail if corresponding ANY type is not
        // present.
        {.result_type = {ARG_MEASURE_TYPE_ANY_1}, .arguments = {}},
        {.result_type = {ARG_MEASURE_TYPE_ANY_1},
         .arguments = {ARG_TYPE_ANY_2}},
        {.result_type = {ARG_TYPE_ANY_2},
         .arguments = {ARG_MEASURE_TYPE_ANY_1}},
        // Templated map result type should fail if key and value are not both
        // present.
        {.result_type = {ARG_MAP_TYPE_ANY_1_2}, .arguments = {}},
        {.result_type = {ARG_MAP_TYPE_ANY_1_2}, .arguments = {ARG_TYPE_ANY_1}},
        {.result_type = {ARG_MAP_TYPE_ANY_1_2}, .arguments = {ARG_TYPE_ANY_2}},
        {.result_type = {ARG_MAP_TYPE_ANY_1_2},
         .arguments = {ARG_TYPE_ANY_1, ARG_TYPE_ANY_3}},
        {.result_type = {ARG_MAP_TYPE_ANY_1_2},
         .arguments = {ARG_TYPE_ANY_1, ARG_ARRAY_TYPE_ANY_1}},
        {.result_type = {ARG_MAP_TYPE_ANY_1_2},
         .arguments = {FunctionArgumentType::Lambda({ARG_TYPE_ANY_1},
                                                    ARG_TYPE_ANY_1)}},
        {.result_type = {ARG_MAP_TYPE_ANY_1_2},
         .arguments = {FunctionArgumentType::Lambda({ARG_TYPE_ANY_2},
                                                    ARG_TYPE_ANY_2)}},
        // Templated proto map type should fail if key and value are not
        // present.
        {.result_type = {ARG_PROTO_MAP_ANY}, .arguments = {}},

        // These probably should fail, but don't currently. Since
        // ARG_PROTO_MAP_ANY is not used as a return type anywhere in our
        // catalog, nor do we expect it to be in the future, this is low
        // priority.
        //
        // {.result_type = {ARG_PROTO_MAP_ANY},
        //  .arguments = {ARG_PROTO_MAP_KEY_ANY}},
        // {.result_type = {ARG_PROTO_MAP_ANY},
        //  .arguments = {ARG_PROTO_MAP_VALUE_ANY}},
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
        {.result_type = {ARG_MAP_TYPE_ANY_1_2},
         .arguments = {ARG_TYPE_ANY_1, ARG_TYPE_ANY_2}},
        {.result_type = {ARG_MAP_TYPE_ANY_1_2},
         .arguments = {ARG_ARRAY_TYPE_ANY_1, ARG_TYPE_ANY_2}},
        {.result_type = {ARG_MAP_TYPE_ANY_1_2},
         .arguments = {ARG_ARRAY_TYPE_ANY_1, ARG_ARRAY_TYPE_ANY_2}},
        {.result_type = {ARG_MAP_TYPE_ANY_1_2},
         .arguments = {ARG_RANGE_TYPE_ANY_1, ARG_TYPE_ANY_2}},
        {.result_type = {ARG_MAP_TYPE_ANY_1_2},
         .arguments = {ARG_RANGE_TYPE_ANY_1, ARG_ARRAY_TYPE_ANY_2}},
        {.result_type = {ARG_MAP_TYPE_ANY_1_2},
         .arguments = {FunctionArgumentType::Lambda({ARG_TYPE_ANY_1},
                                                    ARG_TYPE_ANY_2)}},
        {.result_type = {types::BoolType()},
         .arguments = {ARG_MAP_TYPE_ANY_1_2, ARG_TYPE_ANY_2,
                       FunctionArgumentType::Lambda({ARG_TYPE_ANY_1},
                                                    ARG_TYPE_ANY_1)}},
    }));

}  // namespace googlesql
