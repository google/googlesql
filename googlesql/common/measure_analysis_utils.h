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

#ifndef GOOGLESQL_COMMON_MEASURE_ANALYSIS_UTILS_H_
#define GOOGLESQL_COMMON_MEASURE_ANALYSIS_UTILS_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql {

// A measure column definition.
struct MeasureColumnDef {
  std::string name;
  std::string expression;
  bool is_pseudo_column = false;
  std::optional<std::vector<int>> row_identity_column_indices = std::nullopt;
};

// Implements core logic for `AnalyzeMeasureExpression`.
absl::StatusOr<const ResolvedExpr*> AnalyzeMeasureExpressionInternal(
    absl::string_view measure_expr, const Table& table, Catalog& catalog,
    TypeFactory& type_factory, AnalyzerOptions analyzer_options,
    std::unique_ptr<const AnalyzerOutput>& analyzer_output);

// Adds measure columns to a SimpleTable.
// `table` is the table to which the measure columns should be added. `table`
// cannot allow duplicate column names.
// `measures` is a vector of name+expression pairs for the new measure
//   columns. The expressions are resolved against the columns in `table`.
// `type_factory` is used to create types.
// `catalog` should contain builtin functions which can be referenced in
//   measure expressions.
// `analyzer_options` is used when resolving the measure expressions and should
//   enable any features required for those expressions.
// The returned vector of AnalyzerOutputs corresponds to the resolved ASTs of
// each of the measure columns, and must outlive the table.
absl::StatusOr<std::vector<std::unique_ptr<const AnalyzerOutput>>>
AddMeasureColumnsToTable(SimpleTable& table,
                         std::vector<MeasureColumnDef> measures,
                         TypeFactory& type_factory, Catalog& catalog,
                         AnalyzerOptions analyzer_options);

// Add measure values to a table represented as an ARRAY<STRUCT> Value
// containing its rows.
//
// Parameter Requirements:
//   - `array_value` is ARRAY<STRUCT> value containing rows for `simple_table`.
//     `array_value` **does not** contain rows for measure columns in
//     `simple_table`.
//   - `simple_table` contains columns representing the values in `array_value`,
//     as well as N additional measure columns ( N > 0). All measure columns
//     in `simple_table` must appear after all non-measure columns specified in
//     `array_value`.
//   - `measure_column_defs`: A vector of `MeasureColumnDef`s describing each
//     measure column in `simple_table`. The size of this vector must match the
//     number of measure columns in `simple_table`, i.e., N.
//   - `table_level_row_identity_columns`: A vector of integers specifying the
//     indices of the row identity columns in `simple_table` used to grain-lock
//     measures. If a measure in `measure_column_defs` contains measure-level
//     row identity columns, those will be used instead.
//   - `type_factory`: Used to create types.
//   - `language_options`: Used to create the measure values.
absl::StatusOr<Value> UpdateTableRowsWithMeasureValues(
    const Value& array_value, const SimpleTable* simple_table,
    absl::Span<const MeasureColumnDef> measure_column_defs,
    std::vector<int> table_level_row_identity_columns,
    TypeFactory* type_factory, const LanguageOptions& language_options);

}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_MEASURE_ANALYSIS_UTILS_H_
