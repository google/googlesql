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

#include "googlesql/tools/execute_query/value_as_table_adapter.h"

#include <memory>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/evaluator_table_iterator.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace googlesql {
namespace {

using testing::ElementsAre;
using testing::NotNull;

TEST(ValueAsTableAdapterTest, EmptyValue) {
  TypeFactory type_factory;
  const StructType* struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory.MakeStructType({}, &struct_type));
  const ArrayType* array_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory.MakeArrayType(struct_type, &array_type));

  Value value = Value::EmptyArray(array_type);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<ValueAsTableAdapter> adapter,
                       ValueAsTableAdapter::Create(value));

  const Table* table = adapter->GetTable();
  ASSERT_THAT(table, NotNull());
  EXPECT_EQ(table->NumColumns(), 0);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       adapter->CreateEvaluatorTableIterator());
  EXPECT_FALSE(iter->NextRow());
}

TEST(ValueAsTableAdapterTest, SingleColumnSingleRow) {
  TypeFactory type_factory;
  const StructType* struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory.MakeStructType({{"int64_field", types::Int64Type()}},
                                        &struct_type));
  const ArrayType* array_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory.MakeArrayType(struct_type, &array_type));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value column_value,
                       Value::MakeStruct(struct_type, {Value::Int64(1)}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value value,
                       Value::MakeArray(array_type, {column_value}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<ValueAsTableAdapter> adapter,
                       ValueAsTableAdapter::Create(value));
  const Table* table = adapter->GetTable();
  ASSERT_THAT(table, NotNull());

  EXPECT_EQ(table->NumColumns(), 1);
  std::vector<Value> rows;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       adapter->CreateEvaluatorTableIterator());
  while (iter->NextRow()) {
    EXPECT_THAT(iter->NumColumns(), 1);
    EXPECT_THAT(iter->GetColumnType(0), types::Int64Type());
    rows.push_back(iter->GetValue(0));
  }

  EXPECT_THAT(rows.size(), 1);
  EXPECT_THAT(rows[0].int64_value(), 1);
}

TEST(ValueAsTableAdapterTest, MuMultipleColumnsMultipleRows) {
  TypeFactory type_factory;
  const StructType* struct_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory.MakeStructType({{"int64_field", types::Int64Type()},
                                         {"string_field", types::StringType()}},
                                        &struct_type));
  const ArrayType* array_type = nullptr;
  GOOGLESQL_ASSERT_OK(type_factory.MakeArrayType(struct_type, &array_type));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value r1,
      Value::MakeStruct(struct_type, {Value::Int64(1), Value::String("foo")}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value r2,
      Value::MakeStruct(struct_type, {Value::Int64(2), Value::String("bar")}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      Value r3,
      Value::MakeStruct(struct_type, {Value::Int64(3), Value::String("baz")}));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(Value value, Value::MakeArray(array_type, {r1, r2, r3}));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<ValueAsTableAdapter> adapter,
                       ValueAsTableAdapter::Create(value));
  const Table* table = adapter->GetTable();
  ASSERT_THAT(table, NotNull());

  EXPECT_EQ(table->NumColumns(), 2);
  std::vector<Value> row_values;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(std::unique_ptr<EvaluatorTableIterator> iter,
                       adapter->CreateEvaluatorTableIterator());
  while (iter->NextRow()) {
    EXPECT_THAT(iter->NumColumns(), 2);
    EXPECT_THAT(iter->GetColumnType(0), types::Int64Type());
    EXPECT_THAT(iter->GetColumnType(1), types::StringType());
    row_values.push_back(iter->GetValue(0));
    row_values.push_back(iter->GetValue(1));
  }

  EXPECT_THAT(row_values, ElementsAre(r1.field(0), r1.field(1), r2.field(0),
                                      r2.field(1), r3.field(0), r3.field(1)));
}

}  // namespace
}  // namespace googlesql
