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

#ifndef GOOGLESQL_ANALYZER_GRAPH_DML_RESOLVER_H_
#define GOOGLESQL_ANALYZER_GRAPH_DML_RESOLVER_H_

#include <memory>
#include <vector>

#include "googlesql/analyzer/graph_query_resolver_helper.h"
#include "googlesql/analyzer/name_scope.h"
#include "googlesql/analyzer/resolver.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace googlesql {

// This class performs resolution for Graph DML operators during GoogleSQL
// analysis.
//
class GraphDmlResolver {
 public:
  GraphDmlResolver(Resolver* resolver, const PropertyGraph* graph)
      : resolver_(resolver), graph_(graph) {}

  // Resolves a Graph INSERT operator.
  // Returns a ResolvedGraphInsertScan that appends the newly created elements
  // as new columns.
  //
  // An INSERT operator can have multiple INSERT path patterns, each of which
  // must start and end with a node variable. A variable can be either of the
  // following:
  //  * Declaration: If a variable within an INSERT path pattern is not in
  //    scope, it is a declaration. The name becomes in scope for remaining
  //    INSERT path patterns and linear scans. A declaration in INSERT must
  //    have at least a label and optionally an element property specification.
  //  * Reference: If a variable within an INSERT path pattern is already in
  //    scope, it is a reference. A reference in INSERT must not contain any
  //    label or property set specification.
  //
  // If a new variable name appears multiple times within a single INSERT
  // operator, the first occurrence performs the declaration, and all subsequent
  // occurrences are references to that same element.
  //
  // Rules:
  //  * A declaration must match exactly ONE graph element table.
  //  * Edge variable reference is not allowed, as that implies mutating an
  //    existing edge, which is not allowed in INSERT. INSERT only creates new
  //    nodes or new edges between existing nodes.
  //  * Only label name as identifiers and label conjunctions (&) are allowed in
  //    a label expression.
  //  * Property specifications must map to properties in the matched table,
  //    and their values are coerced to property types using implicit
  //    assignment.
  //  * Duplicate property names in the same element specification are errors.
  // See (broken link):dml-insert for more details.
  absl::StatusOr<ResolvedGraphWithNameList<const ResolvedScan>>
  ResolveGqlInsert(const ASTGqlInsert& ast_insert, const NameScope* input_scope,
                   ResolvedGraphWithNameList<const ResolvedScan> input);

  // Resolves a Graph SET operator.
  // Returns a ResolvedGraphUpdateScan that applies property and label
  // modifications to existing graph elements in the working table, and replaces
  // the modified element columns with new columns produced by
  // `ResolvedGraphUpdateElement`.
  //
  // A SET operator modifies graph elements by setting individual properties,
  // replacing all properties, or adding labels:
  //  * Setting individual properties (`SET a.prop = expr`): Updates or adds a
  //    property value on target element `a`. Static properties are coerced to
  //    their declared type. For dynamic properties, only `expr` with JSON
  //    convertible type is allowed.
  //  * Replacing all properties (`SET a = {prop: expr, ...}`): Replaces all
  //    properties (both static and dynamic) on target element `a` according to
  //    the element property specification. Unlisted updatable static properties
  //    are replaced with NULL, and unlisted dynamic properties are discarded.
  //  * Adding labels (`SET a:Label`): Adds a dynamic label to target element
  //    `a` (does not support static graph elements).
  //
  // If a SET operator targets the same variable multiple times (e.g.,
  // `SET a.prop = 1, a:Label`), all modifications for that variable are grouped
  // into a single `ResolvedGraphUpdateElement`.
  //
  // Rules:
  //  * Target variable must be in scope and evaluate to a graph node or edge.
  //  * Static property values are coerced to the property's declared type.
  //  * Setting dynamic properties or adding dynamic labels requires the
  //    FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE language feature.
  //  * Duplicate property names targeting the same element in a SET
  //    operator are errors; duplicate label names are deduplicated
  //    idempotently.
  //  * TODO: Add analysis-time table-level checks (whether
  //    candidate tables support dynamic properties or dynamic labels, whether
  //    properties exist on all underlying element tables, or whether backing
  //    columns are writable) when updatable graph element types are supported.
  // See (broken link):dml-update for more details.
  absl::StatusOr<ResolvedGraphWithNameList<const ResolvedScan>> ResolveGqlSet(
      const ASTGqlSet& ast_set, const NameScope* input_scope,
      ResolvedGraphWithNameList<const ResolvedScan> input);

  // Resolves a Graph REMOVE operator.
  // Returns a ResolvedGraphUpdateScan that removes dynamic properties or labels
  // from existing graph elements in the working table, and replaces the
  // modified element columns with new columns produced by
  // `ResolvedGraphUpdateElement`.
  //
  // A REMOVE operator modifies graph elements by removing dynamic properties or
  // labels:
  //  * Removing dynamic properties (`REMOVE a.prop`): Deletes a dynamic
  //    property from target element `a`. Static/schematized properties cannot
  //    be removed.
  //  * Removing labels (`REMOVE a:Label`): Deletes a dynamic label from target
  //    element `a`. Static labels cannot be removed.
  //
  // If a REMOVE operator targets the same variable multiple times (e.g.,
  // `REMOVE a.prop1, a.prop2`), all removals for that variable are grouped
  // into a single `ResolvedGraphUpdateElement`.
  //
  // Rules:
  //  * Target variable must be in scope and evaluate to a graph node or edge.
  //  * Removing dynamic properties or dynamic labels requires the
  //    FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE language feature.
  //  * Static (schematized) properties and static labels cannot be removed
  //    (use SET a.prop = NULL to clear a static property value).
  //  * Duplicate property names and label names are deduplicated
  //    idempotently.
  //  * TODO: Add analysis-time table-level checks (whether
  //    candidate tables support dynamic properties or dynamic labels, or
  //    whether backing columns are writable) when updatable graph element types
  //    are supported.
  // See (broken link):dml-update for more details.
  absl::StatusOr<ResolvedGraphWithNameList<const ResolvedScan>>
  ResolveGqlRemove(const ASTGqlRemove& ast_remove, const NameScope* input_scope,
                   ResolvedGraphWithNameList<const ResolvedScan> input);

  // Resolves a Graph DELETE operator.
  // Returns a `ResolvedGraphDeleteScan` that deletes the specified graph
  // elements in the working table, and replaces the deleted element columns
  // with new computed columns produced by `delete_item_list`.
  //
  // Rules:
  //  * Target variable must be in scope and evaluate to a graph node or edge.
  //  * Deleting the same variable multiple times in a single DELETE operator
  //    (e.g., `DELETE a, a`) is idempotent. These repeated references are
  //    deduplicated during resolution.
  // See (broken link):dml-delete for more details.
  absl::StatusOr<ResolvedGraphWithNameList<const ResolvedScan>>
  ResolveGqlDelete(const ASTGqlDelete& ast_delete, const NameScope* input_scope,
                   ResolvedGraphWithNameList<const ResolvedScan> input);

 private:
  // Resolves an AST property specification against the target element table.
  // Performs assignment based type coercion and validates that:
  //  - Property names are unique in the specification.
  //  - Properties do not map to edge endpoint referencing key columns.
  //  - Properties do not map to the dynamic label backing column when dynamic
  //    labels are specified.
  //  - Properties do not map to the dynamic properties backing column when
  //    dynamic properties are specified.
  //  - Multiple properties do not map to the same target table column.
  // `has_dynamic_label` indicates whether the INSERT element pattern specifies
  // at least one dynamic label.
  absl::StatusOr<
      std::vector<std::unique_ptr<const ResolvedGraphDMLPropertyItem>>>
  ResolveInsertProperties(const ASTGraphPropertySpecification* ast_prop_spec,
                          const GraphElementTable& target_table,
                          bool has_dynamic_label, const NameScope* input_scope);

  // Builds the GraphElementType based on the properties of the target table.
  // Supports both static and dynamic graph element types.
  absl::StatusOr<const GraphElementType*> BuildGraphElementType(
      const GraphElementTable& target_table);

  // Resolves an insert node element pattern.
  // - `node_pattern` is the AST node pattern to resolve.
  // - `input_scope` is the input name scope of the INSERT statement.
  // - `node_tables` is the set of all node tables in the graph.
  // - `variables` is a map of all named graph element variables seen so far in
  //   the same INSERT statement. Newly declared node variables are added to
  //   this map.
  // - `insert_node_list` is the list of all inserted node elements seen so far
  //   in the same INSERT statement. Newly created node elements are appended to
  //   this list.
  absl::StatusOr<ResolvedColumn> ResolveInsertNodePattern(
      const ASTGraphInsertNodePattern* node_pattern,
      const NameScope* input_scope,
      const absl::flat_hash_set<const GraphNodeTable*>& node_tables,
      IdStringLinkedHashMapCase<ResolvedColumn>& variables,
      std::vector<std::unique_ptr<const ResolvedComputedColumnBase>>&
          insert_node_list);

  // Resolves an insert edge element pattern.
  // - `edge_pattern` is the AST edge pattern to resolve.
  // - `input_scope` is the input name scope of the INSERT statement.
  // - `source_col` is the resolved source node column of the edge.
  // - `dest_col` is the resolved destination node column of the edge.
  // - `edge_tables` is the set of all edge tables in the graph.
  // - `variables` is a map of all named graph element variables seen so far in
  //   the same INSERT statement. Newly declared edge variables are added to
  //   this map.
  // - `insert_node_list` is the list of all inserted node elements seen so far
  //   in the same INSERT statement.
  // - `insert_edge_list` is the list of all inserted edge elements seen so far
  //   in the same INSERT statement. Newly created edge elements are appended to
  //   this list.
  absl::StatusOr<ResolvedColumn> ResolveInsertEdgePattern(
      const ASTGraphInsertEdgePattern* edge_pattern,
      const NameScope* input_scope, const ResolvedColumn& source_col,
      const ResolvedColumn& dest_col,
      const absl::flat_hash_set<const GraphEdgeTable*>& edge_tables,
      IdStringLinkedHashMapCase<ResolvedColumn>& variables,
      const std::vector<std::unique_ptr<const ResolvedComputedColumnBase>>&
          insert_node_list,
      std::vector<std::unique_ptr<const ResolvedComputedColumnBase>>&
          insert_edge_list);

  // Validates an edge endpoint (source or destination node) is valid for the
  // to-be-inserted edge.
  // If the endpoint node is a newly inserted node:
  //  - `actual_node_table` must not be nullptr. Validate that it matches the
  //    `expected_node_table`. Otherwise, return an error.
  // If the endpoint node is a referenced node:
  //  - `actual_node_table` must be nullptr. Validate that the node type of
  //    `node_col` is a supertype of the graph element type of the
  //    `expected_node_table`. Otherwise, return an error.
  //
  // - `error_location` is the AST node where the error occurred.
  // - `target_table` is the edge table being inserted.
  // - `expected_node_table` is the expected node table for the edge endpoint.
  // - `is_source` indicates whether the endpoint is a source or destination
  //   node.
  absl::Status ValidateEdgeEndpoint(
      const ASTGraphInsertEdgePattern& error_location,
      const GraphEdgeTable& target_table,
      const GraphNodeTable* actual_node_table,
      const GraphNodeTable& expected_node_table, const ResolvedColumn& node_col,
      bool is_source);

  // Resolves property and label modifications for a single target element
  // variable in a SET operator.
  absl::StatusOr<std::unique_ptr<const ResolvedGraphUpdateElement>>
  ResolveSetGraphUpdateElement(const ResolvedColumn& target_col,
                               const std::vector<const ASTGqlSetItem*>& items,
                               const NameScope* input_scope);

  // Resolves a single property assignment (name and value expression) for a SET
  // operator, performing static property type coercion or checking that
  // dynamic properties are allowed by the target element type.
  // TODO: Add table-level physical checks when updatable graph
  // element types are supported.
  absl::StatusOr<std::unique_ptr<const ResolvedGraphDMLPropertyItem>>
  ResolveSetPropertyItemValue(IdString prop_id, const ASTNode* ast_prop_name,
                              const ASTExpression* ast_prop_val_expr,
                              const GraphElementType* target_element_type,
                              const ResolvedColumn& target_col,
                              const NameScope* input_scope);

  // Resolves label assignments (`SET n:Label`) for a SET operator.
  absl::StatusOr<std::vector<std::unique_ptr<const ResolvedGraphLabel>>>
  ResolveSetLabelItems(
      const std::vector<const ASTGqlSetLabelItem*>& label_items);

  // Resolves property and label removals for a single target element variable
  // in a REMOVE operator.
  absl::StatusOr<std::unique_ptr<const ResolvedGraphUpdateElement>>
  ResolveRemoveGraphUpdateElement(
      const ResolvedColumn& target_col,
      const std::vector<const ASTGqlRemoveItem*>& items);

  // Resolves property removals (`REMOVE n.prop`) for a REMOVE operator.
  absl::StatusOr<
      std::vector<std::unique_ptr<const ResolvedGraphDMLPropertyItem>>>
  ResolveRemovePropertyItems(
      const std::vector<const ASTGqlRemovePropertyItem*>& property_items,
      const GraphElementType* target_element_type);

  // Resolves label removals (`REMOVE n:Label`) for a REMOVE operator.
  absl::StatusOr<std::vector<std::unique_ptr<const ResolvedGraphLabel>>>
  ResolveRemoveLabelItems(
      const std::vector<const ASTGqlRemoveLabelItem*>& label_items);

  Resolver* resolver_ = nullptr;
  const PropertyGraph* graph_ = nullptr;
};

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_GRAPH_DML_RESOLVER_H_
