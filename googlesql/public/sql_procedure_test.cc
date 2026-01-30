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

#include "googlesql/public/sql_procedure.h"

#include <memory>

#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/module_details.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "googlesql/base/testing/status_matchers.h"

namespace googlesql {
namespace {

using ::testing::NotNull;

TEST(SQLProcedureTest, Create) {
  TypeFactory type_factory;
  AnalyzerOptions options;
  options.mutable_language()->EnableMaximumLanguageFeatures();
  options.mutable_language()->SetSupportsAllStatementKinds();
  std::unique_ptr<const AnalyzerOutput> output;
  SimpleCatalog catalog("test_catalog", &type_factory);
  catalog.AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());
  GOOGLESQL_ASSERT_OK(AnalyzeStatement("CREATE PROCEDURE P() BEGIN SELECT 1; END;",
                             options, &catalog, &type_factory, &output));
  const auto* stmt =
      output->resolved_statement()->GetAs<ResolvedCreateProcedureStmt>();

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<SQLProcedure> procedure,
                       SQLProcedure::Create(stmt));

  ASSERT_THAT(procedure, NotNull());
  EXPECT_EQ(procedure->Name(), "P");
  EXPECT_EQ(procedure->resolved_statement(), stmt);
}

TEST(SQLProcedureTest, CreateWithModuleDetails) {
  TypeFactory type_factory;
  AnalyzerOptions options;
  options.mutable_language()->EnableMaximumLanguageFeatures();
  options.mutable_language()->SetSupportsAllStatementKinds();
  std::unique_ptr<const AnalyzerOutput> output;
  SimpleCatalog catalog("test_catalog", &type_factory);
  catalog.AddBuiltinFunctions(BuiltinFunctionOptions::AllReleasedFunctions());
  GOOGLESQL_ASSERT_OK(AnalyzeStatement("CREATE PROCEDURE P() BEGIN SELECT 1; END;",
                             options, &catalog, &type_factory, &output));
  const auto* stmt =
      output->resolved_statement()->GetAs<ResolvedCreateProcedureStmt>();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ModuleDetails module_details,
                       ModuleDetails::Create("a/b/c.sqlm", {}, nullptr,
                                             ModuleOptions(), {"a", "b", "c"}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<SQLProcedure> procedure,
                       SQLProcedure::Create(stmt, module_details));

  ASSERT_THAT(procedure, NotNull());
  EXPECT_EQ(procedure->Name(), "P");
  EXPECT_EQ(procedure->resolved_statement(), stmt);
  EXPECT_EQ(procedure->module_details().module_fullname(),
            module_details.module_fullname());
  EXPECT_EQ(procedure->module_details().default_resolution_scope(),
            module_details.default_resolution_scope());
  EXPECT_EQ(procedure->module_details().module_name_from_import(),
            module_details.module_name_from_import());
}

}  // namespace
}  // namespace googlesql
