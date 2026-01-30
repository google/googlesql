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

#include "googlesql/public/simple_catalog_util.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status_macros.h"

using ::testing::HasSubstr;
using ::testing::MatchesRegex;
using ::testing::Not;
using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;

namespace googlesql {

TEST(SimpleCatalogUtilTest, AddFunctionFromCreateFunctionTest) {
  SimpleCatalog simple("simple");
  AnalyzerOptions analyzer_options;
  std::unique_ptr<const AnalyzerOutput> analyzer_output;

  // Invalid analyzer options
  EXPECT_THAT(
      AddFunctionFromCreateFunction(
          "CREATE TEMP FUNCTION Basic() AS (1)", analyzer_options,
          /*allow_persistent_function=*/false,
          /*function_options=*/nullptr, analyzer_output, simple, simple),
      Not(IsOk()));

  analyzer_options.mutable_language()->AddSupportedStatementKind(
      RESOLVED_CREATE_FUNCTION_STMT);
  GOOGLESQL_EXPECT_OK(AddFunctionFromCreateFunction(
      "CREATE TEMP FUNCTION Basic() AS (1)", analyzer_options,
      /*allow_persistent_function=*/false, /*function_options=*/nullptr,
      analyzer_output, simple, simple));
  const Function* function;
  GOOGLESQL_EXPECT_OK(simple.FindFunction({"Basic"}, &function));
  EXPECT_EQ(function->FullName(), "Lazy_resolution_function:Basic");

  // Duplicate
  EXPECT_THAT(
      AddFunctionFromCreateFunction(
          "CREATE TEMP FUNCTION Basic() AS (1)", analyzer_options,
          /*allow_persistent_function=*/false,
          /*function_options=*/nullptr, analyzer_output, simple, simple),
      Not(IsOk()));

  // Invalid persistent function.
  EXPECT_THAT(
      AddFunctionFromCreateFunction(
          "CREATE FUNCTION Persistent() AS (1)", analyzer_options,
          /*allow_persistent_function=*/false,
          /*function_options=*/nullptr, analyzer_output, simple, simple),
      Not(IsOk()));
  GOOGLESQL_EXPECT_OK(AddFunctionFromCreateFunction(
      "CREATE FUNCTION Persistent() AS (1)", analyzer_options,
      /*allow_persistent_function=*/true, /*function_options=*/nullptr,
      analyzer_output, simple, simple));

  // Analysis failure
  EXPECT_THAT(
      AddFunctionFromCreateFunction(
          "CREATE TEMP FUNCTION Template(arg ANY TYPE) AS (arg)",
          analyzer_options, /*allow_persistent_function=*/false,
          /*function_options=*/nullptr, analyzer_output, simple, simple),
      Not(IsOk()));
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_TEMPLATE_FUNCTIONS);
  GOOGLESQL_EXPECT_OK(AddFunctionFromCreateFunction(
      "CREATE TEMP FUNCTION Template(arg ANY TYPE) AS (arg)", analyzer_options,
      /*allow_persistent_function=*/false, /*function_options=*/nullptr,
      analyzer_output, simple, simple));
  GOOGLESQL_EXPECT_OK(simple.FindFunction({"Template"}, &function));
  EXPECT_EQ(function->FullName(), "Templated_SQL_Function:Template");

  // Different resolving catalog.
  std::unique_ptr<googlesql::SimpleConstant> constant;
  GOOGLESQL_ASSERT_OK(
      SimpleConstant::Create({"TestConstant"}, Value::Int32(42), &constant));
  SimpleCatalog resolving_catalog("resolving");
  resolving_catalog.AddOwnedConstant("TestConstant", std::move(constant));
  GOOGLESQL_EXPECT_OK(AddFunctionFromCreateFunction(
      "CREATE TEMP FUNCTION MyFunc() RETURNS INT32 AS (TestConstant)",
      analyzer_options,
      /*allow_persistent_function=*/false, /*function_options=*/nullptr,
      analyzer_output, resolving_catalog, simple));
  GOOGLESQL_EXPECT_OK(simple.FindFunction({"MyFunc"}, &function));
  EXPECT_EQ(function->FullName(), "Lazy_resolution_function:MyFunc");
  EXPECT_THAT(resolving_catalog.FindFunction({"MyFunc"}, &function),
              Not(IsOk()));

  GOOGLESQL_EXPECT_OK(AddFunctionFromCreateFunction(
      "CREATE TEMP FUNCTION NonSQL(x INT64) RETURNS DOUBLE LANGUAGE C",
      analyzer_options,
      /*allow_persistent_function=*/false, /*function_options=*/nullptr,
      analyzer_output, simple, simple));
  GOOGLESQL_EXPECT_OK(simple.FindFunction({"NonSQL"}, &function));
  EXPECT_EQ(function->FullName(), "External_function:NonSQL");
}

TEST(SimpleCatalogUtilTest, MakeFunctionFromCreateFunctionBasic) {
  SimpleCatalog catalog("simple");
  AnalyzerOptions analyzer_options;
  analyzer_options.mutable_language()->AddSupportedStatementKind(
      RESOLVED_CREATE_FUNCTION_STMT);

  TypeFactory type_factory;
  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  GOOGLESQL_EXPECT_OK(AnalyzeStatement("CREATE TEMP FUNCTION Basic() AS (1)",
                             analyzer_options, &catalog, &type_factory,
                             &analyzer_output));

  ASSERT_TRUE(
      analyzer_output->resolved_statement()->Is<ResolvedCreateFunctionStmt>());

  const ResolvedCreateFunctionStmt* create_function_stmt =
      analyzer_output->resolved_statement()
          ->GetAs<ResolvedCreateFunctionStmt>();

  FunctionOptions function_options;
  function_options.set_is_deprecated(true);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<Function> function,
      MakeFunctionFromCreateFunction(*create_function_stmt, &function_options));
  EXPECT_EQ(function->Name(), "Basic");
  EXPECT_TRUE(function->IsDeprecated());
  EXPECT_EQ(function->mode(), Function::SCALAR);
  EXPECT_EQ(function->NumSignatures(), 1);

  // Use string comparison as a proxy for signature equality.
  EXPECT_EQ(FunctionSignature::SignaturesToString(function->signatures()),
            FunctionSignature::SignaturesToString(
                {create_function_stmt->signature()}));
}

TEST(SimpleCatalogUtilTest, MakeFunctionFromCreateFunctionAgg) {
  SimpleCatalog catalog("simple");
  AnalyzerOptions analyzer_options;
  analyzer_options.mutable_language()->AddSupportedStatementKind(
      RESOLVED_CREATE_FUNCTION_STMT);
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_CREATE_AGGREGATE_FUNCTION);
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_TEMPLATE_FUNCTIONS);

  TypeFactory type_factory;
  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  GOOGLESQL_EXPECT_OK(AnalyzeStatement(
      "CREATE AGGREGATE FUNCTION Path.to.F(x any type) AS (sum(x))",
      analyzer_options, &catalog, &type_factory, &analyzer_output));

  ASSERT_TRUE(
      analyzer_output->resolved_statement()->Is<ResolvedCreateFunctionStmt>());

  const ResolvedCreateFunctionStmt* create_function_stmt =
      analyzer_output->resolved_statement()
          ->GetAs<ResolvedCreateFunctionStmt>();

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<Function> function,
      MakeFunctionFromCreateFunction(*create_function_stmt,
                                     /*function_options=*/nullptr));
  EXPECT_EQ(function->Name(), "F");
  EXPECT_THAT(function->FunctionNamePath(),
              testing::ElementsAre("Path", "to", "F"));
  EXPECT_EQ(function->mode(), Function::AGGREGATE);
  EXPECT_EQ(function->NumSignatures(), 1);

  // Use string comparison as a proxy for signature equality.
  EXPECT_EQ(FunctionSignature::SignaturesToString(function->signatures()),
            FunctionSignature::SignaturesToString(
                {create_function_stmt->signature()}));
}

TEST(SimpleCatalogUtilTest, AddTableFromCreateTable) {
  SimpleCatalog catalog("simple");
  SimpleTable* table;
  AnalyzerOptions analyzer_options;
  std::unique_ptr<const AnalyzerOutput> analyzer_output;

  const char* create_t1 = "CREATE TEMP TABLE t1 (x INT64)";

  // Invalid analyzer options
  EXPECT_THAT(AddTableFromCreateTable(create_t1, analyzer_options,
                                      /*allow_non_temp=*/false, analyzer_output,
                                      table, catalog),
              Not(IsOk()));

  analyzer_options.mutable_language()->AddSupportedStatementKind(
      RESOLVED_CREATE_TABLE_STMT);
  GOOGLESQL_EXPECT_OK(AddTableFromCreateTable(create_t1, analyzer_options,
                                    /*allow_non_temp=*/false, analyzer_output,
                                    table, catalog));

  // Duplicate table.
  EXPECT_THAT(AddTableFromCreateTable(create_t1, analyzer_options,
                                      /*allow_non_temp=*/false, analyzer_output,
                                      table, catalog),
              Not(IsOk()));

  const char* create_t2 = "CREATE TABLE t2 (x INT64)";

  // Invalid persistent table.
  EXPECT_THAT(AddTableFromCreateTable(create_t2, analyzer_options,
                                      /*allow_non_temp=*/false, analyzer_output,
                                      table, catalog),
              Not(IsOk()));

  GOOGLESQL_EXPECT_OK(AddTableFromCreateTable(create_t2, analyzer_options,
                                    /*allow_non_temp=*/true, analyzer_output,
                                    table, catalog));

  // Check the table got created correctly.
  const Table* found_table;
  GOOGLESQL_EXPECT_OK(catalog.FindTable({"t2"}, &found_table));
  EXPECT_EQ(table, found_table);
  EXPECT_EQ(found_table->Name(), "t2");
  EXPECT_EQ(found_table->FullName(), "t2");
  EXPECT_EQ(found_table->NumColumns(), 1);
  const Column* found_column = found_table->GetColumn(0);
  EXPECT_EQ(found_column->Name(), "x");
  EXPECT_EQ(found_column->GetType()->DebugString(), "INT64");

  // Check we get an error if the CREATE has any modifiers that aren't
  // handled by AddTableFromCreateTable.
  const char* create_t3 = "CREATE TEMP TABLE t3 (x INT64) OPTIONS(opt=2)";

  EXPECT_THAT(AddTableFromCreateTable(create_t3, analyzer_options,
                                      /*allow_non_temp=*/false, analyzer_output,
                                      table, catalog),
              StatusIs(absl::StatusCode::kUnimplemented));
}

TEST(SimpleCatalogUtilTest, AddTVFFromCreateTableFunction) {
  SimpleCatalog catalog("simple");
  AnalyzerOptions analyzer_options;
  std::unique_ptr<const AnalyzerOutput> analyzer_output;

  // Non-templated SQL TVF, with TEMP.
  const char* create_tvf1 =
      "CREATE TEMP TABLE FUNCTION tvf1(x INT64) AS (SELECT x)";

  // Invalid analyzer options
  EXPECT_THAT(AddTVFFromCreateTableFunction(create_tvf1, analyzer_options,
                                            /*allow_persistent=*/false,
                                            analyzer_output, catalog),
              Not(IsOk()));

  analyzer_options.mutable_language()->AddSupportedStatementKind(
      RESOLVED_CREATE_TABLE_FUNCTION_STMT);
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_CREATE_TABLE_FUNCTION);
  analyzer_options.mutable_language()->EnableLanguageFeature(
      FEATURE_TEMPLATE_FUNCTIONS);
  GOOGLESQL_EXPECT_OK(AddTVFFromCreateTableFunction(create_tvf1, analyzer_options,
                                          /*allow_persistent=*/false,
                                          analyzer_output, catalog));

  // Check the TVF got created correctly.
  const TableValuedFunction* found_tvf;
  GOOGLESQL_EXPECT_OK(catalog.FindTableValuedFunction({"tvf1"}, &found_tvf));
  EXPECT_EQ(found_tvf->Name(), "tvf1");
  EXPECT_EQ(found_tvf->FullName(), "tvf1");
  EXPECT_EQ(found_tvf->NumSignatures(), 1);
  EXPECT_EQ(found_tvf->GetSignature(0)->DebugString(),
            "(INT64 x) -> TABLE<x INT64>");

  // Duplicate TVF.
  EXPECT_THAT(AddTVFFromCreateTableFunction(create_tvf1, analyzer_options,
                                            /*allow_persistent=*/false,
                                            analyzer_output, catalog),
              Not(IsOk()));

  // Templated SQL TVF, without TEMP.
  const char* create_tvf2 =
      "CREATE TABLE FUNCTION tvf2(t ANY TABLE) AS (SELECT * FROM t)";

  // Invalid non-TEMP table.
  EXPECT_THAT(AddTVFFromCreateTableFunction(create_tvf2, analyzer_options,
                                            /*allow_persistent=*/false,
                                            analyzer_output, catalog),
              Not(IsOk()));

  // Allowed if allow_persistent is true.
  GOOGLESQL_EXPECT_OK(AddTVFFromCreateTableFunction(create_tvf2, analyzer_options,
                                          /*allow_persistent=*/true,
                                          analyzer_output, catalog));

  // Non-SQL TVF with a fixed output schema.
  const char* create_tvf3 =
      "CREATE TABLE FUNCTION tvf3(t ANY TABLE) RETURNS TABLE<x INT64>";

  GOOGLESQL_EXPECT_OK(AddTVFFromCreateTableFunction(create_tvf3, analyzer_options,
                                          /*allow_persistent=*/true,
                                          analyzer_output, catalog));

  // Non-SQL TVF without a fixed output schema.
  const char* create_tvf4 = "CREATE TABLE FUNCTION tvf4(t ANY TABLE)";

  EXPECT_THAT(
      AddTVFFromCreateTableFunction(create_tvf4, analyzer_options,
                                    /*allow_persistent=*/true, analyzer_output,
                                    catalog),
      StatusIs(
          absl::StatusCode::kInternal,
          MatchesRegex(
              ".*Only TVFs with fixed output table schemas are supported.*")));

  // Check we get an error if the CREATE has any modifiers that aren't
  // handled by AddTVFFromCreateTableFunction.
  const char* create_tvf5 =
      "CREATE TABLE FUNCTION tvf2(t ANY TABLE) "
      "OPTIONS (opt=5) "
      "AS (SELECT * FROM t)";

  EXPECT_THAT(AddTVFFromCreateTableFunction(create_tvf5, analyzer_options,
                                            /*allow_persistent=*/true,
                                            analyzer_output, catalog),
              StatusIs(absl::StatusCode::kUnimplemented));
}

// Helper function to analyze a CREATE TABLE statement and return the resolved
// statement.
static absl::StatusOr<std::unique_ptr<const AnalyzerOutput>>
AnalyzeCreateTableStmt(absl::string_view sql, AnalyzerOptions& options,
                       Catalog& catalog, TypeFactory& type_factory) {
  options.mutable_language()->AddSupportedStatementKind(
      RESOLVED_CREATE_TABLE_STMT);
  std::unique_ptr<const AnalyzerOutput> analyzer_output;
  GOOGLESQL_RETURN_IF_ERROR(AnalyzeStatement(sql, options, &catalog, &type_factory,
                                   &analyzer_output));
  if (!analyzer_output->resolved_statement()->Is<ResolvedCreateTableStmt>()) {
    return absl::InvalidArgumentError(
        "Statement is not a CREATE TABLE statement");
  }
  return analyzer_output;
}

struct MakeTableFromCreateTableTestParams {
  std::string name;
  std::string create_sql;
  absl::Status expected_analysis_status = absl::OkStatus();
  absl::Status expected_make_table_status = absl::OkStatus();
  std::optional<std::vector<int>> expected_primary_key;
};

class MakeTableFromCreateTableTest
    : public ::testing::TestWithParam<MakeTableFromCreateTableTestParams> {
 protected:
  MakeTableFromCreateTableTest()
      : catalog_("simple"), analyzer_options_([] {
          AnalyzerOptions options;
          options.mutable_language()->AddSupportedStatementKind(
              RESOLVED_CREATE_TABLE_STMT);
          options.mutable_language()->EnableLanguageFeature(
              FEATURE_UNENFORCED_PRIMARY_KEYS);
          return options;
        }()) {}

  SimpleCatalog catalog_;
  AnalyzerOptions analyzer_options_;
  TypeFactory type_factory_;
};

TEST_P(MakeTableFromCreateTableTest, MakeTableFromCreateTable) {
  const MakeTableFromCreateTableTestParams& params = GetParam();

  absl::StatusOr<std::unique_ptr<const AnalyzerOutput>> analyzer_output =
      AnalyzeCreateTableStmt(params.create_sql, analyzer_options_, catalog_,
                             type_factory_);

  if (!params.expected_analysis_status.ok()) {
    EXPECT_THAT(analyzer_output.status(),
                StatusIs(params.expected_analysis_status.code(),
                         HasSubstr(params.expected_analysis_status.message())));
    return;
  }
  GOOGLESQL_ASSERT_OK(analyzer_output);

  const auto* stmt = (*analyzer_output)
                         ->resolved_statement()
                         ->GetAs<ResolvedCreateTableStmt>();

  absl::StatusOr<std::unique_ptr<SimpleTable>> table =
      MakeTableFromCreateTable(*stmt);

  if (!params.expected_make_table_status.ok()) {
    EXPECT_THAT(
        table.status(),
        StatusIs(params.expected_make_table_status.code(),
                 HasSubstr(params.expected_make_table_status.message())));
    return;
  }
  GOOGLESQL_ASSERT_OK(table);

  if (params.expected_primary_key.has_value()) {
    EXPECT_TRUE((*table)->PrimaryKey().has_value());
    EXPECT_THAT((*table)->PrimaryKey().value(),
                testing::ElementsAreArray(params.expected_primary_key.value()));
  } else {
    EXPECT_FALSE((*table)->PrimaryKey().has_value());
  }
}

INSTANTIATE_TEST_SUITE_P(
    MakeTableFromCreateTableTests, MakeTableFromCreateTableTest,
    ::testing::ValuesIn<MakeTableFromCreateTableTestParams>({
        {
            .name = "NoPrimaryKey",
            .create_sql = "CREATE TABLE t (c1 INT64)",
        },
        {
            .name = "PrimaryKeySingleColumn",
            .create_sql =
                "CREATE TABLE t (k1 INT64, v STRING, PRIMARY KEY (k1))",
            .expected_primary_key = std::vector<int>{0},
        },
        {
            .name = "PrimaryKeyMultipleColumns",
            .create_sql = "CREATE TABLE t (c1 INT64, c2 STRING, c3 BOOL, "
                          "PRIMARY KEY (c2, c1))",
            .expected_primary_key = std::vector<int>{1, 0},
        },
        {
            .name = "PrimaryKeyZeroColumns",
            .create_sql = "CREATE TABLE t (c1 INT64, c2 STRING, c3 BOOL, "
                          "PRIMARY KEY ())",
            .expected_primary_key = std::vector<int>{},
        },
        {
            .name = "PrimaryKeyUnenforcedFieldNotAccessed",
            .create_sql =
                "CREATE TABLE t (c1 INT64, PRIMARY KEY (c1) NOT ENFORCED)",
            .expected_make_table_status = absl::UnimplementedError(
                "Unimplemented feature (ResolvedPrimaryKey::unenforced not "
                "accessed and has non-default value)"),
        },
        {
            .name = "PrimaryKeyCaseInsensitive",
            .create_sql = "CREATE TABLE t (ColA INT64, ColB STRING, PRIMARY "
                          "KEY (cOLb, COLa))",
            .expected_primary_key = std::vector<int>{1, 0},
        },
        {
            .name = "PrimaryKeyColumnNotFound",
            .create_sql =
                "CREATE TABLE t (c1 INT64, PRIMARY KEY (non_existent))",
            .expected_analysis_status = absl::InvalidArgumentError(
                "Unsupported primary key column non_existent either does not "
                "exist or is a pseudocolumn"),
        },
    }),
    [](const ::testing::TestParamInfo<MakeTableFromCreateTableTest::ParamType>&
           info) { return info.param.name; });

}  // namespace googlesql
