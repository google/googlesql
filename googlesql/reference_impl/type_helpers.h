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

// Helper methods for producing the types required by TestDriver
// implementations.
#include <string>
#include <vector>

#include "googlesql/public/catalog.h"
#include "googlesql/public/type.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"

#ifndef GOOGLESQL_REFERENCE_IMPL_TYPE_HELPERS_H_
#define GOOGLESQL_REFERENCE_IMPL_TYPE_HELPERS_H_

namespace googlesql {

// Creates the Relational Algebra representation of a relation from the
// resolved AST, which is modeled as an array in the compliance tests.
absl::StatusOr<const ArrayType*> CreateTableArrayType(
    const ResolvedColumnList& table_columns, bool is_value_table,
    TypeFactory* type_factory);

// Same as the above, but uses 'const Column*' instead of ResolvedColumnList.
absl::StatusOr<const ArrayType*> CreateTableArrayType(
    const std::vector<const Column*>& table_columns, bool is_value_table,
    TypeFactory* type_factory);

// Names of DML output columns used by CreateDMLOutputType().
extern const char* kDMLOutputNumRowsModifiedColumnName;
extern const char* kDMLOutputAllRowsColumnName;
extern const char* kDMLOutputReturningColumnName;

extern const char kCreatedObjectType[];
extern const char kCreatedObjectName[];

// Creates a Struct type representing the primary key of a table, where each key
// column is represented by a field named StrCat("$pk#", column_name).
absl::StatusOr<const StructType*> CreatePrimaryKeyType(
    const Table* table, TypeFactory* type_factory);

// Creates the DML output struct type corresponding to a DML statement on a
// table whose corresponding array type is 'table_array_type'.
//
// The returned type is a struct with two fields: an int64 representing the
// number of rows modified by the statement, and an array of structs, where each
// element of the array represents a row of the modified table.
absl::StatusOr<const StructType*> CreateDMLOutputType(
    const ArrayType* table_array_type, TypeFactory* type_factory);

// Creates the DML output struct type corresponding to a DML statement on a
// table whose corresponding array type is 'table_array_type' and a returning
// result table whose corresponding array type is 'returning_array_type'.
// If "returning_array_type" is nullptr, this table as a return type of
// returning clause is ignored from the DML output struct type.
// and
//
// The returned type is a struct with three fields: an int64 representing the
// number of rows modified by the statement, and an array of structs, where each
// element of the array represents a row of the modified table.
absl::StatusOr<const StructType*> CreateDMLOutputTypeWithReturning(
    const ArrayType* table_array_type, const ArrayType* returning_array_type,
    TypeFactory* type_factory);

// Represents type information for a modified table in Graph DML.
struct GraphTargetTableTypeInfo {
  // The string name of the target table.
  //
  // A string name is sufficient (no Table* or catalog lookup is needed) for
  // two reasons:
  // 1. Output Schema Construction: `name` is used directly as the field name
  //    for this table's nested struct in the outer DML output struct type.
  // 2. Runtime Evaluation: In the reference implementation, `EvaluationContext`
  //    maintains table state in string-keyed maps (e.g. `GetTableAsArray(name)`
  //    and `GetNumRowsModified(name)`), so runtime table lookups key directly
  //    off this table name string.
  std::string name;
  const ArrayType* table_type = nullptr;
};

// Creates the DML output struct type corresponding to a Graph DML statement on
// multiple target tables.
//
// The returned type is a struct with multiple nested structs and an optional
// top-level returning_rows field:
//   STRUCT<
//     table1_name STRUCT< num_rows_modified INT64,
//                         all_rows ARRAY<...> >,
//     ...
//     tableN_name STRUCT< num_rows_modified INT64,
//                         all_rows ARRAY<...> >,
//    [ returning_rows ARRAY<...> ]
//   >
//
// For each target table in `table_types`, a field named after the table whose
// type is a nested struct is created in the top-level output struct. The nested
// struct has the following fields:
//   - kDMLOutputNumRowsModifiedColumnName (INT64), representing the number of
//     rows modified in this table.
//   - kDMLOutputAllRowsColumnName (ARRAY<...>), representing all rows in the
//     table after modification.
//
// If `returning_array_type` is not nullptr, a top-level field named
// kDMLOutputReturningColumnName (ARRAY<...>) is appended to represent the
// returned rows.
absl::StatusOr<const StructType*> CreateGraphDMLOutputType(
    absl::Span<const GraphTargetTableTypeInfo> table_types,
    const ArrayType* returning_array_type, TypeFactory* type_factory);

}  // namespace googlesql

#endif  // GOOGLESQL_REFERENCE_IMPL_TYPE_HELPERS_H_
