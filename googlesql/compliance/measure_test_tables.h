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

#ifndef GOOGLESQL_COMPLIANCE_MEASURE_TEST_TABLES_H_
#define GOOGLESQL_COMPLIANCE_MEASURE_TEST_TABLES_H_

#include "googlesql/compliance/test_driver.h"

namespace googlesql {

// Returns a TestDatabase with predefined measure tables. Specifically, the
// `tables` field contains the measure tables, and the `measure_function_defs`
// field contains the measure functions used in the measure tables.
//
// `add_measures_with_udas` indicates whether to add measure columns with UDA
// functions to the test database.
//
// This function is used to generate measure test tables for compliance testing.
TestDatabase GetMeasureTablesTestDatabase(bool add_measures_with_udas);

}  // namespace googlesql

#endif  // GOOGLESQL_COMPLIANCE_MEASURE_TEST_TABLES_H_
