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

#include "googlesql/common/graph_element_utils.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/catalog.h"
#include "googlesql/public/functions/json.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "googlesql/base/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {

bool TypeIsOrContainsGraphElement(const Type* type) {
  if (type->IsGraphElement()) {
    return true;
  }

  for (const Type* component_type : type->ComponentTypes()) {
    if (TypeIsOrContainsGraphElement(component_type)) {
      return true;
    }
  }
  return false;
}

absl::StatusOr<const Column*> GetPropertyBackingColumn(
    const GraphPropertyDefinition& prop_def) {
  GOOGLESQL_RET_CHECK(prop_def.GetDeclaration().kind() !=
            GraphPropertyDeclaration::Kind::kInvalid);
  if (prop_def.GetDeclaration().kind() ==
      GraphPropertyDeclaration::Kind::kMeasure) {
    return nullptr;
  }

  GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedExpr* val_expr, prop_def.GetValueExpression());
  GOOGLESQL_RET_CHECK_NE(val_expr, nullptr)
      << "Property definition must have a value expression";

  if (!val_expr->Is<ResolvedCatalogColumnRef>()) {
    return nullptr;
  }

  const Column* col = val_expr->GetAs<ResolvedCatalogColumnRef>()->column();
  GOOGLESQL_RET_CHECK_NE(col, nullptr);
  return col;
}

absl::StatusOr<absl::flat_hash_set<const Column*>> GetPropertyBackingColumns(
    const GraphElementTable& target_table) {
  absl::flat_hash_set<const GraphPropertyDefinition*> prop_defs;
  GOOGLESQL_RETURN_IF_ERROR(target_table.GetPropertyDefinitions(prop_defs));

  absl::flat_hash_set<const Column*> backing_columns;
  for (const GraphPropertyDefinition* prop_def : prop_defs) {
    GOOGLESQL_RET_CHECK_NE(prop_def, nullptr);
    GOOGLESQL_ASSIGN_OR_RETURN(const Column* backing_col,
                     GetPropertyBackingColumn(*prop_def));
    if (backing_col != nullptr) {
      backing_columns.insert(backing_col);
    }
  }
  return backing_columns;
}

absl::StatusOr<std::vector<const Column*>> GetElementKeyColumns(
    const GraphElementTable& target_table) {
  GOOGLESQL_RET_CHECK(!target_table.GetKeyColumns().empty())
      << "Graph element table '" << target_table.Name()
      << "' has no key columns";
  const Table* base_table = target_table.GetTable();
  GOOGLESQL_RET_CHECK_NE(base_table, nullptr)
      << "Graph element table '" << target_table.Name()
      << "' has no underlying base table";

  std::vector<const Column*> key_columns;
  key_columns.reserve(target_table.GetKeyColumns().size());
  for (int key_idx : target_table.GetKeyColumns()) {
    GOOGLESQL_RET_CHECK_GE(key_idx, 0);
    GOOGLESQL_RET_CHECK_LT(key_idx, base_table->NumColumns());
    const Column* key_col = base_table->GetColumn(key_idx);
    GOOGLESQL_RET_CHECK_NE(key_col, nullptr);
    key_columns.push_back(key_col);
  }
  return key_columns;
}

absl::StatusOr<bool> AreAllKeyColumnsExposedAsProperties(
    const GraphElementTable& target_table, bool must_be_writable,
    std::string* error_message) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::vector<const Column*> key_columns,
                   GetElementKeyColumns(target_table));
  GOOGLESQL_ASSIGN_OR_RETURN(absl::flat_hash_set<const Column*> exposed_columns,
                   GetPropertyBackingColumns(target_table));

  // Element key columns of target_table itself must be exposed as (optionally
  // writable) properties in its property definition list.
  for (const Column* key_col : key_columns) {
    if (must_be_writable && !key_col->IsWritableColumn()) {
      if (error_message != nullptr) {
        *error_message =
            absl::StrCat("Graph element table '", target_table.Name(),
                         "' is not updatable because the element key column '",
                         key_col->Name(), "' is not writable");
      }
      return false;
    }

    // An element table key column must be exposed as a property whose value
    // expression is a direct catalog column reference (not a derived property
    // or a measure property).
    if (!exposed_columns.contains(key_col)) {
      if (error_message != nullptr) {
        *error_message =
            absl::StrCat("Graph element table '", target_table.Name(),
                         "' is not updatable because the element key column '",
                         key_col->Name(), "' is not exposed as a ",
                         must_be_writable ? "writable " : "", "property");
      }
      return false;
    }
  }
  return true;
}

absl::StatusOr<const Column*> GetDynamicLabelBackingColumn(
    const GraphElementTable& target_table) {
  if (!target_table.HasDynamicLabel()) {
    return nullptr;
  }
  const GraphDynamicLabel* dynamic_label = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(target_table.GetDynamicLabel(dynamic_label));
  GOOGLESQL_RET_CHECK_NE(dynamic_label, nullptr);
  GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedExpr* val_expr,
                   dynamic_label->GetValueExpression());
  GOOGLESQL_RET_CHECK_NE(val_expr, nullptr);
  GOOGLESQL_RET_CHECK(val_expr->Is<ResolvedCatalogColumnRef>());
  const Column* col = val_expr->GetAs<ResolvedCatalogColumnRef>()->column();
  GOOGLESQL_RET_CHECK_NE(col, nullptr);
  return col;
}

absl::StatusOr<const Column*> GetDynamicPropertiesBackingColumn(
    const GraphElementTable& target_table) {
  if (!target_table.HasDynamicProperties()) {
    return nullptr;
  }
  const GraphDynamicProperties* dynamic_properties = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(target_table.GetDynamicProperties(dynamic_properties));
  GOOGLESQL_RET_CHECK_NE(dynamic_properties, nullptr);
  GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedExpr* val_expr,
                   dynamic_properties->GetValueExpression());
  GOOGLESQL_RET_CHECK_NE(val_expr, nullptr);
  GOOGLESQL_RET_CHECK(val_expr->Is<ResolvedCatalogColumnRef>());
  const Column* col = val_expr->GetAs<ResolvedCatalogColumnRef>()->column();
  GOOGLESQL_RET_CHECK_NE(col, nullptr);
  return col;
}

absl::StatusOr<EdgeEndpointBackingColumns> GetEdgeEndpointBackingColumns(
    const GraphElementTable& target_table) {
  EdgeEndpointBackingColumns endpoint_columns;
  if (target_table.kind() != GraphElementTable::Kind::kEdge) {
    return endpoint_columns;
  }
  const GraphEdgeTable* edge_table = target_table.AsEdgeTable();
  GOOGLESQL_RET_CHECK_NE(edge_table, nullptr);
  const Table* base_table = edge_table->GetTable();
  GOOGLESQL_RET_CHECK_NE(base_table, nullptr);
  if (edge_table->GetSourceNodeTable() != nullptr) {
    for (int col_idx :
         edge_table->GetSourceNodeTable()->GetEdgeTableColumns()) {
      GOOGLESQL_RET_CHECK_GE(col_idx, 0);
      GOOGLESQL_RET_CHECK_LT(col_idx, base_table->NumColumns());
      const Column* col = base_table->GetColumn(col_idx);
      GOOGLESQL_RET_CHECK_NE(col, nullptr);
      endpoint_columns.source_key_columns.insert(col);
    }
  }
  if (edge_table->GetDestNodeTable() != nullptr) {
    for (int col_idx : edge_table->GetDestNodeTable()->GetEdgeTableColumns()) {
      GOOGLESQL_RET_CHECK_GE(col_idx, 0);
      GOOGLESQL_RET_CHECK_LT(col_idx, base_table->NumColumns());
      const Column* col = base_table->GetColumn(col_idx);
      GOOGLESQL_RET_CHECK_NE(col, nullptr);
      endpoint_columns.dest_key_columns.insert(col);
    }
  }
  return endpoint_columns;
}

absl::StatusOr<bool> IsTargetTableInsertable(
    const GraphElementTable& target_table, bool needs_dynamic_label,
    std::string* error_message) {
  GOOGLESQL_ASSIGN_OR_RETURN(bool key_cols_writable,
                   AreAllKeyColumnsExposedAsProperties(
                       target_table, /*must_be_writable=*/true, error_message));
  if (!key_cols_writable) {
    return false;
  }

  if (needs_dynamic_label) {
    if (!target_table.HasDynamicLabel()) {
      if (error_message != nullptr) {
        *error_message =
            absl::StrCat("Graph element table '", target_table.Name(),
                         "' does not support dynamic labels");
      }
      return false;
    }
    GOOGLESQL_ASSIGN_OR_RETURN(const Column* dynamic_label_col,
                     GetDynamicLabelBackingColumn(target_table));
    GOOGLESQL_RET_CHECK_NE(dynamic_label_col, nullptr);
    if (!dynamic_label_col->IsWritableColumn()) {
      if (error_message != nullptr) {
        *error_message = absl::StrCat(
            "Cannot insert dynamic label because the dynamic label backing "
            "column '",
            dynamic_label_col->Name(), "' is read-only");
      }
      return false;
    }
  }

  if (target_table.kind() == GraphElementTable::Kind::kEdge) {
    const GraphEdgeTable* edge_table = target_table.AsEdgeTable();
    GOOGLESQL_RET_CHECK_NE(edge_table, nullptr);

    // Edge table side: SOURCE KEY and DESTINATION KEY columns must be writable
    // columns in the edge base table (they do not need to be exposed as edge
    // properties).
    auto are_edge_endpoint_key_columns_writable =
        [&edge_table, error_message](
            absl::Span<const int> col_indices,
            absl::string_view endpoint_kind) -> absl::StatusOr<bool> {
      const Table* edge_base_table = edge_table->GetTable();
      GOOGLESQL_RET_CHECK_NE(edge_base_table, nullptr);
      for (int col_idx : col_indices) {
        GOOGLESQL_RET_CHECK_GE(col_idx, 0);
        GOOGLESQL_RET_CHECK_LT(col_idx, edge_base_table->NumColumns());
        const Column* col = edge_base_table->GetColumn(col_idx);
        GOOGLESQL_RET_CHECK_NE(col, nullptr);
        if (!col->IsWritableColumn()) {
          if (error_message != nullptr) {
            *error_message = absl::StrCat(
                "Graph element table '", edge_table->Name(),
                "' is not updatable because the edge ", endpoint_kind,
                " key column '", col->Name(), "' is not writable");
          }
          return false;
        }
      }
      return true;
    };

    // The following columns must be exposed as properties on the endpoint node
    // tables (they do not need to be writable on the node tables):
    // 1. Referenced endpoint columns (used to populate edge referencing
    //    columns).
    // 2. Each endpoint node table's own element key columns (used to construct
    //    endpoint ResolvedGraphElementIdentifiers when REFERENCES targets
    //    non-key columns).
    auto are_node_table_columns_exposed =
        [&edge_table, error_message](
            const GraphNodeTable& node_table,
            const absl::flat_hash_set<const Column*>& exposed_columns,
            absl::Span<const int> column_indices,
            absl::string_view endpoint_kind,
            absl::string_view column_kind) -> absl::StatusOr<bool> {
      const Table* node_base_table = node_table.GetTable();
      GOOGLESQL_RET_CHECK_NE(node_base_table, nullptr);
      for (int col_idx : column_indices) {
        GOOGLESQL_RET_CHECK_GE(col_idx, 0);
        GOOGLESQL_RET_CHECK_LT(col_idx, node_base_table->NumColumns());
        const Column* col = node_base_table->GetColumn(col_idx);
        GOOGLESQL_RET_CHECK_NE(col, nullptr);
        if (!exposed_columns.contains(col)) {
          if (error_message != nullptr) {
            *error_message = absl::StrCat(
                "The edge table '", edge_table->Name(),
                "' is not updatable because the ", endpoint_kind,
                " node table '", node_table.Name(), "' does not expose the ",
                column_kind, " '", col->Name(), "' as a property");
          }
          return false;
        }
      }
      return true;
    };

    auto are_edge_endpoint_columns_valid =
        [&](const GraphNodeTableReference* endpoint_ref,
            absl::string_view endpoint_kind) -> absl::StatusOr<bool> {
      if (endpoint_ref == nullptr) {
        return true;
      }
      GOOGLESQL_ASSIGN_OR_RETURN(bool edge_cols_writable,
                       are_edge_endpoint_key_columns_writable(
                           endpoint_ref->GetEdgeTableColumns(), endpoint_kind));
      if (!edge_cols_writable) {
        return false;
      }
      const GraphNodeTable* node_table = endpoint_ref->GetReferencedNodeTable();
      GOOGLESQL_RET_CHECK_NE(node_table, nullptr);
      GOOGLESQL_ASSIGN_OR_RETURN(absl::flat_hash_set<const Column*> exposed_columns,
                       GetPropertyBackingColumns(*node_table));
      GOOGLESQL_ASSIGN_OR_RETURN(
          bool key_cols_exposed,
          are_node_table_columns_exposed(*node_table, exposed_columns,
                                         node_table->GetKeyColumns(),
                                         endpoint_kind, "element key column"));
      if (!key_cols_exposed) {
        return false;
      }
      GOOGLESQL_ASSIGN_OR_RETURN(
          bool ref_cols_exposed,
          are_node_table_columns_exposed(*node_table, exposed_columns,
                                         endpoint_ref->GetNodeTableColumns(),
                                         endpoint_kind, "referenced column"));
      if (!ref_cols_exposed) {
        return false;
      }
      return true;
    };

    GOOGLESQL_ASSIGN_OR_RETURN(bool src_endpoint_valid,
                     are_edge_endpoint_columns_valid(
                         edge_table->GetSourceNodeTable(), "source"));
    if (!src_endpoint_valid) {
      return false;
    }
    GOOGLESQL_ASSIGN_OR_RETURN(bool dst_endpoint_valid,
                     are_edge_endpoint_columns_valid(
                         edge_table->GetDestNodeTable(), "destination"));
    if (!dst_endpoint_valid) {
      return false;
    }

    // An edge table cannot be insertable if its source and destination keys
    // share underlying edge base table columns, because inserting an edge
    // would produce duplicate column insertions into the target table scan.
    if (edge_table->GetSourceNodeTable() != nullptr &&
        edge_table->GetDestNodeTable() != nullptr) {
      absl::flat_hash_set<int> source_col_set(
          edge_table->GetSourceNodeTable()->GetEdgeTableColumns().begin(),
          edge_table->GetSourceNodeTable()->GetEdgeTableColumns().end());
      for (int dest_col_idx :
           edge_table->GetDestNodeTable()->GetEdgeTableColumns()) {
        if (source_col_set.contains(dest_col_idx)) {
          const Table* edge_base_table = edge_table->GetTable();
          GOOGLESQL_RET_CHECK_NE(edge_base_table, nullptr);
          GOOGLESQL_RET_CHECK_GE(dest_col_idx, 0);
          GOOGLESQL_RET_CHECK_LT(dest_col_idx, edge_base_table->NumColumns());
          const Column* shared_col = edge_base_table->GetColumn(dest_col_idx);
          GOOGLESQL_RET_CHECK_NE(shared_col, nullptr);
          if (error_message != nullptr) {
            *error_message = absl::StrCat(
                "The edge table '", edge_table->Name(),
                "' is not updatable because its source and destination keys "
                "share underlying edge table column '",
                shared_col->Name(), "'");
          }
          return false;
        }
      }
    }
  }
  return true;
}

absl::StatusOr<bool> IsTargetTableUpdatable(
    const GraphElementTable& target_table, std::string* error_message) {
  return AreAllKeyColumnsExposedAsProperties(
      target_table, /*must_be_writable=*/false, error_message);
}

absl::StatusOr<const GraphElementType*> BuildGraphElementTypeForTable(
    const GraphElementTable& target_table,
    absl::Span<const std::string> graph_name_path, TypeFactory& type_factory,
    const LanguageOptions& language_options) {
  const GraphElementType::ElementKind element_kind =
      target_table.kind() == GraphElementTable::Kind::kNode
          ? GraphElementType::kNode
          : GraphElementType::kEdge;

  absl::flat_hash_set<const GraphPropertyDefinition*> prop_defs;
  GOOGLESQL_RETURN_IF_ERROR(target_table.GetPropertyDefinitions(prop_defs));

  std::vector<GraphElementType::PropertyType> property_types;
  property_types.reserve(prop_defs.size());
  for (const auto* def : prop_defs) {
    GOOGLESQL_RET_CHECK_NE(def, nullptr);
    property_types.push_back(
        {def->GetDeclaration().Name(), def->GetDeclaration().Type()});
  }

  std::vector<std::string> element_table_names;
  if (language_options.LanguageFeatureEnabled(
          FEATURE_SQL_GRAPH_UPDATABLE_GRAPH_ELEMENT_TYPE)) {
    element_table_names.push_back(target_table.Name());
  }

  const GraphElementType* elem_type = nullptr;
  if (target_table.HasDynamicProperties()) {
    GOOGLESQL_RETURN_IF_ERROR(type_factory.MakeDynamicGraphElementType(
        graph_name_path, element_kind, property_types, element_table_names,
        &elem_type));
  } else {
    GOOGLESQL_RETURN_IF_ERROR(type_factory.MakeGraphElementType(
        graph_name_path, element_kind, property_types, element_table_names,
        &elem_type));
  }
  return elem_type;
}

absl::StatusOr<std::vector<const GraphElementTable*>> GetTargetElementTables(
    const PropertyGraph& property_graph, const GraphElementType& element_type) {
  std::vector<const GraphElementTable*> target_tables;
  target_tables.reserve(element_type.element_tables().size());
  for (const std::string& table_name : element_type.element_tables()) {
    const GraphElementTable* table = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(property_graph.FindElementTableByName(table_name, table));
    GOOGLESQL_RET_CHECK_NE(table, nullptr);
    target_tables.push_back(table);
  }
  return target_tables;
}

absl::StatusOr<JSONValue> MakePropertiesJsonValue(
    absl::Span<Value::Property> properties,
    const LanguageOptions& language_options) {
  functions::JsonObjectBuilder json_builder(language_options,
                                            /*canonicalize_zero=*/false);
  std::vector<absl::string_view> json_keys;
  std::vector<const Value*> json_values;
  for (const auto& p : properties) {
    json_keys.emplace_back(p.first);
    json_values.emplace_back(&p.second);
  }
  GOOGLESQL_ASSIGN_OR_RETURN(JSONValue json_value,
                   functions::JsonObject(json_keys, absl::MakeSpan(json_values),
                                         json_builder));
  return json_value;
}

absl::StatusOr<std::string> GetPropertyName(
    const ResolvedGraphGetElementProperty* node) {
  GOOGLESQL_RET_CHECK(node != nullptr);
  if (node->property_name() != nullptr) {
    GOOGLESQL_RET_CHECK(node->property_name()->Is<ResolvedLiteral>() &&
              node->property_name()->type()->IsString())
        << "Expecting a string literal for property name, but got: "
        << node->property_name()->DebugString();
    return node->property_name()
        ->GetAs<ResolvedLiteral>()
        ->value()
        .string_value();
  }
  GOOGLESQL_RET_CHECK(node->property() != nullptr) << "Expecting property field is set "
                                            "in: "
                                         << node->DebugString();
  return node->property()->Name();
}

namespace {

bool IsDynamicPropertyEquals(const ResolvedNode* node) {
  return node->Is<ResolvedFunctionCall>() &&
         node->GetAs<ResolvedFunctionCall>()->function()->Name() ==
             "$dynamic_property_equals";
}

bool IsStaticPropertyEquals(const ResolvedNode* node) {
  return node->Is<ResolvedFunctionCall>() &&
         node->GetAs<ResolvedFunctionCall>()->function()->Name() == "$equal" &&
         node->GetAs<ResolvedFunctionCall>()->argument_list_size() == 2 &&
         node->GetAs<ResolvedFunctionCall>()
             ->argument_list(0)
             ->Is<ResolvedGraphGetElementProperty>();
}

}  // namespace

absl::StatusOr<bool> ContainsDynamicPropertySpecification(
    const ResolvedExpr* filter_expr,
    std::vector<const ResolvedExpr*>& property_specifications,
    std::vector<const ResolvedExpr*>& remaining_conjuncts) {
  if (filter_expr != nullptr && filter_expr->Is<ResolvedFunctionCall>()) {
    const auto* function_call = filter_expr->GetAs<ResolvedFunctionCall>();
    if (function_call->function()->Name() == "$and") {
      bool found_dynamic_property_specifications = false;
      for (const auto& argument : function_call->argument_list()) {
        GOOGLESQL_ASSIGN_OR_RETURN(
            bool found_dynamic_property_specification,
            ContainsDynamicPropertySpecification(
                argument.get(), property_specifications, remaining_conjuncts));
        found_dynamic_property_specifications |=
            found_dynamic_property_specification;
      }
      return found_dynamic_property_specifications;
    }
    if (IsDynamicPropertyEquals(function_call)) {
      property_specifications.push_back(function_call);
      return true;
    }
    if (IsStaticPropertyEquals(function_call)) {
      property_specifications.push_back(function_call);
      return false;
    }
  }
  // Otherwise, add the function call to remaining conjuncts.
  if (filter_expr != nullptr) {
    remaining_conjuncts.push_back(filter_expr);
  }
  return false;
}

absl::StatusOr<std::vector<std::pair<std::string, const ResolvedExpr*>>>
ToPropertySpecifications(std::vector<const ResolvedExpr*> exprs) {
  std::vector<std::pair<std::string, const ResolvedExpr*>>
      property_specifications;
  property_specifications.reserve(exprs.size());
  for (const ResolvedExpr* expr : exprs) {
    GOOGLESQL_RET_CHECK(expr->Is<ResolvedFunctionCall>());
    const auto* function_call = expr->GetAs<ResolvedFunctionCall>();
    if (IsDynamicPropertyEquals(function_call)) {
      GOOGLESQL_RET_CHECK_EQ(function_call->argument_list_size(), 3);
      GOOGLESQL_RET_CHECK(function_call->argument_list(1)->Is<ResolvedLiteral>());
      GOOGLESQL_RET_CHECK(function_call->argument_list(1)->type()->IsString());
      std::string property_name = function_call->argument_list(1)
                                      ->GetAs<ResolvedLiteral>()
                                      ->value()
                                      .string_value();
      property_specifications.emplace_back(property_name,
                                           function_call->argument_list(2));
    } else {
      GOOGLESQL_RET_CHECK(IsStaticPropertyEquals(function_call));
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::string property_name,
          GetPropertyName(function_call->argument_list(0)
                              ->GetAs<ResolvedGraphGetElementProperty>()));
      property_specifications.emplace_back(property_name,
                                           function_call->argument_list(1));
    }
  }
  GOOGLESQL_RET_CHECK_EQ(property_specifications.size(), exprs.size());
  return property_specifications;
}

bool IsRestrictivePathMode(ResolvedGraphPathModeEnums::PathMode path_mode) {
  return path_mode == ResolvedGraphPathModeEnums::SIMPLE ||
         path_mode == ResolvedGraphPathModeEnums::ACYCLIC ||
         path_mode == ResolvedGraphPathModeEnums::TRAIL;
}

bool HasSearchPrefix(
    ResolvedGraphPathSearchPrefixEnums::PathSearchPrefixType prefix_type) {
  return prefix_type != ResolvedGraphPathSearchPrefixEnums::
                            PATH_SEARCH_PREFIX_TYPE_UNSPECIFIED;
}

absl::StatusOr<std::optional<Value>> IntersectPeriods(const Value& period1,
                                                      const Value& period2) {
  GOOGLESQL_RET_CHECK(!period1.is_null() && !period2.is_null());
  GOOGLESQL_RET_CHECK(period1.type()->IsRange() && period2.type()->IsRange());
  GOOGLESQL_RET_CHECK(period1.type()->AsRange()->element_type()->IsTimestamp());
  GOOGLESQL_RET_CHECK(period2.type()->AsRange()->element_type()->IsTimestamp());

  // A NULL start sorts before any timestamp, so this picks the later start and
  // treats NULL as unbounded.
  Value intersect_start = period1.start().LessThan(period2.start())
                              ? period2.start()
                              : period1.start();
  Value intersect_end;
  if (period1.end().is_null()) {
    intersect_end = period2.end();
  } else if (period2.end().is_null()) {
    intersect_end = period1.end();
  } else {
    intersect_end =
        period1.end().LessThan(period2.end()) ? period1.end() : period2.end();
  }

  if (!intersect_start.is_null() && !intersect_end.is_null() &&
      !intersect_start.LessThan(intersect_end)) {
    return std::nullopt;
  }
  GOOGLESQL_ASSIGN_OR_RETURN(Value intersection,
                   Value::MakeRange(intersect_start, intersect_end));
  return intersection;
}

absl::StatusOr<bool> PeriodContainsPoint(const Value& period,
                                         const Value& point) {
  GOOGLESQL_RET_CHECK(!period.is_null() && !point.is_null());
  GOOGLESQL_RET_CHECK(period.type()->IsRange());
  GOOGLESQL_RET_CHECK(period.type()->AsRange()->element_type()->IsTimestamp());
  GOOGLESQL_RET_CHECK(point.type()->IsTimestamp());

  bool ge_start = period.start().is_null() || !point.LessThan(period.start());
  bool lt_end = period.end().is_null() || point.LessThan(period.end());

  return ge_start && lt_end;
}

}  // namespace googlesql
