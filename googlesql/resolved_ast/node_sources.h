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

#ifndef GOOGLESQL_RESOLVED_AST_NODE_SOURCES_H_
#define GOOGLESQL_RESOLVED_AST_NODE_SOURCES_H_

// `node_source` is a field of ResolvedScan that can be populated by the
// resolver, rewriters, or the random query generator to record why a node was
// added. The SQLBuilder may also use `node_source` to influence which query
// patterns to generate.

namespace googlesql {

// When the resolver needs to add a ProjectScan for set operations with
// CORRESPONDING (to select or reorder columns, or to pad NULL columns), it will
// populate the `node_source` of the added ProjectScan with this constant.
// SQLBuilder will try to generate a query using CORRESPONDING, expecting this
// ProjectScan is only used to match columns.
inline constexpr char kNodeSourceResolverSetOperationCorresponding[] =
    "resolver_set_operation_corresponding";

inline constexpr char kNodeSourceSingleTableArrayNamePath[] =
    "single_table_array_name_path";

// When the resolver encounters a TABLE clause (e.g., TABLE my_table), it
// resolves the underlying table or TVF and creates a ResolvedScan. This
// constant is used to populate the `node_source` field of that ResolvedScan.
// This allows components like SQLBuilder to identify scans that were generated
// from a TABLE clause, so they can potentially regenerate the TABLE syntax
// instead of a SELECT * FROM.
inline constexpr char kNodeSourceTableClause[] = "table_clause";

}  // namespace googlesql

#endif  // GOOGLESQL_RESOLVED_AST_NODE_SOURCES_H_
