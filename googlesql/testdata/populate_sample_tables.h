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

#ifndef GOOGLESQL_TESTDATA_POPULATE_SAMPLE_TABLES_H_
#define GOOGLESQL_TESTDATA_POPULATE_SAMPLE_TABLES_H_

#include "googlesql/public/catalog.h"
#include "googlesql/public/type.h"
#include "googlesql/reference_impl/evaluation.h"
#include "googlesql/testdata/sample_catalog.h"
#include "absl/status/status.h"
#include "googlesql/base/status.h"

namespace googlesql {

// Populates <evaluation_context> with sample data, used in command-line
// drivers and tests.  <sample_catalog> defines the available tables and their
// schemas.
//
// If <emulate_primary_keys> is true, the first column of each table is treated
// as its primary key.  If false, talbes do not have primary keys.  Useful for
// executing DML statements where GoogleSQL semantics demend on whether the
// table has a primary key.
//
// The following describes the tables which are populated, along with the names
// and types of their columns:
//   KeyValue:
//     Key: INT64
//     Value: STRING
//
//   KeyValue2:
//     Key: INT64
//     Value: STRING
//
//   KitchenSinkValueTable:
//     Value table of type googlesql_test::KitchenSinkPB
//
//   ComplexTypes:
//     key: INT32
//     TestEnum: googlesql_test::TestEnum
//     KitchenSink: googlesql_test::KitchenSink
//     Int32Array: ARRAY<INT32>
//     TestStruct: STRUCT: {c: INT32, d: STRUCT {a: INT32, b: STRING} }
absl::Status PopulateSampleTables(TypeFactory* type_factory,
                                  SampleCatalog* catalog);

}  // namespace googlesql

#endif  // GOOGLESQL_TESTDATA_POPULATE_SAMPLE_TABLES_H_
