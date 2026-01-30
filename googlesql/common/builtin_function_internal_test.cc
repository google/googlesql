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

#include "googlesql/common/builtin_function_internal.h"

#include <memory>
#include <string>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/type_factory.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"

namespace googlesql {
namespace {

using ::testing::Eq;
using ::testing::Key;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;
using ::absl_testing::StatusIs;

TEST(BuiltinFunctionInternalTest, InsertSimpleTableValuedFunction) {
  TypeFactory type_factory;
  absl::flat_hash_map<std::string, std::unique_ptr<TableValuedFunction>> tvfs;
  GoogleSQLBuiltinFunctionOptions options;
  std::vector<FunctionSignatureOnHeap> signatures;
  signatures.push_back({ARG_TYPE_RELATION, {type_factory.get_string()}, -1});
  GOOGLESQL_ASSERT_OK(InsertSimpleTableValuedFunction(&tvfs, options, "test_tvf",
                                            signatures, {}));
  EXPECT_THAT(tvfs, UnorderedElementsAre(Key("test_tvf")));
  EXPECT_THAT(tvfs["test_tvf"]->signatures(), SizeIs(1));
}

TEST(BuiltinFunctionInternalTest,
     InsertSimpleTableValuedFunctionSignaturesIsEmpty) {
  TypeFactory type_factory;
  absl::flat_hash_map<std::string, std::unique_ptr<TableValuedFunction>> tvfs;
  GoogleSQLBuiltinFunctionOptions options;
  GOOGLESQL_ASSERT_OK(
      InsertSimpleTableValuedFunction(&tvfs, options, "test_tvf", {}, {}));
  EXPECT_THAT(tvfs, UnorderedElementsAre());
}

TEST(BuiltinFunctionInternalTest,
     InsertSimpleTableValuedFunctionSignaturesAreDisabled) {
  TypeFactory type_factory;
  absl::flat_hash_map<std::string, std::unique_ptr<TableValuedFunction>> tvfs;
  GoogleSQLBuiltinFunctionOptions options;
  std::vector<FunctionSignatureOnHeap> signatures;
  signatures.push_back({ARG_TYPE_RELATION, {type_factory.get_string()}, -1});
  options.exclude_function_ids.emplace(static_cast<FunctionSignatureId>(-1));
  GOOGLESQL_ASSERT_OK(InsertSimpleTableValuedFunction(&tvfs, options, "test_tvf",
                                            signatures, {}));
  EXPECT_THAT(tvfs, UnorderedElementsAre());
}

TEST(BuiltinFunctionInternalTest,
     InsertSimpleTableValuedFunctionSignaturesHaveRewriteOptions) {
  TypeFactory type_factory;
  absl::flat_hash_map<std::string, std::unique_ptr<TableValuedFunction>> tvfs;
  GoogleSQLBuiltinFunctionOptions options;
  std::vector<FunctionSignatureOnHeap> signatures;
  signatures.push_back({ARG_TYPE_RELATION,
                        {type_factory.get_string()},
                        -1,
                        FunctionSignatureOptions().set_rewrite_options(
                            FunctionSignatureRewriteOptions())});
  EXPECT_THAT(InsertSimpleTableValuedFunction(&tvfs, options, "test_tvf",
                                              signatures, {}),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(BuiltinFunctionInternalTest,
     InsertSimpleTableValuedFunctionsWithExcludeOptions) {
  TypeFactory type_factory;
  absl::flat_hash_map<std::string, std::unique_ptr<TableValuedFunction>> tvfs;
  GoogleSQLBuiltinFunctionOptions options;
  std::vector<FunctionSignatureOnHeap> signatures;
  signatures.push_back({ARG_TYPE_RELATION, {type_factory.get_string()}, -2});
  signatures.push_back({ARG_TYPE_RELATION, {type_factory.get_bool()}, -1});
  options.exclude_function_ids.insert(static_cast<FunctionSignatureId>(-1));
  GOOGLESQL_ASSERT_OK(InsertSimpleTableValuedFunction(&tvfs, options, "test_tvf",
                                            signatures, {}));
  EXPECT_THAT(tvfs, UnorderedElementsAre(Key("test_tvf")));
  EXPECT_THAT(tvfs["test_tvf"]->signatures(), SizeIs(1));
  EXPECT_THAT(
      tvfs["test_tvf"]->GetSignature(0)->DebugString("test_tvf"),
      Eq(FunctionSignature(signatures.at(0).Get()).DebugString("test_tvf")));
}

TEST(BuiltinFunctionInternalTest,
     InsertSimpleTableValuedFunctionsWithIncludeOptions) {
  TypeFactory type_factory;
  absl::flat_hash_map<std::string, std::unique_ptr<TableValuedFunction>> tvfs;
  GoogleSQLBuiltinFunctionOptions options;
  std::vector<FunctionSignatureOnHeap> signatures;
  signatures.push_back({ARG_TYPE_RELATION, {type_factory.get_string()}, -2});
  signatures.push_back({ARG_TYPE_RELATION, {type_factory.get_bool()}, -1});
  options.include_function_ids.insert(static_cast<FunctionSignatureId>(-1));
  GOOGLESQL_ASSERT_OK(InsertSimpleTableValuedFunction(&tvfs, options, "test_tvf",
                                            signatures, {}));
  EXPECT_THAT(tvfs, UnorderedElementsAre(Key("test_tvf")));
  EXPECT_THAT(tvfs["test_tvf"]->signatures(), SizeIs(1));
  EXPECT_THAT(
      tvfs["test_tvf"]->GetSignature(0)->DebugString("test_tvf"),
      Eq(FunctionSignature(signatures.at(1).Get()).DebugString("test_tvf")));
}

TEST(BuiltinFunctionInternalTest,
     InsertSimpleTableValuedFunctionSignaturesRewriteEnabled) {
  TypeFactory type_factory;
  absl::flat_hash_map<std::string, std::unique_ptr<TableValuedFunction>> tvfs;
  GoogleSQLBuiltinFunctionOptions options;
  std::vector<FunctionSignatureOnHeap> signatures;
  signatures.push_back({ARG_TYPE_RELATION, {type_factory.get_string()}, -1});
  options.rewrite_enabled.emplace(static_cast<FunctionSignatureId>(-1), true);
  EXPECT_THAT(InsertSimpleTableValuedFunction(&tvfs, options, "test_tvf",
                                              signatures, {}),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(BuiltinFunctionInternalTest,
     InsertSimpleTableValuedFunctionNoRequiredFeature) {
  TypeFactory type_factory;
  absl::flat_hash_map<std::string, std::unique_ptr<TableValuedFunction>> tvfs;

  LanguageOptions language_options;
  GoogleSQLBuiltinFunctionOptions no_options_enabled(language_options);

  std::vector<FunctionSignatureOnHeap> signatures;
  signatures.push_back({ARG_TYPE_RELATION, {type_factory.get_string()}, -1});

  GOOGLESQL_EXPECT_OK(InsertSimpleTableValuedFunction(&tvfs, no_options_enabled,
                                            "test_tvf", signatures, {}));
  EXPECT_TRUE(tvfs.contains("test_tvf"));
}

TEST(BuiltinFunctionInternalTest,
     InsertSimpleTableValuedFunctionOneRequiredFeature) {
  TypeFactory type_factory;
  absl::flat_hash_map<std::string, std::unique_ptr<TableValuedFunction>> tvfs;

  LanguageOptions language_options;
  GoogleSQLBuiltinFunctionOptions no_options_enabled(language_options);
  language_options.EnableLanguageFeature(FEATURE_ANALYTIC_FUNCTIONS);
  GoogleSQLBuiltinFunctionOptions options_enabled(language_options);

  TableValuedFunctionOptions tvf_options;
  tvf_options.AddRequiredLanguageFeature(FEATURE_ANALYTIC_FUNCTIONS);

  std::vector<FunctionSignatureOnHeap> signatures;
  signatures.push_back({ARG_TYPE_RELATION, {type_factory.get_string()}, -1});

  GOOGLESQL_EXPECT_OK(InsertSimpleTableValuedFunction(
      &tvfs, no_options_enabled, "test_tvf", signatures, tvf_options));
  EXPECT_FALSE(tvfs.contains("test_tvf"));
  GOOGLESQL_EXPECT_OK(InsertSimpleTableValuedFunction(&tvfs, options_enabled, "test_tvf",
                                            signatures, tvf_options));
  EXPECT_TRUE(tvfs.contains("test_tvf"));
}

TEST(BuiltinFunctionInternalTest,
     InsertSimpleTableValuedFunctionManyRequiredFeature) {
  TypeFactory type_factory;
  absl::flat_hash_map<std::string, std::unique_ptr<TableValuedFunction>> tvfs;

  LanguageOptions language_options;
  GoogleSQLBuiltinFunctionOptions no_options_enabled(language_options);

  language_options.EnableLanguageFeature(FEATURE_ANALYTIC_FUNCTIONS);
  language_options.EnableLanguageFeature(FEATURE_TABLESAMPLE);
  GoogleSQLBuiltinFunctionOptions partial_options_enabled(language_options);

  language_options.EnableLanguageFeature(FEATURE_DISALLOW_GROUP_BY_FLOAT);
  language_options.EnableLanguageFeature(FEATURE_TIMESTAMP_NANOS);
  language_options.EnableLanguageFeature(FEATURE_DML_UPDATE_WITH_JOIN);
  GoogleSQLBuiltinFunctionOptions full_options_enabled(language_options);

  TableValuedFunctionOptions tvf_options;
  tvf_options.AddRequiredLanguageFeature(FEATURE_ANALYTIC_FUNCTIONS);
  tvf_options.AddRequiredLanguageFeature(FEATURE_TABLESAMPLE);
  tvf_options.AddRequiredLanguageFeature(FEATURE_DISALLOW_GROUP_BY_FLOAT);
  tvf_options.AddRequiredLanguageFeature(FEATURE_TIMESTAMP_NANOS);
  tvf_options.AddRequiredLanguageFeature(FEATURE_DML_UPDATE_WITH_JOIN);

  std::vector<FunctionSignatureOnHeap> signatures;
  signatures.push_back({ARG_TYPE_RELATION, {type_factory.get_string()}, -1});

  GOOGLESQL_EXPECT_OK(InsertSimpleTableValuedFunction(
      &tvfs, no_options_enabled, "test_tvf", signatures, tvf_options));
  EXPECT_FALSE(tvfs.contains("test_tvf"));
  GOOGLESQL_EXPECT_OK(InsertSimpleTableValuedFunction(
      &tvfs, partial_options_enabled, "test_tvf", signatures, tvf_options));
  EXPECT_FALSE(tvfs.contains("test_tvf"));
  GOOGLESQL_EXPECT_OK(InsertSimpleTableValuedFunction(
      &tvfs, full_options_enabled, "test_tvf", signatures, tvf_options));
  EXPECT_TRUE(tvfs.contains("test_tvf"));
}

}  // namespace
}  // namespace googlesql
