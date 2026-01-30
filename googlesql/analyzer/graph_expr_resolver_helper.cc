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

#include "googlesql/analyzer/graph_expr_resolver_helper.h"

#include <memory>
#include <utility>
#include <vector>

#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parse_tree_errors.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

absl::StatusOr<const GraphDynamicLabel*> GetDynamicLabelOfElementTable(
    const GraphElementTable& element_table) {
  if (!element_table.HasDynamicLabel()) {
    return nullptr;
  }
  const GraphDynamicLabel* label = nullptr;
  GOOGLESQL_RET_CHECK_OK(element_table.GetDynamicLabel(label));
  return label;
}

absl::Status ValidateGraphElementTablesDynamicLabelAndProperties(
    const LanguageOptions& language_options, const ASTNode* error_location,
    const PropertyGraph& property_graph) {
  absl::flat_hash_set<const GraphNodeTable*> node_tables;
  GOOGLESQL_RETURN_IF_ERROR(property_graph.GetNodeTables(node_tables));
  absl::flat_hash_set<const GraphEdgeTable*> edge_tables;
  GOOGLESQL_RETURN_IF_ERROR(property_graph.GetEdgeTables(edge_tables));

  absl::flat_hash_set<const GraphElementTable*> element_tables{
      node_tables.begin(), node_tables.end()};
  element_tables.insert(edge_tables.begin(), edge_tables.end());
  for (const GraphElementTable* element_table : element_tables) {
    // Both DDL and query language features must be enabled for dynamic labels
    // and properties to be supported.
    if (!language_options.LanguageFeatureEnabled(
            FEATURE_SQL_GRAPH_DYNAMIC_ELEMENT_TYPE) ||
        !language_options.LanguageFeatureEnabled(
            FEATURE_SQL_GRAPH_DYNAMIC_LABEL_PROPERTIES_IN_DDL)) {
      if (element_table->HasDynamicLabel()) {
        return MakeSqlErrorAt(error_location)
               << "Dynamic label is not supported";
      }
      if (element_table->HasDynamicProperties()) {
        return MakeSqlErrorAt(error_location)
               << "Dynamic properties are not supported";
      }
    }

    // If the feature is not enabled, then we should not allow dynamic label
    // that holds ARRAY<STRING> expression.
    if (!language_options.LanguageFeatureEnabled(
            FEATURE_SQL_GRAPH_DYNAMIC_MULTI_LABEL_NODES)) {
      if (element_table->HasDynamicLabel() &&
          element_table->DynamicLabelCardinality() ==
              GraphElementTable::DynamicLabelCardinality::kMultiple) {
        return MakeSqlErrorAt(error_location)
               << "Dynamic label that holds ARRAY<STRING> expression is not "
                  "supported";
      }
    }
    // Language feature is enabled, then we should check only node tables can
    // have dynamic label that holds ARRAY<STRING> expression.
    if (element_table->HasDynamicLabel() &&
        element_table->DynamicLabelCardinality() ==
            GraphElementTable::DynamicLabelCardinality::kMultiple &&
        element_table->kind() != GraphElementTable::Kind::kNode) {
      return MakeSqlErrorAt(error_location)
             << "Dynamic label that holds ARRAY<STRING> expression is only "
                "supported for node tables";
    }
  }
  return absl::OkStatus();
}

absl::Status FindAllLabelsApplicableToElementKind(
    const PropertyGraph& property_graph, GraphElementTable::Kind element_kind,
    absl::flat_hash_set<const GraphElementLabel*>& static_labels,
    absl::flat_hash_map<const GraphElementTable*, const GraphDynamicLabel*>&
        dynamic_labels) {
  absl::flat_hash_set<const GraphElementTable*> element_tables;
  if (element_kind == GraphElementTable::Kind::kNode) {
    absl::flat_hash_set<const GraphNodeTable*> node_tables;
    GOOGLESQL_RETURN_IF_ERROR(property_graph.GetNodeTables(node_tables));
    element_tables = absl::flat_hash_set<const GraphElementTable*>(
        {node_tables.begin(), node_tables.end()});
  } else {
    absl::flat_hash_set<const GraphEdgeTable*> edge_tables;
    GOOGLESQL_RETURN_IF_ERROR(property_graph.GetEdgeTables(edge_tables));
    element_tables = absl::flat_hash_set<const GraphElementTable*>(
        {edge_tables.begin(), edge_tables.end()});
  }

  for (const GraphElementTable* element_table : element_tables) {
    absl::flat_hash_set<const GraphElementLabel*> labels;
    GOOGLESQL_RETURN_IF_ERROR(element_table->GetLabels(labels));
    static_labels.insert(labels.begin(), labels.end());

    GOOGLESQL_ASSIGN_OR_RETURN(const GraphDynamicLabel* dynamic_label,
                     GetDynamicLabelOfElementTable(*element_table));
    if (dynamic_label != nullptr) {
      dynamic_labels[element_table] = dynamic_label;
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<LabelSatisfyResult> ElementLabelsSatisfyResolvedGraphLabelExpr(
    absl::flat_hash_set<const GraphElementLabel*> element_labels,
    const GraphDynamicLabel* dynamic_label,
    const ResolvedGraphLabelExpr* label_expr) {
  // Recursively determines whether a set of element labels
  // satisfies the given label expression.
  switch (label_expr->node_kind()) {
    case RESOLVED_GRAPH_LABEL: {
      // Base case: This is a simple, non-wildcard base label, e.g. `Worker`.
      const ResolvedGraphLabel* label = label_expr->GetAs<ResolvedGraphLabel>();
      // If the label is static and present in the static label set then
      // we know for sure that it is carried by the element.
      if (label->label() != nullptr &&
          element_labels.contains(label->label())) {
        return LabelSatisfyResult::kTrue;
      }
      // If the element table has dynamic label, we cannot determine
      // whether the label is contained or not at resolution time.
      if (dynamic_label != nullptr) {
        return LabelSatisfyResult::kUndetermined;
      }
      // If the element table does not have a dynamic label, the resolved label
      // expression must point to a static label.
      // This is because there is either only one dynamic element table or
      // at least one static element tables in the node (or edge) table list of
      // the same graph.
      GOOGLESQL_RET_CHECK(label->label() != nullptr)
          << "Must be a static label expression if element table has no "
             "dynamic label defined";
      return LabelSatisfyResult::kFalse;
    }
    case RESOLVED_GRAPH_WILD_CARD_LABEL: {
      // Base case: This is the wildcard label % and should return true so long
      // as the set of element labels is non-empty.
      return element_labels.empty() ? LabelSatisfyResult::kFalse
                                    : LabelSatisfyResult::kTrue;
    }
    case RESOLVED_GRAPH_LABEL_NARY_EXPR: {
      const ResolvedGraphLabelNaryExpr* label_nary_expr =
          label_expr->GetAs<ResolvedGraphLabelNaryExpr>();
      switch (label_nary_expr->op()) {
        case ResolvedGraphLabelNaryExpr::NOT: {
          // In the case of !, return the simple negation of the recursive
          // function called on the inner expression being negated.
          GOOGLESQL_RET_CHECK_EQ(label_nary_expr->operand_list().size(), 1);
          GOOGLESQL_ASSIGN_OR_RETURN(auto result,
                           ElementLabelsSatisfyResolvedGraphLabelExpr(
                               element_labels, dynamic_label,
                               label_nary_expr->operand_list()[0].get()));
          if (result == LabelSatisfyResult::kUndetermined) {
            return LabelSatisfyResult::kUndetermined;
          }
          return result == LabelSatisfyResult::kTrue
                     ? LabelSatisfyResult::kFalse
                     : LabelSatisfyResult::kTrue;
        }
        case ResolvedGraphLabelNaryExpr::AND: {
          // In the case of & and |, return the result of applying conjunction
          // or disjunction, respectively, on the result of the recursive calls
          // to each operand in the operand list.
          bool has_non_determined_result = false;
          GOOGLESQL_RET_CHECK_GE(label_nary_expr->operand_list().size(), 2);
          for (const std::unique_ptr<const ResolvedGraphLabelExpr>& operand :
               label_nary_expr->operand_list()) {
            GOOGLESQL_ASSIGN_OR_RETURN(auto satisfied,
                             ElementLabelsSatisfyResolvedGraphLabelExpr(
                                 element_labels, dynamic_label, operand.get()));
            if (satisfied == LabelSatisfyResult::kFalse) {
              return LabelSatisfyResult::kFalse;
            }
            has_non_determined_result |=
                (satisfied == LabelSatisfyResult::kUndetermined);
          }
          return has_non_determined_result ? LabelSatisfyResult::kUndetermined
                                           : LabelSatisfyResult::kTrue;
        }
        case ResolvedGraphLabelNaryExpr::OR: {
          GOOGLESQL_RET_CHECK_GE(label_nary_expr->operand_list().size(), 2);
          bool has_non_determined_result = false;
          for (const std::unique_ptr<const ResolvedGraphLabelExpr>& operand :
               label_nary_expr->operand_list()) {
            GOOGLESQL_ASSIGN_OR_RETURN(auto satisfied,
                             ElementLabelsSatisfyResolvedGraphLabelExpr(
                                 element_labels, dynamic_label, operand.get()));
            if (satisfied == LabelSatisfyResult::kTrue) {
              return LabelSatisfyResult::kTrue;
            }
            has_non_determined_result |=
                (satisfied == LabelSatisfyResult::kUndetermined);
          }
          return has_non_determined_result ? LabelSatisfyResult::kUndetermined
                                           : LabelSatisfyResult::kFalse;
        }
        case ResolvedGraphLabelNaryExpr::OPERATION_TYPE_UNSPECIFIED: {
          GOOGLESQL_RET_CHECK_FAIL() << "Unexpected graph label operation: "
                           << label_nary_expr->op();
        }
      }
    }
    default: {
      GOOGLESQL_RET_CHECK_FAIL() << "Unexpected graph label expression: "
                       << label_expr->DebugString();
    }
  }
}

static absl::StatusOr<ResolvedGraphLabelNaryExprEnums_GraphLogicalOpType>
GetGraphLabelExprOp(const ASTGraphLabelOperation* ast_graph_label_expr) {
  switch (ast_graph_label_expr->op_type()) {
    case ASTGraphLabelOperation::NOT: {
      return ResolvedGraphLabelNaryExpr::NOT;
    }
    case ASTGraphLabelOperation::AND: {
      return ResolvedGraphLabelNaryExpr::AND;
    }
    case ASTGraphLabelOperation::OR: {
      return ResolvedGraphLabelNaryExpr::OR;
    }
    case ASTGraphLabelOperation::OPERATION_TYPE_UNSPECIFIED:
      GOOGLESQL_RET_CHECK_FAIL() << "Operation type unspecified";
  }
}

absl::StatusOr<std::unique_ptr<const ResolvedGraphLabelExpr>>
ResolveGraphLabelExpr(
    const ASTGraphLabelExpression* ast_graph_label_expr,
    const GraphElementTable::Kind element_kind,
    const absl::flat_hash_set<const GraphElementLabel*>& valid_static_labels,
    const PropertyGraph* property_graph, bool supports_dynamic_labels,
    bool element_table_contains_dynamic_label) {
  if (ast_graph_label_expr == nullptr) {
    return nullptr;
  }
  std::unique_ptr<ResolvedGraphLabelExpr> output;
  switch (ast_graph_label_expr->node_kind()) {
    case AST_GRAPH_WILDCARD_LABEL: {
      GOOGLESQL_ASSIGN_OR_RETURN(output,
                       ResolvedGraphWildCardLabelBuilder().BuildMutable());
      break;
    }
    case AST_GRAPH_ELEMENT_LABEL: {
      const GraphElementLabel* label = nullptr;
      absl::string_view name =
          ast_graph_label_expr->GetAsOrDie<ASTGraphElementLabel>()
              ->name()
              ->GetAsStringView();
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedLiteral> label_name,
                       ResolvedLiteralBuilder()
                           .set_type(types::StringType())
                           .set_value(Value::String(name))
                           .set_has_explicit_type(true)
                           .Build());
      absl::Status find_status = property_graph->FindLabelByName(
          ast_graph_label_expr->GetAsOrDie<ASTGraphElementLabel>()
              ->name()
              ->GetAsStringView(),
          label);
      if (!find_status.ok()) {
        if (element_table_contains_dynamic_label) {
          return ResolvedGraphLabelBuilder()
              .set_label_name(std::move(label_name))
              .Build();
        } else {
          return MakeSqlErrorAt(ast_graph_label_expr) << find_status.message();
        }
      }
      // If the label was successfully found, but not present in the set of
      // valid static labels, then it must be a label for the wrong element kind
      // e.g. an edge only label in a label expression referring to a node
      // pattern. Report a user error message in this case.
      if (!valid_static_labels.contains(label)) {
        absl::string_view kind_str =
            (element_kind == GraphElementTable::Kind::kNode) ? "node" : "edge";
        absl::string_view correct_kind_str =
            (kind_str == "node") ? "edge" : "node";
        return MakeSqlErrorAt(ast_graph_label_expr) << absl::StrFormat(
                   "Label %s is only valid for %ss, but used here on a %s",
                   label->Name(), correct_kind_str, kind_str);
      }
      ResolvedGraphLabelBuilder builder =
          ResolvedGraphLabelBuilder().set_label(label);
      if (supports_dynamic_labels) {
        builder.set_label_name(std::move(label_name));
      }
      GOOGLESQL_ASSIGN_OR_RETURN(output, std::move(builder).BuildMutable());
      break;
    }
    case AST_GRAPH_LABEL_OPERATION: {
      std::vector<std::unique_ptr<const ResolvedGraphLabelExpr>> operand_list;
      const ASTGraphLabelOperation* ast_graph_label_operation =
          ast_graph_label_expr->GetAsOrDie<ASTGraphLabelOperation>();
      auto& inputs = ast_graph_label_operation->inputs();
      operand_list.reserve(inputs.size());
      for (const ASTGraphLabelExpression* input : inputs) {
        GOOGLESQL_ASSIGN_OR_RETURN(
            std::unique_ptr<const ResolvedGraphLabelExpr> next_operand,
            ResolveGraphLabelExpr(input, element_kind, valid_static_labels,
                                  property_graph, supports_dynamic_labels,
                                  element_table_contains_dynamic_label));
        operand_list.emplace_back(std::move(next_operand));
      }
      GOOGLESQL_ASSIGN_OR_RETURN(ResolvedGraphLabelNaryExprEnums_GraphLogicalOpType op,
                       GetGraphLabelExprOp(ast_graph_label_operation));
      GOOGLESQL_ASSIGN_OR_RETURN(output, ResolvedGraphLabelNaryExprBuilder()
                                   .set_op(op)
                                   .set_operand_list(std::move(operand_list))
                                   .BuildMutable());
      break;
    }
    default:
      GOOGLESQL_RET_CHECK_FAIL() << "Unrecognized graph label node type";
  }
  output->SetParseLocationRange(ast_graph_label_expr->location());
  return output;
}

static absl::StatusOr<std::unique_ptr<const ResolvedLiteral>>
GetResolvedLiteralForPropertyName(absl::string_view property_name) {
  return ResolvedLiteralBuilder()
      .set_value(Value::String(property_name))
      .set_type(types::StringType())
      .set_has_explicit_type(true)
      .Build();
}

absl::StatusOr<std::unique_ptr<const ResolvedGraphGetElementProperty>>
ResolveGraphGetElementProperty(
    const ASTNode* error_location, const PropertyGraph* graph,
    const GraphElementType* element_type, absl::string_view property_name,
    bool supports_dynamic_properties,
    std::unique_ptr<const ResolvedExpr> resolved_lhs) {
  auto builder = ResolvedGraphGetElementPropertyBuilder().set_expr(
      std::move(resolved_lhs));
  std::unique_ptr<const ResolvedGraphGetElementProperty>
      get_element_property_expr;

  if (element_type->FindPropertyType(property_name) == nullptr) {
    if (element_type->is_dynamic()) {
      GOOGLESQL_RET_CHECK(supports_dynamic_properties);
      GOOGLESQL_ASSIGN_OR_RETURN(get_element_property_expr,
                       std::move(builder)
                           .set_type(types::JsonType())
                           .set_property_name(
                               GetResolvedLiteralForPropertyName(property_name))
                           .Build());
    } else {
      return MakeSqlErrorAt(error_location)
             << "Property " << property_name
             << " is not exposed by element type "
             << element_type->DebugString();
    }
  } else {
    const GraphPropertyDeclaration* prop_dcl;
    GOOGLESQL_RETURN_IF_ERROR(
        graph->FindPropertyDeclarationByName(property_name, prop_dcl));
    GOOGLESQL_RET_CHECK(prop_dcl != nullptr);

    if (supports_dynamic_properties) {
      builder.set_property_name(
          GetResolvedLiteralForPropertyName(property_name));
    }
    GOOGLESQL_ASSIGN_OR_RETURN(get_element_property_expr, std::move(builder)
                                                    .set_type(prop_dcl->Type())
                                                    .set_property(prop_dcl)
                                                    .Build());
  }
  return get_element_property_expr;
}

}  // namespace googlesql
