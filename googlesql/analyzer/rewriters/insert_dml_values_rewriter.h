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

#ifndef GOOGLESQL_ANALYZER_REWRITERS_INSERT_DML_VALUES_REWRITER_H_
#define GOOGLESQL_ANALYZER_REWRITERS_INSERT_DML_VALUES_REWRITER_H_

#include "googlesql/public/rewriter_interface.h"

namespace googlesql {

// Gets a pointer to the dml values rewriter. This is a global object which
// is never destructed.
// This rewriter rewrites insert dmls with values to select queries representing
// the corresponding values.
// For example, below query
// INSERT INTO T VALUES (1),(2) will be rewritten to
// INSERT INTO T SELECT 1 UNION ALL SELECT 2
const Rewriter* GetInsertDmlValuesRewriter();

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_REWRITERS_INSERT_DML_VALUES_REWRITER_H_
