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

#ifndef GOOGLESQL_ANALYZER_REWRITERS_SQL_FUNCTION_INLINER_H_
#define GOOGLESQL_ANALYZER_REWRITERS_SQL_FUNCTION_INLINER_H_

#include "googlesql/public/rewriter_interface.h"

namespace googlesql {

// Gets a pointer to the sql function inliner. This inliner handles scalar sql
// functions and scalar sql function templates.
const Rewriter* GetSqlFunctionInliner();

// Gets a pointer to the sql table function inliner. This inliner handles table
// sql functions and table sql function templates.
const Rewriter* GetSqlTvfInliner();

// Gets a pointer to the sql aggregate function inliner.  This inliner handles
// aggregate sql functions and aggregate sql function templates.
const Rewriter* GetSqlAggregateInliner();

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_REWRITERS_SQL_FUNCTION_INLINER_H_
