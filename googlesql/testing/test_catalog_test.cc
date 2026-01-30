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

#include "googlesql/testing/test_catalog.h"

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/type.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"

namespace googlesql {

// This test covers error injection in TestCatalog, and error handling in
// the default recursive implementations of Catalog::Find*.
// It also tests TestFunction.
TEST(TestCatalogTest, Test) {
  TypeFactory type_factory;
  const Type* int_type = type_factory.get_int32();
  const Type* string_type = type_factory.get_string();
  const absl::Status error(absl::StatusCode::kUnimplemented, "Unimplemented!");

  TestCatalog catalog("catalog");
  EXPECT_EQ("catalog", catalog.FullName());

  catalog.AddError("errOR", error);

  SimpleTable t1("T1", {{"C1", int_type}, {"C2", string_type}});
  catalog.AddTable(&t1);

  catalog.AddType("MyString", string_type);

  TestCatalog nested1("nested1");
  catalog.AddCatalog(&nested1);
  nested1.AddError("error", error);

  const Table* table;
  const Type* type;
  const Function* function;
  Catalog* found_catalog;

  GOOGLESQL_EXPECT_OK(catalog.GetTable("t1", &table));
  EXPECT_EQ(&t1, table);

  GOOGLESQL_EXPECT_OK(catalog.FindTable({"t1"}, &table));
  EXPECT_EQ(&t1, table);

  GOOGLESQL_EXPECT_OK(catalog.FindType({"mySTRING"}, &type));
  EXPECT_EQ(string_type, type);

  GOOGLESQL_EXPECT_OK(catalog.GetCatalog("nested1", &found_catalog));
  EXPECT_EQ(&nested1, found_catalog);

  EXPECT_EQ(error, catalog.GetTable("ERRor", &table));
  EXPECT_EQ(error, catalog.GetType("ERRor", &type));
  EXPECT_EQ(error, catalog.GetFunction("ERRor", &function));
  EXPECT_EQ(error, catalog.GetCatalog("ERRor", &found_catalog));

  EXPECT_EQ(error, catalog.FindTable({"ERRor"}, &table));
  EXPECT_EQ(error, catalog.FindType({"ERRor"}, &type));
  EXPECT_EQ(error, catalog.FindFunction({"ERRor"}, &function));

  EXPECT_EQ(error, catalog.FindTable({"neSTed1", "error", "bad"}, &table));
  EXPECT_EQ(error, catalog.FindType({"neSTed1", "error", "bad"}, &type));
  EXPECT_EQ(error, catalog.FindFunction({"neSTed1", "error", "bad"},
                                        &function));

  // Function tests
  const Type* int64_type = type_factory.get_int64();
  TestFunction fn("FOO", Function::SCALAR,
                  { { int64_type, {int64_type, int64_type}, -1} });
  catalog.AddFunction(&fn);

  GOOGLESQL_EXPECT_OK(catalog.FindFunction({"FOO"}, &function));
  EXPECT_EQ(&fn, function);

  GOOGLESQL_EXPECT_OK(catalog.FindFunction({"foo"}, &function));
  EXPECT_EQ(&fn, function);
}

}  // namespace googlesql
