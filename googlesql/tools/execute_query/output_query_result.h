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

#ifndef GOOGLESQL_TOOLS_EXECUTE_QUERY_OUTPUT_QUERY_RESULT_H_
#define GOOGLESQL_TOOLS_EXECUTE_QUERY_OUTPUT_QUERY_RESULT_H_

#include <string>
#include <vector>

#include "googlesql/public/value.h"
#include "googlesql/reference_impl/operator.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/types/span.h"

namespace googlesql {

// Given the query:  select true a, 1 bb, 2.5 c, date '2015-01-01' d,
//                   timestamp '2015-01-02 12:34:56+08' e, 'f' f, b'g' g;
// This will produce:
//
// +------+----+-----+------------+------------------------+---+---+
// | a    | bb | c   | d          | e                      | f | g |
// +------+----+-----+------------+------------------------+---+---+
// | true | 1  | 2.5 | 2015-01-01 | 2015-01-02 04:34:56+00 | f | g |
// +------+----+-----+------------+------------------------+---+---+
//
// For value tables, the output looks like a single column, with no column name.
// +-------------+
// | {Value2, 2} |
// | {Value4, 4} |
// | {Value1, 1} |
// | {Value3, 3} |
// +-------------+
//
// This function generally works well for simple types and arrays of simple
// types, but hasn't been adequately tested for protos or enums.
//
// If an error condition is detected or we cannot currently produce consistent
// output then returns an error string.
std::string ToPrettyOutputStyle(const googlesql::Value& result,
                                bool is_value_table,
                                absl::Span<const std::string> column_names,
                                bool use_box_glyphs = false);

// Helper function shared by analyze_query and execute_script to convert
// a query result into a human-readable display string.
//
// `resolved_stmt` cannot be nullptr, and provides additional information about
// the query used to produce `result`, which may affect the formatting. For
// example, if the query is a DML statement, the output format is altered to
// display the number of rows modified, and if the query is a 'SELECT AS VALUE'
// statement, the column name is omitted from output.
std::string OutputPrettyStyleQueryResult(const googlesql::Value& result,
                                         const ResolvedStatement* resolved_stmt,
                                         bool use_box_glyphs = false);

// Outputs the result of a standalone expression.  If `include_box` is true
// surrounds the result with a pretty-style boxes.
std::string OutputPrettyStyleExpressionResult(const googlesql::Value& result,
                                              bool include_box = true,
                                              bool use_box_glyphs = false);

// Converts 'value' to an output string and returns it.
std::string ValueToOutputString(const Value& value, bool escape_strings);

}  // namespace googlesql

#endif  // GOOGLESQL_TOOLS_EXECUTE_QUERY_OUTPUT_QUERY_RESULT_H_
