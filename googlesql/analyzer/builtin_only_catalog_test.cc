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

#include "googlesql/analyzer/builtin_only_catalog.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/constant.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/simple_property_graph.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"

namespace googlesql {
namespace {

using ::testing::IsNull;
using ::absl_testing::StatusIs;

TEST(BuiltinOnlyCatalogTest, FindFunctionGoogleSQLBuiltin) {
  auto catalog = std::make_unique<SimpleCatalog>("test_catalog");
  auto function = std::make_unique<Function>(
      "TestFunction", "GoogleSQL", Function::SCALAR,
      std::vector<FunctionSignature>{{{types::Int64Type()}, {}, nullptr}});
  catalog->AddFunction(function.get());
  auto builtin_only_catalog = BuiltinOnlyCatalog("builtin_catalog", *catalog);

  const Function* out;
  GOOGLESQL_EXPECT_OK(builtin_only_catalog.FindFunction({"TestFunction"}, &out, {}));
  EXPECT_EQ(out, function.get());
}

TEST(BuiltinOnlyCatalogTest, FindFunctionNonBuiltin) {
  auto catalog = std::make_unique<SimpleCatalog>("test_catalog");
  auto function = std::make_unique<Function>(
      "TestFunction", "NotBuiltin", Function::SCALAR,
      std::vector<FunctionSignature>{{{types::Int64Type()}, {}, nullptr}});
  catalog->AddFunction(function.get());
  auto builtin_only_catalog = BuiltinOnlyCatalog("builtin_catalog", *catalog);

  // By default, don't allow non-GoogleSQL-builtin functions; return
  // kInvalidArgument if the underlying catalog returns such a function.
  const Function* out;
  EXPECT_THAT(builtin_only_catalog.FindFunction({"TestFunction"}, &out, {}),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(out, IsNull());
}

TEST(BuiltinOnlyCatalogTest, FindFunctionEngineBuiltin) {
  auto catalog = std::make_unique<SimpleCatalog>("test_catalog");
  auto function = std::make_unique<Function>(
      "TestFunction", "EngineBuiltin", Function::SCALAR,
      std::vector<FunctionSignature>{{{types::Int64Type()}, {}, nullptr}});
  catalog->AddFunction(function.get());
  auto builtin_only_catalog = BuiltinOnlyCatalog("builtin_catalog", *catalog);

  // Caller can specify allowed function groups to treat as builtin.
  const Function* out;
  builtin_only_catalog.set_allowed_function_groups({"EngineBuiltin"});
  GOOGLESQL_EXPECT_OK(builtin_only_catalog.FindFunction({"TestFunction"}, &out, {}));
  EXPECT_EQ(out, function.get());
}

TEST(BuiltinOnlyCatalogTest, FindFunctionEngineBuiltinCaseSensitive) {
  auto catalog = std::make_unique<SimpleCatalog>("test_catalog");
  auto function = std::make_unique<Function>(
      "TestFunction", "EngineBuiltin", Function::SCALAR,
      std::vector<FunctionSignature>{{{types::Int64Type()}, {}, nullptr}});
  catalog->AddFunction(function.get());
  auto builtin_only_catalog = BuiltinOnlyCatalog("builtin_catalog", *catalog);

  // Allowed function groups are case sensitive.
  builtin_only_catalog.set_allowed_function_groups({"ENGINEBuiltin"});
  const Function* out;
  EXPECT_THAT(builtin_only_catalog.FindFunction({"TestFunction"}, &out, {}),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(out, IsNull());
}

TEST(BuiltinOnlyCatalogTest, FindFunctionResetAllows) {
  auto catalog = std::make_unique<SimpleCatalog>("test_catalog");
  auto function = std::make_unique<Function>(
      "TestFunction", "EngineBuiltin", Function::SCALAR,
      std::vector<FunctionSignature>{{{types::Int64Type()}, {}, nullptr}});
  catalog->AddFunction(function.get());
  auto builtin_only_catalog = BuiltinOnlyCatalog("builtin_catalog", *catalog);

  // Reset function restores default behavior.
  const Function* out;
  builtin_only_catalog.set_allowed_function_groups({"EngineBuiltin"});
  builtin_only_catalog.reset_allows();
  EXPECT_THAT(builtin_only_catalog.FindFunction({"TestFunction"}, &out, {}),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(out, IsNull());
}

TEST(BuiltinOnlyCatalogTest, FindTVFGoogleSQLBuiltin) {
  auto catalog = std::make_unique<SimpleCatalog>("test_catalog");
  std::vector<std::string> name_path = {"TestFunction"};
  auto function = std::make_unique<TableValuedFunction>(name_path, "GoogleSQL");
  catalog->AddTableValuedFunction(function.get());
  auto builtin_only_catalog = BuiltinOnlyCatalog("builtin_catalog", *catalog);

  const TableValuedFunction* out;
  GOOGLESQL_EXPECT_OK(builtin_only_catalog.FindTableValuedFunction(name_path, &out, {}));
  EXPECT_EQ(out, function.get());
}

TEST(BuiltinOnlyCatalogTest, FindTVFNonBuiltin) {
  auto catalog = std::make_unique<SimpleCatalog>("test_catalog");
  std::vector<std::string> name_path = {"TestFunction"};
  auto function =
      std::make_unique<TableValuedFunction>(name_path, "NotBuiltin");
  catalog->AddTableValuedFunction(function.get());
  auto builtin_only_catalog = BuiltinOnlyCatalog("builtin_catalog", *catalog);

  // By default, don't allow non-GoogleSQL-builtin TVFs; return
  // kInvalidArgument if the underlying catalog returns such a TVF.
  const TableValuedFunction* out;
  EXPECT_THAT(
      builtin_only_catalog.FindTableValuedFunction({"TestFunction"}, &out, {}),
      StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(out, IsNull());
}

TEST(BuiltinOnlyCatalogTest, FindTVFEngineBuiltin) {
  auto catalog = std::make_unique<SimpleCatalog>("test_catalog");
  std::vector<std::string> name_path = {"TestFunction"};
  auto function =
      std::make_unique<TableValuedFunction>(name_path, "EngineBuiltin");
  catalog->AddTableValuedFunction(function.get());
  auto builtin_only_catalog = BuiltinOnlyCatalog("builtin_catalog", *catalog);

  // Caller can specify allowed groups to be treat as builtin.
  const TableValuedFunction* out;
  builtin_only_catalog.set_allowed_function_groups({"EngineBuiltin"});
  GOOGLESQL_EXPECT_OK(builtin_only_catalog.FindTableValuedFunction(name_path, &out, {}));
  EXPECT_EQ(out, function.get());
}

TEST(BuiltinOnlyCatalogTest, FindTVFEngineBuiltinCaseSensitive) {
  auto catalog = std::make_unique<SimpleCatalog>("test_catalog");
  std::vector<std::string> name_path = {"TestFunction"};
  auto function =
      std::make_unique<TableValuedFunction>(name_path, "EngineBuiltin");
  catalog->AddTableValuedFunction(function.get());
  auto builtin_only_catalog = BuiltinOnlyCatalog("builtin_catalog", *catalog);

  // Allowed groups are case sensitive.
  const TableValuedFunction* out;
  builtin_only_catalog.set_allowed_function_groups({"ENGINEBuiltin"});
  EXPECT_THAT(builtin_only_catalog.FindTableValuedFunction(name_path, &out, {}),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(out, IsNull());
}

TEST(BuiltinOnlyCatalogTest, FindTVFResetAllows) {
  auto catalog = std::make_unique<SimpleCatalog>("test_catalog");
  std::vector<std::string> name_path = {"TestFunction"};
  auto function =
      std::make_unique<TableValuedFunction>(name_path, "EngineBuiltin");
  catalog->AddTableValuedFunction(function.get());
  auto builtin_only_catalog = BuiltinOnlyCatalog("builtin_catalog", *catalog);

  // Reset allows function restores default behavior.
  const TableValuedFunction* out;
  builtin_only_catalog.set_allowed_function_groups({"EngineBuiltin"});
  builtin_only_catalog.reset_allows();
  EXPECT_THAT(builtin_only_catalog.FindTableValuedFunction(name_path, &out, {}),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(out, IsNull());
}

TEST(BuiltinOnlyCatalogTest, FindTableDisallowed) {
  auto catalog = std::make_unique<SimpleCatalog>("test_catalog");
  auto table = std::make_unique<SimpleTable>(
      "TestTable",
      std::vector<SimpleTable::NameAndType>{{"a", types::Int64Type()}});
  catalog->AddTable(table.get());
  auto builtin_only_catalog = BuiltinOnlyCatalog("builtin_catalog", *catalog);

  // By default, don't allow tables; always return kNotFound.
  const Table* out;
  EXPECT_THAT(builtin_only_catalog.FindTable({"TestTable"}, &out, {}),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(out, IsNull());
}

TEST(BuiltinOnlyCatalogTest, FindTableAllowed) {
  auto catalog = std::make_unique<SimpleCatalog>("test_catalog");
  auto table = std::make_unique<SimpleTable>(
      "TestTable",
      std::vector<SimpleTable::NameAndType>{{"a", types::Int64Type()}});
  catalog->AddTable(table.get());
  auto builtin_only_catalog = BuiltinOnlyCatalog("builtin_catalog", *catalog);

  // Allow tables if set.
  const Table* out;
  builtin_only_catalog.set_allow_tables(true);
  GOOGLESQL_EXPECT_OK(builtin_only_catalog.FindTable({"TestTable"}, &out, {}));
  EXPECT_EQ(out, table.get());

  // Reset function restores default behavior.
  builtin_only_catalog.reset_allows();
  EXPECT_THAT(builtin_only_catalog.FindTable({"TestTable"}, &out, {}),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(out, IsNull());
}

TEST(BuiltinOnlyCatalogTest, FindTableResetAllows) {
  auto catalog = std::make_unique<SimpleCatalog>("test_catalog");
  auto table = std::make_unique<SimpleTable>(
      "TestTable",
      std::vector<SimpleTable::NameAndType>{{"a", types::Int64Type()}});
  catalog->AddTable(table.get());
  auto builtin_only_catalog = BuiltinOnlyCatalog("builtin_catalog", *catalog);

  // Reset function restores default behavior.
  const Table* out;
  builtin_only_catalog.set_allow_tables(true);
  builtin_only_catalog.reset_allows();
  EXPECT_THAT(builtin_only_catalog.FindTable({"TestTable"}, &out, {}),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(out, IsNull());
}

TEST(BuiltinOnlyCatalogTest, FindProcedure) {
  auto catalog = std::make_unique<SimpleCatalog>("test_catalog");
  catalog->AddOwnedProcedure(new Procedure(
      {"TestProcedure"}, {types::Int64Type(), {types::Int64Type()}, nullptr}));
  auto builtin_only_catalog = BuiltinOnlyCatalog("builtin_catalog", *catalog);

  // There are no builtin procedures, so always returns kNotFound.
  const Procedure* out;
  EXPECT_THAT(builtin_only_catalog.FindProcedure({"TestProcedure"}, &out, {}),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(out, IsNull());
}

TEST(BuiltinOnlyCatalogTest, FindModel) {
  auto catalog = std::make_unique<SimpleCatalog>("test_catalog");
  catalog->AddOwnedModel(new SimpleModel("TestModel", {},
                                         {{"o1", types::DoubleType()}},
                                         /*id=*/1));
  auto builtin_only_catalog = BuiltinOnlyCatalog("builtin_catalog", *catalog);

  // There are no builtin models, so always returns kNotFound.
  const Model* out;
  EXPECT_THAT(builtin_only_catalog.FindModel({"TestModel"}, &out, {}),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(out, IsNull());
}

TEST(BuiltinOnlyCatalogTest, FindTypeDisallowed) {
  auto catalog = std::make_unique<SimpleCatalog>("test_catalog");
  catalog->AddType("TestType", types::Int64Type());
  auto builtin_only_catalog = BuiltinOnlyCatalog("builtin_catalog", *catalog);

  // By default, don't allow types; always return kNotFound.
  const Type* out;
  EXPECT_THAT(builtin_only_catalog.FindType({"TestType"}, &out, {}),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(out, IsNull());
}

TEST(BuiltinOnlyCatalogTest, FindTypeAllowed) {
  auto catalog = std::make_unique<SimpleCatalog>("test_catalog");
  catalog->AddType("TestType", types::Int64Type());
  auto builtin_only_catalog = BuiltinOnlyCatalog("builtin_catalog", *catalog);

  // Allow types if set.
  const Type* out;
  builtin_only_catalog.set_allow_types(true);
  GOOGLESQL_EXPECT_OK(builtin_only_catalog.FindType({"TestType"}, &out, {}));
  EXPECT_EQ(out, types::Int64Type());

  // Reset function restores default behavior.
  builtin_only_catalog.reset_allows();
  EXPECT_THAT(builtin_only_catalog.FindType({"TestType"}, &out, {}),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(out, IsNull());
}

TEST(BuiltinOnlyCatalogTest, FindTypeResetAllows) {
  auto catalog = std::make_unique<SimpleCatalog>("test_catalog");
  catalog->AddType("TestType", types::Int64Type());
  auto builtin_only_catalog = BuiltinOnlyCatalog("builtin_catalog", *catalog);

  // Reset function restores default behavior.
  const Type* out;
  builtin_only_catalog.set_allow_types(true);
  builtin_only_catalog.reset_allows();
  EXPECT_THAT(builtin_only_catalog.FindType({"TestType"}, &out, {}),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(out, IsNull());
}

TEST(BuiltinOnlyCatalogTest, FindPropertyGraph) {
  auto catalog = std::make_unique<SimpleCatalog>("test_catalog");
  catalog->AddOwnedPropertyGraph(std::make_unique<SimplePropertyGraph>(
      std::vector<std::string>{"TestPropertyGraph"}));
  auto builtin_only_catalog = BuiltinOnlyCatalog("builtin_catalog", *catalog);

  // There are no builtin property graphs, so always returns kNotFound.
  const PropertyGraph* out;
  EXPECT_THAT(
      builtin_only_catalog.FindPropertyGraph({"TestPropertyGraph"}, out, {}),
      StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(out, IsNull());
}

TEST(BuiltinOnlyCatalogTest, FindConstantWithPathPrefix) {
  auto catalog = std::make_unique<SimpleCatalog>("test_catalog");
  std::unique_ptr<SimpleConstant> c1;
  GOOGLESQL_ASSERT_OK(SimpleConstant::Create({"TestConstant"}, Value::Int64(1UL), &c1));
  catalog->AddOwnedConstant(std::move(c1));
  auto builtin_only_catalog = BuiltinOnlyCatalog("builtin_catalog", *catalog);

  // There are no builtin constants, so always returns kNotFound.
  const Constant* out;
  int num_names_consumed = 1;
  EXPECT_THAT(builtin_only_catalog.FindConstantWithPathPrefix(
                  {"TestConstant"}, &num_names_consumed, &out, {}),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(out, IsNull());
  EXPECT_EQ(num_names_consumed, 0);
}

}  // namespace
}  // namespace googlesql
