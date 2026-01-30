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

#include "googlesql/tools/execute_query/selectable_catalog.h"

#include <memory>
#include <string>
#include <utility>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/language_options.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace googlesql {

namespace {
class TestCatalogAcceptor : public CatalogAcceptor {
 public:
  void Accept(Catalog* catalog) override { catalog_ = catalog; }
  void Accept(std::unique_ptr<Catalog> catalog) override {
    owned_catalog_ = std::move(catalog);
  }

  Catalog* GetCatalog() {
    return catalog_ != nullptr ? catalog_ : owned_catalog_.get();
  }

 private:
  Catalog* catalog_ = nullptr;
  std::unique_ptr<Catalog> owned_catalog_;
};

}  // namespace

TEST(SelectableCatalog, GetSelectableCatalogsInfo) {
  const auto& selectable_catalogs = GetSelectableCatalogsInfo();
  EXPECT_GE(selectable_catalogs.size(), 2);

  EXPECT_EQ(selectable_catalogs[0].name, "none");
  EXPECT_EQ(selectable_catalogs[1].name, "sample");
}

TEST(SelectableCatalog, FindSelectableCatalog) {
  EXPECT_FALSE(FindSelectableCatalog("bad").ok());
}

TEST(SelectableCatalog, FindSelectableCatalog_none) {
  auto found = FindSelectableCatalog("none");
  GOOGLESQL_ASSERT_OK(found);
  SelectableCatalog* selectable = found.value();

  EXPECT_EQ(selectable->name(), "none");

  TestCatalogAcceptor acceptor;
  absl::Status status =
      selectable->ProvideCatalog(LanguageOptions(), &acceptor);
  GOOGLESQL_ASSERT_OK(status);
  // There are no tables in this catalog to look up.
}

// Test that catalog `catalog_name` can be found, and that it includes a table
// called `table_name`.
static void TestCatalog(absl::string_view catalog_name,
                        absl::string_view table_name) {
  auto found = FindSelectableCatalog(catalog_name);
  GOOGLESQL_ASSERT_OK(found);
  SelectableCatalog* selectable = found.value();

  EXPECT_EQ(selectable->name(), catalog_name);
  TestCatalogAcceptor acceptor;
  absl::Status status =
      selectable->ProvideCatalog(LanguageOptions(), &acceptor);
  GOOGLESQL_ASSERT_OK(status);
  Catalog* catalog = acceptor.GetCatalog();

  const Table* table;
  GOOGLESQL_EXPECT_OK(catalog->FindTable({std::string(table_name)}, &table));
  EXPECT_EQ(table->Name(), table_name);
}

TEST(SelectableCatalog, FindSelectableCatalog_sample) {
  TestCatalog("sample", "TestTable");
}

TEST(SelectableCatalog, FindSelectableCatalog_tpch) {
  TestCatalog("tpch", "LineItem");
}

}  // namespace googlesql
