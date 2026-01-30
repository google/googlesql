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

#include "googlesql/public/function.h"

#include <cstddef>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/common/function_signature_testutil.h"
#include "googlesql/common/testing/proto_matchers.h"  
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/common/testing/testing_proto_util.h"
#include "googlesql/proto/function.pb.h"
#include "googlesql/public/builtin_function.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/deprecation_warning.pb.h"
#include "googlesql/public/error_location.pb.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/parse_location_range.pb.h"
#include "googlesql/public/sql_function.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/type_deserializer.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/case.h"

// Note - test coverage for the 'Function' class interface is primarily
// provided by builtin_function_test.cc which instantiates the concrete
// subclass BuiltinFunction for testing.

namespace googlesql {
using ::googlesql::testing::EqualsProto;
using ::testing::ElementsAre;
using ::testing::IsNull;
using ::testing::NotNull;

class TestSQLFunction : public SQLFunctionInterface {
 public:
  TestSQLFunction()
      : SQLFunctionInterface("test_function", "test_group", Function::SCALAR,
                             /*function_signatures=*/{}, FunctionOptions()) {}
  const ResolvedExpr* FunctionExpression() const override { return nullptr; }
  std::vector<std::string> GetArgumentNames() const override {
    return {"a", "b", "c"};
  }
  const std::vector<std::unique_ptr<const ResolvedComputedColumn>>*
  aggregate_expression_list() const override {
    return nullptr;
  }
};

TEST(SimpleFunctionTests, FunctionMethodTests) {
  // Basic tests for 'Function' class methods.
  TypeFactory type_factory;
  const Type* int64_type = type_factory.get_int64();
  const Type* int32_type = type_factory.get_int32();

  Function fn("test_function_name", Function::kGoogleSQLFunctionGroupName,
              Function::SCALAR);
  EXPECT_EQ(0, fn.NumSignatures());
  EXPECT_EQ("GoogleSQL:test_function_name", fn.DebugString(true /* verbose */));

  EXPECT_EQ(Function::SCALAR, fn.mode());
  EXPECT_TRUE(fn.IsScalar());
  EXPECT_FALSE(fn.IsAggregate());
  EXPECT_FALSE(fn.IsAnalytic());

  fn.AddSignatureOrDie(TYPE_STRING, {TYPE_BYTES}, nullptr, &type_factory);
  EXPECT_EQ(1, fn.NumSignatures());
  EXPECT_EQ("GoogleSQL:test_function_name\n  (BYTES) -> STRING",
            fn.DebugString(true /* verbose */));

  FunctionSignature simple_signature({int64_type, {int32_type}, -1});
  fn.AddSignature(simple_signature);
  EXPECT_EQ(2, fn.NumSignatures());
  EXPECT_EQ(
      "GoogleSQL:test_function_name\n  "
      "(BYTES) -> STRING\n  (INT32) -> INT64",
      fn.DebugString(true /* verbose */));

  const FunctionSignature* signature = fn.GetSignature(1);
  ASSERT_THAT(signature, NotNull());
  EXPECT_EQ(simple_signature.DebugString(), signature->DebugString())
      << "expected signature: " << simple_signature.DebugString()
      << "\nactual signature: " << signature->DebugString();

  signature = fn.GetSignature(2);
  EXPECT_THAT(signature, IsNull());

  int num_signatures;
  EXPECT_EQ("TEST_FUNCTION_NAME", fn.SQLName());
  EXPECT_EQ("TEST_FUNCTION_NAME(BYTES); TEST_FUNCTION_NAME(INT32)",
            fn.GetSupportedSignaturesUserFacingText(
                LanguageOptions(),
                FunctionArgumentType::NamePrintingStyle::kIfNamedOnly,
                &num_signatures));
  EXPECT_EQ(2, num_signatures);

  Function fn2("test_Function_NAME", Function::kGoogleSQLFunctionGroupName,
               Function::SCALAR,
               FunctionOptions().set_uses_upper_case_sql_name(false));
  fn2.AddSignatureOrDie(TYPE_STRING, {TYPE_BYTES}, nullptr, &type_factory);
  EXPECT_EQ("test_Function_NAME", fn2.SQLName());
  EXPECT_EQ("test_Function_NAME(BYTES)",
            fn2.GetSupportedSignaturesUserFacingText(
                LanguageOptions(),
                FunctionArgumentType::NamePrintingStyle::kIfNamedOnly,
                &num_signatures));
  EXPECT_EQ(1, num_signatures);

  std::vector<FunctionSignature> no_signatures;
  fn.ResetSignatures(no_signatures);
  EXPECT_EQ(0, fn.NumSignatures());

  // TYPE_PROTO is invalid in this context.
  EXPECT_FALSE(fn.AddSignature(TYPE_PROTO, {}, nullptr, &type_factory).ok());

  // Test for Is<>() and GetAs<>()
  EXPECT_TRUE(fn.Is<Function>());
  EXPECT_EQ(&fn, fn.GetAs<Function>());

  TestSQLFunction sql_fn;
  EXPECT_TRUE(sql_fn.Is<Function>());
  EXPECT_EQ(&sql_fn, sql_fn.GetAs<Function>());

  EXPECT_TRUE(sql_fn.Is<SQLFunctionInterface>());
  EXPECT_EQ(&sql_fn, sql_fn.GetAs<SQLFunctionInterface>());

  EXPECT_EQ("a,b,c", absl::StrJoin(sql_fn.GetArgumentNames(), ","));
}

TEST(SimpleFunctionTests, WindowSupportTests) {
  FunctionOptions with_window_support(
      FunctionOptions::ORDER_REQUIRED /* window_ordering_support */,
      true /* window_framing_support */);
  EXPECT_DEATH(Function("scalar_function_name",
                        Function::kGoogleSQLFunctionGroupName, Function::SCALAR,
                        {} /* function_signatures */, with_window_support),
               "Scalar functions cannot support OVER clause");

  FunctionOptions without_window_support;
  EXPECT_DEATH(
      Function("analytic_function_name", Function::kGoogleSQLFunctionGroupName,
               Function::ANALYTIC, {} /* function_signatures */,
               without_window_support),
      "Analytic functions must support OVER clause");

  EXPECT_DEATH(
      Function("analytic_function_name", Function::kGoogleSQLFunctionGroupName,
               Function::ANALYTIC),
      "Analytic functions must support OVER clause");

  Function aggregate_function("aggregate_function_name",
                              Function::kGoogleSQLFunctionGroupName,
                              Function::AGGREGATE);

  EXPECT_FALSE(aggregate_function.IsScalar());
  EXPECT_TRUE(aggregate_function.IsAggregate());
  EXPECT_FALSE(aggregate_function.IsAnalytic());

  EXPECT_FALSE(aggregate_function.SupportsOverClause());
  EXPECT_FALSE(aggregate_function.SupportsWindowOrdering());
  EXPECT_FALSE(aggregate_function.SupportsWindowFraming());
  EXPECT_FALSE(aggregate_function.RequiresWindowOrdering());

  Function aggregate_analytic_function(
      "aggregate_analytic_function_name", Function::kGoogleSQLFunctionGroupName,
      Function::AGGREGATE, {} /* function_signatures */, with_window_support);

  EXPECT_FALSE(aggregate_analytic_function.IsScalar());
  EXPECT_TRUE(aggregate_analytic_function.IsAggregate());
  EXPECT_FALSE(aggregate_analytic_function.IsAnalytic());

  EXPECT_TRUE(aggregate_analytic_function.SupportsOverClause());
  EXPECT_TRUE(aggregate_analytic_function.SupportsWindowOrdering());
  EXPECT_TRUE(aggregate_analytic_function.SupportsWindowFraming());
  EXPECT_TRUE(aggregate_analytic_function.RequiresWindowOrdering());

  Function analytic_function(
      "analytic_function", Function::kGoogleSQLFunctionGroupName,
      Function::ANALYTIC, {} /* function_signatures */,
      {FunctionOptions::ORDER_OPTIONAL /* window_ordering_support */,
       false /* window_framing_support */});

  EXPECT_FALSE(analytic_function.IsScalar());
  EXPECT_FALSE(analytic_function.IsAggregate());
  EXPECT_TRUE(analytic_function.IsAnalytic());

  EXPECT_TRUE(analytic_function.SupportsOverClause());
  EXPECT_TRUE(analytic_function.SupportsWindowOrdering());
  EXPECT_FALSE(analytic_function.SupportsWindowFraming());
  EXPECT_FALSE(analytic_function.RequiresWindowOrdering());
}

static void AddFunctionToSet(
    const absl::flat_hash_map<std::string, std::unique_ptr<Function>>&
        functions,
    absl::string_view name,
    absl::flat_hash_set<const Function*>& scoping_functions) {
  auto it = functions.find(name);
  ASSERT_TRUE(it != functions.end()) << "Function not found: " << name;
  scoping_functions.insert(it->second.get());
}

TEST(ConditionalEvaluationFunctionsTest,
     VerifyListOfBuiltinFunctionsScopeingSideEffects) {
  TypeFactory type_factory;
  LanguageOptions language_options;
  // Even functions that are "in_development" should serialize properly.
  language_options.EnableMaximumLanguageFeaturesForDevelopment();

  absl::flat_hash_map<std::string, std::unique_ptr<Function>> functions;
  absl::flat_hash_map<std::string, const Type*> types_ignored;
  GOOGLESQL_ASSERT_OK(
      GetBuiltinFunctionsAndTypes(BuiltinFunctionOptions(language_options),
                                  type_factory, functions, types_ignored));

  absl::flat_hash_set<const Function*> scoping_functions;
  AddFunctionToSet(functions, "if", scoping_functions);
  AddFunctionToSet(functions, "ifnull", scoping_functions);
  AddFunctionToSet(functions, "$case_with_value", scoping_functions);
  AddFunctionToSet(functions, "$case_no_value", scoping_functions);
  AddFunctionToSet(functions, "coalesce", scoping_functions);
  AddFunctionToSet(functions, "iferror", scoping_functions);
  AddFunctionToSet(functions, "iserror", scoping_functions);
  AddFunctionToSet(functions, "nulliferror", scoping_functions);

  EXPECT_EQ(scoping_functions.size(), 8);
  for (const auto& [_, function] : functions) {
    if (scoping_functions.contains(function)) {
      EXPECT_TRUE(function->MaySuppressSideEffects());
    } else {
      EXPECT_FALSE(function->MaySuppressSideEffects());
    }
  }
}

class AnyAndRelatedTypeSimpleFunctionTests
    : public ::testing::TestWithParam<SignatureArgumentKindGroup> {};

INSTANTIATE_TEST_SUITE_P(
    AnyAndArrayTypeSimpleFunction, AnyAndRelatedTypeSimpleFunctionTests,
    ::testing::ValuesIn(GetRelatedSignatureArgumentGroup()));

TEST_P(AnyAndRelatedTypeSimpleFunctionTests,
       LambdaMultipleSignaturePossiblyMatchingSameCallTests) {
  const SignatureArgumentKind any_kind = GetParam().kind;
  const SignatureArgumentKind array_kind = GetParam().array_kind;

  const Type* bool_type = googlesql::types::Int64Type();
  const Type* int64_type = googlesql::types::Int64Type();
  const FunctionSignature sig1{
      array_kind,
      {any_kind, FunctionArgumentType::Lambda({any_kind}, bool_type)},
      /*context_id=*/-1};
  const FunctionSignature sig2{
      int64_type,
      {int64_type, FunctionArgumentType::Lambda({int64_type}, bool_type)},
      /*context_id=*/-1};

  EXPECT_DEATH(
      Function("fp_test", Function::kGoogleSQLFunctionGroupName,
               Function::SCALAR, {sig1, sig2}, FunctionOptions()),
      "Having two signatures with the same lambda at the same argument index "
      "is not allowed");

  auto function = std::make_unique<Function>(
      "fp_test", Function::kGoogleSQLFunctionGroupName, Function::SCALAR);
  function->AddSignature(sig1);
  EXPECT_DEATH(
      function->AddSignature(sig2),
      "Having two signatures with the same lambda at the same argument index "
      "is not allowed");
}

class FunctionSerializationTests : public ::testing::Test {
 public:
  static void ExpectEqualsIgnoringCallbacks(
      const FunctionArgumentType& argument1,
      const FunctionArgumentType& argument2) {
    EXPECT_EQ(argument1.kind_, argument2.kind_);
    if (argument1.type_ != nullptr) {
      ASSERT_TRUE(argument2.type_ != nullptr);
      EXPECT_TRUE(argument1.type_->Equals(argument2.type_));
    }
    EXPECT_EQ(argument1.cardinality(), argument2.cardinality());
    EXPECT_EQ(argument1.num_occurrences_, argument2.num_occurrences_);
    EXPECT_EQ(argument1.options().must_be_non_null(),
              argument2.options().must_be_non_null());
    EXPECT_EQ(argument1.options().must_be_constant(),
              argument2.options().must_be_constant());
    EXPECT_EQ(argument1.options().must_be_analysis_constant(),
              argument2.options().must_be_analysis_constant());
    EXPECT_EQ(argument1.options().must_be_immutable_constant(),
              argument2.options().must_be_immutable_constant());
    EXPECT_EQ(argument1.options().must_be_constant_expression(),
              argument2.options().must_be_constant_expression());
    EXPECT_EQ(argument1.options().has_argument_name(),
              argument2.options().has_argument_name());
    if (argument1.options().has_argument_name()) {
      EXPECT_EQ(argument1.options().argument_name(),
                argument2.options().argument_name());
    }
    EXPECT_EQ(argument1.options().argument_name_parse_location(),
              argument2.options().argument_name_parse_location());
    EXPECT_EQ(argument1.options().argument_type_parse_location(),
              argument2.options().argument_type_parse_location());
    EXPECT_EQ(argument1.options().argument_collation_mode(),
              argument2.options().argument_collation_mode());
    EXPECT_EQ(argument1.options().uses_array_element_for_collation(),
              argument2.options().uses_array_element_for_collation());
    EXPECT_EQ(argument1.options().procedure_argument_mode(),
              argument2.options().procedure_argument_mode());
    EXPECT_EQ(argument1.options().argument_alias_kind(),
              argument2.options().argument_alias_kind());
  }

  static void ExpectEqualsIgnoringCallbacks(
      const FunctionArgumentTypeList& list1,
      const FunctionArgumentTypeList& list2) {
    EXPECT_EQ(list1.size(), list2.size());
    for (int i = 0; i < list1.size(); ++i) {
      ExpectEqualsIgnoringCallbacks(list1[i], list2[i]);
    }
  }

  static void ExpectEqualsIgnoringCallbacks(
      const FunctionSignatureOptions& options1,
      const FunctionSignatureOptions& options2) {
    EXPECT_EQ(options1.is_deprecated(), options2.is_deprecated());
    EXPECT_EQ(options1.additional_deprecation_warnings().size(),
              options2.additional_deprecation_warnings().size());
    for (int i = 0; i < options1.additional_deprecation_warnings().size();
         ++i) {
      EXPECT_THAT(options1.additional_deprecation_warnings()[i],
                  EqualsProto(options2.additional_deprecation_warnings()[i]));
    }

    EXPECT_EQ(options1.required_language_features_,
              options2.required_language_features_);
    EXPECT_EQ(options1.is_aliased_signature(), options2.is_aliased_signature());
    EXPECT_EQ(options1.propagates_collation(), options2.propagates_collation());
    EXPECT_EQ(options1.uses_operation_collation(),
              options2.uses_operation_collation());
    EXPECT_EQ(options1.rejects_collation(), options2.rejects_collation());
  }

  static void ExpectEqualsIgnoringCallbacks(
      const FunctionSignature& signature1,
      const FunctionSignature& signature2) {
    ExpectEqualsIgnoringCallbacks(signature1.arguments_, signature2.arguments_);
    ExpectEqualsIgnoringCallbacks(signature1.result_type_,
                                  signature2.result_type_);
    EXPECT_EQ(signature1.context_id_, signature2.context_id_);
    ExpectEqualsIgnoringCallbacks(signature1.options_, signature2.options_);
    EXPECT_EQ(signature1.is_concrete_, signature2.is_concrete_);
    ExpectEqualsIgnoringCallbacks(signature1.concrete_arguments_,
                                  signature2.concrete_arguments_);

    // These will test that all FunctionArgumentTypeOptions get serialized
    // and deserialized correctly.
    EXPECT_EQ(signature1.DebugString("func", true /* verbose */),
              signature2.DebugString("func", true /* verbose */));
    EXPECT_EQ(signature1.GetSQLDeclaration({} /* arg_names */,
                                           ProductMode::PRODUCT_INTERNAL),
              signature2.GetSQLDeclaration({} /* arg_names */,
                                           ProductMode::PRODUCT_INTERNAL));
  }

  static void ExpectEqualsIgnoringCallbacks(
      absl::Span<const FunctionSignature> list1,
      absl::Span<const FunctionSignature> list2) {
    EXPECT_EQ(list1.size(), list2.size());
    for (int i = 0; i < list1.size(); ++i) {
      ExpectEqualsIgnoringCallbacks(list1[i], list2[i]);
    }
  }

  static void ExpectEqualsIgnoringCallbacks(const FunctionOptions& options1,
                                            const FunctionOptions& options2) {
    EXPECT_EQ(options1.supports_over_clause, options2.supports_over_clause);
    EXPECT_EQ(options1.window_ordering_support,
              options2.window_ordering_support);
    EXPECT_EQ(options1.supports_window_framing,
              options2.supports_window_framing);
    EXPECT_EQ(options1.arguments_are_coercible,
              options2.arguments_are_coercible);
    EXPECT_EQ(options1.is_deprecated, options2.is_deprecated);
    EXPECT_EQ(options1.alias_name, options2.alias_name);
    EXPECT_EQ(options1.sql_name, options2.sql_name);
    EXPECT_EQ(options1.allow_external_usage, options2.allow_external_usage);
    EXPECT_EQ(options1.volatility, options2.volatility);
    EXPECT_EQ(options1.supports_safe_error_mode,
              options2.supports_safe_error_mode);
  }

  static void ExpectEqualsIgnoringCallbacks(const Function& function1,
                                            const Function& function2) {
    EXPECT_EQ(function1.FunctionNamePath(), function2.FunctionNamePath());
    EXPECT_EQ(function1.GetGroup(), function2.GetGroup());
    EXPECT_EQ(function1.mode(), function2.mode());
    ExpectEqualsIgnoringCallbacks(function1.function_options(),
                                  function2.function_options());
    ExpectEqualsIgnoringCallbacks(function1.signatures(),
                                  function2.signatures());
  }

  static void CheckSerializationAndDeserialization(const Function& function) {
    FileDescriptorSetMap file_descriptor_set_map;

    FunctionProto proto;
    GOOGLESQL_CHECK_OK(function.Serialize(&file_descriptor_set_map, &proto));

    std::vector<const google::protobuf::DescriptorPool*> pools(
        file_descriptor_set_map.size());
    for (const auto& pair : file_descriptor_set_map) {
      pools[pair.second->descriptor_set_index] = pair.first;
    }

    TypeFactory factory;
    std::unique_ptr<Function> result;
    GOOGLESQL_CHECK_OK(Function::Deserialize(proto, pools, &factory, &result));
    ExpectEqualsIgnoringCallbacks(function, *result);
  }

  static void CheckSerializationAndDeserialization(
      const FunctionArgumentType& argument_type) {
    FileDescriptorSetMap file_descriptor_set_map;
    FunctionArgumentTypeProto proto;
    GOOGLESQL_ASSERT_OK(argument_type.Serialize(&file_descriptor_set_map, &proto));
    std::vector<const google::protobuf::DescriptorPool*> pools(
        file_descriptor_set_map.size());
    for (const auto& pair : file_descriptor_set_map) {
      pools[pair.second->descriptor_set_index] = pair.first;
    }
    TypeFactory factory;
    std::unique_ptr<FunctionArgumentType> result =
        FunctionArgumentType::Deserialize(proto,
                                          TypeDeserializer(&factory, pools))
            .value();
    ExpectEqualsIgnoringCallbacks(argument_type, *result);
  }

  static size_t SizeOfFunctionArgumentTypeOptionsData() {
    return sizeof(FunctionArgumentTypeOptions::Data);
  }
};

static FreestandingDeprecationWarning CreateDeprecationWarning() {
  FreestandingDeprecationWarning warning;
  warning.set_message("foo is deprecated");
  warning.mutable_deprecation_warning()->set_kind(
      DeprecationWarning::PROTO3_FIELD_PRESENCE);
  ErrorLocation* location = warning.mutable_error_location();
  location->set_line(10);
  location->set_column(50);

  return warning;
}

TEST_F(FunctionSerializationTests, BuiltinFunctions) {
  TypeFactory type_factory;
  LanguageOptions language_options;
  // Even functions that are "in_development" should serialize properly.
  language_options.EnableMaximumLanguageFeaturesForDevelopment();
  // TODO: Extend this test to cover external product mode too.
  language_options.set_product_mode(PRODUCT_INTERNAL);

  absl::flat_hash_map<std::string, std::unique_ptr<Function>> functions;
  absl::flat_hash_map<std::string, const Type*> types_ignored;
  GOOGLESQL_ASSERT_OK(
      GetBuiltinFunctionsAndTypes(BuiltinFunctionOptions(language_options),
                                  type_factory, functions, types_ignored));

  Function* chosen_function = nullptr;
  for (const auto& [name, function] : functions) {
    ABSL_LOG(INFO) << "Testing serialization of function " << name;
    CheckSerializationAndDeserialization(*function);
    if (chosen_function == nullptr && function->NumSignatures() > 0) {
      chosen_function = function.get();
      break;
    }
  }

  // Test a function with a signature that triggers a deprecation warning.
  ASSERT_FALSE(functions.empty());
  ASSERT_TRUE(chosen_function != nullptr);
  ASSERT_GT(chosen_function->NumSignatures(), 0) << chosen_function->Name();
  FunctionSignature new_signature = *chosen_function->GetSignature(0);
  new_signature.SetAdditionalDeprecationWarnings({CreateDeprecationWarning()});
  CheckSerializationAndDeserialization(*chosen_function);
}

TEST_F(FunctionSerializationTests, Volatility) {
  FunctionOptions options;
  options.set_volatility(FunctionEnums::VOLATILE);
  FunctionOptionsProto proto;
  proto.set_volatility(FunctionEnums::VOLATILE);
  std::unique_ptr<FunctionOptions> result;
  GOOGLESQL_EXPECT_OK(FunctionOptions::Deserialize(proto, &result));
  ExpectEqualsIgnoringCallbacks(options, *result);
}

TEST_F(FunctionSerializationTests, InconsistentWindowSupport) {
  FunctionOptionsProto proto;
  proto.set_supports_over_clause(false);
  proto.set_window_ordering_support(FunctionOptions::ORDER_OPTIONAL);
  std::unique_ptr<FunctionOptions> options;
  EXPECT_FALSE(FunctionOptions::Deserialize(proto, &options).ok());
  proto.set_window_ordering_support(FunctionOptions::ORDER_UNSUPPORTED);
  GOOGLESQL_EXPECT_OK(FunctionOptions::Deserialize(proto, &options));
  proto.set_supports_window_framing(true);
  EXPECT_FALSE(FunctionOptions::Deserialize(proto, &options).ok());
}

TEST_F(FunctionSerializationTests, RequiredLanguageFeaturesTest) {
  FunctionOptions options;
  options.AddRequiredLanguageFeature(FEATURE_CIVIL_TIME);

  FunctionOptionsProto proto;
  options.Serialize(&proto);
  EXPECT_EQ(1, proto.required_language_feature_size());
  EXPECT_EQ(FEATURE_CIVIL_TIME, proto.required_language_feature(0));

  std::unique_ptr<FunctionOptions> deserialize_result;
  GOOGLESQL_EXPECT_OK(FunctionOptions::Deserialize(proto, &deserialize_result));
  ExpectEqualsIgnoringCallbacks(options, *deserialize_result);
}

TEST_F(FunctionSerializationTests,
       SerializationAndDeserializationModuleNameFromImport) {
  FunctionOptions options;
  options.set_module_name_from_import({"a", "B", "cc"});
  FunctionOptionsProto proto;
  options.Serialize(&proto);
  EXPECT_THAT(proto.module_name_from_import(), ElementsAre("a", "B", "cc"));

  std::unique_ptr<FunctionOptions> deserialize_result;
  GOOGLESQL_EXPECT_OK(FunctionOptions::Deserialize(proto, &deserialize_result));
  EXPECT_THAT(deserialize_result->module_name_from_import,
              ElementsAre("a", "B", "cc"));
}

// Test serialization and deserialization of the optional argument name in the
// function signature options.
TEST_F(FunctionSerializationTests,
       CheckSignatureSerializationAndDeserializationWithArgumentNames) {
  FunctionSignature simple_signature(
      /*result_type=*/googlesql::types::Int64Type(),
      /*arguments=*/
      {{googlesql::types::Int32Type(),
        FunctionArgumentTypeOptions().set_argument_name("arg_int32",
                                                        kPositionalOrNamed)},
       {googlesql::types::Int64Type(),
        FunctionArgumentTypeOptions().set_argument_name("arg_int64",
                                                        kPositionalOrNamed)}},
      /*context_id=*/-1);
  FileDescriptorSetMap file_descriptor_set_map;
  FunctionSignatureProto signature_proto;
  GOOGLESQL_ASSERT_OK(
      simple_signature.Serialize(&file_descriptor_set_map, &signature_proto));
  std::vector<const google::protobuf::DescriptorPool*> pools(
      file_descriptor_set_map.size());
  for (const auto& pair : file_descriptor_set_map) {
    pools[pair.second->descriptor_set_index] = pair.first;
  }
  TypeFactory factory;
  std::unique_ptr<FunctionSignature> result;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(result, FunctionSignature::Deserialize(
                                   signature_proto, googlesql::TypeDeserializer(
                                                        &factory, pools)));
  ExpectEqualsIgnoringCallbacks(simple_signature, *result);
  ASSERT_EQ(result->arguments().size(), 2);
  ASSERT_TRUE(result->argument(0).options().has_argument_name());
  EXPECT_EQ(result->argument(0).options().argument_name(), "arg_int32");
  ASSERT_TRUE(result->argument(1).options().has_argument_name());
  EXPECT_EQ(result->argument(1).options().argument_name(), "arg_int64");
}

TEST_F(FunctionSerializationTests, SignatureRequiredLanguageFeaturesTest) {
  FunctionSignatureOptions options;
  options.AddRequiredLanguageFeature(FEATURE_CIVIL_TIME);
  options.set_is_aliased_signature(true);

  FunctionSignatureOptionsProto proto;
  options.Serialize(&proto);
  EXPECT_EQ(1, proto.required_language_feature_size());
  EXPECT_EQ(FEATURE_CIVIL_TIME, proto.required_language_feature(0));

  std::unique_ptr<FunctionSignatureOptions> deserialize_result;
  GOOGLESQL_EXPECT_OK(FunctionSignatureOptions::Deserialize(proto, &deserialize_result));
  ExpectEqualsIgnoringCallbacks(options, *deserialize_result);
}

TEST_F(FunctionSerializationTests, CollationOptionsTest) {
  FunctionSignatureOptions options;
  options.set_propagates_collation(false);
  options.set_uses_operation_collation(true);
  options.set_rejects_collation(true);

  FunctionSignatureOptionsProto proto;
  options.Serialize(&proto);
  EXPECT_FALSE(proto.propagates_collation());
  EXPECT_TRUE(proto.uses_operation_collation());

  std::unique_ptr<FunctionSignatureOptions> deserialize_result;
  GOOGLESQL_EXPECT_OK(FunctionSignatureOptions::Deserialize(proto, &deserialize_result));
  ExpectEqualsIgnoringCallbacks(options, *deserialize_result);
}

// Test serialization and deserialization of the optional arguments with default
// values.
TEST_F(FunctionSerializationTests,
       CheckSignatureSerializationAndDeserializationWithDefaultValues) {
  TypeFactory type_factory;
  const ProtoType* proto_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory.MakeProtoType(ParseLocationRangeProto::descriptor(),
                                       &proto_type));
  const ArrayType* array_double_type = nullptr;
  GOOGLESQL_ASSERT_OK(
      type_factory.MakeArrayType(types::DoubleType(), &array_double_type));
  const ArrayType* array_proto_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory.MakeArrayType(proto_type, &array_proto_type));
  ParseLocationRangeProto proto_value;
  proto_value.set_filename("abc.sql");
  proto_value.set_start(734);
  proto_value.set_end(10086);

  FunctionSignature simple_signature(
      /*result_type=*/googlesql::types::Int64Type(),
      /*arguments=*/
      {
          {types::StringType(),
           FunctionArgumentTypeOptions()
               .set_argument_name("arg_string", kPositionalOrNamed)
               .set_cardinality(FunctionEnums::OPTIONAL)},
          {types::Int64Type(),
           FunctionArgumentTypeOptions()
               .set_argument_name("arg_int64", kPositionalOrNamed)
               .set_cardinality(FunctionEnums::OPTIONAL)
               .set_default(values::Int64(-123456778987654321))},
          {array_double_type,
           FunctionArgumentTypeOptions()
               .set_argument_name("arg_double_arr", kPositionalOrNamed)
               .set_cardinality(FunctionEnums::OPTIONAL)
               .set_default(values::EmptyArray(array_double_type))},
          {proto_type,
           FunctionArgumentTypeOptions()
               .set_argument_name("arg_proto", kPositionalOrNamed)
               .set_cardinality(FunctionEnums::OPTIONAL)
               .set_default(values::Proto(proto_type, proto_value))},
          {array_proto_type,
           FunctionArgumentTypeOptions()
               .set_argument_name("arg_proto_arr", kPositionalOrNamed)
               .set_cardinality(FunctionEnums::OPTIONAL)
               .set_default(
                   values::Array(array_proto_type,
                                 {values::Proto(proto_type, proto_value),
                                  values::Proto(proto_type, proto_value)}))},
          {proto_type,
           FunctionArgumentTypeOptions()
               .set_argument_name("arg_proto_null", kPositionalOrNamed)
               .set_cardinality(FunctionEnums::OPTIONAL)
               .set_default(values::Null(proto_type))},
          {array_proto_type,
           FunctionArgumentTypeOptions()
               .set_argument_name("arg_proto_arr_null", kPositionalOrNamed)
               .set_cardinality(FunctionEnums::OPTIONAL)
               .set_default(values::Null(array_proto_type))},
          {types::BytesType(),
           FunctionArgumentTypeOptions()
               .set_argument_name("arg_bytes_null", kPositionalOrNamed)
               .set_cardinality(FunctionEnums::OPTIONAL)
               .set_default(values::NullBytes())},
      },
      /*context_id=*/-1);
  FileDescriptorSetMap file_descriptor_set_map;
  FunctionSignatureProto signature_proto;
  GOOGLESQL_ASSERT_OK(
      simple_signature.Serialize(&file_descriptor_set_map, &signature_proto));
  std::vector<const google::protobuf::DescriptorPool*> pools(
      file_descriptor_set_map.size());
  for (const auto& pair : file_descriptor_set_map) {
    pools[pair.second->descriptor_set_index] = pair.first;
  }

  // Another factory for deserialization.
  TypeFactory factory;
  std::unique_ptr<FunctionSignature> result;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(result, FunctionSignature::Deserialize(
                                   signature_proto, googlesql::TypeDeserializer(
                                                        &factory, pools)));
  ExpectEqualsIgnoringCallbacks(simple_signature, *result);

  ASSERT_EQ(result->arguments().size(), 8);

  ASSERT_TRUE(result->argument(0).options().has_argument_name());
  EXPECT_EQ(result->argument(0).options().argument_name(), "arg_string");
  EXPECT_FALSE(result->argument(0).GetDefault().has_value());

  ASSERT_TRUE(result->argument(1).options().has_argument_name());
  EXPECT_EQ(result->argument(1).options().argument_name(), "arg_int64");
  EXPECT_EQ(-123456778987654321,
            result->argument(1).GetDefault().value().int64_value());

  ASSERT_TRUE(result->argument(2).options().has_argument_name());
  EXPECT_EQ(result->argument(2).options().argument_name(), "arg_double_arr");
  EXPECT_TRUE(result->argument(2).GetDefault().value().empty());

  ASSERT_TRUE(result->argument(3).options().has_argument_name());
  EXPECT_EQ(result->argument(3).options().argument_name(), "arg_proto");
  EXPECT_EQ(SerializeToCord(proto_value),
            result->argument(3).GetDefault().value().ToCord());

  ASSERT_TRUE(result->argument(4).options().has_argument_name());
  EXPECT_EQ(result->argument(4).options().argument_name(), "arg_proto_arr");
  EXPECT_EQ(2, result->argument(4).GetDefault().value().num_elements());
  for (int i = 0; i < 2; ++i) {
    EXPECT_EQ(SerializeToCord(proto_value),
              result->argument(4).GetDefault().value().elements()[i].ToCord());
  }

  ASSERT_TRUE(result->argument(5).options().has_argument_name());
  EXPECT_EQ(result->argument(5).options().argument_name(), "arg_proto_null");
  EXPECT_TRUE(result->argument(5).GetDefault().value().is_null());

  ASSERT_TRUE(result->argument(6).options().has_argument_name());
  EXPECT_EQ(result->argument(6).options().argument_name(),
            "arg_proto_arr_null");
  EXPECT_TRUE(result->argument(6).GetDefault().value().is_null());
  EXPECT_TRUE(result->argument(6).GetDefault().value().type()->Equals(
      array_proto_type));

  ASSERT_TRUE(result->argument(7).options().has_argument_name());
  EXPECT_EQ(result->argument(7).options().argument_name(), "arg_bytes_null");
  EXPECT_TRUE(result->argument(7).GetDefault().value().is_null());
  EXPECT_TRUE(result->argument(7).GetDefault().value().type()->IsBytes());
}

TEST_F(FunctionSerializationTests,
       CheckSignatureSerializationAndDeserializationWithTemplatedDefaultValue) {
  TypeFactory type_factory;
  const ProtoType* proto_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory.MakeProtoType(ParseLocationRangeProto::descriptor(),
                                       &proto_type));
  const ArrayType* array_double_type = nullptr;
  GOOGLESQL_ASSERT_OK(
      type_factory.MakeArrayType(types::DoubleType(), &array_double_type));
  const ArrayType* array_proto_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory.MakeArrayType(proto_type, &array_proto_type));
  const ArrayType* array_int64_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory.MakeArrayType(types::Int64Type(), &array_int64_type));
  ParseLocationRangeProto proto_value;
  proto_value.set_filename("abc.sql");
  proto_value.set_start(734);
  proto_value.set_end(10086);

  FunctionSignature templated_signature(
      /*result_type=*/googlesql::types::Int64Type(),
      /*arguments=*/
      {{ARG_TYPE_ANY_1, FunctionArgumentTypeOptions()
                            .set_argument_name("arg_any", kPositionalOrNamed)
                            .set_cardinality(FunctionEnums::OPTIONAL)
                            .set_default(values::NullInt64())},
       {ARG_TYPE_ANY_2,
        FunctionArgumentTypeOptions()
            .set_argument_name("arg_double_arr", kPositionalOrNamed)
            .set_cardinality(FunctionEnums::OPTIONAL)
            .set_default(values::EmptyArray(array_double_type))},
       {ARG_PROTO_ANY,
        FunctionArgumentTypeOptions()
            .set_argument_name("arg_proto", kPositionalOrNamed)
            .set_cardinality(FunctionEnums::OPTIONAL)
            .set_default(values::Proto(proto_type, proto_value))},
       {ARG_ARRAY_TYPE_ANY_1,
        FunctionArgumentTypeOptions()
            .set_argument_name("arg_proto_arr", kPositionalOrNamed)
            .set_cardinality(FunctionEnums::OPTIONAL)
            .set_default(values::Array(
                array_proto_type, {values::Proto(proto_type, proto_value),
                                   values::Proto(proto_type, proto_value)}))},
       {ARG_TYPE_ARBITRARY,
        FunctionArgumentTypeOptions()
            .set_argument_name("arg_proto_null", kPositionalOrNamed)
            .set_cardinality(FunctionEnums::OPTIONAL)
            .set_default(values::Null(proto_type))},
       {ARG_ARRAY_TYPE_ANY_2,
        FunctionArgumentTypeOptions()
            .set_argument_name("arg_proto_arr_null", kPositionalOrNamed)
            .set_cardinality(FunctionEnums::OPTIONAL)
            .set_default(values::Null(array_proto_type))},
       {ARG_TYPE_ANY_3, FunctionArgumentTypeOptions()
                            .set_argument_name("arg_any_3_empty_int64_array",
                                               kPositionalOrNamed)
                            .set_cardinality(FunctionEnums::OPTIONAL)
                            .set_default(values::EmptyArray(array_int64_type))},
       {ARG_ARRAY_TYPE_ANY_3,
        FunctionArgumentTypeOptions()
            .set_argument_name("arg_array_any_3_proto_arr_null",
                               kPositionalOrNamed)
            .set_cardinality(FunctionEnums::OPTIONAL)
            .set_default(values::Null(array_proto_type))},
       {ARG_TYPE_ANY_4, FunctionArgumentTypeOptions()
                            .set_argument_name("arg_any_4_empty_int64_array",
                                               kPositionalOrNamed)
                            .set_cardinality(FunctionEnums::OPTIONAL)
                            .set_default(values::EmptyArray(array_int64_type))},
       {ARG_ARRAY_TYPE_ANY_4,
        FunctionArgumentTypeOptions()
            .set_argument_name("arg_array_any_4_proto_arr_null",
                               kPositionalOrNamed)
            .set_cardinality(FunctionEnums::OPTIONAL)
            .set_default(values::Null(array_proto_type))},
       {ARG_TYPE_ANY_5, FunctionArgumentTypeOptions()
                            .set_argument_name("arg_any_5_empty_int64_array",
                                               kPositionalOrNamed)
                            .set_cardinality(FunctionEnums::OPTIONAL)
                            .set_default(values::EmptyArray(array_int64_type))},
       {ARG_ARRAY_TYPE_ANY_5,
        FunctionArgumentTypeOptions()
            .set_argument_name("arg_array_any_5_proto_arr_null",
                               kPositionalOrNamed)
            .set_cardinality(FunctionEnums::OPTIONAL)
            .set_default(values::Null(array_proto_type))}},
      /*context_id=*/-1);

  FileDescriptorSetMap file_descriptor_set_map;
  FunctionSignatureProto signature_proto;
  GOOGLESQL_ASSERT_OK(templated_signature.Serialize(&file_descriptor_set_map,
                                          &signature_proto));

  std::vector<const google::protobuf::DescriptorPool*> pools(
      file_descriptor_set_map.size());
  for (const auto& pair : file_descriptor_set_map) {
    pools[pair.second->descriptor_set_index] = pair.first;
  }

  TypeFactory factory;
  std::unique_ptr<FunctionSignature> result;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(result, FunctionSignature::Deserialize(
                                   signature_proto, googlesql::TypeDeserializer(
                                                        &factory, pools)));
  ExpectEqualsIgnoringCallbacks(templated_signature, *result);

  ASSERT_EQ(result->arguments().size(), 12);

  ASSERT_TRUE(result->argument(0).options().has_argument_name());
  EXPECT_EQ(result->argument(0).options().argument_name(), "arg_any");
  EXPECT_TRUE(result->argument(0).GetDefault().value().is_null());

  ASSERT_TRUE(result->argument(1).options().has_argument_name());
  EXPECT_EQ(result->argument(1).options().argument_name(), "arg_double_arr");
  EXPECT_TRUE(result->argument(1).GetDefault().value().empty());

  ASSERT_TRUE(result->argument(2).options().has_argument_name());
  EXPECT_EQ(result->argument(2).options().argument_name(), "arg_proto");
  EXPECT_EQ(SerializeToCord(proto_value),
            result->argument(2).GetDefault().value().ToCord());

  ASSERT_TRUE(result->argument(3).options().has_argument_name());
  EXPECT_EQ(result->argument(3).options().argument_name(), "arg_proto_arr");
  EXPECT_EQ(2, result->argument(3).GetDefault().value().num_elements());
  for (int i = 0; i < 2; ++i) {
    EXPECT_EQ(SerializeToCord(proto_value),
              result->argument(3).GetDefault().value().elements()[i].ToCord());
  }

  ASSERT_TRUE(result->argument(4).options().has_argument_name());
  EXPECT_EQ(result->argument(4).options().argument_name(), "arg_proto_null");
  EXPECT_TRUE(result->argument(4).GetDefault().value().is_null());

  ASSERT_TRUE(result->argument(5).options().has_argument_name());
  EXPECT_EQ(result->argument(5).options().argument_name(),
            "arg_proto_arr_null");
  EXPECT_TRUE(result->argument(5).GetDefault().value().is_null());
  EXPECT_TRUE(result->argument(5).GetDefault().value().type()->Equals(
      array_proto_type));

  ASSERT_TRUE(result->argument(6).options().has_argument_name());
  EXPECT_EQ(result->argument(6).options().argument_name(),
            "arg_any_3_empty_int64_array");
  EXPECT_TRUE(result->argument(6).GetDefault().value().empty());

  ASSERT_TRUE(result->argument(7).options().has_argument_name());
  EXPECT_EQ(result->argument(7).options().argument_name(),
            "arg_array_any_3_proto_arr_null");
  EXPECT_TRUE(result->argument(7).GetDefault().value().is_null());
  EXPECT_TRUE(result->argument(7).GetDefault().value().type()->Equals(
      array_proto_type));

  ASSERT_TRUE(result->argument(8).options().has_argument_name());
  EXPECT_EQ(result->argument(8).options().argument_name(),
            "arg_any_4_empty_int64_array");
  EXPECT_TRUE(result->argument(8).GetDefault().value().empty());

  ASSERT_TRUE(result->argument(9).options().has_argument_name());
  EXPECT_EQ(result->argument(9).options().argument_name(),
            "arg_array_any_4_proto_arr_null");
  EXPECT_TRUE(result->argument(9).GetDefault().value().is_null());
  EXPECT_TRUE(result->argument(9).GetDefault().value().type()->Equals(
      array_proto_type));

  ASSERT_TRUE(result->argument(10).options().has_argument_name());
  EXPECT_EQ(result->argument(10).options().argument_name(),
            "arg_any_5_empty_int64_array");
  EXPECT_TRUE(result->argument(10).GetDefault().value().empty());

  ASSERT_TRUE(result->argument(11).options().has_argument_name());
  EXPECT_EQ(result->argument(11).options().argument_name(),
            "arg_array_any_5_proto_arr_null");
  EXPECT_TRUE(result->argument(11).GetDefault().value().is_null());
  EXPECT_TRUE(result->argument(11).GetDefault().value().type()->Equals(
      array_proto_type));
}

// Test serialization and deserialization of the optional argument name and type
// parse location ranges in the function argument type options.
TEST_F(FunctionSerializationTests,
       SerializationAndDeserializationWithArgumentNameAndTypeLocations) {
  ParseLocationRange location1, location2;
  location1.set_start(ParseLocationPoint::FromByteOffset("file", 11));
  location1.set_end(ParseLocationPoint::FromByteOffset("file", 13));
  location2.set_start(ParseLocationPoint::FromByteOffset("file", 15));
  location2.set_end(ParseLocationPoint::FromByteOffset("file", 24));

  FunctionArgumentTypeOptions options;
  options.set_argument_name_parse_location(location1);
  options.set_argument_type_parse_location(location2);
  FunctionArgumentType argument_type(googlesql::types::Int64Type(), options);

  CheckSerializationAndDeserialization(argument_type);
}

// Test serialization and deserialization of the optional procedure argument
// mode in the function argument type options.
TEST_F(FunctionSerializationTests,
       SerializationAndDeserializationWithProcedureArgumentMode) {
  FunctionArgumentTypeOptions options;
  options.set_procedure_argument_mode(FunctionEnums::INOUT);
  FunctionArgumentType argument_type(googlesql::types::Int64Type(), options);

  CheckSerializationAndDeserialization(argument_type);
}

// Test serialization and deserialization of the optional argument collation
// mode in the function argument type options.
TEST_F(FunctionSerializationTests,
       SerializationAndDeserializationWithCollationOptions) {
  for (int mode = FunctionEnums::AFFECTS_NONE;
       mode <= FunctionEnums::AFFECTS_OPERATION_AND_PROPAGATION; mode++) {
    FunctionArgumentTypeOptions options;
    options.set_argument_collation_mode(
        static_cast<FunctionArgumentTypeOptions::ArgumentCollationMode>(mode));
    options.set_uses_array_element_for_collation(true);
    FunctionArgumentType argument_type(googlesql::types::StringType(), options);

    CheckSerializationAndDeserialization(argument_type);
  }
}

class AnyTypeFunctionSerializationTests
    : public FunctionSerializationTests,
      public ::testing::WithParamInterface<SignatureArgumentKindGroup> {};

INSTANTIATE_TEST_SUITE_P(
    AnyTypeSerialization, AnyTypeFunctionSerializationTests,
    ::testing::ValuesIn(GetRelatedSignatureArgumentGroup()));

// Test serialization and deserialization of array element type comparison
// constraint signal in the function argument type options.
TEST_P(AnyTypeFunctionSerializationTests,
       SerializationAndDeserializationArrayElementTypeConstraint) {
  const SignatureArgumentKind any_kind = GetParam().kind;

  // array_element_must_support_equality
  {
    FunctionArgumentTypeOptions options;
    options.set_array_element_must_support_equality();
    FunctionArgumentType argument_type(any_kind, options);
    CheckSerializationAndDeserialization(argument_type);
  }

  // array_element_must_support_ordering
  {
    FunctionArgumentTypeOptions options;
    options.set_array_element_must_support_ordering();
    FunctionArgumentType argument_type(any_kind, options);
    CheckSerializationAndDeserialization(argument_type);
  }

  // array_element_must_support_grouping
  {
    FunctionArgumentTypeOptions options;
    options.set_array_element_must_support_grouping();
    FunctionArgumentType argument_type(any_kind, options);
    CheckSerializationAndDeserialization(argument_type);
  }
  // Mixed constraints.
  {
    FunctionArgumentTypeOptions options;
    options.set_array_element_must_support_equality();
    options.set_array_element_must_support_ordering();
    options.set_array_element_must_support_grouping();
    FunctionArgumentType argument_type(any_kind, options);
    CheckSerializationAndDeserialization(argument_type);
  }
}

// Test serialization and deserialization of `argument_alias_kind`.
TEST_F(FunctionSerializationTests,
       SerializationAndDeserializationArgumentAliasKind) {
  {
    FunctionArgumentTypeOptions options;
    options.set_argument_alias_kind(FunctionEnums::ARGUMENT_ALIASED);
    FunctionArgumentType argument_type(ARG_ARRAY_TYPE_ANY_1, options);
    CheckSerializationAndDeserialization(argument_type);
  }
  {
    FunctionArgumentTypeOptions options;
    options.set_argument_alias_kind(FunctionEnums::ARGUMENT_NON_ALIASED);
    FunctionArgumentType argument_type(ARG_ARRAY_TYPE_ANY_1, options);
    CheckSerializationAndDeserialization(argument_type);
  }
}

}  // namespace googlesql
