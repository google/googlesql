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

#include "googlesql/examples/tpch/catalog/tpch_catalog.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/evaluator_table_iterator.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/types/row_type.h"
#include "googlesql/public/types/type.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

using ::absl_testing::StatusIs;

namespace googlesql {

static void CheckRowCount(SimpleCatalog& catalog, const char* table_name,
                          int expected_num_rows) {
  const Table* table;
  GOOGLESQL_ASSERT_OK(catalog.GetTable(table_name, &table));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iterator,
                       table->CreateEvaluatorTableIterator(/*column_idxs=*/{}));

  int64_t num_rows = 0;
  while (iterator->NextRow()) {
    ++num_rows;
  }
  GOOGLESQL_ASSERT_OK(iterator->Status());
  EXPECT_EQ(num_rows, expected_num_rows);
}

static void CheckColumnExists(const std::unique_ptr<SimpleCatalog>& catalog,
                              absl::string_view table_name,
                              const std::string& column_name,
                              bool check_not_exists = false) {
  const Table* table;
  GOOGLESQL_ASSERT_OK(catalog->FindTable({std::string(table_name)}, &table))
      << table_name;
  ASSERT_TRUE(table != nullptr);

  const Column* column = table->FindColumnByName(column_name);
  if (check_not_exists) {
    ASSERT_TRUE(column == nullptr) << column_name;
  } else {
    ASSERT_TRUE(column != nullptr) << column_name;
  }
}

static void CheckPrimaryKey(const std::unique_ptr<SimpleCatalog>& catalog,
                            absl::string_view table_name,
                            absl::Span<const std::string> column_names) {
  const Table* table;
  GOOGLESQL_ASSERT_OK(catalog->FindTable({std::string(table_name)}, &table))
      << table_name;
  ASSERT_TRUE(table != nullptr);

  ASSERT_TRUE(table->PrimaryKey());
  const std::vector<int> indexes = table->PrimaryKey().value();

  ASSERT_EQ(indexes.size(), column_names.size());
  for (int i = 0; i < indexes.size(); ++i) {
    EXPECT_EQ(table->GetColumn(indexes[i])->Name(), column_names[i]);
  }
}

TEST(TpchCatalog, Make) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto catalog, MakeTpchCatalog());

  CheckColumnExists(catalog, "Nation", "n_name");
  CheckColumnExists(catalog, "Nation", "Customers", /*check_not_exists=*/true);

  // Check primary keys work, for single and multi-part.
  CheckPrimaryKey(catalog, "Nation", {"N_NATIONKEY"});
  CheckPrimaryKey(catalog, "Orders", {"O_ORDERKEY"});
  CheckPrimaryKey(catalog, "PartSupp", {"PS_PARTKEY", "PS_SUPPKEY"});
  CheckPrimaryKey(catalog, "LineItem", {"L_ORDERKEY", "L_LINENUMBER"});
}

TEST(TpchCatalog, MakeWithGraph) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto catalog,
                       MakeTpchCatalog(/*with_semantic_graph=*/true));

  CheckColumnExists(catalog, "Nation", "n_name");
  CheckColumnExists(catalog, "Nation", "Customers");

  // Check we got correct singular/plural names for "Orders" table (with "s").
  CheckColumnExists(catalog, "Customer", "Orders");
  CheckColumnExists(catalog, "Lineitem", "Order");
  // We also make an alias `Order_`, since `Order` is reserved keyword.
  CheckColumnExists(catalog, "Lineitem", "Order_");

  // Check we got correct singular/plural names for "Customer" table (no "s").
  CheckColumnExists(catalog, "Nation", "Customers");
  CheckColumnExists(catalog, "Orders", "Customer");
}

// Test that reading the tpch tables works, including when there are
// non-readable join column pseudo-columns.
class TpchMakeAndReadTest : public ::testing::TestWithParam<bool> {};
TEST_P(TpchMakeAndReadTest, WithSemanticGraph) {
  const bool with_semantic_graph = GetParam();

  // Run the test twice to make sure that when we make a new catalog, the
  // cached values from the previous catalog still work.
  for (int iter = 0; iter < 2; ++iter) {
    auto make_result = MakeTpchCatalog(with_semantic_graph);
    GOOGLESQL_EXPECT_OK(make_result);

    SimpleCatalog& catalog = *make_result.value();
    EXPECT_EQ(catalog.tables().size(), 8);

    // Test a sample of one expected table and column.
    const Table* table;
    GOOGLESQL_EXPECT_OK(catalog.GetTable("Orders", &table));
    if (!with_semantic_graph) {
      EXPECT_EQ(table->NumColumns(), 9);
    } else {
      EXPECT_EQ(table->NumColumns(), 11);
    }
    EXPECT_EQ(table->GetColumn(3)->Name(), "O_TOTALPRICE");
    EXPECT_EQ(table->GetColumn(3)->GetType()->DebugString(), "DOUBLE");
    if (with_semantic_graph) {
      // Order.LineItems is a ROW join from Order.O_CUSTKEY to
      // Customer.C_CUSTKEY.
      EXPECT_TRUE(table->GetColumn(9)->IsPseudoColumn());
      EXPECT_EQ(table->GetColumn(9)->Name(), "Customer");
      const Type* type = table->GetColumn(9)->GetType();
      EXPECT_EQ(type->DebugString(), "ROW<Customer (join)>");
      EXPECT_TRUE(type->IsSingleRow());
      const RowType* row_type = type->AsRow();
      EXPECT_TRUE(row_type->IsJoin());
      EXPECT_EQ(row_type->bound_source_table(), table);
      EXPECT_EQ(row_type->bound_source_columns().size(), 1);
      EXPECT_EQ(row_type->bound_columns().size(), 1);
      EXPECT_EQ(row_type->bound_source_columns()[0]->Name(), "O_CUSTKEY");
      EXPECT_EQ(row_type->bound_columns()[0]->Name(), "C_CUSTKEY");
      EXPECT_EQ(row_type->element_type()->DebugString(), "ROW<Customer>");

      // Order.LineItems is a MULTIROW<LineItem> join from Order.O_ORDERKEY to
      // LineItem.L_ORDERKEY.
      EXPECT_TRUE(table->GetColumn(10)->IsPseudoColumn());
      EXPECT_EQ(table->GetColumn(10)->Name(), "LineItems");
      type = table->GetColumn(10)->GetType();
      EXPECT_EQ(type->DebugString(), "MULTIROW<LineItem (join)>");
      EXPECT_TRUE(type->IsMultiRow());
      EXPECT_TRUE(type->AsRow()->IsJoin());
      row_type = type->AsRow();
      EXPECT_TRUE(row_type->IsJoin());
      EXPECT_EQ(row_type->bound_source_table(), table);
      EXPECT_EQ(row_type->bound_source_columns().size(), 1);
      EXPECT_EQ(row_type->bound_columns().size(), 1);
      EXPECT_EQ(row_type->bound_source_columns()[0]->Name(), "O_ORDERKEY");
      EXPECT_EQ(row_type->bound_columns()[0]->Name(), "L_ORDERKEY");
      EXPECT_EQ(row_type->element_type()->DebugString(), "ROW<LineItem>");
    }

    // Check reading content from all the tables works, and we get the
    // right row counts.  Case insenitive table name lookup works.
    CheckRowCount(catalog, "customer", 150);
    CheckRowCount(catalog, "lineITEM", 6005);
    CheckRowCount(catalog, "nation", 25);
    CheckRowCount(catalog, "orders", 1500);
    CheckRowCount(catalog, "part", 200);
    CheckRowCount(catalog, "partsupp", 800);
    CheckRowCount(catalog, "region", 5);
    CheckRowCount(catalog, "supplier", 10);

    // Check the first row of Orders, where we can check the value of one column
    // of each supported type.
    std::vector<int> column_idxs;
    for (int i = 0; i < table->NumColumns(); ++i) {
      if (with_semantic_graph && table->GetColumn(i)->IsPseudoColumn()) {
        continue;
      }
      column_idxs.push_back(i);
    }
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iterator,
                         table->CreateEvaluatorTableIterator(column_idxs));
    ASSERT_TRUE(iterator->NextRow());
    EXPECT_EQ(iterator->NumColumns(), 9);

    EXPECT_EQ(iterator->GetColumnName(1), "O_CUSTKEY");
    EXPECT_EQ(iterator->GetColumnType(1)->DebugString(), "UINT64");
    EXPECT_EQ(iterator->GetValue(1).DebugString(), "37");

    EXPECT_EQ(iterator->GetColumnName(3), "O_TOTALPRICE");
    EXPECT_EQ(iterator->GetColumnType(3)->DebugString(), "DOUBLE");
    EXPECT_EQ(iterator->GetValue(3).DebugString(), "131251.81");

    EXPECT_EQ(iterator->GetColumnName(4), "O_ORDERDATE");
    EXPECT_EQ(iterator->GetColumnType(4)->DebugString(), "DATE");
    EXPECT_EQ(iterator->GetValue(4).DebugString(), "1996-01-02");

    EXPECT_EQ(iterator->GetColumnName(5), "O_ORDERPRIORITY");
    EXPECT_EQ(iterator->GetColumnType(5)->DebugString(), "STRING");
    EXPECT_EQ(iterator->GetValue(5).DebugString(), "\"5-LOW\"");

    EXPECT_EQ(iterator->GetColumnName(7), "O_SHIPPRIORITY");
    EXPECT_EQ(iterator->GetColumnType(7)->DebugString(), "INT64");
    EXPECT_EQ(iterator->GetValue(7).DebugString(), "0");

    // There are 9 real columns, and in `with_semantic_graph` mode, some extra
    // pseudo-columns.  The join columns are not readable so it is always an
    // error to ask to read column 9.
    column_idxs = {0, 1, 8, 9};
    EXPECT_THAT(table->CreateEvaluatorTableIterator(column_idxs),
                StatusIs(absl::StatusCode::kInternal));
  }
}
INSTANTIATE_TEST_SUITE_P(TpchCatalog, TpchMakeAndReadTest, ::testing::Bool());

}  // namespace googlesql
