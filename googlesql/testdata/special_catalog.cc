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

#include "googlesql/testdata/special_catalog.h"

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/catalog.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/type.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "absl/container/btree_set.h"
#include "googlesql/base/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"

namespace googlesql {

// Simplified version of SimpleColumn to allow empty column names.
class ColumnAllowEmptyName : public Column {
 public:
  ColumnAllowEmptyName(absl::string_view table_name, absl::string_view name,
                       const Type* type)
      : name_(name),
        full_name_(absl::StrCat(table_name, ".", name)),
        type_(type) {}
  ~ColumnAllowEmptyName() override = default;

  std::string Name() const override { return name_; }
  std::string FullName() const override { return full_name_; }
  const Type* GetType() const override { return type_; }
  bool IsPseudoColumn() const override { return false; }

 private:
  const std::string name_;
  const std::string full_name_;
  const Type* type_;
};

// Simplified version of SimpleTable to support anonymous / duplicated column
// names.
class TableWithAnonymousAndDuplicatedColumnNames : public Table {
 public:
  // Make a table with columns with the given names and types.
  typedef std::pair<std::string, const Type*> NameAndType;
  TableWithAnonymousAndDuplicatedColumnNames(
      absl::string_view name, absl::Span<const NameAndType> columns)
      : name_(name) {
    absl::btree_set<std::string> column_names;
    for (const NameAndType& name_and_type : columns) {
      const std::string& column_name = name_and_type.first;
      if (!column_names.contains(column_name)) {
        googlesql_base::InsertOrDie(&column_names, column_name);
      } else {
        googlesql_base::InsertIfNotPresent(&duplicated_column_names_, column_name);
      }
    }
    for (const NameAndType& name_and_type : columns) {
      const std::string& column_name = name_and_type.first;
      const Type* column_type = name_and_type.second;
      columns_.emplace_back(new ColumnAllowEmptyName(
          name, column_name, column_type));
      if (!googlesql_base::ContainsKey(duplicated_column_names_, column_name)) {
        googlesql_base::InsertOrDie(&column_by_unique_name_, column_name,
                         columns_.back().get());
      }
    }
  }
  ~TableWithAnonymousAndDuplicatedColumnNames() override = default;

  std::string Name() const override { return name_; }
  std::string FullName() const override { return name_; }

  int NumColumns() const override { return columns_.size(); }
  const Column* GetColumn(int i) const override { return columns_[i].get(); }

  // FindColumnByName only works for columns with a unique <name>. For columns
  // with duplicated name, it returns nullptr. Also, there is no name that can
  // be used to find anonymous columns.
  const Column* FindColumnByName(const std::string& name) const override {
    return googlesql_base::FindWithDefault(column_by_unique_name_, name);
  }

  bool IsValueTable() const override { return false; }

  int64_t GetSerializationId() const override { return 0; }

 private:
  const std::string name_;
  std::vector<std::unique_ptr<Column>> columns_;
  std::map<std::string, Column*> column_by_unique_name_;
  std::set<std::string> duplicated_column_names_;
};

std::unique_ptr<SimpleCatalog> GetSpecialCatalog() {
  auto catalog = std::make_unique<SimpleCatalog>("special_catalog",
                                                 nullptr /* type_factory */);

  TypeFactory* types = catalog->type_factory();
  catalog->AddOwnedTable(new TableWithAnonymousAndDuplicatedColumnNames(
        "TableWithAnonymousAndDuplicatedColumnNames",
        {{"key", types->get_int32()},
         {"" /* anonymous column name */, types->get_int32()},
         {"DuplicatedColumnName", types->get_int32()},
         {"DuplicatedColumnName", types->get_int32()}}));

  catalog->AddOwnedTable(new TableWithAnonymousAndDuplicatedColumnNames(
        "TableWithTwoAnonymousColumns",
        {{"key", types->get_int32()},
         {"" /* anonymous column name */, types->get_int32()},
         {"" /* anonymous column name */, types->get_int32()}}));

  // Add types with custom catalog path to the catalog.
  const EnumType* enum_EnumAnnotations = nullptr;
  GOOGLESQL_CHECK_OK(catalog->type_factory()->MakeEnumType(
      googlesql_test::EnumAnnotations_NestedEnum_descriptor(),
      &enum_EnumAnnotations, {"custom", "catalog"}));
  catalog->AddType("googlesql_test.EnumAnnotations.NestedEnum",
                    enum_EnumAnnotations);

  const ProtoType* proto_FakeStringInt32EntryType = nullptr;
  GOOGLESQL_CHECK_OK(catalog->type_factory()->MakeProtoType(
      googlesql_test::FakeStringInt32EntryType::descriptor(),
      &proto_FakeStringInt32EntryType, {"custom", "catalog"}));
  catalog->AddType("googlesql_test.FakeStringInt32EntryType",
                    proto_FakeStringInt32EntryType);

  // Add the sub-catalogs to make the re-analysis happy.
  auto sub_catalog = std::make_unique<SimpleCatalog>("custom",
                                                     /*type_factory=*/nullptr);
  auto sub_catalog2 = std::make_unique<SimpleCatalog>("catalog",
                                                      /*type_factory=*/nullptr);
  sub_catalog2->AddType("googlesql_test.EnumAnnotations.NestedEnum",
                        enum_EnumAnnotations);
  sub_catalog2->AddType("googlesql_test.FakeStringInt32EntryType",
                        proto_FakeStringInt32EntryType);
  sub_catalog->AddOwnedCatalog(std::move(sub_catalog2));
  catalog->AddOwnedCatalog(std::move(sub_catalog));

  return catalog;
}

}  // namespace googlesql
