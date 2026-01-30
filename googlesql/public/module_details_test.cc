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

#include "googlesql/public/module_details.h"

#include <memory>
#include <optional>

#include "googlesql/common/resolution_scope.h"
#include "googlesql/common/testing/proto_matchers.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/proto/module_options.pb.h"
#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/constant_evaluator.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/prepared_expression_constant_evaluator.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/text_format.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {

using testing::EqualsProto;
using ::testing::Optional;
using ::absl_testing::StatusIs;

class ModuleDetailsTest : public ::testing::Test {
 public:
  ModuleDetailsTest() : catalog_("simple", &factory_) {
    catalog_.AddBuiltinFunctions(
        BuiltinFunctionOptions::AllReleasedFunctions());
    analyzer_options_.mutable_language()->SetSupportsAllStatementKinds();
    analyzer_options_.mutable_language()->EnableLanguageFeature(
        FEATURE_EXPERIMENTAL_MODULES);
    analyzer_options_.AddQueryParameter("int_parameter", types::Int64Type())
        .IgnoreError();
  }

  absl::StatusOr<ModuleDetails> BuildModuleDetailsFromStatement(
      absl::string_view sql, ConstantEvaluator* constant_evaluator = nullptr,
      const ModuleOptions option_overrides = ModuleOptions()) {
    std::unique_ptr<const AnalyzerOutput> output;
    GOOGLESQL_RET_CHECK_OK(AnalyzeStatement(sql, analyzer_options_, &catalog_, &factory_,
                                  &output));
    const ResolvedModuleStmt* resolved_module_stmt =
        output->resolved_statement()->GetAs<ResolvedModuleStmt>();
    return ModuleDetails::Create(
        absl::StrJoin(resolved_module_stmt->name_path(), "."),
        resolved_module_stmt->option_list(), constant_evaluator,
        option_overrides);
  }

 protected:
  TypeFactory factory_;
  SimpleCatalog catalog_;
  AnalyzerOptions analyzer_options_;
};

TEST_F(ModuleDetailsTest, EmptyModuleDetailsContainsNoOptions) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ModuleDetails empty_module_details,
                       BuildModuleDetailsFromStatement("module a.b"));
  EXPECT_EQ(empty_module_details.udf_server_options(), std::nullopt);
  EXPECT_EQ(empty_module_details.default_resolution_scope(),
            ResolutionScope::kBuiltin);
}

TEST_F(ModuleDetailsTest,
       ModuleDetailsWithValidExpressionOptionsNoConstantEvaluator) {
  EXPECT_THAT(
      BuildModuleDetailsFromStatement(
          R"(
                         module a.b options(
                           stub_module_type='udf_server_catalog',
                           udf_server_address=concat('blade:', 'dummy_gslb'),
                           udf_namespace='dummy_namespace',
                           udf_server_import_mode='CALLER_PROVIDED',
                           udf_scaling_factor=(select 2.5)))"),
      StatusIs(absl::StatusCode::kInvalidArgument,
               ::testing::HasSubstr(
                   "ConstantEvaluator not specified for the expression of "
                   "option udf_server_address in module a.b")));
}

TEST_F(ModuleDetailsTest,
       ModuleDetailsWithValidExpressionOptionsUseConstantEvaluator) {
  PreparedExpressionConstantEvaluator constant_evaluator(/*options=*/{});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ModuleDetails module_details,
                       BuildModuleDetailsFromStatement(R"(
                         module a.b options(
                           stub_module_type='udf_server_catalog',
                           udf_server_address=concat('blade:', 'dummy_gslb'),
                           udf_namespace='dummy_namespace',
                           udf_server_import_mode='CALLER_PROVIDED',
                           udf_scaling_factor=(select 2.5)))",
                                                       &constant_evaluator));
  EXPECT_THAT(module_details.udf_server_options(), Optional(EqualsProto(R"pb(
                udf_server_address: "blade:dummy_gslb"
                udf_server_import_mode: CALLER_PROVIDED
                udf_scaling_factor: 2.5
                udf_namespace: "dummy_namespace"
              )pb")));
}

TEST_F(ModuleDetailsTest, OptionNameShouldBeCaseInsensitive) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ModuleDetails module_details,
                       BuildModuleDetailsFromStatement(R"(
                         module a.b options(
                           allowed_REFERENCES='gLoBaL',
                           stub_module_type='udf_server_catalog',
                           UDF_SERVER_ADDRESS='dummy_address',
                           Udf_Namespace='dummy_namespace',
                           UDF_SERVER_IMPORT_MODE='CALLER_PROVIDED',
                           udf_scaling_factor=2.5))"));
  EXPECT_THAT(module_details.udf_server_options(), Optional(EqualsProto(R"pb(
                udf_server_address: "dummy_address"
                udf_server_import_mode: CALLER_PROVIDED
                udf_scaling_factor: 2.5
                udf_namespace: "dummy_namespace"
              )pb")));
  EXPECT_EQ(module_details.default_resolution_scope(),
            ResolutionScope::kGlobal);
}

TEST_F(ModuleDetailsTest, DuplicateOptionNamesShouldFail) {
  EXPECT_THAT(
      BuildModuleDetailsFromStatement(R"(
                         module a.b options(
                           stub_module_type='udf_server_catalog',
                           UDF_SERVER_ADDRESS='dummy_address',
                           Udf_Server_Address='dummy_address2',
                           udf_scaling_factor=2.5))"),
      StatusIs(absl::StatusCode::kInvalidArgument,
               ::testing::HasSubstr(
                   "Duplicate option Udf_Server_Address in module a.b")));
}

TEST_F(ModuleDetailsTest, OptionValueInvalidExpressionShouldFail) {
  PreparedExpressionConstantEvaluator constant_evaluator(/*options=*/{});
  EXPECT_THAT(BuildModuleDetailsFromStatement(R"(module a.b options(
                           stub_module_type='udf_server_catalog',
                           udf_scaling_factor=1/0))",
                                              &constant_evaluator),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       ::testing::HasSubstr(
                           "Error in module a.b: division by zero: 1 / 0")));
}

TEST_F(ModuleDetailsTest, OptionValueWithParametersShouldFail) {
  PreparedExpressionConstantEvaluator constant_evaluator(/*options=*/{});
  EXPECT_THAT(BuildModuleDetailsFromStatement(
                  "module a.b options(stub_module_type=concat('prefix:', "
                  "@int_parameter))",
                  &constant_evaluator),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       ::testing::HasSubstr("Error in module a.b: Incomplete "
                                            "query parameters int_parameter")));
}

TEST_F(ModuleDetailsTest, NoStubTypeShouldFail) {
  EXPECT_THAT(
      BuildModuleDetailsFromStatement(R"(
                   module a.b options(
                     udf_server_address='dummy_address',
                     udf_namespace='dummy_namespace',
                     udf_server_import_mode='CALLER_PROVIDED',
                     udf_scaling_factor=2.5))"),
      StatusIs(absl::StatusCode::kInvalidArgument,
               ::testing::HasSubstr(
                   "The option list for a UDF server stub module should "
                   "contain an option with name: stub_module_type")));
}

TEST_F(ModuleDetailsTest, WrongStubModuleTypeShouldFail) {
  EXPECT_THAT(BuildModuleDetailsFromStatement(R"(
                   module a.b options(
                     stub_module_type='wrong_type',
                     udf_server_address='dummy_address',
                     udf_namespace='dummy_namespace',
                     udf_server_import_mode='CALLER_PROVIDED',
                     udf_scaling_factor=2.5))"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       ::testing::HasSubstr(
                           "Allowed stub module types are "
                           "[udf_server_catalog], but got: wrong_type")));
}

TEST_F(ModuleDetailsTest, WrongOptionNameShouldFail) {
  EXPECT_THAT(BuildModuleDetailsFromStatement(R"(
                   module a.b options(
                     stub_module_type='udf_server_catalog',
                     wrong_option_name='dummy_address',
                     udf_namespace='dummy_namespace',
                     udf_server_import_mode='CALLER_PROVIDED',
                     udf_scaling_factor=2.5))"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       ::testing::HasSubstr(
                           "Unexpected option name: wrong_option_name, allowed "
                           "option names are ")));  // Allowed list omitted.
}

TEST_F(ModuleDetailsTest, WrongTypeStubModuleTypeShouldFail) {
  EXPECT_THAT(BuildModuleDetailsFromStatement(R"(
                   module a.b options(
                     stub_module_type=1))"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       ::testing::HasSubstr(
                           "Module option stub_module_type is expected to "
                           "have type STRING, but got: INT64")));
}

TEST_F(ModuleDetailsTest, WrongTypeUdfServerAddressShouldFail) {
  EXPECT_THAT(BuildModuleDetailsFromStatement(R"(
                   module a.b options(
                     stub_module_type='udf_server_catalog',
                     udf_server_address=1))"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       ::testing::HasSubstr(
                           "Module option udf_server_address is expected to "
                           "have type STRING, but got: INT64")));
}

TEST_F(ModuleDetailsTest, WrongTypeUdfNamespaceShouldFail) {
  EXPECT_THAT(BuildModuleDetailsFromStatement(R"(
                   module a.b options(
                     stub_module_type='udf_server_catalog',
                     udf_namespace=1))"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       ::testing::HasSubstr(
                           "Module option udf_namespace is expected to "
                           "have type STRING, but got: INT64")));
}

TEST_F(ModuleDetailsTest, WrongTypeUdfServerImportModeShouldFail) {
  EXPECT_THAT(BuildModuleDetailsFromStatement(R"(
                   module a.b options(
                     stub_module_type='udf_server_catalog',
                     udf_server_import_mode=1))"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       ::testing::HasSubstr(
                           "Module option udf_server_import_mode is expected "
                           "to have type STRING, but got: INT64")));
}

TEST_F(ModuleDetailsTest, WrongTypeUdfScalingFactorShouldFail) {
  EXPECT_THAT(BuildModuleDetailsFromStatement(R"(
                   module a.b options(
                     stub_module_type='udf_server_catalog',
                     udf_scaling_factor='wrong'))"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       ::testing::HasSubstr(
                           "Module option udf_scaling_factor is expected to "
                           "have type DOUBLE, but got: STRING")));
}

TEST_F(ModuleDetailsTest, WrongTypeAllowedReferencesShouldFail) {
  EXPECT_THAT(BuildModuleDetailsFromStatement(R"(
                   module a.b options(allowed_references=1))"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       ::testing::HasSubstr(
                           "Module option allowed_references is expected to "
                           "have type STRING, but got: INT64")));
}

TEST_F(ModuleDetailsTest, WrongValueImportModeShouldFail) {
  EXPECT_THAT(
      BuildModuleDetailsFromStatement(R"(
                   module a.b options(
                     stub_module_type='udf_server_catalog',
                     udf_server_address='dummy_address',
                     udf_namespace='dummy_namespace',
                     udf_server_import_mode='WRONG_MODE',
                     udf_scaling_factor=2.5))"),
      StatusIs(absl::StatusCode::kInvalidArgument,
               ::testing::HasSubstr(
                   "Unrecognized mode: WRONG_MODE, allowed modes are "
                   "[SERVER_ADDRESS_FROM_MODULE,CALLER_PROVIDED,MANUAL]")));
}

TEST_F(ModuleDetailsTest, AllowedReferencesBuiltin) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ModuleDetails module_details,
                       BuildModuleDetailsFromStatement(R"(
                         module a.b options(allowed_references='BUILTIN'))"));
  EXPECT_EQ(module_details.default_resolution_scope(),
            ResolutionScope::kBuiltin);
}

TEST_F(ModuleDetailsTest, AllowedReferencesGlobal) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ModuleDetails module_details,
                       BuildModuleDetailsFromStatement(R"(
                         module a.b options(allowed_references='GLOBAL'))"));
  EXPECT_EQ(module_details.default_resolution_scope(),
            ResolutionScope::kGlobal);
}

TEST_F(ModuleDetailsTest, WrongValueAllowedReferencesShouldFail) {
  ASSERT_THAT(BuildModuleDetailsFromStatement(R"(
                   module a.b options(allowed_references='WRONG'))"),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       ::testing::HasSubstr(
                           "Allowed values for the allowed_references module "
                           "option are [BUILTIN, GLOBAL], but got: WRONG")));
}

TEST_F(ModuleDetailsTest, GenerateNonEmptyUDFModule) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ModuleDetails module_details,
                       BuildModuleDetailsFromStatement(R"(
                         module a.b options(
                           stub_module_type='udf_server_catalog',
                           udf_server_address='dummy_address',
                           udf_namespace='dummy_namespace',
                           udf_server_import_mode='CALLER_PROVIDED',
                           udf_scaling_factor=2.5))"));
  EXPECT_THAT(module_details.udf_server_options(), Optional(EqualsProto(R"pb(
                udf_server_address: "dummy_address"
                udf_server_import_mode: CALLER_PROVIDED
                udf_scaling_factor: 2.5
                udf_namespace: "dummy_namespace"
              )pb")));
}

TEST_F(ModuleDetailsTest, GlobalOptionsOverrideUDFModules) {
  ModuleOptions overrides;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        global_options {
          udf_server_address: 'global_address',
          udf_server_import_mode: SERVER_ADDRESS_FROM_MODULE,
          udf_scaling_factor: 10.1,
        }
      )pb",
      &overrides));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ModuleDetails module_details,
                       BuildModuleDetailsFromStatement(
                           R"(
                         module a.b options(
                           stub_module_type='udf_server_catalog',
                           udf_server_address='dummy_address',
                           udf_namespace='dummy_namespace',
                           udf_server_import_mode='CALLER_PROVIDED',
                           udf_scaling_factor=2.5))",
                           /*constant_evaluator=*/nullptr, overrides));
  EXPECT_THAT(module_details.udf_server_options(), Optional(EqualsProto(R"pb(
                udf_server_address: "global_address"
                udf_server_import_mode: SERVER_ADDRESS_FROM_MODULE
                udf_scaling_factor: 10.1
                udf_namespace: "dummy_namespace"
              )pb")));
}

TEST_F(ModuleDetailsTest, GlobalOptionsOverrideEmptyUDFModules) {
  ModuleOptions overrides;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        global_options {
          udf_server_address: 'global_address',
          udf_server_import_mode: SERVER_ADDRESS_FROM_MODULE,
          udf_scaling_factor: 10.1,
        }
      )pb",
      &overrides));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ModuleDetails module_details,
                       BuildModuleDetailsFromStatement(
                           R"(
                         module a.b options(
                           stub_module_type='udf_server_catalog'))",
                           /*constant_evaluator=*/nullptr, overrides));
  EXPECT_THAT(module_details.udf_server_options(), Optional(EqualsProto(R"pb(
                udf_server_address: "global_address"
                udf_server_import_mode: SERVER_ADDRESS_FROM_MODULE
                udf_scaling_factor: 10.1
              )pb")));
}

TEST_F(ModuleDetailsTest, GlobalOptionsOverrideUnspecifiedFields) {
  ModuleOptions overrides;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        global_options {
          udf_server_address: 'global_address',
          udf_server_import_mode: SERVER_ADDRESS_FROM_MODULE,
          udf_scaling_factor: 10.1,
        }
      )pb",
      &overrides));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ModuleDetails module_details,
                       BuildModuleDetailsFromStatement(
                           R"(
                         module a.b options(
                           stub_module_type='udf_server_catalog',
                           udf_namespace='dummy_namespace',
                           udf_scaling_factor=2.5))",
                           /*constant_evaluator=*/nullptr, overrides));
  EXPECT_THAT(module_details.udf_server_options(), Optional(EqualsProto(R"pb(
                udf_server_address: "global_address"
                udf_server_import_mode: SERVER_ADDRESS_FROM_MODULE
                udf_scaling_factor: 10.1
                udf_namespace: "dummy_namespace"
              )pb")));
}

TEST_F(ModuleDetailsTest, PerModuleOptionsOverrideUDFModules) {
  ModuleOptions overrides;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        per_module_options {
          key: 'a.b'
          value {
            udf_server_address: 'per_module_address_a_b',
            udf_server_import_mode: SERVER_ADDRESS_FROM_MODULE,
            udf_scaling_factor: 10.1,
          }
        }
      )pb",
      &overrides));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ModuleDetails module_details,
                       BuildModuleDetailsFromStatement(
                           R"(
                         module a.b options(
                           stub_module_type='udf_server_catalog',
                           udf_server_address='dummy_address',
                           udf_namespace='dummy_namespace',
                           udf_server_import_mode='CALLER_PROVIDED',
                           udf_scaling_factor=2.5))",
                           /*constant_evaluator=*/nullptr, overrides));
  EXPECT_THAT(module_details.udf_server_options(), Optional(EqualsProto(R"pb(
                udf_server_address: "per_module_address_a_b"
                udf_server_import_mode: SERVER_ADDRESS_FROM_MODULE
                udf_scaling_factor: 10.1
                udf_namespace: "dummy_namespace"
              )pb")));
}

TEST_F(ModuleDetailsTest, PerModuleOptionsOverrideUnspecifiedFields) {
  ModuleOptions overrides;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        per_module_options {
          key: 'a.b'
          value {
            udf_server_address: 'per_module_address_a_b',
            udf_server_import_mode: SERVER_ADDRESS_FROM_MODULE,
            udf_scaling_factor: 10.1,
          }
        }
      )pb",
      &overrides));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ModuleDetails module_details,
                       BuildModuleDetailsFromStatement(
                           R"(
                         module a.b options(
                           stub_module_type='udf_server_catalog',
                           udf_namespace='dummy_namespace',
                           udf_scaling_factor=2.5))",
                           /*constant_evaluator=*/nullptr, overrides));
  EXPECT_THAT(module_details.udf_server_options(), Optional(EqualsProto(R"pb(
                udf_server_address: "per_module_address_a_b"
                udf_server_import_mode: SERVER_ADDRESS_FROM_MODULE
                udf_scaling_factor: 10.1
                udf_namespace: "dummy_namespace"
              )pb")));
}

TEST_F(ModuleDetailsTest, PerModuleOptionsOverrideEmptyUDFModules) {
  ModuleOptions overrides;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        per_module_options {
          key: 'a.b'
          value {
            udf_server_address: 'per_module_address_a_b',
            udf_server_import_mode: SERVER_ADDRESS_FROM_MODULE,
            udf_scaling_factor: 10.1,
          }
        }
      )pb",
      &overrides));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ModuleDetails module_details,
                       BuildModuleDetailsFromStatement(
                           R"(
                         module a.b options(
                           stub_module_type='udf_server_catalog'))",
                           /*constant_evaluator=*/nullptr, overrides));
  EXPECT_THAT(module_details.udf_server_options(), Optional(EqualsProto(R"pb(
                udf_server_address: "per_module_address_a_b"
                udf_server_import_mode: SERVER_ADDRESS_FROM_MODULE
                udf_scaling_factor: 10.1
              )pb")));
}

TEST_F(ModuleDetailsTest, PerModuleOptionsForOtherModuleDoesNotOverride) {
  ModuleOptions overrides;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        per_module_options {
          key: 'a.c'
          value {
            udf_server_address: 'per_module_address_a_b',
            udf_server_import_mode: SERVER_ADDRESS_FROM_MODULE,
            udf_scaling_factor: 10.1,
          }
        }
      )pb",
      &overrides));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ModuleDetails module_details,
                       BuildModuleDetailsFromStatement(
                           R"(
                         module a.b options(
                           stub_module_type='udf_server_catalog',
                           udf_server_address='dummy_address',
                           udf_namespace='dummy_namespace',
                           udf_server_import_mode='CALLER_PROVIDED',
                           udf_scaling_factor=2.5))",
                           /*constant_evaluator=*/nullptr, overrides));
  EXPECT_THAT(module_details.udf_server_options(), Optional(EqualsProto(R"pb(
                udf_server_address: "dummy_address"
                udf_server_import_mode: CALLER_PROVIDED
                udf_scaling_factor: 2.5
                udf_namespace: "dummy_namespace"
              )pb")));
}

TEST_F(ModuleDetailsTest, PerModuleOptionsOverrideGlobalOptions) {
  ModuleOptions overrides;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        global_options {
          udf_server_address: 'global_address',
          udf_server_import_mode: SERVER_ADDRESS_FROM_MODULE,
          udf_scaling_factor: 10.1,
        }
        per_module_options {
          key: 'a.b'
          value {
            udf_server_address: 'per_module_address_a_b',
            udf_scaling_factor: 100.1,
          }
        }
      )pb",
      &overrides));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ModuleDetails module_details,
                       BuildModuleDetailsFromStatement(
                           R"(
                         module a.b options(
                           stub_module_type='udf_server_catalog',
                           udf_namespace='dummy_namespace',
                           udf_scaling_factor=2.5))",
                           /*constant_evaluator=*/nullptr, overrides));
  EXPECT_THAT(module_details.udf_server_options(), Optional(EqualsProto(R"pb(
                udf_server_address: "per_module_address_a_b"
                udf_server_import_mode: SERVER_ADDRESS_FROM_MODULE
                udf_scaling_factor: 100.1
                udf_namespace: "dummy_namespace"
              )pb")));
}

}  // namespace googlesql
