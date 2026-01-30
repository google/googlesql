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
#include <optional>
#include <string>
#include <vector>

#include "googlesql/common/simple_evaluator_table_iterator.h"
#include "googlesql/examples/tpch/catalog/customer.tbl.h"
#include "googlesql/examples/tpch/catalog/lineitem.tbl.h"
#include "googlesql/examples/tpch/catalog/nation.tbl.h"
#include "googlesql/examples/tpch/catalog/orders.tbl.h"
#include "googlesql/examples/tpch/catalog/part.tbl.h"
#include "googlesql/examples/tpch/catalog/partsupp.tbl.h"
#include "googlesql/examples/tpch/catalog/region.tbl.h"
#include "googlesql/examples/tpch/catalog/supplier.tbl.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/evaluator_table_iterator.h"
#include "googlesql/public/functions/date_time_util.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/simple_catalog_util.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "absl/base/const_init.h"
#include "absl/base/macros.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "riegeli/base/maker.h"
#include "riegeli/bytes/string_reader.h"
#include "riegeli/csv/csv_reader.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"
#include "googlesql/base/clock.h"

namespace googlesql {

// These are create statements for the TPCH tables.
// I found copies of these in various places with varying NOT NULL constraints.
// I'm not sure which version is official, but these constraints aren't
// supported or used here anyway so they are just commented out.
// I added appropriate PRIMARY KEY definitions.
static const char* kCreateRegion =
    R"(CREATE TABLE Region (
  R_REGIONKEY UINT64 PRIMARY KEY, -- NOT NULL
  R_NAME STRING,
  R_COMMENT STRING,
))";

static const char* kCreateNation =
    R"(CREATE TABLE Nation (
  N_NATIONKEY UINT64 PRIMARY KEY, -- NOT NULL
  N_NAME STRING,
  N_REGIONKEY UINT64, -- NOT NULL
  N_COMMENT STRING,
))";

static const char* kCreateCustomer =
    R"(CREATE TABLE Customer (
  C_CUSTKEY UINT64 PRIMARY KEY, -- NOT NULL
  C_NAME STRING,
  C_ADDRESS STRING,
  C_NATIONKEY UINT64, -- NOT NULL
  C_PHONE STRING,
  C_ACCTBAL DOUBLE,
  C_MKTSEGMENT STRING,
  C_COMMENT STRING,
))";

static const char* kCreateSupplier =
    R"(CREATE TABLE Supplier (
  S_SUPPKEY UINT64 PRIMARY KEY, -- NOT NULL
  S_NAME STRING,
  S_ADDRESS STRING,
  S_NATIONKEY UINT64,
  S_PHONE STRING,
  S_ACCTBAL DOUBLE,
  S_COMMENT STRING,
))";

static const char* kCreateOrders =
    R"(CREATE TABLE Orders (
  O_ORDERKEY UINT64 PRIMARY KEY, -- NOT NULL
  O_CUSTKEY UINT64, -- NOT NULL
  O_ORDERSTATUS STRING,
  O_TOTALPRICE DOUBLE,
  O_ORDERDATE DATE,
  O_ORDERPRIORITY STRING,
  O_CLERK STRING,
  O_SHIPPRIORITY INT64,
  O_COMMENT STRING,
))";

static const char* kCreateLineItem =
    R"(CREATE TABLE LineItem (
  L_ORDERKEY UINT64, -- NOT NULL
  L_PARTKEY UINT64, -- NOT NULL
  L_SUPPKEY UINT64, -- NOT NULL
  L_LINENUMBER UINT64, -- NOT NULL
  L_QUANTITY DOUBLE,
  L_EXTENDEDPRICE DOUBLE,
  L_DISCOUNT DOUBLE,
  L_TAX DOUBLE,
  L_RETURNFLAG STRING,
  L_LINESTATUS STRING,
  L_SHIPDATE DATE,
  L_COMMITDATE DATE,
  L_RECEIPTDATE DATE,
  L_SHIPINSTRUCT STRING,
  L_SHIPMODE STRING,
  L_COMMENT STRING,
  PRIMARY KEY(L_ORDERKEY, L_LINENUMBER)
))";

static const char* kCreatePart =
    R"(CREATE TABLE Part (
  P_PARTKEY UINT64 PRIMARY KEY, -- NOT NULL
  P_NAME STRING,
  P_MFGR STRING,
  P_BRAND STRING,
  P_TYPE STRING,
  P_SIZE INT64,
  P_CONTAINER STRING,
  P_RETAILPRICE DOUBLE,
  P_COMMENT STRING,
))";

static const char* kCreatePartSupp =
    R"(CREATE TABLE PartSupp (
  PS_PARTKEY UINT64, -- NOT NULL
  PS_SUPPKEY UINT64, -- NOT NULL
  PS_AVAILQTY INT64,
  PS_SUPPLYCOST DOUBLE,
  PS_COMMENT STRING,
  PRIMARY KEY(PS_PARTKEY, PS_SUPPKEY)
))";

// This stores the data parsed from CSV and pre-processed for one TPCH table.
// It's computed lazily and then stored once so it can be reused each time
// we need to make an iterator to scan the table.
struct ParsedTpchTableData {
  std::vector<std::shared_ptr<const std::vector<Value>>> column_values;
  int num_rows = 0;
};
// This stores a ParsedTpchTableData or an error.
// The `optional` is filled in once loading this tables has been attempted.
typedef std::optional<
    absl::StatusOr<std::unique_ptr<const ParsedTpchTableData>>>
    ParsedTpchTableDataHolder;

// Load the data for one TPCH table and return it in a ParsedTpchTableData.
static ParsedTpchTableDataHolder::value_type MakeParsedTpchTableData(
    const SimpleTable& table, absl::string_view contents) {
  riegeli::CsvReader csv_reader(
      riegeli::Maker<riegeli::StringReader>(contents),
      riegeli::CsvReaderBase::Options().set_field_separator('|'));

  // We expect the join column pseudo-columns are at the end.
  // `num_columns` will be the number of non-pseudo-columns.
  const int total_num_columns = table.NumColumns();
  int num_columns = total_num_columns;
  while (num_columns > 0 &&
         table.GetColumn(num_columns - 1)->IsPseudoColumn()) {
    --num_columns;
  }
  // Columns before the pseudo-columns are all non-pseudo-columns.
  // The data file has content for non-pseudo-columns only.
  GOOGLESQL_RET_CHECK_GT(num_columns, 0);
  for (int i = 0; i < num_columns; ++i) {
    GOOGLESQL_RET_CHECK(!table.GetColumn(i)->IsPseudoColumn()) << i;
  }

  // Values are in column_major order.
  std::vector<std::shared_ptr<std::vector<Value>>> column_values(num_columns);
  for (int i = 0; i < num_columns; ++i) {
    column_values[i] = std::make_shared<std::vector<Value>>();
  }

  int num_rows = 0;
  std::vector<std::string> record;
  while (csv_reader.ReadRecord(record)) {
    ++num_rows;

    // The TPCH files have a '|' separator at the end of the line, so they
    // look like they have one extra column.
    GOOGLESQL_RET_CHECK_EQ(record.size(), num_columns + 1);

    for (int i = 0; i < num_columns; ++i) {
      const Type* type = table.GetColumn(i)->GetType();
      const std::string& field = record[i];

      if (type->IsString()) {
        column_values[i]->push_back(Value::String(field));
      } else if (type->IsInt64()) {
        int64_t value;
        GOOGLESQL_RET_CHECK(absl::SimpleAtoi(field, &value)) << field;
        column_values[i]->push_back(Value::Int64(value));
      } else if (type->IsUint64()) {
        uint64_t value;
        GOOGLESQL_RET_CHECK(absl::SimpleAtoi(field, &value)) << field;
        column_values[i]->push_back(Value::Uint64(value));
      } else if (type->IsDouble()) {
        double value;
        GOOGLESQL_RET_CHECK(absl::SimpleAtod(field, &value)) << field;
        column_values[i]->push_back(Value::Double(value));
      } else if (type->IsDate()) {
        int32_t date;
        GOOGLESQL_RET_CHECK_OK(googlesql::functions::ConvertStringToDate(field, &date))
            << field;
        column_values[i]->push_back(Value::Date(date));
      } else {
        GOOGLESQL_RET_CHECK_FAIL() << "Unhandled type: " << type->DebugString();
      }
    }
  }
  if (!csv_reader.Close()) return csv_reader.status();

  auto data = std::make_unique<ParsedTpchTableData>();
  data->num_rows = num_rows;

  data->column_values.reserve(num_columns);
  for (auto& values : column_values) {
    GOOGLESQL_RET_CHECK_EQ(values->size(), num_rows);

    // Copying the outer vector was necessary to make the inner vector a
    // vector of const.
    data->column_values.push_back(values);
  }

  return data;
}

// Make the callback that makes an iterator for a particular TPCH table.
// All work happens lazily and at most once, storing the state in `data_holder`.
//
// This has enough functionality for the TPCH tables here.  It could be
// generalized to support more use cases.
static SimpleTable::EvaluatorTableIteratorFactory
MakeIteratorFactoryFromCsvFile(const absl::string_view contents,
                               const SimpleTable& table,
                               ParsedTpchTableDataHolder* data_holder) {
  auto factory = [contents, &table,
                  data_holder](absl::Span<const int> column_idxs)
      -> absl::StatusOr<std::unique_ptr<EvaluatorTableIterator>> {
    // Initialize the ParsedTpchTableData if it hasn't been done for this table
    // yet, using a Mutex to avoid race conditions.
    {
      static absl::Mutex mutex(absl::kConstInit);
      absl::MutexLock lock(&mutex);
      if (!*data_holder) {
        *data_holder = MakeParsedTpchTableData(table, contents);
      }
    }

    const ParsedTpchTableData* data = (*data_holder)->value().get();
    std::vector<const Column*> columns;
    std::vector<std::shared_ptr<const std::vector<Value>>> column_values;
    column_values.reserve(column_idxs.size());
    for (const int column_idx : column_idxs) {
      GOOGLESQL_RET_CHECK_LT(column_idx, table.NumColumns());
      columns.push_back(table.GetColumn(column_idx));
      GOOGLESQL_RET_CHECK_LT(column_idx, data->column_values.size());
      column_values.push_back(data->column_values[column_idx]);
    }

    // Make the iterator to return the table contents.
    std::unique_ptr<EvaluatorTableIterator> iter(
        new SimpleEvaluatorTableIterator(
            columns, column_values, data->num_rows,
            /*end_status=*/absl::OkStatus(), /*filter_column_idxs=*/
            absl::flat_hash_set<int>(column_idxs.begin(), column_idxs.end()),
            /*cancel_cb=*/[]() {},
            /*set_deadline_cb=*/[](absl::Time t) {}, googlesql_base::Clock::RealClock()));
    return iter;
  };
  return factory;
}

// Find Columns by name and return the corresponding vector.
// Fail if the columns don't exist.
static absl::StatusOr<const std::vector<const Column*>> FindColumns(
    const Table* table, absl::Span<const std::string> column_names) {
  std::vector<const Column*> columns;
  for (const std::string& column_name : column_names) {
    columns.push_back(table->FindColumnByName(column_name));
    GOOGLESQL_RET_CHECK(columns.back() != nullptr) << column_name;
  }
  return columns;
}

// Add or remove "s" from `name` to make it `plural` or not.
static std::string MakePlural(std::string name, bool plural) {
  bool has_s = absl::EndsWith(name, "s");
  if (plural && !has_s) {
    absl::StrAppend(&name, "s");
  } else if (!plural && has_s) {
    name.pop_back();
  }
  return name;
}

// Add a join column in one direction from `table` to `target_table`.
static absl::Status AddOneJoinColumn(
    SimpleTable* table, const Table* target_table, bool is_multi,
    const Type* target_type, const SimpleColumn::Attributes& attributes) {
  std::string column_name = MakePlural(target_table->Name(), is_multi);

  // Loop only if we hit the `continue` to add a second alias column.
  while (true) {
    GOOGLESQL_RET_CHECK_OK(table->AddColumn(std::make_unique<SimpleColumn>(
        table->FullName(), column_name, target_type, attributes)));

    // `Order` is a reserved keyword, which makes it awkward to query.
    // Add another pseudo-column `Order_` as an alias for that column.
    // I'm not sure what name would be most convenient.
    if (column_name == "Order") {
      column_name = "Order_";
      continue;
    }
    break;
  }

  return absl::OkStatus();
}

// Add the join column in both directions between `table1` and `table2`.
// This takes const pointers to the Tables for convenience, so the
// const_cast isn't required on all the callers.
static absl::Status AddJoinColumn(const Table* table1_const,
                                  const Table* table2_const,
                                  absl::Span<const std::string> column_names1,
                                  absl::Span<const std::string> column_names2,
                                  bool is_multi1, bool is_multi2,
                                  TypeFactory* type_factory) {
  // These became `const Table*` when passed through the Catalog, but we know
  // they are the SimpleTables we added.
  SimpleTable* table1 =
      const_cast<SimpleTable*>(table1_const->GetAs<SimpleTable>());
  SimpleTable* table2 =
      const_cast<SimpleTable*>(table2_const->GetAs<SimpleTable>());

  GOOGLESQL_ASSIGN_OR_RETURN(std::vector<const Column*> columns1,
                   FindColumns(table1, column_names1));
  GOOGLESQL_ASSIGN_OR_RETURN(std::vector<const Column*> columns2,
                   FindColumns(table2, column_names2));

  const RowType* row_type1;
  const RowType* row_type2;
  GOOGLESQL_RET_CHECK_OK(type_factory->MakeRowType(table1, table1->FullName(), is_multi1,
                                         columns1, table2, columns2,
                                         &row_type1));
  GOOGLESQL_RET_CHECK_OK(type_factory->MakeRowType(table2, table2->FullName(), is_multi2,
                                         columns2, table1, columns1,
                                         &row_type2));

  SimpleColumn::Attributes attributes2 = {
      .is_pseudo_column = true,
      .is_writable_column = false,
      .join_column =
          Column::JoinColumnAttributes(columns2, table1, columns2, is_multi2)};

  // Add join column from table1 to table2.
  GOOGLESQL_RETURN_IF_ERROR(AddOneJoinColumn(
      table1, table2, is_multi2, row_type2,
      SimpleColumn::Attributes{.is_pseudo_column = true,
                               .is_writable_column = false,
                               .join_column = Column::JoinColumnAttributes(
                                   columns1, table2, columns2, is_multi2)}));

  // Add join column from table2 to table1.
  GOOGLESQL_RETURN_IF_ERROR(AddOneJoinColumn(
      table2, table1, is_multi1, row_type1,
      SimpleColumn::Attributes{.is_pseudo_column = true,
                               .is_writable_column = false,
                               .join_column = Column::JoinColumnAttributes(
                                   columns2, table1, columns1, is_multi1)}));

  return absl::OkStatus();
}

// Add join columns for all foreign key relationships in the tpch schema.
static absl::Status AddJoinColumns(SimpleCatalog* catalog) {
  const Table *table_Region, *table_LineItem, *table_Nation, *table_Customer,
      *table_Supplier, *table_Orders, *table_Part, *table_PartSupp;
  GOOGLESQL_RETURN_IF_ERROR(catalog->FindTable({"Region"}, &table_Region));
  GOOGLESQL_RETURN_IF_ERROR(catalog->FindTable({"LineItem"}, &table_LineItem));
  GOOGLESQL_RETURN_IF_ERROR(catalog->FindTable({"Nation"}, &table_Nation));
  GOOGLESQL_RETURN_IF_ERROR(catalog->FindTable({"Customer"}, &table_Customer));
  GOOGLESQL_RETURN_IF_ERROR(catalog->FindTable({"Supplier"}, &table_Supplier));
  GOOGLESQL_RETURN_IF_ERROR(catalog->FindTable({"Orders"}, &table_Orders));
  GOOGLESQL_RETURN_IF_ERROR(catalog->FindTable({"Part"}, &table_Part));
  GOOGLESQL_RETURN_IF_ERROR(catalog->FindTable({"PartSupp"}, &table_PartSupp));

  TypeFactory* type_factory = catalog->type_factory();

  GOOGLESQL_RETURN_IF_ERROR(
      AddJoinColumn(table_Customer, table_Orders, {"c_custkey"}, {"o_custkey"},
                    /*is_multi1=*/false, /*is_multi2=*/true, type_factory));

  GOOGLESQL_RETURN_IF_ERROR(AddJoinColumn(
      table_Orders, table_LineItem, {"o_orderkey"}, {"l_orderkey"},
      /*is_multi1=*/false, /*is_multi2=*/true, type_factory));

  GOOGLESQL_RETURN_IF_ERROR(AddJoinColumn(
      table_Region, table_Nation, {"r_regionkey"}, {"n_regionkey"},
      /*is_multi1=*/false, /*is_multi2=*/true, type_factory));

  GOOGLESQL_RETURN_IF_ERROR(AddJoinColumn(
      table_Nation, table_Supplier, {"n_nationkey"}, {"s_nationkey"},
      /*is_multi1=*/false, /*is_multi2=*/true, type_factory));

  GOOGLESQL_RETURN_IF_ERROR(AddJoinColumn(
      table_Nation, table_Customer, {"n_nationkey"}, {"c_nationkey"},
      /*is_multi1=*/false, /*is_multi2=*/true, type_factory));

  GOOGLESQL_RETURN_IF_ERROR(AddJoinColumn(
      table_Supplier, table_PartSupp, {"s_suppkey"}, {"ps_suppkey"},
      /*is_multi1=*/false, /*is_multi2=*/true, type_factory));

  GOOGLESQL_RETURN_IF_ERROR(
      AddJoinColumn(table_Part, table_PartSupp, {"p_partkey"}, {"ps_partkey"},
                    /*is_multi1=*/false, /*is_multi2=*/true, type_factory));

  GOOGLESQL_RETURN_IF_ERROR(
      AddJoinColumn(table_PartSupp, table_LineItem,
                    {"ps_partkey", "ps_suppkey"}, {"l_partkey", "l_suppkey"},
                    /*is_multi1=*/false, /*is_multi2=*/true, type_factory));

  // Joins from Lineitem directly to Part and Supplier might not be part of the
  // official schema but they seem convenient.
  GOOGLESQL_RETURN_IF_ERROR(
      AddJoinColumn(table_Part, table_LineItem, {"p_partkey"}, {"l_partkey"},
                    /*is_multi1=*/false, /*is_multi2=*/true, type_factory));

  GOOGLESQL_RETURN_IF_ERROR(AddJoinColumn(
      table_Supplier, table_LineItem, {"s_suppkey"}, {"l_suppkey"},
      /*is_multi1=*/false, /*is_multi2=*/true, type_factory));

  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<SimpleCatalog>> MakeTpchCatalog(
    bool with_semantic_graph) {
  auto catalog = std::make_unique<SimpleCatalog>("tpch_catalog");

  AnalyzerOptions analyzer_options;
  analyzer_options.mutable_language()->AddSupportedStatementKind(
      RESOLVED_CREATE_TABLE_STMT);

  std::unique_ptr<const AnalyzerOutput> analyzer_output;

  // These are pairs with {create_statement, file_contents}.
  static const char* kTableDefinitions[][2] = {
      {kCreateRegion, embedded_resources::kTpchData_region},
      {kCreateLineItem, embedded_resources::kTpchData_lineitem},
      {kCreateNation, embedded_resources::kTpchData_nation},
      {kCreateCustomer, embedded_resources::kTpchData_customer},
      {kCreateSupplier, embedded_resources::kTpchData_supplier},
      {kCreateOrders, embedded_resources::kTpchData_orders},
      {kCreatePart, embedded_resources::kTpchData_part},
      {kCreatePartSupp, embedded_resources::kTpchData_partsupp},
  };
  static const int kNumTables = ABSL_ARRAYSIZE(kTableDefinitions);
  static ParsedTpchTableDataHolder* const parsed_datas =
      new ParsedTpchTableDataHolder[kNumTables];

  for (int i = 0; i < kNumTables; ++i) {
    SimpleTable* table;
    GOOGLESQL_RETURN_IF_ERROR(AddTableFromCreateTable(
        kTableDefinitions[i][0], analyzer_options,
        /*allow_non_temp=*/true, analyzer_output, table, *catalog));

    table->SetEvaluatorTableIteratorFactory(MakeIteratorFactoryFromCsvFile(
        kTableDefinitions[i][1], *table, &parsed_datas[i]));
  }

  if (with_semantic_graph) {
    GOOGLESQL_RETURN_IF_ERROR(AddJoinColumns(catalog.get()));
  }

  return catalog;
}

}  // namespace googlesql
