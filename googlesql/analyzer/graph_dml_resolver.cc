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

#include "googlesql/analyzer/graph_dml_resolver.h"

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "googlesql/analyzer/expr_resolver_helper.h"
#include "googlesql/analyzer/graph_query_resolver_helper.h"
#include "googlesql/analyzer/name_scope.h"
#include "googlesql/analyzer/resolver.h"
#include "googlesql/common/graph_element_utils.h"
#include "googlesql/common/json_conversion_utils.h"
#include "googlesql/parser/ast_node.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parse_tree_errors.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/type_modifiers.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "googlesql/base/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {

STATIC_IDSTRING(kElementTableName, "$element_table");
STATIC_IDSTRING(kInsertNodeName, "$insert_node");
STATIC_IDSTRING(kInsertEdgeName, "$insert_edge");

namespace {

// Returns a list of requested label names in the label filter.
// Returns an error if the label filter is not a simple label or a
// conjunction (&) of simple labels.
absl::StatusOr<std::vector<IdString>> ExtractInsertLabelNames(
    const ASTGraphLabelFilter* ast_label_filter) {
  GOOGLESQL_RET_CHECK_NE(ast_label_filter, nullptr);
  GOOGLESQL_RET_CHECK_NE(ast_label_filter->label_expression(), nullptr);

  std::vector<IdString> labels;
  const auto* expr = ast_label_filter->label_expression();

  // This is a simple label.
  if (expr->Is<ASTGraphElementLabel>()) {
    labels.push_back(
        expr->GetAsOrDie<ASTGraphElementLabel>()->name()->GetAsIdString());
    return labels;
  }

  auto make_invalid_label_sql_error = [&]() {
    return MakeSqlErrorAt(ast_label_filter)
           << "Only label identifiers and label conjunction (&) are "
              "supported in INSERT";
  };

  // This is a conjunction of simple labels.
  if (expr->Is<ASTGraphLabelOperation>()) {
    const auto* op = expr->GetAsOrDie<ASTGraphLabelOperation>();
    if (op->op_type() != ASTGraphLabelOperation::AND) {
      return make_invalid_label_sql_error();
    }

    for (const auto* input : op->inputs()) {
      if (!input->Is<ASTGraphElementLabel>()) {
        return make_invalid_label_sql_error();
      }
      labels.push_back(
          input->GetAsOrDie<ASTGraphElementLabel>()->name()->GetAsIdString());
    }
    return labels;
  }

  return make_invalid_label_sql_error();
}

// Validates whether a static property is writable. Rejects read-only
// properties, generated properties, and measure properties.
// Returns the underlying base table column if writable.
absl::StatusOr<const Column*> ValidatePropertyIsWritable(
    const GraphPropertyDefinition& prop_def, const ASTNode* error_location) {
  GOOGLESQL_RET_CHECK(prop_def.GetDeclaration().kind() !=
            GraphPropertyDeclaration::Kind::kInvalid);
  if (prop_def.GetDeclaration().kind() ==
      GraphPropertyDeclaration::Kind::kMeasure) {
    return MakeSqlErrorAt(error_location)
           << "Cannot insert into measure property '"
           << prop_def.GetDeclaration().Name() << "'";
  }
  GOOGLESQL_ASSIGN_OR_RETURN(const Column* col, GetPropertyBackingColumn(prop_def));
  if (col == nullptr) {
    return MakeSqlErrorAt(error_location)
           << "Cannot insert into derived property '"
           << prop_def.GetDeclaration().Name() << "'";
  }
  if (!col->IsWritableColumn()) {
    return MakeSqlErrorAt(error_location) << "Cannot insert into property '"
                                          << prop_def.GetDeclaration().Name()
                                          << "' mapped from a read-only column";
  }
  return col;
}

// Returns an ordered de-duplicated list of requested unique label names in the
// label filter.
// Returns an error if the label filter is not present.
absl::StatusOr<std::vector<IdString>> ValidateAndGetUniqueInsertLabelNames(
    const ASTGraphInsertElementPatternFiller* filler,
    GraphElementType::ElementKind element_kind) {
  if (filler->label_filter() == nullptr) {
    return MakeSqlErrorAt(filler)
           << "Labels are required for new "
           << (element_kind == GraphElementType::kNode ? "node" : "edge")
           << " variable in INSERT";
  }

  GOOGLESQL_ASSIGN_OR_RETURN(std::vector<IdString> label_names,
                   ExtractInsertLabelNames(filler->label_filter()));

  // Deduplicate and preserve order
  std::vector<IdString> unique_label_names;
  IdStringHashSetCase seen_labels;
  for (const auto& name : label_names) {
    if (seen_labels.insert(name).second) {
      unique_label_names.push_back(name);
    }
  }
  return unique_label_names;
}

template <typename T>
struct TargetTableAndLabels {
  const T* table = nullptr;
  std::vector<std::unique_ptr<const ResolvedGraphLabel>> labels;
  bool has_dynamic_label = false;
};

// Finds exactly one table matching all the requested labels and returns the
// resolved labels.
// Returns a SQL error if no table matches or if multiple tables match
// (i.e. the matches are ambiguous).
template <typename T>
absl::StatusOr<TargetTableAndLabels<T>> ResolveTargetTableAndLabels(
    const absl::flat_hash_set<const T*>& tables,
    const std::vector<IdString>& unique_label_names,
    const ASTNode* error_location, GraphElementType::ElementKind element_kind,
    bool supports_dynamic_element_type) {
  static_assert(
      std::is_same_v<T, GraphNodeTable> || std::is_same_v<T, GraphEdgeTable>,
      "Type parameter must be GraphNodeTable or GraphEdgeTable");
  std::vector<TargetTableAndLabels<T>> matches;
  // Tracks whether we've seen a table with dynamic labels, so that multiple
  // tables with dynamic labels are not permitted.
  bool seen_dynamic_label_table = false;
  for (const auto* table : tables) {
    bool has_all_requested_labels = true;
    // Tracks the number of dynamic labels matched on this table to ensure
    // single-dynamic-label tables reject multiple dynamic labels, and so
    // that multiple dynamic labels on the same table are permitted for
    // multi-label tables.
    int dynamic_label_count = 0;
    std::vector<std::unique_ptr<const ResolvedGraphLabel>> target_labels;
    target_labels.reserve(unique_label_names.size());
    for (const auto& label_name : unique_label_names) {
      const GraphElementLabel* found_label = nullptr;
      std::unique_ptr<const ResolvedLiteral> resolved_label_name;
      if (table->FindLabelByName(label_name.ToString(), found_label).ok()) {
        // Static label.
        if (supports_dynamic_element_type) {
          GOOGLESQL_ASSIGN_OR_RETURN(resolved_label_name,
                           ResolvedLiteralBuilder()
                               .set_type(types::StringType())
                               .set_value(Value::String(label_name.ToString()))
                               .set_has_explicit_type(true)
                               .Build());
        }
        target_labels.push_back(MakeResolvedGraphLabel(
            found_label, std::move(resolved_label_name)));
      } else if (table->HasDynamicLabel() && supports_dynamic_element_type) {
        // Ensure we only check for ambiguous tables with dynamic labels once
        // per table, allowing multiple dynamic labels on the same candidate
        // table.
        if (dynamic_label_count == 0) {
          GOOGLESQL_RET_CHECK(!seen_dynamic_label_table)
              << "Multiple tables with dynamic labels are not supported";
          seen_dynamic_label_table = true;
        }
        ++dynamic_label_count;
        if (dynamic_label_count > 1 &&
            table->DynamicLabelCardinality() ==
                GraphElementTable::DynamicLabelCardinality::kSingle) {
          has_all_requested_labels = false;
          break;
        }
        // Dynamic label.
        GOOGLESQL_ASSIGN_OR_RETURN(resolved_label_name,
                         ResolvedLiteralBuilder()
                             .set_type(types::StringType())
                             .set_value(Value::String(label_name.ToString()))
                             .set_has_explicit_type(true)
                             .Build());
        target_labels.push_back(MakeResolvedGraphLabel(
            found_label, std::move(resolved_label_name)));
      } else {
        has_all_requested_labels = false;
        break;
      }
    }
    if (has_all_requested_labels) {
      matches.push_back(TargetTableAndLabels<T>{
          .table = table,
          .labels = std::move(target_labels),
          .has_dynamic_label = (dynamic_label_count > 0)});
    }
  }

  const absl::string_view element_type_str =
      (element_kind == GraphElementType::kNode) ? "node" : "edge";
  if (matches.empty()) {
    return MakeSqlErrorAt(error_location)
           << "No " << element_type_str
           << " table matches all requested labels";
  }
  if (matches.size() > 1) {
    return MakeSqlErrorAt(error_location)
           << "Ambiguous label specification matches multiple "
           << element_type_str << " tables";
  }

  return std::move(matches.front());
}

// Merges the input name list and variables resolved in the INSERT statement
// into a new NameList.
absl::StatusOr<std::shared_ptr<NameList>> MergeInsertVariablesToNameList(
    const NameList* input_name_list,
    const IdStringLinkedHashMapCase<ResolvedColumn>& variables) {
  auto new_name_list = std::make_shared<NameList>();
  if (input_name_list != nullptr) {
    for (const NamedColumn& col : input_name_list->columns()) {
      GOOGLESQL_RETURN_IF_ERROR(new_name_list->AddColumn(col.name(), col.column(),
                                               col.is_explicit()));
    }
  }
  for (const auto& [var_name, col] : variables) {
    NameTarget unused_target;
    GOOGLESQL_ASSIGN_OR_RETURN(bool found,
                     new_name_list->LookupName(var_name, &unused_target));
    if (!found) {
      GOOGLESQL_RETURN_IF_ERROR(
          new_name_list->AddColumn(var_name, col, /*is_explicit=*/true));
    }
  }
  return new_name_list;
}

// Replaces updated or deleted graph element columns in `input_name_list` with
// their corresponding post-DML operation `output_column`s from
// `modified_col_map`.
absl::StatusOr<std::unique_ptr<NameList>> ReplaceModifiedVariablesInNameList(
    const NameList* input_name_list,
    const absl::flat_hash_map<ResolvedColumn, ResolvedColumn>&
        modified_col_map) {
  auto new_name_list = std::make_unique<NameList>();
  if (input_name_list != nullptr) {
    for (const NamedColumn& col : input_name_list->columns()) {
      auto it = modified_col_map.find(col.column());
      const ResolvedColumn& output_col =
          (it != modified_col_map.end()) ? it->second : col.column();
      GOOGLESQL_RETURN_IF_ERROR(
          new_name_list->AddColumn(col.name(), output_col, col.is_explicit()));
    }
  }
  return new_name_list;
}

// Checks if the variable is already defined in the current INSERT statement or
// in the input name scope.
// If the variable exists:
//  - Returns error if the variable is re-declared with labels or properties;
//  - Returns error if the variable is an edge `element_kind`;
//  - Returns error if the variable is a node `element_kind` but found an edge;
//  - Otherwise, returns the existing ResolvedColumn and add it to the named
//    `variables` map.
// If the variable does not exist, returns std::nullopt.
absl::StatusOr<std::optional<ResolvedColumn>> ResolveExistingVariableIfExists(
    const ASTGraphInsertElementPatternFiller* filler,
    const NameScope* input_scope, GraphElementType::ElementKind element_kind,
    IdStringLinkedHashMapCase<ResolvedColumn>& variables) {
  GOOGLESQL_RET_CHECK_NE(filler, nullptr)
      << "Insert element filler must always be present";
  const ASTIdentifier* ast_variable = filler->variable_name();
  // Anonymous variable is always a new variable and does not exist before.
  if (ast_variable == nullptr) {
    return std::nullopt;
  }
  IdString var_name = ast_variable->GetAsIdString();
  bool is_existing_name = false;

  // Check if the variable is already seen in the current INSERT statement or
  // in the input name scope.
  if (variables.contains(var_name)) {
    is_existing_name = true;
  } else if (input_scope != nullptr) {
    NameTarget target;
    GOOGLESQL_ASSIGN_OR_RETURN(bool found, input_scope->LookupName(var_name, &target));
    if (found) {
      is_existing_name = true;
      variables[var_name] = target.column();
    }
  }

  // Simply return, when the variable does not exist.
  if (!is_existing_name) {
    return std::nullopt;
  }

  // The variable already exists.
  if (filler->label_filter() != nullptr) {
    return MakeSqlErrorAt(ast_variable)
           << "Label specification is not allowed for existing variable '"
           << var_name << "' in INSERT";
  }
  if (filler->property_specification() != nullptr) {
    return MakeSqlErrorAt(ast_variable)
           << "Property specification is not allowed for existing "
              "variable '"
           << var_name << "' in INSERT";
  }
  // Edge variable cannot be referenced in INSERT.
  if (element_kind == GraphElementType::kEdge) {
    return MakeSqlErrorAt(ast_variable)
           << "Referencing an existing variable '" << var_name
           << "' as an edge in INSERT is not allowed";
  }
  // This is a node variable.
  const ResolvedColumn& existing_col = variables[var_name];
  const GraphElementType* existing_element_type =
      existing_col.type()->AsGraphElement();
  GOOGLESQL_RET_CHECK_NE(existing_element_type, nullptr);
  if (existing_element_type->element_kind() != element_kind) {
    return MakeSqlErrorAt(ast_variable)
           << "Variable '" << var_name
           << "' is declared as an edge elsewhere, but used as a node here";
  }
  return existing_col;
}

// Returns the matched GraphNodeTable if the given column corresponds to a node
// being inserted in the current statement, or nullptr otherwise.
const GraphNodeTable* GetInsertedNodeTable(
    const ResolvedColumn& node_col,
    absl::Span<const std::unique_ptr<const ResolvedComputedColumnBase>>
        insert_node_list) {
  for (const auto& computed_col_base : insert_node_list) {
    if (computed_col_base->column() == node_col) {
      const auto* computed_col =
          computed_col_base->GetAs<ResolvedComputedColumn>();
      const auto* insert_element =
          computed_col->expr()->GetAs<ResolvedGraphInsertElement>();
      return insert_element->element_table()->GetAs<GraphNodeTable>();
    }
  }
  return nullptr;
}

// Validates that `target_table` is updatable via `IsTargetTableUpdatable`, and
// returns a SQL error at `error_location` if it is not.
absl::Status ValidateTargetTableIsUpdatable(
    const GraphElementTable& target_table, bool needs_dynamic_label,
    const ASTNode* error_location) {
  std::string error_message;
  GOOGLESQL_ASSIGN_OR_RETURN(bool is_updatable,
                   IsTargetTableUpdatable(target_table, needs_dynamic_label,
                                          &error_message));
  if (!is_updatable) {
    return MakeSqlErrorAt(error_location) << error_message;
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<std::vector<std::unique_ptr<const ResolvedGraphDMLPropertyItem>>>
GraphDmlResolver::ResolveInsertProperties(
    const ASTGraphPropertySpecification* ast_prop_spec,
    const GraphElementTable& target_table, bool has_dynamic_label,
    const NameScope* input_scope) {
  std::vector<std::unique_ptr<const ResolvedGraphDMLPropertyItem>>
      property_items;
  if (ast_prop_spec == nullptr) {
    return property_items;
  }
  property_items.reserve(ast_prop_spec->property_name_and_value().size());

  // Collect edge endpoint key columns. For edge tables, source and destination
  // endpoint key columns are automatically populated from the endpoint nodes
  // during INSERT, and therefore cannot be explicitly specified in the property
  // specification.
  GOOGLESQL_ASSIGN_OR_RETURN(const EdgeEndpointBackingColumns edge_endpoint_columns,
                   GetEdgeEndpointBackingColumns(target_table));

  // Collect the dynamic label backing column if the element pattern specifies
  // at least one dynamic label.
  const Column* dynamic_label_column = nullptr;
  if (has_dynamic_label) {
    GOOGLESQL_RET_CHECK(target_table.HasDynamicLabel());
    GOOGLESQL_ASSIGN_OR_RETURN(dynamic_label_column,
                     GetDynamicLabelBackingColumn(target_table));
    GOOGLESQL_RET_CHECK_NE(dynamic_label_column, nullptr);
  }

  // Collect the dynamic properties backing column if any dynamic properties
  // are specified in `ast_prop_spec`.
  const Column* dynamic_properties_column = nullptr;
  if (target_table.HasDynamicProperties()) {
    GOOGLESQL_ASSIGN_OR_RETURN(const Column* backing_col,
                     GetDynamicPropertiesBackingColumn(target_table));
    GOOGLESQL_RET_CHECK_NE(backing_col, nullptr);
    for (const ASTGraphPropertyNameAndValue* prop :
         ast_prop_spec->property_name_and_value()) {
      const GraphPropertyDefinition* unused_def = nullptr;
      if (absl::IsNotFound(target_table.FindPropertyDefinitionByName(
              prop->property_name()->GetAsIdString().ToStringView(),
              unused_def))) {
        dynamic_properties_column = backing_col;
        break;
      }
    }
  }

  IdStringHashSetCase seen_props;
  // Set of underlying base table columns already targeted by static properties
  // in this INSERT specification. This is used to detect duplicate column
  // insertions caused by:
  // - Multiple specified properties mapping to the same column;
  // - Explicitly specified properties that map to edge endpoint key columns;
  // - Explicitly specified properties that map to the dynamic label backing
  //   column when dynamic labels are also being inserted;
  // - Explicitly specified properties that map to the dynamic properties
  //   backing column when dynamic properties are also being inserted.
  absl::flat_hash_set<const Column*> seen_target_columns;
  for (const ASTGraphPropertyNameAndValue* prop :
       ast_prop_spec->property_name_and_value()) {
    IdString prop_id = prop->property_name()->GetAsIdString();

    if (!seen_props.insert(prop_id).second) {
      return MakeSqlErrorAt(prop)
             << "Duplicate property '" << prop_id
             << "' is not allowed in INSERT property specification";
    }

    // First, try to find the static property definition in the target element
    // table.
    const GraphPropertyDefinition* static_prop_def = nullptr;
    absl::Status find_prop_status = target_table.FindPropertyDefinitionByName(
        prop_id.ToStringView(), static_prop_def);

    if (find_prop_status.ok()) {
      // If the property is found, validate that it is writable and fetch the
      // backing column in the base table.
      GOOGLESQL_RET_CHECK_NE(static_prop_def, nullptr);
      GOOGLESQL_ASSIGN_OR_RETURN(const Column* target_column,
                       ValidatePropertyIsWritable(*static_prop_def, prop));

      // Reject explicitly specified properties that map to edge endpoint key
      // columns, as they would cause duplicate insertion into the same column
      // alongside the automatically populated endpoint keys.
      const bool is_source_key =
          edge_endpoint_columns.source_key_columns.contains(target_column);
      const bool is_dest_key =
          edge_endpoint_columns.dest_key_columns.contains(target_column);
      if (is_source_key || is_dest_key) {
        absl::string_view key_kind =
            (is_source_key && is_dest_key)
                ? "source and destination"
                : (is_source_key ? "source" : "destination");
        return MakeSqlErrorAt(prop)
               << "Cannot explicitly specify property '" << prop_id
               << "' because it causes duplicate insertion into edge "
               << key_kind << " key column '" << target_column->Name() << "'";
      }

      // Reject explicitly specified properties that map to the dynamic label
      // or dynamic properties backing columns when dynamic labels or dynamic
      // properties are also being inserted.
      if (target_column == dynamic_label_column) {
        return MakeSqlErrorAt(prop)
               << "Cannot explicitly specify property '" << prop_id
               << "' because it causes duplicate insertion into dynamic label "
                  "backing column '"
               << target_column->Name() << "'";
      }
      if (target_column == dynamic_properties_column) {
        return MakeSqlErrorAt(prop)
               << "Cannot explicitly specify property '" << prop_id
               << "' because it causes duplicate insertion into dynamic "
                  "properties backing column '"
               << target_column->Name() << "'";
      }

      // Reject multiple properties that map to the same underlying base table
      // column (property aliasing).
      if (!seen_target_columns.insert(target_column).second) {
        return MakeSqlErrorAt(prop) << "Duplicate insertion into column '"
                                    << target_column->Name() << "'";
      }
    } else if (absl::IsNotFound(find_prop_status)) {
      if (dynamic_properties_column == nullptr) {
        return MakeSqlErrorAt(prop)
               << "Property '" << prop_id << "' not found in table "
               << target_table.Name();
      }
      if (!dynamic_properties_column->IsWritableColumn()) {
        return MakeSqlErrorAt(prop)
               << "Cannot insert dynamic property '" << prop_id
               << "' because the dynamic properties backing column '"
               << dynamic_properties_column->Name() << "' is read-only";
      }
    } else {
      GOOGLESQL_RETURN_IF_ERROR(find_prop_status);
    }

    std::unique_ptr<const ResolvedExpr> resolved_value_expr;
    auto expr_info = std::make_unique<ExprResolutionInfo>(
        input_scope, "INSERT property value");
    GOOGLESQL_RETURN_IF_ERROR(resolver_->ResolveExpr(prop->value(), expr_info.get(),
                                           &resolved_value_expr));

    if (static_prop_def != nullptr) {
      // Coerce the value expression to the property's static type using
      // implicit assignment semantics.
      GOOGLESQL_RETURN_IF_ERROR(resolver_->CoerceExprToType(
          prop->value(), static_prop_def->GetDeclaration().Type(),
          TypeModifiers(), Resolver::kImplicitAssignment,
          absl::StrCat("Cannot insert value of type $1 into property '",
                       prop_id.ToStringView(), "' of type $0"),
          &resolved_value_expr));
    } else {
      const Type* unsupported_type =
          GetUnsupportedTypeForJsonConversion(resolved_value_expr->type());
      if (unsupported_type != nullptr) {
        return MakeSqlErrorAt(prop->value())
               << "Cannot insert value of type "
               << resolved_value_expr->type()->ShortTypeName(
                      resolver_->product_mode())
               << " into dynamic property '" << prop_id << "': "
               << unsupported_type->ShortTypeName(resolver_->product_mode())
               << " does not support conversion to JSON";
      }
    }

    const GraphPropertyDeclaration* static_property_decl =
        static_prop_def != nullptr ? &static_prop_def->GetDeclaration()
                                   : nullptr;
    auto prop_item = MakeResolvedGraphDMLPropertyItem(
        prop_id.ToString(), static_property_decl,
        std::move(resolved_value_expr));
    property_items.push_back(std::move(prop_item));
  }
  return property_items;
}

absl::StatusOr<const GraphElementType*> GraphDmlResolver::BuildGraphElementType(
    const GraphElementTable& target_table) {
  GOOGLESQL_RET_CHECK(resolver_->language().LanguageFeatureEnabled(
      FEATURE_SQL_GRAPH_UPDATABLE_GRAPH_ELEMENT_TYPE))
      << "Updatable graph element type is required for Graph DML";
  GOOGLESQL_RET_CHECK_NE(resolver_->type_factory_, nullptr);

  return BuildGraphElementTypeForTable(target_table, graph_->NamePath(),
                                       *resolver_->type_factory_,
                                       resolver_->language());
}

absl::StatusOr<ResolvedGraphWithNameList<const ResolvedScan>>
GraphDmlResolver::ResolveGqlInsert(
    const ASTGqlInsert& ast_insert, const NameScope* input_scope,
    ResolvedGraphWithNameList<const ResolvedScan> input) {
  auto [input_scan, input_graph_name_lists] = std::move(input);
  auto input_name_list = input_graph_name_lists.singleton_name_list;

  // Keep track of all the variables resolved in the current INSERT statement.
  IdStringLinkedHashMapCase<ResolvedColumn> variables;

  // Fetch all the node and edge tables in the graph. This includes both static
  // and dynamic element tables.
  absl::flat_hash_set<const GraphNodeTable*> node_tables;
  GOOGLESQL_RETURN_IF_ERROR(graph_->GetNodeTables(node_tables));
  absl::flat_hash_set<const GraphEdgeTable*> edge_tables;
  GOOGLESQL_RETURN_IF_ERROR(graph_->GetEdgeTables(edge_tables));

  std::vector<std::unique_ptr<const ResolvedComputedColumnBase>>
      insert_node_list;
  std::vector<std::unique_ptr<const ResolvedComputedColumnBase>>
      insert_edge_list;
  std::vector<ResolvedColumn> path_element_list;

  for (const ASTGraphInsertPathPattern* ast_path : ast_insert.path_patterns()) {
    GOOGLESQL_RET_CHECK(!ast_path->elements().empty())
        << "Path pattern must have at least one element";
    GOOGLESQL_RET_CHECK_EQ(ast_path->elements().size() % 2, 1)
        << "Path pattern must have an odd number of elements";
    std::vector<ResolvedColumn> element_cols(ast_path->elements().size());

    // First pass: Resolve nodes
    for (int i = 0; i < ast_path->elements().size(); ++i) {
      const ASTGraphInsertElementPattern* ast_element = ast_path->elements(i);
      if (ast_element->Is<ASTGraphInsertEdgePattern>()) {
        GOOGLESQL_RET_CHECK_EQ(i % 2, 1) << "Expected edge pattern at odd index " << i;
        continue;
      }

      GOOGLESQL_RET_CHECK(ast_element->Is<ASTGraphInsertNodePattern>());
      GOOGLESQL_RET_CHECK_EQ(i % 2, 0) << "Expected node pattern at even index " << i;
      GOOGLESQL_ASSIGN_OR_RETURN(
          element_cols[i],
          ResolveInsertNodePattern(
              ast_element->GetAsOrDie<ASTGraphInsertNodePattern>(), input_scope,
              node_tables, variables, insert_node_list));
    }

    // Second pass: Resolve edges
    for (int i = 0; i < ast_path->elements().size(); ++i) {
      const ASTGraphInsertElementPattern* ast_element = ast_path->elements(i);
      if (ast_element->Is<ASTGraphInsertNodePattern>()) {
        continue;
      }

      const auto* edge_pattern =
          ast_element->GetAsOrDie<ASTGraphInsertEdgePattern>();
      GOOGLESQL_RET_CHECK(edge_pattern->orientation() == ASTGraphEdgePattern::LEFT ||
                edge_pattern->orientation() == ASTGraphEdgePattern::RIGHT);
      bool is_left = edge_pattern->orientation() == ASTGraphEdgePattern::LEFT;
      ResolvedColumn source_col =
          is_left ? element_cols[i + 1] : element_cols[i - 1];
      ResolvedColumn dest_col =
          is_left ? element_cols[i - 1] : element_cols[i + 1];
      GOOGLESQL_ASSIGN_OR_RETURN(
          element_cols[i],
          ResolveInsertEdgePattern(edge_pattern, input_scope, source_col,
                                   dest_col, edge_tables, variables,
                                   insert_node_list, insert_edge_list));
    }

    for (int i = 0; i < ast_path->elements().size(); ++i) {
      path_element_list.push_back(element_cols[i]);
    }
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      input_graph_name_lists.singleton_name_list,
      MergeInsertVariablesToNameList(input_name_list.get(), variables));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto resolved_insert,
      ResolvedGraphInsertScanBuilder()
          .set_column_list(
              input_graph_name_lists.singleton_name_list->GetResolvedColumns())
          .set_input_scan(std::move(input_scan))
          .set_insert_node_list(std::move(insert_node_list))
          .set_insert_edge_list(std::move(insert_edge_list))
          .set_path_element_list(std::move(path_element_list))
          .Build());

  return {{.resolved_node = std::move(resolved_insert),
           .graph_name_lists = std::move(input_graph_name_lists)}};
}

absl::StatusOr<ResolvedColumn> GraphDmlResolver::ResolveInsertNodePattern(
    const ASTGraphInsertNodePattern* node_pattern, const NameScope* input_scope,
    const absl::flat_hash_set<const GraphNodeTable*>& node_tables,
    IdStringLinkedHashMapCase<ResolvedColumn>& variables,
    std::vector<std::unique_ptr<const ResolvedComputedColumnBase>>&
        insert_node_list) {
  const ASTGraphInsertElementPatternFiller* filler = node_pattern->filler();

  // Check if the variable is already defined in the current INSERT statement
  // or in the input name scope.
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::optional<ResolvedColumn> existing_var,
      ResolveExistingVariableIfExists(filler, input_scope,
                                      GraphElementType::kNode, variables));
  if (existing_var.has_value()) {
    return existing_var.value();
  }

  // The variable is new. It's a declaration.
  // Validate that label specification is not empty and get unique label names.
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::vector<IdString> unique_label_names,
      ValidateAndGetUniqueInsertLabelNames(filler, GraphElementType::kNode));

  // Find a single matching node table that has all the requested labels.
  GOOGLESQL_ASSIGN_OR_RETURN(auto result,
                   ResolveTargetTableAndLabels(
                       node_tables, unique_label_names, filler->label_filter(),
                       GraphElementType::kNode,
                       resolver_->language().LanguageFeatureEnabled(
                           FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE)));
  const GraphNodeTable* target_table = result.table;
  GOOGLESQL_RET_CHECK_NE(target_table, nullptr);

  GOOGLESQL_RETURN_IF_ERROR(ValidateTargetTableIsUpdatable(
      *target_table, result.has_dynamic_label, filler));

  // Resolve the property specification.
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto property_items,
      ResolveInsertProperties(filler->property_specification(), *target_table,
                              result.has_dynamic_label, input_scope));

  // Add the newly declared variable to the variables map.
  GOOGLESQL_ASSIGN_OR_RETURN(const GraphElementType* element_type,
                   BuildGraphElementType(*target_table));
  const ASTIdentifier* ast_variable = filler->variable_name();
  ResolvedColumn element_col(resolver_->AllocateColumnId(),
                             /*table_name=*/kElementTableName,
                             /*name=*/ast_variable != nullptr
                                 ? ast_variable->GetAsIdString()
                                 : kInsertNodeName,
                             element_type);
  if (ast_variable != nullptr) {
    variables[ast_variable->GetAsIdString()] = element_col;
  }

  auto computed_col = MakeResolvedComputedColumn(
      element_col,
      MakeResolvedGraphInsertElement(element_type, std::move(property_items),
                                     std::move(result.labels), target_table));
  insert_node_list.push_back(std::move(computed_col));
  return element_col;
}

absl::StatusOr<ResolvedColumn> GraphDmlResolver::ResolveInsertEdgePattern(
    const ASTGraphInsertEdgePattern* edge_pattern, const NameScope* input_scope,
    const ResolvedColumn& source_col, const ResolvedColumn& dest_col,
    const absl::flat_hash_set<const GraphEdgeTable*>& edge_tables,
    IdStringLinkedHashMapCase<ResolvedColumn>& variables,
    const std::vector<std::unique_ptr<const ResolvedComputedColumnBase>>&
        insert_node_list,
    std::vector<std::unique_ptr<const ResolvedComputedColumnBase>>&
        insert_edge_list) {
  GOOGLESQL_RET_CHECK(source_col.type()->AsGraphElement()->IsNode());
  GOOGLESQL_RET_CHECK(dest_col.type()->AsGraphElement()->IsNode());
  const ASTGraphInsertElementPatternFiller* filler = edge_pattern->filler();

  // Check if the variable is already defined in the current INSERT statement
  // or in the input name scope. If so, it's an error because edge variable
  // cannot be referenced in INSERT.
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::optional<ResolvedColumn> existing_var,
      ResolveExistingVariableIfExists(filler, input_scope,
                                      GraphElementType::kEdge, variables));
  GOOGLESQL_RET_CHECK(!existing_var.has_value());

  // The variable is new. It's a declaration.
  // Validate that label specification is not empty and get unique label names.
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::vector<IdString> unique_label_names,
      ValidateAndGetUniqueInsertLabelNames(filler, GraphElementType::kEdge));

  // Find a single matching edge table that has all the requested labels.
  GOOGLESQL_ASSIGN_OR_RETURN(auto result,
                   ResolveTargetTableAndLabels(
                       edge_tables, unique_label_names, filler->label_filter(),
                       GraphElementType::kEdge,
                       resolver_->language().LanguageFeatureEnabled(
                           FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE)));

  // Validate that the edge's expected source and destination node tables match
  // the provided source and destination node tables.
  const GraphEdgeTable* target_table = result.table;
  GOOGLESQL_RET_CHECK_NE(target_table, nullptr);
  const GraphNodeTable* expected_source_node_table =
      target_table->GetSourceNodeTable()->GetReferencedNodeTable();
  const GraphNodeTable* expected_dest_node_table =
      target_table->GetDestNodeTable()->GetReferencedNodeTable();

  // Try to find the endpoint node element tables if any of the edge's endpoints
  // is newly inserted.
  const GraphNodeTable* actual_source_node_table =
      GetInsertedNodeTable(source_col, insert_node_list);
  const GraphNodeTable* actual_dest_node_table =
      GetInsertedNodeTable(dest_col, insert_node_list);

  // Validate the edge's source node.
  GOOGLESQL_RETURN_IF_ERROR(ValidateEdgeEndpoint(*edge_pattern, *target_table,
                                       actual_source_node_table,
                                       *expected_source_node_table, source_col,
                                       /*is_source=*/true));

  // Validate the edge's destination node.
  GOOGLESQL_RETURN_IF_ERROR(ValidateEdgeEndpoint(*edge_pattern, *target_table,
                                       actual_dest_node_table,
                                       *expected_dest_node_table, dest_col,
                                       /*is_source=*/false));

  GOOGLESQL_RETURN_IF_ERROR(ValidateTargetTableIsUpdatable(
      *target_table, result.has_dynamic_label, filler));

  // Resolve the property specification.
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto property_items,
      ResolveInsertProperties(filler->property_specification(), *target_table,
                              result.has_dynamic_label, input_scope));

  // Add the newly declared variable to the variables map.
  GOOGLESQL_ASSIGN_OR_RETURN(const GraphElementType* element_type,
                   BuildGraphElementType(*target_table));
  const ASTIdentifier* ast_variable = filler->variable_name();
  ResolvedColumn elem_col(resolver_->AllocateColumnId(),
                          /*table_name=*/kElementTableName,
                          /*name=*/ast_variable != nullptr
                              ? ast_variable->GetAsIdString()
                              : kInsertEdgeName,
                          element_type);
  if (ast_variable != nullptr) {
    variables[ast_variable->GetAsIdString()] = elem_col;
  }

  auto source_col_ref = resolver_->MakeColumnRef(source_col);
  auto dest_col_ref = resolver_->MakeColumnRef(dest_col);
  auto computed_col = MakeResolvedComputedColumn(
      elem_col,
      MakeResolvedGraphInsertElement(
          element_type, std::move(property_items), std::move(result.labels),
          std::move(source_col_ref), std::move(dest_col_ref), target_table));
  insert_edge_list.push_back(std::move(computed_col));
  return elem_col;
}

absl::Status GraphDmlResolver::ValidateEdgeEndpoint(
    const ASTGraphInsertEdgePattern& error_location,
    const GraphEdgeTable& target_table, const GraphNodeTable* actual_node_table,
    const GraphNodeTable& expected_node_table, const ResolvedColumn& node_col,
    bool is_source) {
  absl::string_view node_kind = is_source ? "source" : "destination";
  if (actual_node_table != nullptr) {
    // For newly inserted node, verify if the actual node table matches the
    // expected node table.
    if (actual_node_table != &expected_node_table) {
      return MakeSqlErrorAt(&error_location)
             << "The actual " << node_kind << " node table "
             << actual_node_table->Name() << " does not match expected "
             << node_kind << " node table " << expected_node_table.Name()
             << " for edge table " << target_table.Name();
    }
  } else {
    // For referenced node variable, verify if the passed-in node type is a
    // supertype of the required node type.
    const GraphElementType* element_type = node_col.type()->AsGraphElement();
    const GraphElementType* required_element_type = nullptr;
    GOOGLESQL_ASSIGN_OR_RETURN(required_element_type,
                     BuildGraphElementType(expected_node_table));
    if (!required_element_type->CoercibleTo(element_type)) {
      return MakeSqlErrorAt(&error_location)
             << "The actual " << node_kind << " node type "
             << node_col.type()->TypeName(resolver_->product_mode())
             << " does not match expected " << node_kind << " node table "
             << expected_node_table.Name();
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<const ResolvedGraphDMLPropertyItem>>
GraphDmlResolver::ResolveSetPropertyItemValue(
    IdString prop_id, const ASTNode* ast_prop_name,
    const ASTExpression* ast_prop_val_expr,
    const GraphElementType* target_element_type,
    const ResolvedColumn& target_col, const NameScope* input_scope) {
  std::unique_ptr<const ResolvedExpr> resolved_val;
  auto expr_info =
      std::make_unique<ExprResolutionInfo>(input_scope, "SET property value");
  GOOGLESQL_RETURN_IF_ERROR(resolver_->ResolveExpr(ast_prop_val_expr, expr_info.get(),
                                         &resolved_val));

  const PropertyType* prop_type =
      target_element_type->FindPropertyType(prop_id.ToStringView());
  const GraphPropertyDeclaration* static_decl = nullptr;
  if (prop_type != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(graph_->FindPropertyDeclarationByName(
        prop_id.ToStringView(), static_decl));
    GOOGLESQL_RET_CHECK(static_decl != nullptr);

    // Coerce value expression to static property's declared type.
    GOOGLESQL_RETURN_IF_ERROR(resolver_->CoerceExprToType(
        ast_prop_val_expr, static_decl->Type(), TypeModifiers(),
        Resolver::kImplicitAssignment,
        absl::StrCat("Cannot assign value of type $1 into property '",
                     prop_id.ToStringView(), "' of type $0"),
        &resolved_val));
  } else {
    // We only verify that the target element variable's type allows dynamic
    // properties.
    // TODO: Add table-level physical checks.
    if (!target_element_type->is_dynamic()) {
      return MakeSqlErrorAt(ast_prop_name)
             << "Property '" << prop_id
             << "' does not exist on graph element variable '"
             << target_col.name() << "'";
    }
    GOOGLESQL_RET_CHECK(resolver_->language().LanguageFeatureEnabled(
        FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE));
    const Type* unsupported_type =
        GetUnsupportedTypeForJsonConversion(resolved_val->type());
    if (unsupported_type != nullptr) {
      return MakeSqlErrorAt(ast_prop_val_expr)
             << "Cannot assign value of type "
             << resolved_val->type()->ShortTypeName(resolver_->product_mode())
             << " into dynamic property '" << prop_id << "': "
             << unsupported_type->ShortTypeName(resolver_->product_mode())
             << " does not support conversion to JSON";
    }
  }

  return MakeResolvedGraphDMLPropertyItem(prop_id.ToString(), static_decl,
                                          std::move(resolved_val));
}

absl::StatusOr<std::vector<std::unique_ptr<const ResolvedGraphLabel>>>
GraphDmlResolver::ResolveSetLabelItems(
    const std::vector<const ASTGqlSetLabelItem*>& label_items) {
  std::vector<std::unique_ptr<const ResolvedGraphLabel>> resolved_label_items;
  if (label_items.empty()) {
    return resolved_label_items;
  }

  // TODO: Check if the element table supports dynamic labels.
  if (!resolver_->language().LanguageFeatureEnabled(
          FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE)) {
    return MakeSqlErrorAt(label_items[0])
           << "SET label is only allowed on element tables supporting "
              "dynamic labels";
  }

  // Graph labels are matched case-insensitively.
  IdStringHashSetCase seen_labels;
  for (const ASTGqlSetLabelItem* label_item : label_items) {
    IdString label_id = label_item->label_name()->GetAsIdString();
    // Setting the same label multiple times in a single SET clause (e.g.
    // `SET n:Label, n:Label`) is idempotent. We deduplicate them here at
    // analysis time.
    if (!seen_labels.insert(label_id).second) {
      continue;
    }

    GOOGLESQL_ASSIGN_OR_RETURN(auto resolved_label_name,
                     ResolvedLiteralBuilder()
                         .set_type(types::StringType())
                         .set_value(Value::String(label_id.ToString()))
                         .set_has_explicit_type(true)
                         .Build());
    // SET label only supports adding dynamic labels (`label` is nullptr).
    // TODO: Validate that the target element table supports
    // dynamic labels and that `label_id` is not a static label.
    resolved_label_items.push_back(MakeResolvedGraphLabel(
        /*label=*/nullptr, std::move(resolved_label_name)));
  }
  return resolved_label_items;
}

// TODO: Add following checks when the updatable graph element type
// is supported:
//  * Check that the target element type backing tables are writable.
//  * Check that updated properties are universally writable among all
//    underlying element tables.
absl::StatusOr<std::unique_ptr<const ResolvedGraphUpdateElement>>
GraphDmlResolver::ResolveSetGraphUpdateElement(
    const ResolvedColumn& target_col,
    const std::vector<const ASTGqlSetItem*>& items,
    const NameScope* input_scope) {
  const GraphElementType* target_element_type =
      target_col.type()->AsGraphElement();

  const ASTGqlSetAllPropertiesItem* all_properties_item = nullptr;
  std::vector<const ASTGqlSetPropertyItem*> property_items;
  std::vector<const ASTGqlSetLabelItem*> label_items;
  for (const ASTGqlSetItem* item : items) {
    // `SET <var> = {...}` and `SET <var>.<property> = ...` are mutually
    // exclusive for the same variable.
    const bool has_conflicting_property_update_modes =
        (item->Is<ASTGqlSetAllPropertiesItem>() && !property_items.empty()) ||
        (item->Is<ASTGqlSetPropertyItem>() && all_properties_item != nullptr);
    if (has_conflicting_property_update_modes) {
      return MakeSqlErrorAt(item)
             << "Cannot mix SET " << target_col.name() << " = {...} with SET "
             << target_col.name() << ".property = ... for the same variable";
    }
    if (item->Is<ASTGqlSetAllPropertiesItem>()) {
      if (all_properties_item != nullptr) {
        return MakeSqlErrorAt(item) << "Duplicate SET all properties ({...}) "
                                       "specified for variable '"
                                    << target_col.name() << "'";
      }
      all_properties_item = item->GetAsOrDie<ASTGqlSetAllPropertiesItem>();
    } else if (item->Is<ASTGqlSetPropertyItem>()) {
      property_items.push_back(item->GetAsOrDie<ASTGqlSetPropertyItem>());
    } else if (item->Is<ASTGqlSetLabelItem>()) {
      // Label updates can be freely combined with any kind of property update
      // for the same variable.
      label_items.push_back(item->GetAsOrDie<ASTGqlSetLabelItem>());
    } else {
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected SET item type: "
                       << item->GetNodeKindString();
    }
  }

  // Resolve Graph DML property items and update modes.
  ResolvedGraphUpdateElement::LabelUpdateMode label_update_mode =
      label_items.empty() ? ResolvedGraphUpdateElement::LABEL_NO_UPDATE
                          : ResolvedGraphUpdateElement::LABEL_SET;
  ResolvedGraphUpdateElement::PropertyUpdateMode property_update_mode =
      ResolvedGraphUpdateElement::PROPERTY_NO_UPDATE;
  std::vector<std::unique_ptr<const ResolvedGraphDMLPropertyItem>>
      resolved_prop_items;
  if (all_properties_item != nullptr) {
    property_update_mode = ResolvedGraphUpdateElement::PROPERTY_REPLACE;
    if (all_properties_item->property_specification() != nullptr) {
      // Graph property names are matched case-insensitively.
      IdStringHashSetCase seen_props;
      for (const ASTGraphPropertyNameAndValue* ast_prop :
           all_properties_item->property_specification()
               ->property_name_and_value()) {
        IdString prop_id = ast_prop->property_name()->GetAsIdString();
        if (!seen_props.insert(prop_id).second) {
          return MakeSqlErrorAt(ast_prop->property_name())
                 << "Duplicate property '" << prop_id
                 << "' specified for variable '" << target_col.name() << "'";
        }

        GOOGLESQL_ASSIGN_OR_RETURN(
            auto resolved_item,
            ResolveSetPropertyItemValue(prop_id, ast_prop->property_name(),
                                        ast_prop->value(), target_element_type,
                                        target_col, input_scope));
        resolved_prop_items.push_back(std::move(resolved_item));
      }
    }
  } else if (!property_items.empty()) {
    property_update_mode = ResolvedGraphUpdateElement::PROPERTY_SET;
    // Graph property names are matched case-insensitively.
    IdStringHashSetCase seen_props;
    for (const ASTGqlSetPropertyItem* prop_item : property_items) {
      GOOGLESQL_RET_CHECK_EQ(prop_item->property_path()->num_names(), 2);
      IdString prop_id =
          prop_item->property_path()->last_name()->GetAsIdString();
      if (!seen_props.insert(prop_id).second) {
        return MakeSqlErrorAt(prop_item->property_path()->last_name())
               << "Duplicate property '" << prop_id
               << "' specified for variable '" << target_col.name() << "'";
      }

      GOOGLESQL_ASSIGN_OR_RETURN(auto resolved_item,
                       ResolveSetPropertyItemValue(
                           prop_id, prop_item->property_path()->last_name(),
                           prop_item->value(), target_element_type, target_col,
                           input_scope));
      resolved_prop_items.push_back(std::move(resolved_item));
    }
  }

  GOOGLESQL_RET_CHECK(property_update_mode !=
                ResolvedGraphUpdateElement::PROPERTY_NO_UPDATE ||
            label_update_mode != ResolvedGraphUpdateElement::LABEL_NO_UPDATE);

  GOOGLESQL_ASSIGN_OR_RETURN(auto resolved_label_items,
                   ResolveSetLabelItems(label_items));

  ResolvedColumn output_col(resolver_->AllocateColumnId(),
                            target_col.table_name_id(), target_col.name_id(),
                            target_col.annotated_type());
  auto target_ref = MakeResolvedColumnRef(target_col.type(), target_col,
                                          /*is_correlated=*/false);
  return ResolvedGraphUpdateElementBuilder()
      .set_target_element(std::move(target_ref))
      .set_output_column(output_col)
      .set_property_update_mode(property_update_mode)
      .set_property_list(std::move(resolved_prop_items))
      .set_label_update_mode(label_update_mode)
      .set_label_list(std::move(resolved_label_items))
      .Build();
}

absl::StatusOr<ResolvedGraphWithNameList<const ResolvedScan>>
GraphDmlResolver::ResolveGqlSet(
    const ASTGqlSet& ast_set, const NameScope* input_scope,
    ResolvedGraphWithNameList<const ResolvedScan> input) {
  auto [input_scan, input_graph_name_lists] = std::move(input);
  auto input_name_list = input_graph_name_lists.singleton_name_list;

  if (ast_set.items().empty()) {
    return MakeSqlErrorAt(&ast_set)
           << "SET statement must have at least one item";
  }

  // Group SET items by target element variable column to allow aggregate
  // updates for a single element variable into a single
  // ResolvedGraphUpdateElement.
  absl::btree_map<ResolvedColumn, std::vector<const ASTGqlSetItem*>>
      item_groups;

  for (const ASTGqlSetItem* item : ast_set.items()) {
    const ASTIdentifier* ast_var = nullptr;
    if (item->Is<ASTGqlSetPropertyItem>()) {
      const auto* prop_item = item->GetAsOrDie<ASTGqlSetPropertyItem>();
      // Validate LHS property path format: must be exactly
      // `<variable>.<property_name>`.
      if (prop_item->property_path()->num_names() != 2) {
        return MakeSqlErrorAt(prop_item->property_path())
               << "SET property target must be in the form "
                  "'<variable>.<property_name>'";
      }
      ast_var = prop_item->property_path()->first_name();
    } else if (item->Is<ASTGqlSetAllPropertiesItem>()) {
      ast_var =
          item->GetAsOrDie<ASTGqlSetAllPropertiesItem>()->element_variable();
    } else if (item->Is<ASTGqlSetLabelItem>()) {
      ast_var = item->GetAsOrDie<ASTGqlSetLabelItem>()->element_variable();
    } else {
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected SET item type: "
                       << item->GetNodeKindString();
    }

    // Resolve target variable name in current scope and verify it is a graph
    // node/edge column.
    IdString var_id = ast_var->GetAsIdString();
    NameTarget target;
    GOOGLESQL_ASSIGN_OR_RETURN(bool found, input_scope->LookupName(var_id, &target));
    if (!found) {
      return MakeSqlErrorAt(ast_var)
             << "Unrecognized variable " << var_id
             << "; The target variable must be a graph node or edge";
    }
    GOOGLESQL_RET_CHECK(target.IsColumn());

    ResolvedColumn target_col = target.column();
    if (!target_col.type()->IsGraphElement()) {
      return MakeSqlErrorAt(ast_var)
             << "Target of SET must be a graph node or edge, but found type "
             << target_col.type()->TypeName(resolver_->product_mode());
    }
    resolver_->RecordColumnAccess(target_col);

    item_groups[target_col].push_back(item);
  }

  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_element_list;
  update_element_list.reserve(item_groups.size());
  absl::flat_hash_map<ResolvedColumn, ResolvedColumn> updated_col_map;

  // Process updates grouped by target element column.
  for (const auto& [target_col, items] : item_groups) {
    GOOGLESQL_ASSIGN_OR_RETURN(auto update_element, ResolveSetGraphUpdateElement(
                                              target_col, items, input_scope));
    updated_col_map.emplace(target_col, update_element->output_column());
    update_element_list.push_back(std::move(update_element));
  }

  GOOGLESQL_ASSIGN_OR_RETURN(input_graph_name_lists.singleton_name_list,
                   ReplaceModifiedVariablesInNameList(input_name_list.get(),
                                                      updated_col_map));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto resolved_update,
      ResolvedGraphUpdateScanBuilder()
          .set_column_list(
              input_graph_name_lists.singleton_name_list->GetResolvedColumns())
          .set_input_scan(std::move(input_scan))
          .set_update_element_list(std::move(update_element_list))
          .Build());

  return ResolvedGraphWithNameList<const ResolvedScan>{
      std::move(resolved_update), std::move(input_graph_name_lists)};
}

absl::StatusOr<std::vector<std::unique_ptr<const ResolvedGraphDMLPropertyItem>>>
GraphDmlResolver::ResolveRemovePropertyItems(
    const std::vector<const ASTGqlRemovePropertyItem*>& property_items,
    const GraphElementType* target_element_type) {
  std::vector<std::unique_ptr<const ResolvedGraphDMLPropertyItem>>
      resolved_prop_items;
  if (property_items.empty()) {
    return resolved_prop_items;
  }
  if (!target_element_type->is_dynamic()) {
    return MakeSqlErrorAt(property_items.front())
           << "REMOVE property is only allowed on dynamic graph elements";
  }
  GOOGLESQL_RET_CHECK(resolver_->language().LanguageFeatureEnabled(
      FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE));

  // Graph property names are matched case-insensitively.
  IdStringHashSetCase seen_props;
  for (const ASTGqlRemovePropertyItem* prop_item : property_items) {
    GOOGLESQL_RET_CHECK_EQ(prop_item->property_path()->num_names(), 2);
    IdString prop_id = prop_item->property_path()->last_name()->GetAsIdString();
    // Removing the same property multiple times in a single REMOVE clause (e.g.
    // `REMOVE n.prop, n.prop`) is idempotent. We deduplicate them here at
    // analysis time.
    if (!seen_props.insert(prop_id).second) {
      continue;
    }

    const PropertyType* prop_type =
        target_element_type->FindPropertyType(prop_id.ToStringView());
    // Static (schematized) properties cannot be removed.
    if (prop_type != nullptr) {
      return MakeSqlErrorAt(prop_item->property_path()->last_name())
             << "Cannot remove static property '" << prop_id << "'; use SET "
             << prop_item->property_path()->first_name()->GetAsIdString() << "."
             << prop_id << " = NULL to clear a static property value";
    }

    // For REMOVE, `property` and `property_value` are set to nullptr.
    resolved_prop_items.push_back(MakeResolvedGraphDMLPropertyItem(
        prop_id.ToString(), /*property=*/nullptr,
        /*property_value=*/nullptr));
  }

  return resolved_prop_items;
}

absl::StatusOr<std::vector<std::unique_ptr<const ResolvedGraphLabel>>>
GraphDmlResolver::ResolveRemoveLabelItems(
    const std::vector<const ASTGqlRemoveLabelItem*>& label_items) {
  std::vector<std::unique_ptr<const ResolvedGraphLabel>> resolved_label_items;
  if (label_items.empty()) {
    return resolved_label_items;
  }

  // TODO: Check if the element table supports dynamic labels.
  if (!resolver_->language().LanguageFeatureEnabled(
          FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE)) {
    return MakeSqlErrorAt(label_items[0])
           << "REMOVE label is only allowed on element tables supporting "
              "dynamic labels";
  }

  // Graph labels are matched case-insensitively.
  IdStringHashSetCase seen_labels;
  for (const ASTGqlRemoveLabelItem* label_item : label_items) {
    IdString label_id = label_item->label_name()->GetAsIdString();
    // Removing the same label multiple times in a single REMOVE clause (e.g.
    // `REMOVE n:Label, n:Label`) is idempotent. We deduplicate them here at
    // analysis time.
    if (!seen_labels.insert(label_id).second) {
      continue;
    }

    // Static (schematized) labels defined in the property graph cannot be
    // removed.
    const GraphElementLabel* static_label = nullptr;
    absl::Status find_label_status =
        graph_->FindLabelByName(label_id.ToStringView(), static_label);
    if (find_label_status.ok()) {
      return MakeSqlErrorAt(label_item->label_name())
             << "Cannot remove static label '" << label_id << "'";
    }
    if (!absl::IsNotFound(find_label_status)) {
      return find_label_status;
    }

    GOOGLESQL_ASSIGN_OR_RETURN(auto resolved_label_name,
                     ResolvedLiteralBuilder()
                         .set_type(types::StringType())
                         .set_value(Value::String(label_id.ToString()))
                         .set_has_explicit_type(true)
                         .Build());
    // REMOVE label only supports removing dynamic labels (`label` is nullptr).
    resolved_label_items.push_back(MakeResolvedGraphLabel(
        /*label=*/nullptr, std::move(resolved_label_name)));
  }

  return resolved_label_items;
}

// TODO: Add following checks when the updatable graph element type
// is supported:
//  * Check that removed properties backing columns are writable.
//  * Check that removing labels are only allowed on tables that support
//    dynamic labels.
absl::StatusOr<std::unique_ptr<const ResolvedGraphUpdateElement>>
GraphDmlResolver::ResolveRemoveGraphUpdateElement(
    const ResolvedColumn& target_col,
    const std::vector<const ASTGqlRemoveItem*>& items) {
  const GraphElementType* target_element_type =
      target_col.type()->AsGraphElement();
  GOOGLESQL_RET_CHECK_NE(target_element_type, nullptr);

  std::vector<const ASTGqlRemovePropertyItem*> property_items;
  std::vector<const ASTGqlRemoveLabelItem*> label_items;

  for (const ASTGqlRemoveItem* item : items) {
    if (item->Is<ASTGqlRemovePropertyItem>()) {
      property_items.push_back(item->GetAsOrDie<ASTGqlRemovePropertyItem>());
    } else if (item->Is<ASTGqlRemoveLabelItem>()) {
      label_items.push_back(item->GetAsOrDie<ASTGqlRemoveLabelItem>());
    } else {
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected REMOVE item type: "
                       << item->GetNodeKindString();
    }
  }

  ResolvedGraphUpdateElement::PropertyUpdateMode property_update_mode =
      property_items.empty() ? ResolvedGraphUpdateElement::PROPERTY_NO_UPDATE
                             : ResolvedGraphUpdateElement::PROPERTY_REMOVE;

  ResolvedGraphUpdateElement::LabelUpdateMode label_update_mode =
      label_items.empty() ? ResolvedGraphUpdateElement::LABEL_NO_UPDATE
                          : ResolvedGraphUpdateElement::LABEL_REMOVE;

  GOOGLESQL_RET_CHECK(property_update_mode !=
                ResolvedGraphUpdateElement::PROPERTY_NO_UPDATE ||
            label_update_mode != ResolvedGraphUpdateElement::LABEL_NO_UPDATE);

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto resolved_prop_items,
      ResolveRemovePropertyItems(property_items, target_element_type));

  GOOGLESQL_ASSIGN_OR_RETURN(auto resolved_label_items,
                   ResolveRemoveLabelItems(label_items));

  ResolvedColumn output_col(resolver_->AllocateColumnId(),
                            target_col.table_name_id(), target_col.name_id(),
                            target_col.annotated_type());
  auto target_ref = MakeResolvedColumnRef(target_col.type(), target_col,
                                          /*is_correlated=*/false);
  return ResolvedGraphUpdateElementBuilder()
      .set_target_element(std::move(target_ref))
      .set_output_column(output_col)
      .set_property_update_mode(property_update_mode)
      .set_property_list(std::move(resolved_prop_items))
      .set_label_update_mode(label_update_mode)
      .set_label_list(std::move(resolved_label_items))
      .Build();
}

absl::StatusOr<ResolvedGraphWithNameList<const ResolvedScan>>
GraphDmlResolver::ResolveGqlRemove(
    const ASTGqlRemove& ast_remove, const NameScope* input_scope,
    ResolvedGraphWithNameList<const ResolvedScan> input) {
  auto [input_scan, input_graph_name_lists] = std::move(input);
  auto input_name_list = input_graph_name_lists.singleton_name_list;

  if (ast_remove.items().empty()) {
    return MakeSqlErrorAt(&ast_remove)
           << "REMOVE statement must have at least one item";
  }

  // Group REMOVE items by target element variable column.
  absl::btree_map<ResolvedColumn, std::vector<const ASTGqlRemoveItem*>>
      item_groups;

  for (const ASTGqlRemoveItem* item : ast_remove.items()) {
    const ASTIdentifier* ast_var = nullptr;
    if (item->Is<ASTGqlRemovePropertyItem>()) {
      const auto* prop_item = item->GetAsOrDie<ASTGqlRemovePropertyItem>();
      // Validate LHS property path format: must be exactly
      // `<variable>.<property_name>`.
      if (prop_item->property_path()->num_names() != 2) {
        return MakeSqlErrorAt(prop_item->property_path())
               << "REMOVE property target must be in the form "
                  "'<variable>.<property_name>'";
      }
      ast_var = prop_item->property_path()->first_name();
    } else if (item->Is<ASTGqlRemoveLabelItem>()) {
      ast_var = item->GetAsOrDie<ASTGqlRemoveLabelItem>()->element_variable();
    } else {
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected REMOVE item type: "
                       << item->GetNodeKindString();
    }

    // Resolve target variable name in current scope and verify it is a graph
    // node/edge column.
    IdString var_id = ast_var->GetAsIdString();
    NameTarget target;
    GOOGLESQL_ASSIGN_OR_RETURN(bool found, input_scope->LookupName(var_id, &target));
    if (!found) {
      return MakeSqlErrorAt(ast_var)
             << "Unrecognized variable " << var_id
             << "; The target variable must be a graph node or edge";
    }
    GOOGLESQL_RET_CHECK(target.IsColumn());

    ResolvedColumn target_col = target.column();
    if (!target_col.type()->IsGraphElement()) {
      return MakeSqlErrorAt(ast_var)
             << "Target of REMOVE must be a graph node or edge, but found type "
             << target_col.type()->TypeName(resolver_->product_mode());
    }
    resolver_->RecordColumnAccess(target_col);

    item_groups[target_col].push_back(item);
  }

  std::vector<std::unique_ptr<const ResolvedGraphUpdateElement>>
      update_element_list;
  update_element_list.reserve(item_groups.size());
  absl::flat_hash_map<ResolvedColumn, ResolvedColumn> updated_col_map;

  // Process removals grouped by target element column.
  for (const auto& [target_col, items] : item_groups) {
    GOOGLESQL_ASSIGN_OR_RETURN(auto update_element,
                     ResolveRemoveGraphUpdateElement(target_col, items));
    updated_col_map.emplace(target_col, update_element->output_column());
    update_element_list.push_back(std::move(update_element));
  }

  GOOGLESQL_ASSIGN_OR_RETURN(input_graph_name_lists.singleton_name_list,
                   ReplaceModifiedVariablesInNameList(input_name_list.get(),
                                                      updated_col_map));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto resolved_update,
      ResolvedGraphUpdateScanBuilder()
          .set_column_list(
              input_graph_name_lists.singleton_name_list->GetResolvedColumns())
          .set_input_scan(std::move(input_scan))
          .set_update_element_list(std::move(update_element_list))
          .Build());

  return ResolvedGraphWithNameList<const ResolvedScan>{
      std::move(resolved_update), std::move(input_graph_name_lists)};
}

absl::StatusOr<ResolvedGraphWithNameList<const ResolvedScan>>
GraphDmlResolver::ResolveGqlDelete(
    const ASTGqlDelete& ast_delete, const NameScope* input_scope,
    ResolvedGraphWithNameList<const ResolvedScan> input) {
  GOOGLESQL_RET_CHECK(input_scope != nullptr);

  // TODO: Add updated call to the helper function checking
  // whether the target table is updatable, once it is refactored.

  auto [input_scan, input_graph_name_lists] = std::move(input);
  auto input_name_list = input_graph_name_lists.singleton_name_list;

  std::vector<std::unique_ptr<const ResolvedComputedColumn>> delete_item_list;
  absl::flat_hash_map<ResolvedColumn, ResolvedColumn> deleted_col_map;

  // Validating that each deleted item that is referenced is an existing
  // graph element in scope. If it is a valid graph element in scope, then it
  // is deduplicated and added to the delete item list.
  NameTarget name_target;
  for (const ASTGqlDeleteItem* delete_item : ast_delete.delete_item_list()) {
    GOOGLESQL_RET_CHECK_NE(delete_item, nullptr);
    const ASTIdentifier* delete_element_variable =
        delete_item->element_variable();
    GOOGLESQL_RET_CHECK_NE(delete_element_variable, nullptr);

    IdString delete_item_name = delete_element_variable->GetAsIdString();
    GOOGLESQL_ASSIGN_OR_RETURN(bool is_in_scope,
                     input_scope->LookupName(delete_item_name, &name_target));
    if (!is_in_scope) {
      return MakeSqlErrorAt(delete_element_variable)
             << "Unrecognized variable " << delete_item_name
             << "; The target variable must be a graph node or edge";
    }
    GOOGLESQL_RET_CHECK(name_target.IsColumn());
    const ResolvedColumn& name_target_col = name_target.column();
    GOOGLESQL_RET_CHECK_NE(name_target_col.type(), nullptr);
    if (!name_target_col.type()->IsGraphElement()) {
      return MakeSqlErrorAt(delete_element_variable)
             << "DELETE item must be a graph node or edge, but found type "
             << name_target_col.type()->TypeName(resolver_->product_mode());
    }

    // Deleting the same graph element variable multiple times in a single
    // DELETE clause (e.g., `DELETE p, p`) is idempotent. We deduplicate those
    // columns here at analysis time for `delete_item_list` and the output
    // `column_list`.
    auto it = deleted_col_map.find(name_target_col);
    if (it == deleted_col_map.end()) {
      // New column representing the deleted state of a column.
      ResolvedColumn deleted_column(
          /*column_id=*/resolver_->AllocateColumnId(),
          /*table_name=*/name_target_col.table_name_id(),
          /*name=*/name_target_col.name_id(),
          /*annotated_type=*/name_target_col.annotated_type());
      delete_item_list.push_back(MakeResolvedComputedColumn(
          deleted_column, resolver_->MakeColumnRef(name_target_col)));
      deleted_col_map.emplace_hint(it, name_target_col, deleted_column);
    }
  }

  GOOGLESQL_ASSIGN_OR_RETURN(input_graph_name_lists.singleton_name_list,
                   ReplaceModifiedVariablesInNameList(input_name_list.get(),
                                                      deleted_col_map));

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedGraphDeleteScan> resolved_graph_delete_scan,
      ResolvedGraphDeleteScanBuilder()
          .set_column_list(
              input_graph_name_lists.singleton_name_list->GetResolvedColumns())
          .set_input_scan(std::move(input_scan))
          .set_delete_item_list(std::move(delete_item_list))
          .set_is_detach_mode(ast_delete.is_detach_mode())
          .Build());
  return {{.resolved_node = std::move(resolved_graph_delete_scan),
           .graph_name_lists = std::move(input_graph_name_lists)}};
}

}  // namespace googlesql
