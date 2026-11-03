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

#ifndef GOOGLESQL_COMMON_GRAPH_ELEMENT_UTILS_H_
#define GOOGLESQL_COMMON_GRAPH_ELEMENT_UTILS_H_

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/catalog.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
namespace googlesql {

// Helper function that is used to determine a type is or contains in its
// nesting structure a GraphElement or GraphPath type.
bool TypeIsOrContainsGraphElement(const Type* type);

// If `prop_def` is a non-measure property whose value expression is a direct
// catalog column reference, returns that underlying base table column.
// Otherwise (for measure or derived properties), returns nullptr.
absl::StatusOr<const Column*> GetPropertyBackingColumn(
    const GraphPropertyDefinition& prop_def);

// Returns all catalog columns in the base table of `target_table` that are
// exposed as non-measure properties whose value expression is a direct catalog
// column reference.
absl::StatusOr<absl::flat_hash_set<const Column*>> GetPropertyBackingColumns(
    const GraphElementTable& target_table);

// Checks that all element key columns in `target_table` are exposed as writable
// properties in its property definition list.
// Returns false and populates `error_message` (if non-null) if not.
absl::StatusOr<bool> AreAllKeyColumnsWritableProperties(
    const GraphElementTable& target_table,
    std::string* error_message = nullptr);

// Returns the dynamic label backing column in the base table of `target_table`,
// or nullptr if `target_table` does not have a dynamic label.
absl::StatusOr<const Column*> GetDynamicLabelBackingColumn(
    const GraphElementTable& target_table);

// Returns the dynamic properties backing column in the base table of
// `target_table`, or nullptr if `target_table` does not have dynamic
// properties.
absl::StatusOr<const Column*> GetDynamicPropertiesBackingColumn(
    const GraphElementTable& target_table);

// Represents the edge table side key columns in the SOURCE and DESTINATION
// clauses of an edge table.
struct EdgeEndpointBackingColumns {
  absl::flat_hash_set<const Column*> source_key_columns;
  absl::flat_hash_set<const Column*> dest_key_columns;
};

// For an edge table, returns the base table columns referenced by the source
// and destination node table references. Returns empty sets if `target_table`
// is not an edge table.
absl::StatusOr<EdgeEndpointBackingColumns> GetEdgeEndpointBackingColumns(
    const GraphElementTable& target_table);

// Checks that `target_table` satisfies all schema requirements to be updatable:
// - All element key columns of `target_table` must be exposed as writable
//   properties in its property definition list.
// - If `needs_dynamic_label` is true, `target_table` must support dynamic
//   labels and its dynamic label backing column must be writable.
// - If `target_table` is an edge table:
//   * Its edge source and destination key columns must be writable columns in
//     the edge base table (they do not need to be exposed as edge properties).
//   * The referenced source and destination element key columns must be exposed
//     as properties of its source and destination node tables (they do not need
//     to be writable on the node tables).
// Returns false and populates `error_message` (if non-null) if not.
absl::StatusOr<bool> IsTargetTableUpdatable(
    const GraphElementTable& target_table, bool needs_dynamic_label,
    std::string* error_message = nullptr);

// Constructs a static or dynamic GraphElementType for an element of
// `target_table`. Includes `target_table.Name()` in the resulting type's
// element table list when FEATURE_SQL_GRAPH_UPDATABLE_GRAPH_ELEMENT_TYPE is
// enabled in `language_options`.
absl::StatusOr<const GraphElementType*> BuildGraphElementTypeForTable(
    const GraphElementTable& target_table,
    absl::Span<const std::string> graph_name_path, TypeFactory& type_factory,
    const LanguageOptions& language_options);

// Helper function that returns the property name from a get element property
// node.
absl::StatusOr<std::string> GetPropertyName(
    const ResolvedGraphGetElementProperty* node);

// Helper function that merges a list of properties to a JSON value.
// The JSON value is used to represent dynamic properties of a dynamic graph
// element.
absl::StatusOr<JSONValue> MakePropertiesJsonValue(
    absl::Span<Value::Property> properties,
    const LanguageOptions& language_options);

// Returns true if any dynamic property specifications is found.
// Extracts all conjuncts from `filter_expr` that are either dynamic or static
// property specifications into `property_specifications`. Place the rest of
// the conjuncts into `remaining_conjuncts`.
absl::StatusOr<bool> ContainsDynamicPropertySpecification(
    const ResolvedExpr* filter_expr,
    std::vector<const ResolvedExpr*>& property_specifications,
    std::vector<const ResolvedExpr*>& remaining_conjuncts);

// Returns a vector of property name and value pairs from `exprs`.
// REQUIRES: each expr in `exprs` can be converted to either dynamic or static
// property specifications.
absl::StatusOr<std::vector<std::pair<std::string, const ResolvedExpr*>>>
ToPropertySpecifications(std::vector<const ResolvedExpr*> exprs);

// Returns true if the path mode is restrictive.
bool IsRestrictivePathMode(ResolvedGraphPathModeEnums::PathMode path_mode);

// Returns true if the path search prefix is specified.
bool HasSearchPrefix(
    ResolvedGraphPathSearchPrefixEnums::PathSearchPrefixType prefix_type);

// Returns the intersection of the two periods, or std::nullopt if it is empty.
// A NULL start or end means unbounded.
// REQUIRES: `period1` and `period2` are valid non-null RANGE<TIMESTAMP> values.
absl::StatusOr<std::optional<Value>> IntersectPeriods(const Value& period1,
                                                      const Value& period2);

// Returns true if the period contains the timestamp.
// REQUIRES: `period` is a valid non-null RANGE<TIMESTAMP> value
// and `point` is a valid non-null TIMESTAMP value.
absl::StatusOr<bool> PeriodContainsPoint(const Value& period,
                                         const Value& point);

}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_GRAPH_ELEMENT_UTILS_H_
