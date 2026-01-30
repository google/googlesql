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

#include "googlesql/common/scope_error_catalog.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/simple_property_graph.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/status_macros.h"

using testing::HasSubstr;
using testing::TestWithParam;
using testing::ValuesIn;
using absl_testing::StatusIs;

namespace googlesql {
namespace {

// Create a test catalog with one of each object that can be looked up in a
// ScopeErrorCatalog.
absl::StatusOr<std::unique_ptr<SimpleCatalog>> CatalogWithObjects() {
  auto catalog = std::make_unique<SimpleCatalog>("test_with_objects");

  catalog->AddOwnedTable(new SimpleTable(
      "TestTable",
      std::vector<SimpleTable::NameAndType>{{"a", types::Int64Type()}}));

  catalog->AddOwnedFunction(new Function{
      "TestFunction", "TestGroup", Function::SCALAR,
      std::vector<FunctionSignature>{{{types::Int64Type()}, {}, nullptr}}});

  catalog->AddOwnedTableValuedFunction(new FixedOutputSchemaTVF(
      std::vector<std::string>{"TestTVF"},
      std::vector<FunctionSignature>{
          {FunctionArgumentType::AnyRelation(), {}, nullptr}},
      TVFRelation{{TVFSchemaColumn("a", types::Int64Type(), false)}}));

  catalog->AddOwnedProcedure(new Procedure(
      {"TestProcedure"}, {types::Int64Type(), {types::Int64Type()}, nullptr}));

  catalog->AddOwnedModel(new SimpleModel("TestModel", {},
                                         {{"o1", types::DoubleType()}},
                                         /*id=*/1));

  catalog->AddType("TestType", types::Int64Type());

  catalog->AddOwnedPropertyGraph(std::make_unique<SimplePropertyGraph>(
      std::vector<std::string>{"TestPropertyGraph"}));

  std::unique_ptr<SimpleConstant> c1;
  GOOGLESQL_RETURN_IF_ERROR(
      SimpleConstant::Create({"TestConstant"}, Value::Int64(1UL), &c1));
  catalog->AddOwnedConstant(std::move(c1));

  return catalog;
}

absl::StatusOr<std::unique_ptr<SimpleCatalog>> EmptyCatalog() {
  auto catalog = std::make_unique<SimpleCatalog>("test_empty");
  return catalog;
}

struct FindObjectTestCase {
  std::string object_type;
  bool builtin_populated;
  bool global_populated;
};

using FindObjectTest = TestWithParam<FindObjectTestCase>;

TEST_P(FindObjectTest, TestFindObject) {
  const FindObjectTestCase& test_case = GetParam();
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<SimpleCatalog> builtin_catalog,
      test_case.builtin_populated ? CatalogWithObjects() : EmptyCatalog());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<SimpleCatalog> global_catalog,
      test_case.global_populated ? CatalogWithObjects() : EmptyCatalog());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ScopeErrorCatalog> scope_error_catalog,
      ScopeErrorCatalog::Create("test_scope_error_catalog", true,
                                builtin_catalog.get(), global_catalog.get()));
  absl::Status find_status;
  // Store the pointer produced by lookup in ScopeErrorCatalog and in the
  // builtin catalog directly, to prove the lookup in ScopeErrorCatalog
  // produced the same pointer.
  const void* builtin_lookup_result;
  const void* scope_error_catalog_lookup_result;
  if (test_case.object_type == "Table") {
    const Table* out_table;
    find_status = scope_error_catalog->FindTable({"TestTable"}, &out_table);
    scope_error_catalog_lookup_result = out_table;
    builtin_catalog->FindTable({"TestTable"}, &out_table).IgnoreError();
    builtin_lookup_result = out_table;
  } else if (test_case.object_type == "Function") {
    const Function* out_function;
    find_status =
        scope_error_catalog->FindFunction({"TestFunction"}, &out_function);
    scope_error_catalog_lookup_result = out_function;
    builtin_catalog->FindFunction({"TestFunction"}, &out_function)
        .IgnoreError();
    builtin_lookup_result = out_function;
  } else if (test_case.object_type == "TableValuedFunction") {
    const TableValuedFunction* out_tvf;
    find_status =
        scope_error_catalog->FindTableValuedFunction({"TestTVF"}, &out_tvf);
    scope_error_catalog_lookup_result = out_tvf;
    builtin_catalog->FindTableValuedFunction({"TestTVF"}, &out_tvf)
        .IgnoreError();
    builtin_lookup_result = out_tvf;
  } else if (test_case.object_type == "Procedure") {
    const Procedure* out_procedure;
    find_status =
        scope_error_catalog->FindProcedure({"TestProcedure"}, &out_procedure);
    scope_error_catalog_lookup_result = out_procedure;
    builtin_catalog->FindProcedure({"TestProcedure"}, &out_procedure)
        .IgnoreError();
    builtin_lookup_result = out_procedure;
  } else if (test_case.object_type == "Model") {
    const Model* out_model;
    find_status = scope_error_catalog->FindModel({"TestModel"}, &out_model);
    scope_error_catalog_lookup_result = out_model;
    builtin_catalog->FindModel({"TestModel"}, &out_model).IgnoreError();
    builtin_lookup_result = out_model;
  } else if (test_case.object_type == "Type") {
    const Type* out_type;
    find_status = scope_error_catalog->FindType({"TestType"}, &out_type);
    scope_error_catalog_lookup_result = out_type;
    builtin_catalog->FindType({"TestType"}, &out_type).IgnoreError();
    builtin_lookup_result = out_type;
  }

  if (test_case.builtin_populated) {
    // Object is in builtin catalog, should be found and pointer should be the
    // one from the builtin catalog.
    GOOGLESQL_EXPECT_OK(find_status);
    EXPECT_NE(builtin_lookup_result, nullptr);
    EXPECT_EQ(builtin_lookup_result, scope_error_catalog_lookup_result);
  } else if (test_case.global_populated) {
    // Object not in builtin catalog, but is in global catalog, should be
    // kInvalidArgument indicating that a builtin object cannot reference a
    // global object.
    EXPECT_THAT(find_status,
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Invalid reference to global-scope")));
    EXPECT_EQ(builtin_lookup_result, nullptr);
    EXPECT_EQ(scope_error_catalog_lookup_result, nullptr);
  } else {
    // Not in builtin catalog or global catalog, should be kNotFound.
    EXPECT_THAT(find_status, StatusIs(absl::StatusCode::kNotFound));
    EXPECT_EQ(builtin_lookup_result, nullptr);
    EXPECT_EQ(scope_error_catalog_lookup_result, nullptr);
  }
}

std::vector<FindObjectTestCase> GetFindObjectTestCases() {
  std::vector<FindObjectTestCase> test_cases;
  for (const std::string& object_type :
       {"Table", "Function", "TableValuedFunction", "Procedure", "Model",
        "Type"}) {
    for (bool builtin_populated : {false, true}) {
      for (bool global_populated : {false, true}) {
        FindObjectTestCase test_case = {.object_type = object_type,
                                        .builtin_populated = builtin_populated,
                                        .global_populated = global_populated};
        test_cases.push_back(test_case);
      }
    }
  }
  return test_cases;
}

INSTANTIATE_TEST_SUITE_P(
    FindObjectTestSuiteInstantiation, FindObjectTest,
    ValuesIn(GetFindObjectTestCases()),
    [](const testing::TestParamInfo<FindObjectTest::ParamType>& info) {
      // Test name format, e.g. `ObjectTableBuiltinYesGlobalNo`.
      return absl::StrCat("Object", info.param.object_type, "Builtin",
                          info.param.builtin_populated ? "Yes" : "No", "Global",
                          info.param.global_populated ? "Yes" : "No");
    });

TEST(ScopeErrorCatalogTest, FindPropertyGraph) {
  // FindPropertyGraph has a different signature, and so the special error
  // message is not implemented. Will need to be implemented if PropertyGraph
  // objects are added to the global catalog in the future.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<SimpleCatalog> builtin_catalog,
                       CatalogWithObjects());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<SimpleCatalog> global_catalog,
                       EmptyCatalog());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ScopeErrorCatalog> scope_error_catalog,
      ScopeErrorCatalog::Create("test_scope_error_catalog", true,
                                builtin_catalog.get(), global_catalog.get()));
  const PropertyGraph* out;
  GOOGLESQL_EXPECT_OK(
      scope_error_catalog->FindPropertyGraph({"TestPropertyGraph"}, out, {}));
  EXPECT_EQ(out->Name(), "TestPropertyGraph");
}

TEST(ScopeErrorCatalogTest, FindConstantWithPathPrefix) {
  // FindConstantWithPathPrefix has a different signature, and so the special
  // error message is not implemented. Will need to be implemented if
  // Constant objects are added to the global catalog in the future.
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<SimpleCatalog> builtin_catalog,
                       CatalogWithObjects());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<SimpleCatalog> global_catalog,
                       EmptyCatalog());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<ScopeErrorCatalog> scope_error_catalog,
      ScopeErrorCatalog::Create("test_scope_error_catalog", true,
                                builtin_catalog.get(), global_catalog.get()));
  const Constant* out;
  int num_names_consumed = 0;
  GOOGLESQL_EXPECT_OK(scope_error_catalog->FindConstantWithPathPrefix(
      {"TestConstant"}, &num_names_consumed, &out, {}));
  EXPECT_EQ(out->Name(), "TestConstant");
  EXPECT_EQ(num_names_consumed, 1);
}

}  // namespace
}  // namespace googlesql
