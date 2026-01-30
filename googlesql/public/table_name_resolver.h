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

#ifndef GOOGLESQL_PUBLIC_TABLE_NAME_RESOLVER_H_
#define GOOGLESQL_PUBLIC_TABLE_NAME_RESOLVER_H_

#include "googlesql/parser/parse_tree.h"
#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/type.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace table_name_resolver {

// This function is the recursive implementation of
// ExtractTableNamesFromStatement from ../public/analyzer.h.
//
// It works by traversing the AST, doing minimal scoping to keep track of the
// set of known aliases that could be used in a from clause, and then for each
// table path expression, figuring out if it looks like a table name or
// a path relative to some in-scope alias.
//
// <table_names> cannot be null. It will be cleared first.
//
// If <table_resolution_time_info_map> is not null:
//   <table_resolution_time_info_map> is cleared first.
//
//   If <catalog> is non-null then <type_factory> must also be non-null, and
//   the temporal reference expression will be analyzed with the analyzer
//   output stored in <table_resolution_time_info_map>; <catalog> and
//   <type_factory> must outlive <*table_resolution_time_info_map>.
//
// If <table_resolution_time_info_map> is null, <type_factory> and <catalog> are
// ignored.

// If <tvf_names> is not null, then <tvf_names> will be cleared first.
// Then each TVF node path expression encountered during analysis will
// be collected in <tvf_names>
absl::Status FindTableNamesAndResolutionTime(
    absl::string_view sql, const ASTStatement& statement,
    const AnalyzerOptions& analyzer_options, TypeFactory* type_factory,
    Catalog* catalog, TableNamesSet* table_names,
    TableResolutionTimeInfoMap* table_resolution_time_info_map,
    TableNamesSet* tvf_names = nullptr);

// Traverses a script, computing a list of names of all tables referenced by
// any statement or expression within the script.
//
// This works similarly to FindTableNamesAndResolutionTime(), with <catalog>,
// <type_factory>, and <table_resolution_time_info_map> all nullptr.
absl::Status FindTableNamesInScript(absl::string_view sql,
                                    const ASTScript& script,
                                    const AnalyzerOptions& analyzer_options,
                                    TableNamesSet* table_names,
                                    TableNamesSet* tvf_names = nullptr);

inline absl::Status FindTables(absl::string_view sql,
                               const ASTStatement& statement,
                               const AnalyzerOptions& analyzer_options,
                               TableNamesSet* table_names,
                               TableNamesSet* tvf_names = nullptr) {
  return FindTableNamesAndResolutionTime(
      sql, statement, analyzer_options, /* type_factory = */ nullptr,
      /* catalog = */ nullptr, table_names,
      /* table_resolution_time_info_map = */ nullptr, tvf_names);
}

}  // namespace table_name_resolver
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_TABLE_NAME_RESOLVER_H_
