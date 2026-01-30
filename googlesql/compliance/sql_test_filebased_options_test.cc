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

#include "googlesql/compliance/sql_test_filebased_options.h"

#include <memory>
#include <string>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/compliance/test_driver.h"
#include "googlesql/reference_impl/reference_driver.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/string_view.h"

namespace googlesql {

using ::testing::ElementsAre;
using ::testing::Not;
using ::absl_testing::IsOk;

// There aren't very many tests here yet. The classes in this file were
// extracted from SQLTestBase and are largely tested indirectly via
// sql_test_base_test. This file should be used to test changes moving forward.

TEST(FilebasedSQLTestOptoionsTest, BasicUse) {
  ReferenceDriver driver;
  auto test_db_proto = std::make_unique<TestDatabaseProto>();
  GOOGLESQL_ASSERT_OK(SerializeTestDatabase(TestDatabase{}, test_db_proto.get()));
  GOOGLESQL_ASSERT_OK(driver.CreateDatabase(*test_db_proto));
  FilebasedSQLTestFileOptions file_options(&driver);
  std::string reason;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto case_options,
                       file_options.ProcessTestCase("", &reason));
  EXPECT_FALSE(case_options->prepare_database());
}

TEST(FilebasedSQLTestOptoionsTest, SpacyLabels) {
  ReferenceDriver driver;
  auto test_db_proto = std::make_unique<TestDatabaseProto>();
  GOOGLESQL_ASSERT_OK(SerializeTestDatabase(TestDatabase{}, test_db_proto.get()));
  GOOGLESQL_ASSERT_OK(driver.CreateDatabase(*test_db_proto));
  FilebasedSQLTestFileOptions file_options(&driver);
  std::string reason;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto case_options,
                       file_options.ProcessTestCase(R"(
[name=name]
[labels=  a,   b  , c]
SELECT 1)",
                                                    &reason));
  EXPECT_THAT(case_options->local_labels(), ElementsAre("a", "b", "c"));
}

TEST(FilebasedSQLTestOptoionsTest, GlobalLabels) {
  ReferenceDriver driver;
  auto test_db_proto = std::make_unique<TestDatabaseProto>();
  GOOGLESQL_ASSERT_OK(SerializeTestDatabase(TestDatabase{}, test_db_proto.get()));
  GOOGLESQL_ASSERT_OK(driver.CreateDatabase(*test_db_proto));
  FilebasedSQLTestFileOptions file_options(&driver);
  constexpr absl::string_view kFirstTest = R"(
[default global_labels=a,b,c]
[name=foo1]
SELECT 1)";
  constexpr absl::string_view kSecondTest = R"(
[default global_labels=d,e]
[name=foo2]
SELECT 1)";
  constexpr absl::string_view kThirdTest = R"(
[name=foo3]
SELECT 1)";
  std::string reason;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto case_options,
                       file_options.ProcessTestCase(kFirstTest, &reason));
  EXPECT_THAT(file_options.global_labels(), ElementsAre("a", "b", "c"));

  EXPECT_THAT(file_options.ProcessTestCase(kSecondTest, &reason), Not(IsOk()));

  // We re-set the global_labels in test 2. We don't want every subsequent test
  // case in the file to fail because of it. This test that a subsequent test
  // is okay.
  EXPECT_THAT(file_options.ProcessTestCase(kThirdTest, &reason), IsOk());
}

}  // namespace googlesql
