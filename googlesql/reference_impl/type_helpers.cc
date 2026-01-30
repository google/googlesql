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

#include <string>
#include <vector>

#include "googlesql/public/catalog.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/type.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

static std::string GetColumnAlias(const std::string& alias) {
  // We use the empty string as anonymous column name.
  return IsInternalAlias(alias) ? "" : alias;
}

static absl::StatusOr<const StructType*> CreateStructTypeForTableRow(
    const ResolvedColumnList& table_columns, TypeFactory* type_factory) {
  std::vector<StructType::StructField> fields;
  fields.reserve(table_columns.size());
  for (int i = 0; i < table_columns.size(); ++i) {
    fields.push_back(
        {GetColumnAlias(table_columns[i].name()), table_columns[i].type()});
  }
  const StructType* table_struct;
  GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeStructType(fields, &table_struct));
  return table_struct;
}

// Same as the above, but uses std::vector<const Column*> instead of
// ResolvedColumnList.
static absl::StatusOr<const StructType*> CreateStructTypeForTableRow(
    const std::vector<const Column*>& table_columns,
    TypeFactory* type_factory) {
  std::vector<StructType::StructField> fields;
  fields.reserve(table_columns.size());
  for (int i = 0; i < table_columns.size(); ++i) {
    fields.push_back({GetColumnAlias(table_columns[i]->Name()),
                      table_columns[i]->GetType()});
  }
  const StructType* table_struct;
  GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeStructType(fields, &table_struct));
  return table_struct;
}

const char* kDMLOutputNumRowsModifiedColumnName = "num_rows_modified";
const char* kDMLOutputAllRowsColumnName = "all_rows";
const char* kDMLOutputReturningColumnName = "returning_rows";

constexpr char kCreatedObjectType[] = "created_object_type";
constexpr char kCreatedObjectName[] = "created_object_name";

absl::StatusOr<const ArrayType*> CreateTableArrayType(
    const ResolvedColumnList& table_columns, bool is_value_table,
    TypeFactory* type_factory) {
  const Type* element_type = nullptr;
  if (is_value_table) {
    GOOGLESQL_RET_CHECK_EQ(1, table_columns.size());
    element_type = table_columns[0].type();
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(const StructType* table_struct,
                     CreateStructTypeForTableRow(table_columns, type_factory));
    element_type = table_struct;
  }
  const ArrayType* table_array;
  GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeArrayType(element_type, &table_array));
  return table_array;
}

absl::StatusOr<const ArrayType*> CreateTableArrayType(
    const std::vector<const Column*>& table_columns, bool is_value_table,
    TypeFactory* type_factory) {
  const Type* element_type = nullptr;
  if (is_value_table) {
    GOOGLESQL_RET_CHECK_EQ(1, table_columns.size());
    element_type = table_columns[0]->GetType();
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(const StructType* table_struct,
                     CreateStructTypeForTableRow(table_columns, type_factory));
    element_type = table_struct;
  }
  const ArrayType* table_array;
  GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeArrayType(element_type, &table_array));
  return table_array;
}

absl::StatusOr<const StructType*> CreatePrimaryKeyType(
    const Table* table, TypeFactory* type_factory) {
  std::vector<StructType::StructField> fields;
  const auto primary_keys = table->PrimaryKey();
  GOOGLESQL_RET_CHECK(primary_keys.has_value())
      << "Cannot call CreatePrimaryKeyType on a table without a primary key";
  const std::vector<int>& key_column_indexes = *primary_keys;
  fields.reserve(primary_keys->size());
  for (int index : key_column_indexes) {
    GOOGLESQL_RET_CHECK_LT(index, table->NumColumns())
        << "Key column index was out of bounds";
    fields.emplace_back(absl::StrCat("pk#", table->GetColumn(index)->Name()),
                        table->GetColumn(index)->GetType());
  }
  const StructType* key_struct;
  GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeStructType(fields, &key_struct));
  return key_struct;
}

absl::StatusOr<const StructType*> CreateDMLOutputType(
    const ArrayType* table_array_type, TypeFactory* type_factory) {
  return CreateDMLOutputTypeWithReturning(table_array_type,
                                          /*returning_array_type=*/nullptr,
                                          type_factory);
}

absl::StatusOr<const StructType*> CreateDMLOutputTypeWithReturning(
    const ArrayType* table_array_type, const ArrayType* returning_array_type,
    TypeFactory* type_factory) {
  std::vector<StructType::StructField> fields;
  fields.emplace_back(kDMLOutputNumRowsModifiedColumnName, types::Int64Type());
  fields.emplace_back(kDMLOutputAllRowsColumnName, table_array_type);
  if (returning_array_type != nullptr) {
    fields.emplace_back(kDMLOutputReturningColumnName, returning_array_type);
  }

  const StructType* dml_output_type;
  GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeStructType(fields, &dml_output_type));
  return dml_output_type;
}

}  // namespace googlesql
