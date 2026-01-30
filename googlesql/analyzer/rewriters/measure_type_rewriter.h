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

#ifndef GOOGLESQL_ANALYZER_REWRITERS_MEASURE_TYPE_REWRITER_H_
#define GOOGLESQL_ANALYZER_REWRITERS_MEASURE_TYPE_REWRITER_H_

#include "googlesql/public/rewriter_interface.h"

namespace googlesql {

// Gets a pointer to the Measure type rewriter. The rewriter expands
// Measure-typed columns to a STRUCT<> typed column, with fields within the
// STRUCT comprising columns referenced by the measure expression as well as
// row identity columns to grain-lock the measure expression. This expansion
// only occurs for measure columns that are arguments to an `AGGREGATE` function
// call.
//
// `AGGREGATE` function calls over a measure type column are rewritten to a
// multi-level aggregation expression over the fields within the STRUCT to avoid
// overcounting.
const Rewriter* GetMeasureTypeRewriter();

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_REWRITERS_MEASURE_TYPE_REWRITER_H_
