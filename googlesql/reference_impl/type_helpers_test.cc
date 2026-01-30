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

#include "googlesql/reference_impl/type_helpers.h"

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/testing/using_test_value.cc"  // NOLINT
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace googlesql {
namespace {

const char kAllTypesTable[] = "table_all_types";
const char kValueTable[] = "value_table";

// Column names.
const char kInt32Col[] = "col_int32";
const char kUint32Col[] = "col_uint32";
const char kInt64Col[] = "col_int64";
const char kUint64Col[] = "col_uint64";
const char kStringCol[] = "col_string";
const char kBoolCol[] = "col_bool";
const char kDoubleCol[] = "col_double";

// Column ids for kAllTypesTable.
static const int kInt32ColId = 1;
static const int kUint32ColId = 2;
static const int kInt64ColId = 3;
static const int kUint64ColId = 4;
static const int kStringColId = 5;
static const int kBoolColId = 6;
static const int kDoubleColId = 7;

static ResolvedColumnList GetAllTypesColumnList() {
  return {
      ResolvedColumn(kInt32ColId,
                     googlesql::IdString::MakeGlobal(kAllTypesTable),
                     googlesql::IdString::MakeGlobal(kInt32Col), Int32Type()),
      ResolvedColumn(kUint32ColId,
                     googlesql::IdString::MakeGlobal(kAllTypesTable),
                     googlesql::IdString::MakeGlobal(kUint32Col), Uint32Type()),
      ResolvedColumn(kInt64ColId,
                     googlesql::IdString::MakeGlobal(kAllTypesTable),
                     googlesql::IdString::MakeGlobal(kInt64Col), Int64Type()),
      ResolvedColumn(kUint64ColId,
                     googlesql::IdString::MakeGlobal(kAllTypesTable),
                     googlesql::IdString::MakeGlobal(kUint64Col), Uint64Type()),
      ResolvedColumn(kStringColId,
                     googlesql::IdString::MakeGlobal(kAllTypesTable),
                     googlesql::IdString::MakeGlobal(kStringCol), StringType()),
      ResolvedColumn(kBoolColId,
                     googlesql::IdString::MakeGlobal(kAllTypesTable),
                     googlesql::IdString::MakeGlobal(kBoolCol), BoolType()),
      ResolvedColumn(
          kDoubleColId, googlesql::IdString::MakeGlobal(kAllTypesTable),
          googlesql::IdString::MakeGlobal(kDoubleCol), DoubleType())};
}

static ResolvedColumnList GetValueTableColumnList() {
  return {
      ResolvedColumn(kInt32ColId, googlesql::IdString::MakeGlobal(kValueTable),
                     googlesql::IdString::MakeGlobal(kInt32Col), Int32Type())};
}

TEST(CreateTableType, NonValueTableTest) {
  const ResolvedColumnList columns = GetAllTypesColumnList();

  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ArrayType* table_type,
      CreateTableArrayType(columns,
                           /*is_value_table=*/false, &type_factory));
  EXPECT_EQ(
      "ARRAY<STRUCT<col_int32 INT32, col_uint32 UINT32, col_int64 INT64, "
      "col_uint64 UINT64, col_string STRING, col_bool BOOL, "
      "col_double DOUBLE>>",
      table_type->DebugString());
}

TEST(CreateTableType, ValueTableTest) {
  const ResolvedColumnList columns = GetValueTableColumnList();

  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ArrayType* table_type,
      CreateTableArrayType(columns,
                           /*is_value_table=*/true, &type_factory));
  EXPECT_EQ("ARRAY<INT32>", table_type->DebugString());
}

TEST(CreateDMLOutputType, NonValueTableTest) {
  const ResolvedColumnList columns = GetAllTypesColumnList();

  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ArrayType* table_type,
      CreateTableArrayType(columns,
                           /*is_value_table=*/false, &type_factory));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const StructType* dml_output_type,
                       CreateDMLOutputType(table_type, &type_factory));

  EXPECT_EQ(
      "STRUCT<num_rows_modified INT64, "
      "all_rows ARRAY<STRUCT<col_int32 INT32, col_uint32 UINT32, col_int64 "
      "INT64, col_uint64 UINT64, col_string STRING, col_bool BOOL, "
      "col_double DOUBLE>>>",
      dml_output_type->DebugString());
}

TEST(CreateDMLOutputType, ValueTableTest) {
  const ResolvedColumnList columns = GetValueTableColumnList();

  TypeFactory type_factory;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const ArrayType* table_type,
      CreateTableArrayType(columns,
                           /*is_value_table=*/true, &type_factory));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const StructType* dml_output_type,
                       CreateDMLOutputType(table_type, &type_factory));

  EXPECT_EQ("STRUCT<num_rows_modified INT64, all_rows ARRAY<INT32>>",
            dml_output_type->DebugString());
}

}  // namespace
}  // namespace googlesql
