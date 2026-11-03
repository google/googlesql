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

#include "googlesql/analyzer/graph_ddl_resolver.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "googlesql/analyzer/expr_resolver_helper.h"
#include "googlesql/analyzer/name_scope.h"
#include "googlesql/analyzer/resolver.h"
#include "googlesql/analyzer/resolver_common_inl.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/graph_element_utils.h"
#include "googlesql/common/internal_analyzer_options.h"
#include "googlesql/common/measure_utils.h"
#include "googlesql/parser/ast_node.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parse_tree_errors.h"
#include "googlesql/public/annotation/collation.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/types/type.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_ast_comparator.h"
#include "googlesql/resolved_ast/resolved_ast_deep_copy_visitor.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/base/case.h"
#include "absl/algorithm/container.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "googlesql/base/status_macros.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {
namespace {

using StringViewHashSetCase =
    absl::flat_hash_set<absl::string_view, googlesql_base::StringViewCaseHash,
                        googlesql_base::StringViewCaseEqual>;

template <typename T>
using StringViewHashMapCase =
    absl::flat_hash_map<absl::string_view, T, googlesql_base::StringViewCaseHash,
                        googlesql_base::StringViewCaseEqual>;

// Moves `data` to `to`.
template <typename T1, typename T2>
absl::Status AppendUniquePtr(absl::StatusOr<std::unique_ptr<const T1>> data,
                             std::vector<std::unique_ptr<const T2>>& to) {
  static_assert(std::is_convertible_v<T1*, T2*>,
                "The type of `data` must be convertible to type of `to` in "
                "order for `data` to be appended to `to`");
  GOOGLESQL_RETURN_IF_ERROR(data.status());
  to.push_back(std::move(data).value());
  return absl::OkStatus();
}

// Gets the name of property declaration: either explicitly via alias or
// implicitly from the expression itself.
absl::StatusOr<std::string> GetPropertyDeclarationName(
    const ASTGraphDerivedProperty* property) {
  if (property->alias() != nullptr) {
    return property->alias()->GetAsString();
  }
  // without alias, expression must be a column name (ASTPathExpression with
  // length of 1)
  const ASTPathExpression* column_name =
      property->expression()->GetAsOrNull<ASTPathExpression>();
  if (column_name == nullptr || column_name->num_names() != 1) {
    return MakeSqlErrorAt(property->expression())
           << "Without `AS` alias, the property expression must be a simple "
              "reference to a column name";
  }
  return column_name->last_name()->GetAsString();
}

// Resolves column list by catalog columns.
absl::StatusOr<std::vector<std::unique_ptr<const ResolvedExpr>>>
ResolveColumnList(absl::Span<const Column* const> catalog_columns,
                  const ResolvedTableScan& table_scan) {
  absl::flat_hash_map<const Column*, int> column_index;
  for (int i = 0; i < table_scan.column_index_list_size(); ++i) {
    column_index.emplace(
        table_scan.table()->GetColumn(table_scan.column_index_list(i)), i);
  }

  std::vector<std::unique_ptr<const ResolvedExpr>> col_refs;
  col_refs.reserve(catalog_columns.size());
  for (const Column* column : catalog_columns) {
    auto iter = column_index.find(column);
    GOOGLESQL_RET_CHECK(iter != column_index.end());
    const ResolvedColumn& resolved_col = table_scan.column_list(iter->second);
    GOOGLESQL_RETURN_IF_ERROR(AppendUniquePtr(
        ResolvedColumnRefBuilder()
            .set_column(resolved_col)
            .set_type(resolved_col.type())
            .set_type_annotation_map(resolved_col.type_annotation_map())
            .set_is_correlated(false)
            .Build(),
        col_refs));
  }
  return col_refs;
}

// Resolves column list by column names.
absl::StatusOr<std::vector<std::unique_ptr<const ResolvedExpr>>>
ResolveColumnList(absl::Span<const ASTIdentifier* const> identifiers,
                  const ResolvedTableScan& table_scan) {
  std::vector<const Column*> catalog_cols;
  catalog_cols.reserve(identifiers.size());
  const Table* table = table_scan.table();
  for (const auto* identifier : identifiers) {
    const Column* column = table->FindColumnByName(identifier->GetAsString());
    if (column == nullptr) {
      return MakeSqlErrorAt(identifier)
             << "Column "
             << ToSingleQuotedStringLiteral(identifier->GetAsStringView())
             << " not found in table "
             << ToSingleQuotedStringLiteral(table->FullName());
    }
    catalog_cols.push_back(column);
  }
  return ResolveColumnList(catalog_cols, table_scan);
}

// Resolves column list by column indices.
absl::StatusOr<std::vector<std::unique_ptr<const ResolvedExpr>>>
ResolveColumnList(absl::Span<const int> catalog_column_indices,
                  const ResolvedTableScan& table_scan) {
  std::vector<const Column*> catalog_cols;
  catalog_cols.reserve(catalog_column_indices.size());
  const Table* table = table_scan.table();
  for (const int index : catalog_column_indices) {
    const Column* column = table->GetColumn(index);
    GOOGLESQL_RET_CHECK(column != nullptr);
    catalog_cols.push_back(column);
  }
  return ResolveColumnList(catalog_cols, table_scan);
}

// Resolves the element keys, either from explicit key clause or implicitly
// from the underlying table's primary keys, into column references of
// `table_scan`.
absl::StatusOr<std::vector<std::unique_ptr<const ResolvedExpr>>>
ResolveKeyColumns(const ASTNode* ast_location,
                  const GraphElementTable::Kind element_kind,
                  const ASTColumnList* input_ast,
                  const ResolvedTableScan& table_scan) {
  // Explicit key clause.
  if (input_ast != nullptr) {
    return ResolveColumnList(input_ast->identifiers(), table_scan);
  }
  // No key clause: implicitly the underlying primary keys.
  if (table_scan.table()->PrimaryKey().has_value()) {
    return ResolveColumnList(*table_scan.table()->PrimaryKey(), table_scan);
  }
  return MakeSqlErrorAt(ast_location) << absl::StrFormat(
             "The %s table %s does not have primary key "
             "defined; graph element table keys must be explicitly defined",
             element_kind == GraphElementTable::Kind::kNode ? "node" : "edge",
             ToSingleQuotedStringLiteral(table_scan.table()->FullName()));
}

// Finds the node table with given `identifier` from `node_table_map`.
absl::StatusOr<const ResolvedGraphElementTable*> FindNodeTable(
    const ASTIdentifier* identifier,
    const StringViewHashMapCase<const ResolvedGraphElementTable*>&
        node_table_map) {
  auto iter = node_table_map.find(identifier->GetAsStringView());
  if (iter == node_table_map.end()) {
    return MakeSqlErrorAt(identifier)
           << "The referenced node table "
           << ToSingleQuotedStringLiteral(identifier->GetAsStringView())
           << " is not defined in the property graph";
  }
  return iter->second;
}

// Copies all `nodes`.
template <typename T>
absl::StatusOr<std::vector<std::unique_ptr<const T>>> CopyAll(
    const std::vector<std::unique_ptr<const T>>& nodes) {
  std::vector<std::unique_ptr<const T>> copied_nodes;
  copied_nodes.reserve(nodes.size());
  for (const auto& node : nodes) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const T> node_copy,
                     ResolvedASTDeepCopyVisitor::Copy(node.get()));
    copied_nodes.push_back(std::move(node_copy));
  }
  return copied_nodes;
}

// Validates `input_ast` does not contain duplicate names: ok when `input_ast`
// is null which means the list is empty.
absl::Status ValidateColumnsAreUnique(const ASTColumnList* input_ast) {
  if (input_ast == nullptr) {
    return absl::OkStatus();
  }
  StringViewHashSetCase unique_names;
  for (const auto& col : input_ast->identifiers()) {
    if (!unique_names.insert(col->GetAsStringView()).second) {
      return MakeSqlErrorAt(col)
             << "Duplicate column "
             << ToSingleQuotedStringLiteral(col->GetAsStringView())
             << " in the referenced column list";
    }
  }
  return absl::OkStatus();
}

// Validates no duplicate label within element table.
absl::Status ValidateNoDuplicateLabelForElementTable(
    const ASTNode* ast_location, absl::string_view element_table_name,
    absl::Span<const std::string> label_list) {
  StringViewHashSetCase unique_labels;
  for (const std::string& label_name : label_list) {
    if (!unique_labels.insert(label_name).second) {
      return MakeSqlErrorAt(ast_location)
             << "Duplicate label name "
             << ToSingleQuotedStringLiteral(label_name)
             << " in the same element table "
             << ToSingleQuotedStringLiteral(element_table_name);
    }
  }
  return absl::OkStatus();
}

// Validates `create_stmt` against the CatalogObjectType existence in `catalog`.
// For example, ValidateCreationAgainstCatalog<Table>: validates against
// the table names.
template <typename CatalogObjectType>
absl::Status ValidateCreationAgainstCatalog(
    const ASTNode* ast_location, const ResolvedCreateStatement& create_stmt,
    Catalog& catalog, const Catalog::FindOptions& options,
    absl::string_view err_message) {
  const CatalogObjectType* object;
  absl::Status find_object_status =
      catalog.FindObject(create_stmt.name_path(), &object, options);
  // Report ok if no object with given name exists in catalog.
  if (absl::IsNotFound(find_object_status)) {
    return absl::OkStatus();
  }

  // Report other catalog error as it is.
  GOOGLESQL_RETURN_IF_ERROR(find_object_status);

  // Existence is permitted when the create mode allows it.
  if (create_stmt.create_mode() == ResolvedCreateStatement::CREATE_OR_REPLACE ||
      create_stmt.create_mode() ==
          ResolvedCreateStatement::CREATE_IF_NOT_EXISTS) {
    return absl::OkStatus();
  }

  // Otherwise, reports error if object with such name already exists.
  return MakeSqlErrorAt(ast_location) << err_message;
}

// Validates that no object of `CatalogObjectType` shares `create_stmt`'s name.
// Unlike ValidateCreationAgainstCatalog, an existing object is ALWAYS an error
// here, even under CREATE OR REPLACE / CREATE IF NOT EXISTS. Those modifiers
// only apply to a same-kind object being (re)created; a cross-kind name clash
// (e.g. a property graph type whose name is already used by a table) can never
// be satisfied by replacing or skipping, and must be rejected regardless of
// create mode.
template <typename CatalogObjectType>
absl::Status ValidateNoCrossKindCollision(
    const ASTNode* ast_location, const ResolvedCreateStatement& create_stmt,
    Catalog& catalog, const Catalog::FindOptions& options,
    absl::string_view err_message) {
  const CatalogObjectType* object;
  absl::Status find_object_status =
      catalog.FindObject(create_stmt.name_path(), &object, options);
  // Report ok if no object with given name exists in catalog.
  if (absl::IsNotFound(find_object_status)) {
    return absl::OkStatus();
  }
  // Report other catalog error as it is.
  GOOGLESQL_RETURN_IF_ERROR(find_object_status);
  // A cross-kind name clash is always an error, irrespective of create mode.
  return MakeSqlErrorAt(ast_location) << err_message;
}

// Validates element tables all have distinct names and outputs the names to
// `existing_identifiers`. `ast_locations[i]` is the error location reported for
// `element_tables[i]`; the two spans must be parallel.
template <typename AstNodeT>
absl::Status ValidateNoDuplicateElementTables(
    absl::Span<const AstNodeT* const> ast_locations,
    absl::Span<const std::unique_ptr<const ResolvedGraphElementTable>>
        element_tables,
    const GraphElementTable::Kind element_kind,
    StringViewHashSetCase& existing_identifiers) {
  GOOGLESQL_RET_CHECK_EQ(ast_locations.size(), element_tables.size());
  for (size_t i = 0; i < element_tables.size(); ++i) {
    const auto& element_table = element_tables.at(i);
    const auto [_, inserted] =
        existing_identifiers.insert(element_table->alias());
    if (!inserted) {
      return MakeSqlErrorAt(ast_locations.at(i)) << absl::StrFormat(
                 "The %s table %s is defined more than once; use "
                 "a unique name",
                 element_kind == GraphElementTable::Kind::kNode ? "node"
                                                                : "edge",
                 ToSingleQuotedStringLiteral(element_table->alias()));
    }
  }
  return absl::OkStatus();
}

// As above, for an element table list that may be absent (there is no EDGE
// TABLES clause).
absl::Status ValidateNoDuplicateElementTable(
    const ASTGraphElementTableList* ast_table_list,
    absl::Span<const std::unique_ptr<const ResolvedGraphElementTable>>
        element_tables,
    const GraphElementTable::Kind element_kind,
    StringViewHashSetCase& existing_identifiers) {
  if (ast_table_list == nullptr) {
    GOOGLESQL_RET_CHECK(element_tables.empty());
    return absl::OkStatus();
  }
  return ValidateNoDuplicateElementTables(ast_table_list->element_tables(),
                                          element_tables, element_kind,
                                          existing_identifiers);
}

// Validates newly added `element_labels` does not have OPTIONS defined
// previously according to `labels_with_options`.
// Adds new labels with OPTIONS defined into `labels_with_options`.
absl::Status ValidateLabelWithOptionsNotBoundInMultipleElementTables(
    absl::Span<const std::unique_ptr<const ResolvedGraphElementLabel>>
        element_labels,
    StringViewHashSetCase& labels_with_options) {
  for (const auto& element_label : element_labels) {
    if (labels_with_options.contains(element_label->name())) {
      GOOGLESQL_RET_CHECK(element_label->GetParseLocationOrNULL() != nullptr);
      return MakeSqlErrorAtPoint(
                 element_label->GetParseLocationOrNULL()->start())
             << "The label "
             << ToSingleQuotedStringLiteral(element_label->name())
             << " is defined with OPTIONS clause in one of the element tables "
                "and cannot be bound to another element table";
    }
    if (!element_label->options_list().empty()) {
      labels_with_options.insert(element_label->name());
    }
  }
  return absl::OkStatus();
}

// Validates an element table's `element_labels`/`element_property_decls` and
// folds them into the graph-wide `labels`/`property_decls` accumulators.
absl::Status ValidateAndRecordLabelsAndPropertyDeclarations(
    std::vector<std::unique_ptr<const ResolvedGraphElementLabel>>&
        element_labels,
    std::vector<
        std::unique_ptr<GraphDdlResolver::PropertyDeclarationWithIsMeasure>>&
        element_property_decls,
    StringViewHashSetCase& labels_with_options,
    std::vector<std::unique_ptr<const ResolvedGraphElementLabel>>& labels,
    std::vector<
        std::unique_ptr<GraphDdlResolver::PropertyDeclarationWithIsMeasure>>&
        property_decls) {
  GOOGLESQL_RETURN_IF_ERROR(ValidateLabelWithOptionsNotBoundInMultipleElementTables(
      element_labels, labels_with_options));
  labels.insert(labels.end(), std::make_move_iterator(element_labels.begin()),
                std::make_move_iterator(element_labels.end()));
  property_decls.insert(property_decls.end(),
                        std::make_move_iterator(element_property_decls.begin()),
                        std::make_move_iterator(element_property_decls.end()));
  return absl::OkStatus();
}

// Validates a property definition pair: they must have the same expression
// within the same element table. Used by element table resolution.
absl::Status ValidateIdentical(const ResolvedGraphPropertyDefinition& def1,
                               const ResolvedGraphPropertyDefinition& def2) {
  // TODO: find a way to compare two equivalent ResolvedAst
  // like `a+b` and `b+a`.
  GOOGLESQL_ASSIGN_OR_RETURN(
      const bool same_expr,
      ResolvedASTComparator::CompareResolvedAST(def1.expr(), def2.expr()));
  bool same_options;
  if (def1.options_list().size() != def2.options_list().size()) {
    same_options = false;
  } else {
    same_options = true;
    for (int i = 0; i < def1.options_list().size(); ++i) {
      GOOGLESQL_ASSIGN_OR_RETURN(bool same, ResolvedASTComparator::CompareResolvedAST(
                                      def1.options_list()[i].get(),
                                      def2.options_list()[i].get()));
      if (!same) {
        same_options = false;
        break;
      }
    }
  }

  bool same_definition = same_expr && same_options;
  if (!same_definition) {
    GOOGLESQL_RET_CHECK(def1.GetParseLocationOrNULL() != nullptr);
    return MakeSqlErrorAtPoint(def1.GetParseLocationOrNULL()->start())
           << "Property "
           << ToSingleQuotedStringLiteral(def1.property_declaration_name())
           << " has more than one definition in the element table; use the "
              "same property definition or assign different property names";
  }
  return absl::OkStatus();
}

static std::string QuotedStringCommaSeparatedRep(
    const std::vector<std::string>& strings) {
  return ToSingleQuotedStringLiteral(
      absl::StrCat("[", absl::StrJoin(strings, ", "), "]"));
}

// Validates a label pair: they must have the same property declaration list.
absl::Status ValidateIdentical(const ResolvedGraphElementLabel& label1,
                               const ResolvedGraphElementLabel& label2) {
  StringViewHashSetCase properties;
  for (const auto& property_name : label1.property_declaration_name_list()) {
    GOOGLESQL_RET_CHECK(properties.emplace(property_name).second);
  }
  GOOGLESQL_RET_CHECK(label1.GetParseLocationOrNULL() != nullptr);
  if (properties.size() != label2.property_declaration_name_list().size()) {
    return MakeSqlErrorAtPoint(label1.GetParseLocationOrNULL()->start())
           << absl::StrFormat(
                  "The label %s is defined with different properties. One "
                  "definition of the label has %d property declarations: %s. "
                  "Another definition has  %d property declarations: %s. You "
                  "need to use the same set of property declarations under the "
                  "same label",
                  ToSingleQuotedStringLiteral(label1.name()),
                  label1.property_declaration_name_list_size(),
                  QuotedStringCommaSeparatedRep(
                      label1.property_declaration_name_list()),
                  label2.property_declaration_name_list_size(),
                  QuotedStringCommaSeparatedRep(
                      label2.property_declaration_name_list()));
  }

  std::vector<std::string> missing_properties;
  for (const auto& property : label2.property_declaration_name_list()) {
    if (!properties.contains(property)) {
      missing_properties.push_back(property);
    }
  }
  if (!missing_properties.empty()) {
    return MakeSqlErrorAtPoint(label1.GetParseLocationOrNULL()->start())
           << absl::StrFormat(
                  "The label %s is defined with different property "
                  "declarations. There is one instance of this label defined "
                  "with properties of %s. Another instance is defined with "
                  "properties of %s. You need to use the same set of property "
                  "names under the same label",
                  ToSingleQuotedStringLiteral(label1.name()),
                  QuotedStringCommaSeparatedRep(
                      label1.property_declaration_name_list()),
                  QuotedStringCommaSeparatedRep(
                      label2.property_declaration_name_list()));
  }
  return absl::OkStatus();
}

// Validates a property declaration pair: they must have the same type.
absl::Status ValidateIdentical(
    const ResolvedGraphPropertyDeclaration& declaration1,
    const ResolvedGraphPropertyDeclaration& declaration2) {
  if (!declaration1.type()->Equals(declaration2.type())) {
    GOOGLESQL_RET_CHECK(declaration1.GetParseLocationOrNULL() != nullptr);
    return MakeSqlErrorAtPoint(declaration1.GetParseLocationOrNULL()->start())
           << absl::StrFormat(
                  "The property declaration of %s has type conflicts. There is "
                  "an existing declaration of type %s. There is a conflicting "
                  "one of type %s",
                  ToSingleQuotedStringLiteral(declaration1.name()),
                  declaration1.type()->TypeName(PRODUCT_EXTERNAL),
                  declaration2.type()->TypeName(PRODUCT_EXTERNAL));
  }
  if (!AnnotationMap::Equals(declaration1.type_annotation_map(),
                             declaration2.type_annotation_map())) {
    GOOGLESQL_RET_CHECK(declaration1.GetParseLocationOrNULL() != nullptr);
    return MakeSqlErrorAtPoint(declaration1.GetParseLocationOrNULL()->start())
           << absl::StrFormat(
                  "The property declaration of %s has type annotation "
                  "conflicts",
                  ToSingleQuotedStringLiteral(declaration1.name()));
  }
  return absl::OkStatus();
}

// Validates a PropertyDeclarationWithIsMeasure pair: they must have the same
// type, and they can't both be measure properties.
absl::Status ValidateIdentical(
    const GraphDdlResolver::PropertyDeclarationWithIsMeasure& d1,
    const GraphDdlResolver::PropertyDeclarationWithIsMeasure& d2) {
  if (d1.is_measure() || d2.is_measure()) {
    const ParseLocationPoint& measure_location =
        d1.is_measure() ? d1.declaration().GetParseLocationOrNULL()->start()
                        : d2.declaration().GetParseLocationOrNULL()->start();
    return MakeSqlErrorAtPoint(measure_location)
           << "Property "
           << ToSingleQuotedStringLiteral(d1.declaration().name())
           << " is defined as MEASURE, but there are other declarations with "
              "the same name";
  }

  return ValidateIdentical(d1.declaration(), d2.declaration());
}

// Dedupes the objects from `graph_objects` so that the remaining objects in
// `graph_objects` all have distinct names which is computed from
// `get_name_func`. Along deduplication, also ensures the objects with the same
// name are identical by calling ValidateIdentical.
template <typename T, typename GetNameFunc>
absl::Status Dedupe(std::vector<std::unique_ptr<T>>& graph_objects,
                    const GetNameFunc& get_name_func) {
  static_assert(
      std::is_same_v<T, const ResolvedGraphPropertyDefinition> ||
      std::is_same_v<T, const ResolvedGraphPropertyDeclaration> ||
      std::is_same_v<T, const ResolvedGraphElementLabel> ||
      std::is_same_v<T, GraphDdlResolver::PropertyDeclarationWithIsMeasure>);

  StringViewHashMapCase<const T*> node_map;
  std::vector<std::unique_ptr<T>> unique_nodes;
  for (auto& node : graph_objects) {
    const auto& [iter, inserted] =
        node_map.emplace(std::invoke(get_name_func, *node), node.get());
    if (inserted) {
      unique_nodes.push_back(std::move(node));
    } else {
      GOOGLESQL_RETURN_IF_ERROR(ValidateIdentical(*node, *iter->second));
    }
  }
  graph_objects = std::move(unique_nodes);
  return absl::OkStatus();
}

// Validates ResolvedGraphElementLabel.
absl::Status ValidateElementLabel(const ASTNode& ast_location,
                                  const ResolvedGraphElementLabel& label) {
  // Element label level validation:
  // No duplicate property name within a label.
  StringViewHashSetCase unique_properties;
  for (const std::string& property_name :
       label.property_declaration_name_list()) {
    if (!unique_properties.insert(property_name).second) {
      return MakeSqlErrorAt(&ast_location)
             << "Duplicate property name "
             << ToSingleQuotedStringLiteral(property_name)
             << " in the same label "
             << ToSingleQuotedStringLiteral(label.name());
    }
  }
  return absl::OkStatus();
}

// Validates the resolved column lists of a ResolvedGraphNodeTableReference:
// non-empty, matching counts, matching types/annotations. AST-agnostic so it
// can be called from both the explicit-edge path (ASTGraphNodeTableReference)
// and the inlined-edge path (ASTGraphInlinedEdgeDefinition), which carry the
// same columns under different AST shapes.
absl::Status ValidateNodeTableReferenceColumns(
    const ASTNode* error_location,
    const ResolvedGraphNodeTableReference& node_table_ref) {
  const std::vector<std::unique_ptr<const ResolvedExpr>>& node_refs =
      node_table_ref.node_table_column_list();
  const std::vector<std::unique_ptr<const ResolvedExpr>>& edge_refs =
      node_table_ref.edge_table_column_list();
  GOOGLESQL_RET_CHECK(!node_refs.empty());
  GOOGLESQL_RET_CHECK(!edge_refs.empty());

  // Validates node and edge table columns are of the same number.
  if (node_refs.size() != edge_refs.size()) {
    std::string error_message = absl::Substitute(
        "The number of referencing columns in the edge table does not "
        "match that of referenced columns in the node table; the former has a "
        "size of $0 but the latter has a size of $1",
        edge_refs.size(), node_refs.size());
    return MakeSqlErrorAt(error_location) << error_message;
  }

  // Validates node and edge table column types are consistent.
  if (!absl::c_equal(
          node_refs, edge_refs, [](const auto& node_col, const auto& edge_col) {
            return node_col->type()->Equals(edge_col->type()) &&
                   AnnotationMap::Equals(node_col->type_annotation_map(),
                                         edge_col->type_annotation_map());
          })) {
    return MakeSqlErrorAt(error_location)
           << "Data types and/or annotations of the referencing columns in the "
              "edge table do not match those of the referenced columns in the "
              "node table";
  }
  return absl::OkStatus();
}

absl::Status ValidateNodeTableReference(
    const ASTGraphNodeTableReference* input_ast,
    const ResolvedGraphNodeTableReference& node_table_ref) {
  // Validates node table columns are all distinct.
  GOOGLESQL_RETURN_IF_ERROR(ValidateColumnsAreUnique(input_ast->node_table_columns()));
  // Validates edge table columns are all distinct.
  GOOGLESQL_RETURN_IF_ERROR(ValidateColumnsAreUnique(input_ast->edge_table_columns()));
  return ValidateNodeTableReferenceColumns(input_ast, node_table_ref);
}

// Validates ResolvedGraphElementTable.
absl::Status ValidateElementTable(
    const ASTGraphElementTable* input_ast,
    const ResolvedGraphElementTable& element_table,
    GraphElementTable::Kind element_kind) {
  // Element table level validation:
  // 1) No duplicate key names;
  GOOGLESQL_RETURN_IF_ERROR(ValidateColumnsAreUnique(input_ast->key_list()));

  // 2) No duplicate label names;
  GOOGLESQL_RETURN_IF_ERROR(ValidateNoDuplicateLabelForElementTable(
      input_ast, element_table.alias(), element_table.label_name_list()));

  // 3) Node must not have source/destination node reference and
  //    Edge must have source/destination node reference.
  if (element_kind == GraphElementTable::Kind::kNode) {
    if ((input_ast->source_node_reference() != nullptr ||
         input_ast->dest_node_reference() != nullptr)) {
      return MakeSqlErrorAt(input_ast) << "Node table cannot have source or "
                                          "destination references";
    }
    GOOGLESQL_RET_CHECK(element_table.source_node_reference() == nullptr);
    GOOGLESQL_RET_CHECK(element_table.dest_node_reference() == nullptr);
  } else if (element_kind == GraphElementTable::Kind::kEdge) {
    if (input_ast->source_node_reference() == nullptr) {
      return MakeSqlErrorAt(input_ast) << "Edge table must have its "
                                          "source node reference defined";
    }
    if (input_ast->dest_node_reference() == nullptr) {
      return MakeSqlErrorAt(input_ast) << "Edge table must have its "
                                          "destination node reference defined";
    }
    GOOGLESQL_RET_CHECK(element_table.source_node_reference() != nullptr);
    GOOGLESQL_RET_CHECK(element_table.dest_node_reference() != nullptr);
  }

  return absl::OkStatus();
}

template <typename ResolvedElement>
std::unique_ptr<const ResolvedElement> GetResolvedElementWithLocation(
    std::unique_ptr<const ResolvedElement> element,
    const ParseLocationRange& location_range) {
  ResolvedElement* element_ptr = const_cast<ResolvedElement*>(element.get());
  element_ptr->SetParseLocationRange(location_range);
  return element;
}

absl::StatusOr<absl::string_view> GetAstNodeSql(const ASTNode* node,
                                                absl::string_view sql) {
  const ParseLocationRange& ast_query_range = node->location();
  GOOGLESQL_RET_CHECK_GE(sql.length(), ast_query_range.end().GetByteOffset()) << sql;
  return absl::ClippedSubstr(sql, ast_query_range.start().GetByteOffset(),
                             ast_query_range.end().GetByteOffset() -
                                 ast_query_range.start().GetByteOffset());
}

}  // namespace

absl::StatusOr<std::unique_ptr<const ResolvedGraphNodeTableReference>>
GraphDdlResolver::ResolveGraphNodeTableReference(
    const ASTGraphNodeTableReference* ast_node_table_ref,
    const ResolvedTableScan& edge_table_scan,
    const StringViewHashMapCase<const ResolvedGraphElementTable*>&
        node_table_map) const {
  GOOGLESQL_RET_CHECK(ast_node_table_ref != nullptr);
  GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedGraphElementTable* node_table,
                   FindNodeTable(ast_node_table_ref->node_table_identifier(),
                                 node_table_map));

  std::vector<std::unique_ptr<const ResolvedExpr>> node_refs, edge_refs;

  // Resolves node table columns:
  // - either from explicit column clause or
  // - implicitly from the underlying node table's element keys.
  if (ast_node_table_ref->node_table_columns() != nullptr) {
    GOOGLESQL_RET_CHECK(node_table->input_scan()->Is<ResolvedTableScan>());
    GOOGLESQL_ASSIGN_OR_RETURN(
        node_refs, ResolveColumnList(
                       ast_node_table_ref->node_table_columns()->identifiers(),
                       *node_table->input_scan()->GetAs<ResolvedTableScan>()));
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(node_refs, CopyAll(node_table->key_list()));
  }

  // Resolves edge table columns.
  if (ast_node_table_ref->edge_table_columns() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        edge_refs, ResolveColumnList(
                       ast_node_table_ref->edge_table_columns()->identifiers(),
                       edge_table_scan));
  }
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedGraphNodeTableReference> node_table_ref,
      ResolvedGraphNodeTableReferenceBuilder()
          .set_node_table_identifier(
              ast_node_table_ref->node_table_identifier()->GetAsString())
          .set_node_table_column_list(std::move(node_refs))
          .set_edge_table_column_list(std::move(edge_refs))
          .Build());

  // Validates node table reference.
  GOOGLESQL_RETURN_IF_ERROR(
      ValidateNodeTableReference(ast_node_table_ref, *node_table_ref));
  return node_table_ref;
}

// Validates `expr` is allowed as a property value expression. Illegal property
// value expressions:
//   - query expression
//   - functions with non-deterministic output
//   - lambda with arguments (since these will create new ResolvedColumns)
absl::Status ValidatePropertyValueExpr(const ResolvedNode& expr) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  std::vector<const ResolvedNode*> nodes;
  expr.GetDescendantsWithKinds(
      {RESOLVED_SUBQUERY_EXPR, RESOLVED_FUNCTION_CALL, RESOLVED_INLINE_LAMBDA},
      &nodes);

  for (const ResolvedNode* node : nodes) {
    switch (node->node_kind()) {
      case RESOLVED_SUBQUERY_EXPR: {
        return MakeSqlError()
               << "Property value expression cannot contain a subquery";
      }
      case RESOLVED_FUNCTION_CALL: {
        const ResolvedFunctionCall* function_call =
            node->GetAs<ResolvedFunctionCall>();
        if (function_call->function()->function_options().volatility ==
            FunctionEnums::VOLATILE) {
          return MakeSqlError()
                 << "Property value expression cannot use volatile or "
                    "non-deterministic functions";
        }
        // GetDescendantsWithKinds stops traversal when a node of matching kind
        // is found. A Function's arguments may also contain subqueries or
        // volatile functions, validate those too.
        for (const std::unique_ptr<const ResolvedExpr>& arg :
             function_call->argument_list()) {
          GOOGLESQL_RETURN_IF_ERROR(ValidatePropertyValueExpr(*arg));
        }
        for (const std::unique_ptr<const ResolvedFunctionArgument>& arg :
             function_call->generic_argument_list()) {
          GOOGLESQL_RETURN_IF_ERROR(ValidatePropertyValueExpr(*arg));
        }
        break;
      }
      case RESOLVED_INLINE_LAMBDA: {
        const ResolvedInlineLambda* lambda =
            node->GetAs<ResolvedInlineLambda>();
        if (!lambda->argument_list().empty()) {
          return MakeSqlError() << "Lambda with argument is not supported in "
                                   "Property value expression";
        }
        GOOGLESQL_RETURN_IF_ERROR(ValidatePropertyValueExpr(*lambda->body()));
        break;
      }
      default:
        GOOGLESQL_RET_CHECK_FAIL() << "Did not expect to encounter node kind: "
                         << node->node_kind();
    }
  }

  return absl::OkStatus();
}

bool IsMeasureProperty(const ASTGraphDerivedProperty* property) {
  // If the expression of the derived property is a function call with the form
  // "MEASURE(x)", this property is considered a measure property.
  const ASTFunctionCall* function_call =
      property->expression()->GetAsOrNull<ASTFunctionCall>();
  if (function_call == nullptr) {
    return false;
  }

  const ASTPathExpression* function_name = function_call->function();
  if (!(function_name->num_names() == 1 &&
        googlesql_base::CaseEqual(function_name->first_name()->GetAsStringView(),
                               "MEASURE"))) {
    return false;
  }

  // A measure property has exactly one argument.
  if (function_call->arguments().size() != 1) {
    return false;
  }

  return true;
}

const ASTExpression* GetMeasurePropertyExpression(
    const ASTGraphDerivedProperty* property) {
  const ASTFunctionCall* function_call =
      property->expression()->GetAsOrNull<ASTFunctionCall>();
  return function_call->arguments(0);
}

absl::StatusOr<std::unique_ptr<const ResolvedGraphPropertyDefinition>>
GraphDdlResolver::ResolveGraphProperty(
    const ASTGraphDerivedProperty* property,
    ExprResolutionInfo* expr_resolution_info) const {
  const ASTExpression* property_expr = property->expression();
  bool is_measure = IsMeasureProperty(property);
  if (is_measure) {
    property_expr = GetMeasurePropertyExpression(property);
  }

  std::unique_ptr<const ResolvedExpr> resolved_expr;
  GOOGLESQL_RETURN_IF_ERROR(resolver_.ResolveExpr(property_expr, expr_resolution_info,
                                        &resolved_expr));
  GOOGLESQL_RET_CHECK_NE(resolved_expr->type(), nullptr);
  GOOGLESQL_RETURN_IF_ERROR(ValidatePropertyValueExpr(*resolved_expr))
      .With(LocationOverride(property_expr));

  // Extract derived property SQL string from AST.
  GOOGLESQL_ASSIGN_OR_RETURN(absl::string_view sql,
                   GetAstNodeSql(property_expr, resolver_.sql_));
  GOOGLESQL_ASSIGN_OR_RETURN(std::string property_decl_name,
                   GetPropertyDeclarationName(property));

  // Resolve options
  std::vector<std::unique_ptr<const ResolvedOption>> options_list;
  if (property->options_list() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(resolver_.ResolveOptionsList(
        property->options_list(), /*allow_alter_array_operators=*/false,
        &options_list));
  }

  return ResolvedGraphPropertyDefinitionBuilder()
      .set_expr(std::move(resolved_expr))
      .set_sql(sql)
      .set_property_declaration_name(std::move(property_decl_name))
      .set_options_list(std::move(options_list))
      .set_is_measure(is_measure)
      .Build();
}

absl::StatusOr<
    std::vector<std::unique_ptr<const ResolvedGraphPropertyDefinition>>>
GraphDdlResolver::ResolveGraphPropertyList(
    const ASTNode* ast_location,
    absl::Span<const ASTGraphDerivedProperty* const> properties,
    const ResolvedTableScan& base_table_scan,
    const NameScope* input_scope) const {
  GOOGLESQL_RETURN_IF_ERROR(ValidateGraphPropertyList(properties));

  static constexpr char kPropertiesClause[] = "PROPERTIES clause";
  auto expr_resolution_info =
      std::make_unique<ExprResolutionInfo>(input_scope, kPropertiesClause);
  std::vector<std::unique_ptr<const ResolvedGraphPropertyDefinition>>
      property_defs;
  property_defs.reserve(properties.size());

  // First pass, process non-measure properties.
  bool has_measure_properties = false;
  for (const auto& property : properties) {
    if (IsMeasureProperty(property)) {
      has_measure_properties = true;
      continue;
    }

    GOOGLESQL_ASSIGN_OR_RETURN(
        auto property_def,
        ResolveGraphProperty(property, expr_resolution_info.get()));
    property_defs.push_back(GetResolvedElementWithLocation(
        std::move(property_def), property->location()));
  }

  if (!has_measure_properties) {
    return property_defs;
  }

  // Second pass, process measure properties.
  // Add non-measure properties as expression columns.
  absl::Cleanup cleanup = [this] {
    // We need to remove the added graph properties in cleanup.
    InternalAnalyzerOptions::ClearGraphProperty(resolver_.analyzer_options());
  };

  for (const auto& property_def : property_defs) {
    bool is_x_as_x = false;
    if (property_def->expr()->Is<ResolvedColumnRef>()) {
      auto* ref = property_def->expr()->GetAs<ResolvedColumnRef>();
      if (googlesql_base::CaseEqual(ref->column().name(),
                                 property_def->property_declaration_name())) {
        is_x_as_x = true;
      }
    }

    GOOGLESQL_RETURN_IF_ERROR(InternalAnalyzerOptions::AddGraphProperty(
                        resolver_.analyzer_options(),
                        property_def->property_declaration_name(),
                        property_def->expr()->type(), is_x_as_x))
        .With(LocationOverride(ast_location));
  }

  // Build name-to-property map for all measure properties in this list.
  CaseInsensitiveStringHashMap<const ASTGraphDerivedProperty*>
      measure_properties;
  // TODO: b/526817456 - Move this duplicate check to ValidateGraphPropertyList.
  for (const auto& property : properties) {
    if (IsMeasureProperty(property)) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::string name, GetPropertyDeclarationName(property));
      if (!googlesql_base::InsertIfNotPresent(&measure_properties, {name, property})) {
        return MakeSqlErrorAt(property)
               << "Duplicate graph property name " << name;
      }
    }
  }

  // `unrecognized_graph_measure_name_` may be populated by the resolution
  // of measure properties, so we assert it is empty at the beginning to avoid
  // accidentally overwriting it.
  GOOGLESQL_RET_CHECK(resolver_.unrecognized_graph_measure_name_.empty());
  auto cleanup_resolver = absl::MakeCleanup(
      [this] { resolver_.unrecognized_graph_measure_name_.clear(); });

  ResolvedGraphPropertyDefinitionMap resolved_measure_properties;

  for (const auto& property : properties) {
    if (!IsMeasureProperty(property)) {
      continue;
    }
    CaseInsensitiveStringLinkedHashSet visited;
    GOOGLESQL_RETURN_IF_ERROR(ResolveGraphMeasurePropertyRecursive(
        property, input_scope, visited, measure_properties,
        resolved_measure_properties));
  }

  // Populate `property_defs` preserving the original definition order.
  // TODO: b/526818550 - All measure properties are added at the end, which does
  // not follow the syntax order. Fix this in a separate CL.
  for (const auto& property : properties) {
    if (IsMeasureProperty(property)) {
      auto it = resolved_measure_properties.find(property);
      GOOGLESQL_RET_CHECK(it != resolved_measure_properties.end());
      property_defs.push_back(std::move(it->second));
    }
  }

  return property_defs;
}

absl::Status GraphDdlResolver::ValidateGraphPropertyList(
    absl::Span<const ASTGraphDerivedProperty* const> properties) const {
  for (const auto& property : properties) {
    if (property->options_list() != nullptr) {
      if (!resolver_.language().LanguageFeatureEnabled(
              FEATURE_SQL_GRAPH_DEFAULT_LABEL_AND_PROPERTY_DEFINITION_OPTIONS)) {  // NOLINT
        return MakeSqlErrorAt(property->options_list())
               << "OPTIONS clause is not supported for property definitions";
      }
    }

    if (!IsMeasureProperty(property)) {
      // Nothing else to validate for non-measure property.
      continue;
    }

    // Validate the measure property.
    if (!resolver_.language().LanguageFeatureEnabled(
            FEATURE_SQL_GRAPH_MEASURE_DDL)) {
      return MakeSqlErrorAt(property)
             << "MEASURE is not supported for property definitions";
    }

    // Alias is required for measure property.
    if (property->alias() == nullptr) {
      return MakeSqlErrorAt(property)
             << "alias is required for MEASURE property";
    }

    // Validate the measure property expression.
    const ASTFunctionCall* function_call =
        property->expression()->GetAsOrNull<ASTFunctionCall>();

    if (function_call->HasModifiers()) {
      return MakeSqlErrorAt(function_call)
             << "MEASURE expression cannot contain clauses such as GROUP BY, "
                "HAVING MIN/MAX etc.";
    }
  }

  return absl::OkStatus();
}

absl::StatusOr<
    std::vector<std::unique_ptr<const ResolvedGraphPropertyDefinition>>>
GraphDdlResolver::ResolveGraphPropertiesAllColumns(
    const ASTNode* ast_location, const ASTColumnList* all_except_column_list,
    const ResolvedTableScan& base_table_scan) const {
  if (resolver_.language().LanguageFeatureEnabled(
          googlesql::FEATURE_PROPERTY_GRAPH_ENFORCE_EXPLICIT_PROPERTIES)) {
    return MakeSqlErrorAt(ast_location)
           << "Properties list must be explicitly specified using syntax "
              "PROPERTIES (...).";
  }

  const Table* base_table = base_table_scan.table();

  // Figures out the excluded columns.
  absl::flat_hash_set<const Column*> all_except_columns;
  if (all_except_column_list != nullptr) {
    for (const auto& except_column : all_except_column_list->identifiers()) {
      std::string excluded_column_name = except_column->GetAsString();
      const Column* col = base_table->FindColumnByName(excluded_column_name);
      if (col == nullptr) {
        return MakeSqlErrorAt(except_column)
               << "Column " << ToSingleQuotedStringLiteral(excluded_column_name)
               << " in the EXCEPT list is not found in the input table";
      }
      if (!all_except_columns.emplace(col).second) {
        return MakeSqlErrorAt(except_column)
               << "Duplicate column "
               << ToSingleQuotedStringLiteral(excluded_column_name)
               << " is used in the property definition EXCEPT list.";
      }
    }
  }

  // Figures out the included columns: all columns except the excluded ones.
  std::vector<const Column*> catalog_columns;
  for (int i = 0; i < base_table->NumColumns(); ++i) {
    const Column* col = base_table->GetColumn(i);
    if (!all_except_columns.contains(col)) {
      catalog_columns.push_back(col);
    }
  }

  // Resolves all the included columns.
  GOOGLESQL_ASSIGN_OR_RETURN(std::vector<std::unique_ptr<const ResolvedExpr>> col_refs,
                   ResolveColumnList(catalog_columns, base_table_scan));
  GOOGLESQL_RET_CHECK_EQ(catalog_columns.size(), col_refs.size());

  // Builds the property definitions.
  std::vector<std::unique_ptr<const ResolvedGraphPropertyDefinition>>
      property_defs;
  property_defs.reserve(col_refs.size());
  for (int i = 0; i < col_refs.size(); ++i) {
    auto col_ref = std::move(col_refs[i]);

    std::string col_name = catalog_columns[i]->Name();
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedGraphPropertyDefinition> property_def,
        ResolvedGraphPropertyDefinitionBuilder()
            .set_expr(std::move(col_ref))
            .set_property_declaration_name(col_name)
            // `col_name` itself may not be a valid sql expression when it's
            // a reserved keyword. In that case, ToIdentifierLiteral will quote
            // the name with backticks to make it a valid sql expression.
            //
            // Note that this is not needed for property declaration name above
            // which should stick to the original name. When used in query,
            // either the user or any query generation tool needs to quote it.
            .set_sql(ToIdentifierLiteral(col_name))
            .set_is_measure(false)
            .Build());
    property_defs.push_back(GetResolvedElementWithLocation(
        std::move(property_def), ast_location->location()));
  }
  return property_defs;
}

absl::StatusOr<
    std::vector<std::unique_ptr<const ResolvedGraphPropertyDefinition>>>
GraphDdlResolver::ResolveGraphProperties(
    const ASTGraphProperties* ast_properties,
    const ResolvedTableScan& base_table_scan,
    const NameScope* input_scope) const {
  if (ast_properties->no_properties()) {
    // NO PROPERTIES
    GOOGLESQL_RET_CHECK_EQ(ast_properties->derived_property_list(), nullptr);
    GOOGLESQL_RET_CHECK_EQ(ast_properties->all_except_columns(), nullptr);
    std::vector<std::unique_ptr<const ResolvedGraphPropertyDefinition>> output;
    return output;
  }

  if (ast_properties->derived_property_list() != nullptr) {
    // PROPERTIES(<derived_property_list>)
    GOOGLESQL_RET_CHECK_EQ(ast_properties->all_except_columns(), nullptr);
    return ResolveGraphPropertyList(
        ast_properties, ast_properties->derived_property_list()->properties(),
        base_table_scan, input_scope);
  }

  // ALL PROPERTIES [EXCEPT(<all_except_columns>)]
  return ResolveGraphPropertiesAllColumns(
      ast_properties, ast_properties->all_except_columns(), base_table_scan);
}

absl::StatusOr<GraphDdlResolver::LabelAndPropertiesList>
GraphDdlResolver::ResolveLabelAndPropertiesList(
    const ASTGraphElementLabelAndPropertiesList& ast_label_properties_list,
    const ASTOptionsList* default_label_options, IdString element_table_alias,
    const ResolvedTableScan& base_table_scan,
    const NameScope* input_scope) const {
  GOOGLESQL_RET_CHECK(!ast_label_properties_list.label_properties_list().empty());
  bool label_options_enabled = resolver_.language().LanguageFeatureEnabled(
      FEATURE_SQL_GRAPH_DEFAULT_LABEL_AND_PROPERTY_DEFINITION_OPTIONS);
  if (default_label_options != nullptr && !label_options_enabled) {
    return MakeSqlErrorAt(default_label_options)
           << "OPTIONS clause is not supported for labels";
  }
  std::vector<std::unique_ptr<const ResolvedGraphElementLabel>> labels;
  std::vector<std::unique_ptr<const ResolvedGraphPropertyDefinition>>
      property_defs;
  for (const auto* ast_label_and_props :
       ast_label_properties_list.label_properties_list()) {
    bool is_default_label = ast_label_and_props->label_name() == nullptr;
    absl::string_view label_name =
        is_default_label ? element_table_alias.ToStringView()
                         : ast_label_and_props->label_name()->GetAsStringView();
    const ASTOptionsList* label_options =
        ast_label_and_props->label_options_list();
    if (label_options != nullptr) {
      if (!label_options_enabled) {
        return MakeSqlErrorAt(label_options)
               << "OPTIONS clause is not supported for labels";
      }
      if (!is_default_label) {
        return MakeSqlErrorAt(label_options)
               << "OPTIONS clause is not supported for non-default labels";
      }
      if (default_label_options != nullptr) {
        return MakeSqlErrorAt(label_options)
               << "OPTIONS clause for the default label "
               << ToSingleQuotedStringLiteral(label_name)
               << " is defined more than once, consider dropping the OPTIONS "
                  "clause before DEFAULT LABEL clause";
      }
    } else if (is_default_label && default_label_options != nullptr) {
      label_options = default_label_options;
    }
    GOOGLESQL_RET_CHECK(ast_label_and_props->properties() != nullptr);
    GOOGLESQL_ASSIGN_OR_RETURN(
        LabelAndProperties label_and_properties,
        ResolveLabelAndProperties(*ast_label_and_props, label_name,
                                  *ast_label_and_props->properties(),
                                  label_options, base_table_scan, input_scope));
    property_defs.insert(
        property_defs.end(),
        std::make_move_iterator(label_and_properties.property_defs.begin()),
        std::make_move_iterator(label_and_properties.property_defs.end()));
    labels.push_back(std::move(label_and_properties.label));
  }
  return LabelAndPropertiesList{
      .labels = std::move(labels),
      .property_defs = std::move(property_defs),
  };
}

absl::StatusOr<GraphDdlResolver::LabelAndProperties>
GraphDdlResolver::ResolveLabelAndProperties(
    const ASTNode& ast_location, absl::string_view label_name,
    const ASTGraphProperties& properties,
    const ASTOptionsList* label_options_list,
    const ResolvedTableScan& base_table_scan,
    const NameScope* input_scope) const {
  std::vector<std::unique_ptr<const ResolvedOption>> options_list;
  if (label_options_list != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(resolver_.ResolveOptionsList(
        label_options_list,
        /*allow_alter_array_operators=*/false, &options_list));
  }
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::vector<std::unique_ptr<const ResolvedGraphPropertyDefinition>>
          property_defs,
      ResolveGraphProperties(&properties, base_table_scan, input_scope));

  std::vector<std::string> property_declaration_names;
  property_declaration_names.reserve(property_defs.size());
  for (const auto& property_def : property_defs) {
    // Each property definition corresponds to a property declaration.
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedGraphPropertyDeclaration> property_decl,
        ResolvedGraphPropertyDeclarationBuilder()
            .set_name(property_def->property_declaration_name())
            .set_type(property_def->expr()->type())
            .set_type_annotation_map(
                property_def->expr()->type_annotation_map())
            .Build());
    property_declaration_names.push_back(property_decl->name());
  }

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedGraphElementLabel> label,
                   ResolvedGraphElementLabelBuilder()
                       .set_name(label_name)
                       .set_property_declaration_name_list(
                           std::move(property_declaration_names))
                       .set_options_list(std::move(options_list))
                       .Build());
  label =
      GetResolvedElementWithLocation(std::move(label), ast_location.location());
  // Validates label.
  GOOGLESQL_RETURN_IF_ERROR(ValidateElementLabel(ast_location, *label));
  return LabelAndProperties{
      .label = std::move(label),
      .property_defs = std::move(property_defs),
  };
}

absl::StatusOr<std::unique_ptr<const ResolvedTableScan>>
GraphDdlResolver::ResolveBaseTable(
    const ASTPathExpression* input_table_name,
    NameListPtr& input_table_scan_name_list) const {
  const IdString input_table_alias = GetAliasForExpression(input_table_name);

  std::unique_ptr<const ResolvedTableScan> resolved_table_scan;

  // NOTE: We build `output_column_name_list` instead of `output_name_list`
  // because `output_name_list` contains a range variable (from the table name)
  // that could potential override a column with the same name.
  NameListPtr table_scan_name_list;
  GOOGLESQL_RETURN_IF_ERROR(resolver_.ResolvePathExpressionAsTableScan(
      input_table_name, input_table_alias,
      /*has_explicit_alias=*/false,
      /*alias_location=*/input_table_name, /*hints=*/nullptr,
      /*for_version_clause=*/nullptr, resolver_.empty_name_scope_.get(),
      /*read_as_row_type_error_kind=*/"graph query",
      /*remaining_names=*/nullptr, &resolved_table_scan,
      /*output_name_list=*/&table_scan_name_list,
      /*output_column_name_list=*/&input_table_scan_name_list,
      resolver_.resolved_columns_from_table_scans_));
  // keep the column ref for validation
  resolver_.RecordColumnAccess(resolved_table_scan->column_list());
  return resolved_table_scan;
}

bool IsExprStringArray(const ResolvedExpr* expr) {
  return expr->type()->IsArray() &&
         expr->type()->AsArray()->element_type()->IsString();
}

absl::StatusOr<std::vector<
    std::unique_ptr<GraphDdlResolver::PropertyDeclarationWithIsMeasure>>>
GraphDdlResolver::BuildPropertyDeclarations(
    std::vector<std::unique_ptr<const ResolvedGraphPropertyDefinition>>&
        property_defs) const {
  GOOGLESQL_RETURN_IF_ERROR(
      Dedupe(property_defs,
             &ResolvedGraphPropertyDefinition::property_declaration_name));

  std::vector<std::unique_ptr<PropertyDeclarationWithIsMeasure>> property_decls;
  property_decls.reserve(property_defs.size());
  for (const std::unique_ptr<const ResolvedGraphPropertyDefinition>&
           property_def : property_defs) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedGraphPropertyDeclaration> property_decl,
        ResolvedGraphPropertyDeclarationBuilder()
            .set_name(property_def->property_declaration_name())
            .set_type(property_def->expr()->type())
            .set_type_annotation_map(
                property_def->expr()->type_annotation_map())
            .Build());
    GOOGLESQL_RET_CHECK_NE(property_def->GetParseLocationRangeOrNULL(), nullptr);
    property_decls.push_back(std::make_unique<PropertyDeclarationWithIsMeasure>(
        GetResolvedElementWithLocation(
            std::move(property_decl),
            *property_def->GetParseLocationRangeOrNULL()),
        property_def->is_measure()));
  }
  return property_decls;
}

absl::StatusOr<GraphDdlResolver::ElementTableWithLabelsAndProperties>
GraphDdlResolver::ResolveGraphElementTable(
    const ASTGraphElementTable* ast_element_table,
    const GraphElementTable::Kind element_kind,
    const StringViewHashMapCase<const ResolvedGraphElementTable*>&
        node_table_map) const {
  // Resolves the underlying table.
  NameListPtr table_scan_name_list;
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedTableScan> table_scan,
      ResolveBaseTable(ast_element_table->name(), table_scan_name_list));

  // Resolves the element table key clause.
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::vector<std::unique_ptr<const ResolvedExpr>> key_list,
      ResolveKeyColumns(ast_element_table, element_kind,
                        ast_element_table->key_list(), *table_scan));

  // Resolves element table alias: either from explicit alias or implicitly
  // the qualified identifier.
  const IdString alias = ast_element_table->alias() == nullptr
                             ? GetAliasForExpression(ast_element_table->name())
                             : ast_element_table->alias()->GetAsIdString();
  // Resolves all labels and properties.
  GOOGLESQL_RET_CHECK(ast_element_table->label_properties_list() != nullptr);
  NameScope input_table_name_scope(*table_scan_name_list);
  GOOGLESQL_ASSIGN_OR_RETURN(LabelAndPropertiesList label_and_properties_list,
                   ResolveLabelAndPropertiesList(
                       *ast_element_table->label_properties_list(),
                       ast_element_table->default_label_options_list(), alias,
                       *table_scan, &input_table_name_scope));

  // Resolves dynamic labels
  std::unique_ptr<const ResolvedGraphDynamicLabelSpecification>
      dynamic_label_spec;
  if (ast_element_table->dynamic_label() != nullptr) {
    static constexpr char kDynamicLabelClause[] = "DYNAMIC LABEL clause";
    auto expr_resolution_info = std::make_unique<ExprResolutionInfo>(
        &input_table_name_scope, kDynamicLabelClause);
    std::unique_ptr<const ResolvedExpr> resolved_expr;
    GOOGLESQL_RETURN_IF_ERROR(
        resolver_.ResolveExpr(ast_element_table->dynamic_label()->label(),
                              expr_resolution_info.get(), &resolved_expr));
    bool multi_label_feature_enabled =
        resolver_.language().LanguageFeatureEnabled(
            FEATURE_SQL_GRAPH_DYNAMIC_MULTI_LABEL_NODES);
    if (IsExprStringArray(resolved_expr.get())) {
      // If the feature is not enabled, and the dynamic label is an array of
      // strings, return error.
      if (!multi_label_feature_enabled) {
        return MakeSqlErrorAt(ast_element_table->dynamic_label())
               << kDynamicLabelClause
               << " must hold expression of type STRING, but was of type "
                  "ARRAY<STRING> ";
      }
      // If the feature is enabled, and the dynamic label is an array of
      // strings for an edge, return error.
      if (element_kind == GraphElementTable::Kind::kEdge) {
        return MakeSqlErrorAt(ast_element_table->dynamic_label())
               << kDynamicLabelClause
               << " must hold expression of type STRING for edge tables. "
                  "ARRAY<STRING> expression for edge tables is not supported";
      }
    }
    if (!resolved_expr->type()->IsString() &&
        !IsExprStringArray(resolved_expr.get())) {
      // If not of STRING type and not ARRAY<STRING> type, return error.
      return MakeSqlErrorAt(ast_element_table->dynamic_label())
             << kDynamicLabelClause
             << " must hold expression of type STRING or ARRAY<STRING>, but "
                "was of type "
             << resolved_expr->type()->TypeName(resolver_.product_mode());
    }
    if (!resolved_expr->Is<ResolvedColumnRef>()) {
      return MakeSqlErrorAt(ast_element_table->dynamic_label())
             << kDynamicLabelClause << " must hold direct column reference";
    }
    GOOGLESQL_ASSIGN_OR_RETURN(dynamic_label_spec,
                     ResolvedGraphDynamicLabelSpecificationBuilder()
                         .set_label_expr(std::move(resolved_expr))
                         .Build());
  }

  // Resolves dynamic properties
  std::unique_ptr<const ResolvedGraphDynamicPropertiesSpecification>
      dynamic_properties_spec;
  if (ast_element_table->dynamic_properties() != nullptr) {
    static constexpr char kDynamicPropertiesClause[] =
        "DYNAMIC PROPERTIES clause";
    auto expr_resolution_info = std::make_unique<ExprResolutionInfo>(
        &input_table_name_scope, kDynamicPropertiesClause);
    std::unique_ptr<const ResolvedExpr> resolved_expr;
    GOOGLESQL_RETURN_IF_ERROR(resolver_.ResolveExpr(
        ast_element_table->dynamic_properties()->properties(),
        expr_resolution_info.get(), &resolved_expr));
    if (!resolved_expr->type()->IsJson()) {
      return MakeSqlErrorAt(ast_element_table->dynamic_properties())
             << kDynamicPropertiesClause
             << " must hold expression of type JSON, but was of type "
             << resolved_expr->type()->TypeName(resolver_.product_mode());
    }
    if (!resolved_expr->Is<ResolvedColumnRef>()) {
      return MakeSqlErrorAt(ast_element_table->dynamic_properties())
             << kDynamicPropertiesClause
             << " must hold direct column reference";
    }
    GOOGLESQL_ASSIGN_OR_RETURN(dynamic_properties_spec,
                     ResolvedGraphDynamicPropertiesSpecificationBuilder()
                         .set_property_expr(std::move(resolved_expr))
                         .Build());
  }

  std::unique_ptr<const ResolvedGraphNodeTableReference> source_node_ref,
      dest_node_ref;

  // Resolves source node table reference if specified.
  if (ast_element_table->source_node_reference() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(source_node_ref,
                     ResolveGraphNodeTableReference(
                         ast_element_table->source_node_reference(),
                         *table_scan, node_table_map));
  }

  // Resolves destination node table reference if specified.
  if (ast_element_table->dest_node_reference() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        dest_node_ref,
        ResolveGraphNodeTableReference(ast_element_table->dest_node_reference(),
                                       *table_scan, node_table_map));
  }

  std::vector<const ASTGraphInlinedEdgeDefinition*> ast_inlined_edges;
  if (!ast_element_table->inlined_edge_definitions().empty()) {
    if (!resolver_.language().LanguageFeatureEnabled(
            FEATURE_PROPERTY_GRAPH_INLINE_EDGES)) {
      return MakeSqlErrorAt(ast_element_table->inlined_edge_definitions()[0])
             << "Inlined edge definition is not supported";
    }
    if (element_kind != GraphElementTable::Kind::kNode) {
      return MakeSqlErrorAt(ast_element_table->inlined_edge_definitions()[0])
             << "Inlined edge definition is only allowed within a node table";
    }
    ast_inlined_edges.reserve(
        ast_element_table->inlined_edge_definitions().size());
    for (const auto* ast_inlined_edge :
         ast_element_table->inlined_edge_definitions()) {
      ast_inlined_edges.push_back(ast_inlined_edge);
    }
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::vector<std::unique_ptr<PropertyDeclarationWithIsMeasure>>
          property_decls,
      BuildPropertyDeclarations(label_and_properties_list.property_defs));

  // Collect the labels names declared in this element table.
  std::vector<std::string> label_names;
  label_names.reserve(label_and_properties_list.labels.size());
  absl::c_transform(label_and_properties_list.labels,
                    std::back_inserter(label_names),
                    [](const auto& label) { return label->name(); });
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedGraphElementTable> element_table,
      ResolvedGraphElementTableBuilder()
          .set_alias(alias.ToString())
          .set_key_list(std::move(key_list))
          .set_input_scan(std::move(table_scan))
          .set_label_name_list(std::move(label_names))
          .set_property_definition_list(
              std::move(label_and_properties_list.property_defs))
          .set_source_node_reference(std::move(source_node_ref))
          .set_dest_node_reference(std::move(dest_node_ref))
          .set_dynamic_label(std::move(dynamic_label_spec))
          .set_dynamic_properties(std::move(dynamic_properties_spec))
          .Build());
  // Validates ResolvedGraphElementTable.
  GOOGLESQL_RETURN_IF_ERROR(
      ValidateElementTable(ast_element_table, *element_table, element_kind));
  return ElementTableWithLabelsAndProperties{
      .element_table = std::move(element_table),
      .labels = std::move(label_and_properties_list.labels),
      .property_decls = std::move(property_decls),
      .ast_inlined_edges = std::move(ast_inlined_edges)};
}

absl::StatusOr<GraphDdlResolver::ElementTableWithLabelsAndProperties>
GraphDdlResolver::ResolveInlinedEdge(
    const ASTGraphInlinedEdgeDefinition* ast_inlined_edge,
    const ASTGraphElementTable* ast_enclosing_node_table,
    const ResolvedGraphElementTable* enclosing_node_table,
    const StringViewHashMapCase<const ResolvedGraphElementTable*>&
        node_table_map) const {
  const IdString edge_alias = ast_inlined_edge->edge_alias()->GetAsIdString();

  // TODO: b/492867368 - Dynamic labels/properties are accepted by the grammar
  // but not yet wired through for inlined edges. Reject explicitly so the
  // clause isn't silently dropped.
  if (ast_inlined_edge->dynamic_label() != nullptr) {
    return MakeSqlErrorAt(ast_inlined_edge->dynamic_label())
           << "Dynamic label is not supported on inlined edges";
  }
  if (ast_inlined_edge->dynamic_properties() != nullptr) {
    return MakeSqlErrorAt(ast_inlined_edge->dynamic_properties())
           << "Dynamic properties are not supported on inlined edges";
  }

  // User-written column lists must not contain duplicates.
  GOOGLESQL_RETURN_IF_ERROR(ValidateColumnsAreUnique(ast_inlined_edge->join_keys()));
  GOOGLESQL_RETURN_IF_ERROR(
      ValidateColumnsAreUnique(ast_inlined_edge->dest_element_table_columns()));

  // Re-resolve the enclosing node's base table as a regular edge would.
  NameListPtr edge_table_scan_name_list;
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedTableScan> edge_table_scan,
                   ResolveBaseTable(ast_enclosing_node_table->name(),
                                    edge_table_scan_name_list));
  // Inlined edges share the same key columns as the enclosing node table.
  GOOGLESQL_ASSIGN_OR_RETURN(auto edge_key_list,
                   ResolveKeyColumns(
                       ast_enclosing_node_table, GraphElementTable::Kind::kEdge,
                       ast_enclosing_node_table->key_list(), *edge_table_scan));

  // Resolve JOIN KEY (...)
  std::vector<std::unique_ptr<const ResolvedExpr>> join_keys;
  // JOIN KEY clause is guaranteed to be non-null by the grammar.
  GOOGLESQL_RET_CHECK(ast_inlined_edge->join_keys() != nullptr);
  GOOGLESQL_ASSIGN_OR_RETURN(
      join_keys, ResolveColumnList(ast_inlined_edge->join_keys()->identifiers(),
                                   *edge_table_scan));

  GOOGLESQL_ASSIGN_OR_RETURN(
      const ResolvedGraphElementTable* target_node_table,
      FindNodeTable(ast_inlined_edge->referenced_table(), node_table_map));

  std::vector<std::unique_ptr<const ResolvedExpr>> target_node_col_refs;
  if (ast_inlined_edge->dest_element_table_columns() != nullptr) {
    // User specified column list: REFERENCES <node> (cols)
    GOOGLESQL_RET_CHECK(target_node_table->input_scan()->Is<ResolvedTableScan>());
    const ResolvedTableScan* target_input_scan =
        target_node_table->input_scan()->GetAs<ResolvedTableScan>();
    GOOGLESQL_ASSIGN_OR_RETURN(
        target_node_col_refs,
        ResolveColumnList(
            ast_inlined_edge->dest_element_table_columns()->identifiers(),
            *target_input_scan));
  } else {
    // No user specified column list: REFERENCES <node>
    GOOGLESQL_ASSIGN_OR_RETURN(target_node_col_refs,
                     CopyAll(target_node_table->key_list()));
  }

  // Build both endpoint references first; the direction below decides which is
  // source and which is destination.
  std::unique_ptr<const ResolvedGraphNodeTableReference> source_node_ref;
  std::unique_ptr<const ResolvedGraphNodeTableReference> dest_node_ref;

  GOOGLESQL_ASSIGN_OR_RETURN(auto enclosing_edge_col_refs, CopyAll(edge_key_list));
  GOOGLESQL_ASSIGN_OR_RETURN(auto enclosing_node_col_refs,
                   CopyAll(enclosing_node_table->key_list()));

  bool is_forwards = ast_inlined_edge->direction() != ASTGraphEdgePattern::LEFT;

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto resolved_enclosing_node_ref,
      ResolvedGraphNodeTableReferenceBuilder()
          .set_node_table_identifier(enclosing_node_table->alias())
          .set_node_table_column_list(std::move(enclosing_node_col_refs))
          .set_edge_table_column_list(std::move(enclosing_edge_col_refs))
          .Build());
  GOOGLESQL_RETURN_IF_ERROR(ValidateNodeTableReferenceColumns(
      ast_inlined_edge, *resolved_enclosing_node_ref));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto resolved_target_node_ref,
      ResolvedGraphNodeTableReferenceBuilder()
          .set_node_table_identifier(target_node_table->alias())
          .set_node_table_column_list(std::move(target_node_col_refs))
          .set_edge_table_column_list(std::move(join_keys))
          .Build());
  GOOGLESQL_RETURN_IF_ERROR(ValidateNodeTableReferenceColumns(ast_inlined_edge,
                                                    *resolved_target_node_ref));

  // FORWARDS (default): JOIN KEY columns reference the destination, so the
  // enclosing node is the source. BACKWARDS swaps source and destination.
  if (is_forwards) {
    source_node_ref = std::move(resolved_enclosing_node_ref);
    dest_node_ref = std::move(resolved_target_node_ref);
  } else {
    source_node_ref = std::move(resolved_target_node_ref);
    dest_node_ref = std::move(resolved_enclosing_node_ref);
  }

  // Inlined-edge properties resolve only against the enclosing node's base
  // table columns, since the edge shares that base table.
  NameScope input_table_name_scope(*edge_table_scan_name_list);
  GOOGLESQL_ASSIGN_OR_RETURN(LabelAndPropertiesList edge_label_and_properties_list,
                   ResolveLabelAndPropertiesList(
                       *ast_inlined_edge->label_and_properties_list(),
                       ast_inlined_edge->default_label_options(), edge_alias,
                       *edge_table_scan, &input_table_name_scope));

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::vector<std::unique_ptr<PropertyDeclarationWithIsMeasure>>
          property_decls,
      BuildPropertyDeclarations(edge_label_and_properties_list.property_defs));

  std::vector<std::string> edge_label_names;
  edge_label_names.reserve(edge_label_and_properties_list.labels.size());
  absl::c_transform(edge_label_and_properties_list.labels,
                    std::back_inserter(edge_label_names),
                    [](const auto& label) { return label->name(); });

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedGraphElementTable> edge_element_table,
      ResolvedGraphElementTableBuilder()
          .set_alias(edge_alias.ToString())
          .set_key_list(std::move(edge_key_list))
          .set_input_scan(std::move(edge_table_scan))
          .set_label_name_list(std::move(edge_label_names))
          .set_property_definition_list(
              std::move(edge_label_and_properties_list.property_defs))
          .set_source_node_reference(std::move(source_node_ref))
          .set_dest_node_reference(std::move(dest_node_ref))
          .Build());
  edge_element_table = GetResolvedElementWithLocation(
      std::move(edge_element_table), ast_inlined_edge->location());

  return ElementTableWithLabelsAndProperties{
      .element_table = std::move(edge_element_table),
      .labels = std::move(edge_label_and_properties_list.labels),
      .property_decls = std::move(property_decls),
      // Edges cannot themselves contain inlined edges.
      .ast_inlined_edges = {}};
}

absl::Status GraphDdlResolver::ResolveCreatePropertyGraphStmt(
    const ASTCreatePropertyGraphStatement* ast_stmt,
    std::unique_ptr<ResolvedStatement>* output) const {
  // Resolves general settings and options.
  ResolvedCreateStatement::CreateScope create_scope;
  ResolvedCreateStatement::CreateMode create_mode;
  GOOGLESQL_RETURN_IF_ERROR(resolver_.ResolveCreateStatementOptions(
      ast_stmt, "CREATE PROPERTY GRAPH", &create_scope, &create_mode));

  std::vector<std::unique_ptr<const ResolvedOption>> option_list;
  if (ast_stmt->options_list() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(resolver_.ResolveOptionsList(
        ast_stmt->options_list(), /*allow_alter_array_operators=*/false,
        &option_list));
  }

  std::unique_ptr<const ResolvedConnectionList> resolved_connection_list;
  if (ast_stmt->with_connection_clause() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(resolver_.ResolveConnectionClause(
        ast_stmt->with_connection_clause(), &resolved_connection_list,
        /*is_default_connection_allowed=*/true));
  }

  std::vector<std::unique_ptr<const ResolvedGraphElementTable>> node_tables;
  std::vector<std::unique_ptr<const ResolvedGraphElementTable>> edge_tables;
  struct PendingInlinedEdge {
    const ASTGraphInlinedEdgeDefinition* ast_inlined_edge;
    const ASTGraphElementTable* ast_enclosing_node_table;
    const ResolvedGraphElementTable* enclosing_node_table;
  };
  std::vector<PendingInlinedEdge> pending_inlined_edges;

  std::vector<std::unique_ptr<const ResolvedGraphElementLabel>> labels;
  std::vector<std::unique_ptr<PropertyDeclarationWithIsMeasure>> property_decls;

  // Check the feature flag once and store its state.
  const bool dynamic_label_properties_feature_enabled =
      resolver_.language().LanguageFeatureEnabled(
          FEATURE_SQL_GRAPH_DYNAMIC_LABEL_PROPERTIES_IN_DDL);
  bool dynamic_labeled_node_exists = false;
  bool dynamic_labeled_edge_exists = false;

  // Resolves node tables if specified.
  StringViewHashMapCase<const ResolvedGraphElementTable*> node_table_map;
  // Tracks labels that have OPTIONS defined, which have to be uniquely bound to
  // a single element table.
  StringViewHashSetCase labels_with_options;
  if (ast_stmt->node_table_list() != nullptr) {
    for (const auto* ast_node_table :
         ast_stmt->node_table_list()->element_tables()) {
      if (ast_node_table->dynamic_label() != nullptr) {
        if (!dynamic_label_properties_feature_enabled) {
          return MakeSqlErrorAt(ast_node_table->dynamic_label())
                 << "Dynamic label is not supported";
        }
        if (dynamic_labeled_node_exists) {
          return MakeSqlErrorAt(ast_node_table->dynamic_label())
                 << "Only one node table can be assigned a dynamic label";
        }
        dynamic_labeled_node_exists = true;
      }
      if (ast_node_table->dynamic_properties() != nullptr) {
        if (!dynamic_label_properties_feature_enabled) {
          return MakeSqlErrorAt(ast_node_table->dynamic_properties())
                 << "Dynamic properties are not supported";
        }
      }
      GOOGLESQL_ASSIGN_OR_RETURN(
          ElementTableWithLabelsAndProperties
              element_table_with_labels_and_properties,
          ResolveGraphElementTable(
              ast_node_table, GraphElementTable::Kind::kNode, node_table_map));
      node_table_map.emplace(
          element_table_with_labels_and_properties.element_table->alias(),
          element_table_with_labels_and_properties.element_table.get());
      node_tables.push_back(
          std::move(element_table_with_labels_and_properties.element_table));
      // Inlined edges must be resolved in a second pass, after all node tables
      // have been resolved and added to `node_table_map`.
      for (const auto* ast_inlined_edge :
           element_table_with_labels_and_properties.ast_inlined_edges) {
        pending_inlined_edges.push_back(
            {.ast_inlined_edge = ast_inlined_edge,
             .ast_enclosing_node_table = ast_node_table,
             .enclosing_node_table = node_tables.back().get()});
      }
      GOOGLESQL_RETURN_IF_ERROR(ValidateAndRecordLabelsAndPropertyDeclarations(
          element_table_with_labels_and_properties.labels,
          element_table_with_labels_and_properties.property_decls,
          labels_with_options, labels, property_decls));
    }
  }

  // Resolve inlined edges in a second pass.
  std::vector<std::unique_ptr<const ResolvedGraphElementTable>>
      inlined_edge_tables;
  std::vector<const ASTGraphInlinedEdgeDefinition*> inlined_edge_ast_locations;
  for (const auto& pending : pending_inlined_edges) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        ElementTableWithLabelsAndProperties
            element_table_with_labels_and_properties,
        ResolveInlinedEdge(pending.ast_inlined_edge,
                           pending.ast_enclosing_node_table,
                           pending.enclosing_node_table, node_table_map));
    inlined_edge_tables.push_back(
        std::move(element_table_with_labels_and_properties.element_table));
    inlined_edge_ast_locations.push_back(pending.ast_inlined_edge);
    GOOGLESQL_RETURN_IF_ERROR(ValidateAndRecordLabelsAndPropertyDeclarations(
        element_table_with_labels_and_properties.labels,
        element_table_with_labels_and_properties.property_decls,
        labels_with_options, labels, property_decls));
  }

  // Resolves edge tables if specified.
  if (ast_stmt->edge_table_list() != nullptr) {
    for (const auto* ast_edge_table :
         ast_stmt->edge_table_list()->element_tables()) {
      if (ast_edge_table->dynamic_label() != nullptr) {
        if (!dynamic_label_properties_feature_enabled) {
          return MakeSqlErrorAt(ast_edge_table->dynamic_label())
                 << "Dynamic label is not supported";
        }
        if (dynamic_labeled_edge_exists) {
          return MakeSqlErrorAt(ast_edge_table->dynamic_label())
                 << "Only one edge table can be assigned a dynamic label";
        }
        dynamic_labeled_edge_exists = true;
      }
      if (ast_edge_table->dynamic_properties() != nullptr) {
        if (!dynamic_label_properties_feature_enabled) {
          return MakeSqlErrorAt(ast_edge_table->dynamic_properties())
                 << "Dynamic properties are not supported";
        }
      }
      GOOGLESQL_ASSIGN_OR_RETURN(
          ElementTableWithLabelsAndProperties
              element_table_with_labels_and_properties,
          ResolveGraphElementTable(
              ast_edge_table, GraphElementTable::Kind::kEdge, node_table_map));
      edge_tables.push_back(
          std::move(element_table_with_labels_and_properties.element_table));
      GOOGLESQL_RETURN_IF_ERROR(ValidateAndRecordLabelsAndPropertyDeclarations(
          element_table_with_labels_and_properties.labels,
          element_table_with_labels_and_properties.property_decls,
          labels_with_options, labels, property_decls));
    }
  }

  if (dynamic_label_properties_feature_enabled) {
    if (dynamic_labeled_node_exists &&
        ast_stmt->node_table_list()->element_tables().size() > 1) {
      return MakeSqlErrorAt(ast_stmt->node_table_list())
             << "Cannot define more than one node table when using dynamic "
                "labels";
    }
    if (dynamic_labeled_edge_exists &&
        ast_stmt->edge_table_list()->element_tables().size() > 1) {
      return MakeSqlErrorAt(ast_stmt->edge_table_list())
             << "Cannot define more than one edge table when using dynamic "
                "labels";
    }
    // Disallow mixing dynamic nodes with static edges and vice versa if
    // FEATURE_SQL_GRAPH_DYNAMIC_LABEL_EXTENSION_IN_DDL is disabled.
    if (!resolver_.language().LanguageFeatureEnabled(
            FEATURE_SQL_GRAPH_DYNAMIC_LABEL_EXTENSION_IN_DDL)) {
      bool static_labeled_edges =
          ast_stmt->edge_table_list() != nullptr &&
          !ast_stmt->edge_table_list()->element_tables().empty() &&
          !dynamic_labeled_edge_exists;
      bool static_labeled_nodes =
          ast_stmt->node_table_list() != nullptr &&
          !ast_stmt->node_table_list()->element_tables().empty() &&
          !dynamic_labeled_node_exists;

      if ((dynamic_labeled_node_exists && static_labeled_edges)) {
        return MakeSqlErrorAt(ast_stmt)
               << "If a dynamically labeled node table exists, any defined "
                  "edge table must also be dynamically labeled";
      }
      if ((dynamic_labeled_edge_exists && static_labeled_nodes)) {
        return MakeSqlErrorAt(ast_stmt)
               << "If a dynamically labeled edge table exists, any defined "
                  "node table must also be dynamically labeled";
      }
    }
  }

  // Validates element table uniqueness at graph level.
  StringViewHashSetCase element_table_names;
  GOOGLESQL_RETURN_IF_ERROR(ValidateNoDuplicateElementTable(
      ast_stmt->node_table_list(), node_tables, GraphElementTable::Kind::kNode,
      element_table_names));
  GOOGLESQL_RETURN_IF_ERROR(ValidateNoDuplicateElementTable(
      ast_stmt->edge_table_list(), edge_tables, GraphElementTable::Kind::kEdge,
      element_table_names));

  // Check inlined-edge names against the (already-validated) node + edge
  // names, then move them into `edge_tables` for the rest of the pipeline.
  GOOGLESQL_RETURN_IF_ERROR(ValidateNoDuplicateElementTables(
      absl::MakeConstSpan(inlined_edge_ast_locations), inlined_edge_tables,
      GraphElementTable::Kind::kEdge, element_table_names));
  absl::c_move(inlined_edge_tables, std::back_inserter(edge_tables));

  // Dedupe property declarations at graph level.
  GOOGLESQL_RETURN_IF_ERROR(
      Dedupe(property_decls, &PropertyDeclarationWithIsMeasure::name));
  // Dedupe labels at graph level.
  GOOGLESQL_RETURN_IF_ERROR(Dedupe(labels, &ResolvedGraphElementLabel::name));

  std::vector<std::unique_ptr<const ResolvedGraphPropertyDeclaration>>
      property_declarations;
  property_declarations.reserve(property_decls.size());
  absl::c_transform(
      property_decls, std::back_inserter(property_declarations),
      [](auto& property_decl) { return property_decl->release_declaration(); });
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedCreatePropertyGraphStmt>
          create_property_graph_stmt,
      ResolvedCreatePropertyGraphStmtBuilder()
          .set_name_path(ast_stmt->name()->ToIdentifierVector())
          .set_create_scope(create_scope)
          .set_create_mode(create_mode)
          .set_node_table_list(std::move(node_tables))
          .set_edge_table_list(std::move(edge_tables))
          .set_label_list(std::move(labels))
          .set_property_declaration_list(std::move(property_declarations))
          .set_option_list(std::move(option_list))
          .set_connection_list(std::move(resolved_connection_list))
          .Build());

  // Validates name collision against property graphs and tables.
  GOOGLESQL_RETURN_IF_ERROR((ValidateCreationAgainstCatalog<PropertyGraph>(
      ast_stmt->name(), *create_property_graph_stmt, *resolver_.catalog_,
      resolver_.analyzer_options_.find_options(),
      "A property graph with the same name is already defined. "
      "Please use a different name")));
  GOOGLESQL_RETURN_IF_ERROR((ValidateCreationAgainstCatalog<Table>(
      ast_stmt->name(), *create_property_graph_stmt, *resolver_.catalog_,
      resolver_.analyzer_options_.find_options(),
      "The property graph's name is used by a table under the same name. "
      "Please use a different name")));
  *output = absl::WrapUnique(const_cast<ResolvedCreatePropertyGraphStmt*>(
      create_property_graph_stmt.release()));
  return absl::OkStatus();
}

absl::StatusOr<GraphDdlResolver::ElementTypeWithLabelAndProperties>
GraphDdlResolver::ResolveGraphElementType(
    const ASTGraphElementType* ast_element_type,
    PropertyGraphElementType::Kind element_kind) const {
  GOOGLESQL_RET_CHECK_NE(ast_element_type, nullptr);
  GOOGLESQL_RET_CHECK_NE(ast_element_type->name(), nullptr);
  const std::string element_type_name = ast_element_type->name()->GetAsString();

  // Resolve the property declarations exposed by this element type's default
  // label (whose name is the element type name).
  std::vector<std::unique_ptr<PropertyDeclarationWithIsMeasure>> property_decls;
  std::vector<std::string> property_declaration_names;
  StringViewHashSetCase local_property_names;
  if (ast_element_type->property_list() != nullptr) {
    const auto& ast_property_declarations =
        ast_element_type->property_list()->property_declarations();
    property_decls.reserve(ast_property_declarations.size());
    property_declaration_names.reserve(ast_property_declarations.size());
    for (const ASTGraphPropertyDeclaration* ast_property :
         ast_property_declarations) {
      GOOGLESQL_RET_CHECK_NE(ast_property->name(), nullptr);
      const std::string property_name = ast_property->name()->GetAsString();
      if (!local_property_names.insert(ast_property->name()->GetAsStringView())
               .second) {
        return MakeSqlErrorAt(ast_property->name())
               << "Duplicate property name "
               << ToSingleQuotedStringLiteral(property_name)
               << " in element type "
               << ToSingleQuotedStringLiteral(element_type_name);
      }
      // Property-level OPTIONS clause is not supported in
      // ResolvedGraphPropertyDeclaration yet.
      if (ast_property->options_list() != nullptr) {
        return MakeSqlErrorAt(ast_property->options_list())
               << "OPTIONS clause is not supported for the graph type property "
                  "declaration";
      }
      const Type* property_type = nullptr;
      TypeModifiers property_type_modifiers;
      GOOGLESQL_RETURN_IF_ERROR(
          resolver_.ResolveType(ast_property->type(),
                                {.allow_type_parameters = false,
                                 .allow_collation = false,
                                 .context = "graph type property declarations"},
                                &property_type, &property_type_modifiers));
      // Measure-typed properties are not allowed in a graph type property
      // declaration. `ResolveType` above already rejects them today, but that
      // rejection is incidental to measure support being unimplemented; guard
      // explicitly so a future `ResolveType` that resolves measure types cannot
      // silently flow one through here as a plain (is_measure=false) property.
      if (property_type->IsMeasureType()) {
        return MakeSqlErrorAt(ast_property->type())
               << "Measure-typed properties are not supported in a graph type "
                  "property declaration";
      }
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::unique_ptr<const ResolvedGraphPropertyDeclaration> property_decl,
          ResolvedGraphPropertyDeclarationBuilder()
              .set_name(property_name)
              .set_type(property_type)
              .Build());
      property_decls.push_back(
          std::make_unique<PropertyDeclarationWithIsMeasure>(
              GetResolvedElementWithLocation(std::move(property_decl),
                                             ast_property->location()),
              /*is_measure=*/false));
      property_declaration_names.push_back(property_name);
    }
  }

  // Default-label OPTIONS have no place in this statement yet: the resolved
  // shape carries them on the label, but neither the validator (without the
  // feature) nor the SQLBuilder round-trip can handle a label option list, so
  // resolving them would produce an AST that fails downstream. Reject them
  // here, mirroring the property-level OPTIONS rejection above.
  if (ast_element_type->default_label_options_list() != nullptr) {
    return MakeSqlErrorAt(ast_element_type->default_label_options_list())
           << "OPTIONS clause is not supported for the graph type default "
              "label";
  }

  // Build the default label: its name is the element type name and it exposes
  // exactly the property declarations resolved above. A property graph element
  // type is not allowed to have explicit labels, so this default label (named
  // after the element type) is the only label the element type ever carries.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedGraphElementLabel> label,
                   ResolvedGraphElementLabelBuilder()
                       .set_name(element_type_name)
                       .set_property_declaration_name_list(
                           std::move(property_declaration_names))
                       .Build());
  label = GetResolvedElementWithLocation(std::move(label),
                                         ast_element_type->name()->location());

  // Resolve FROM/TO node type references. These are only valid on edge types,
  // where an edge type may carry zero, one, or two of them: an omitted endpoint
  // means any node type is allowed there (see the builder below). The grammar
  // shares the element type production between node and edge types, so a
  // FROM/TO clause can syntactically appear on a node type; reject that here
  // with a user-facing error rather than treating it as an internal bug.
  std::string source_node_type;
  std::string dest_node_type;
  if (element_kind == PropertyGraphElementType::Kind::kEdge) {
    for (const ASTGraphNodeTypeReference* ref :
         ast_element_type->node_type_references()) {
      GOOGLESQL_RET_CHECK_NE(ref->node_type_name(), nullptr);
      const std::string node_type_name = ref->node_type_name()->GetAsString();
      switch (ref->node_reference_type()) {
        case ASTGraphNodeTypeReference::SOURCE:
          source_node_type = node_type_name;
          break;
        case ASTGraphNodeTypeReference::DESTINATION:
          dest_node_type = node_type_name;
          break;
        default:
          return MakeSqlErrorAt(ref)
                 << "Unexpected FROM/TO node type reference kind on edge type "
                 << ToSingleQuotedStringLiteral(element_type_name);
      }
    }
  } else if (!ast_element_type->node_type_references().empty()) {
    return MakeSqlErrorAt(ast_element_type->node_type_references().front())
           << "FROM/TO node type references are not allowed on node type "
           << ToSingleQuotedStringLiteral(element_type_name);
  }

  // An edge type may declare FROM, TO, both, or neither. An omitted endpoint is
  // left unset on the resolved element type, which means any node type is
  // allowed for that endpoint; FROM-only, TO-only, and neither are all valid.
  ResolvedGraphElementTypeBuilder builder;
  builder.set_name(element_type_name).set_label_name_list({element_type_name});
  if (!source_node_type.empty()) {
    builder.set_source_node_type(source_node_type);
  }
  if (!dest_node_type.empty()) {
    builder.set_dest_node_type(dest_node_type);
  }
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedGraphElementType> element_type,
                   std::move(builder).Build());

  return ElementTypeWithLabelAndProperties{
      .element_type = std::move(element_type),
      .label = std::move(label),
      .property_decls = std::move(property_decls)};
}

namespace {

// Moves a resolved element type, its default label, and its property
// declarations into the statement-level accumulators `element_types`, `labels`,
// and `property_decls`.
void CollectElementType(
    std::unique_ptr<const ResolvedGraphElementType> element_type,
    std::unique_ptr<const ResolvedGraphElementLabel> label,
    std::vector<
        std::unique_ptr<GraphDdlResolver::PropertyDeclarationWithIsMeasure>>
        resolved_property_decls,
    std::vector<std::unique_ptr<const ResolvedGraphElementType>>& element_types,
    std::vector<std::unique_ptr<const ResolvedGraphElementLabel>>& labels,
    std::vector<
        std::unique_ptr<GraphDdlResolver::PropertyDeclarationWithIsMeasure>>&
        property_decls) {
  element_types.push_back(std::move(element_type));
  labels.push_back(std::move(label));
  property_decls.insert(
      property_decls.end(),
      std::make_move_iterator(resolved_property_decls.begin()),
      std::make_move_iterator(resolved_property_decls.end()));
}

}  // namespace

absl::Status GraphDdlResolver::ResolveCreatePropertyGraphTypeStmt(
    const ASTCreatePropertyGraphTypeStatement* ast_stmt,
    std::unique_ptr<ResolvedStatement>* output) const {
  GOOGLESQL_RET_CHECK_NE(ast_stmt, nullptr);

  ResolvedCreateStatement::CreateScope create_scope;
  ResolvedCreateStatement::CreateMode create_mode;
  GOOGLESQL_RETURN_IF_ERROR(resolver_.ResolveCreateStatementOptions(
      ast_stmt, "CREATE PROPERTY GRAPH TYPE", &create_scope, &create_mode));

  std::vector<std::unique_ptr<const ResolvedOption>> option_list;
  if (ast_stmt->options_list() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(resolver_.ResolveOptionsList(
        ast_stmt->options_list(), /*allow_alter_array_operators=*/false,
        &option_list));
  }

  std::vector<std::unique_ptr<const ResolvedGraphElementType>> node_types;
  std::vector<std::unique_ptr<const ResolvedGraphElementType>> edge_types;
  std::vector<std::unique_ptr<const ResolvedGraphElementLabel>> labels;
  std::vector<std::unique_ptr<PropertyDeclarationWithIsMeasure>> property_decls;

  // Element type names are unique across both node and edge types. Node type
  // names are tracked separately so edge FROM/TO references can be validated
  // against the full set of declared node types.
  StringViewHashSetCase element_type_names;
  StringViewHashSetCase node_type_names;

  // Resolve node types first so that edge FROM/TO references can be resolved
  // against the complete set of node type names.
  GOOGLESQL_RET_CHECK_NE(ast_stmt->node_type_list(), nullptr);
  for (const ASTGraphElementType* ast_node_type :
       ast_stmt->node_type_list()->element_types()) {
    if (!element_type_names.insert(ast_node_type->name()->GetAsStringView())
             .second) {
      return MakeSqlErrorAt(ast_node_type->name())
             << "Duplicate element type name "
             << ToSingleQuotedStringLiteral(
                    ast_node_type->name()->GetAsString());
    }
    node_type_names.insert(ast_node_type->name()->GetAsStringView());
    GOOGLESQL_ASSIGN_OR_RETURN(ElementTypeWithLabelAndProperties resolved_node,
                     ResolveGraphElementType(
                         ast_node_type, PropertyGraphElementType::Kind::kNode));
    CollectElementType(std::move(resolved_node.element_type),
                       std::move(resolved_node.label),
                       std::move(resolved_node.property_decls), node_types,
                       labels, property_decls);
  }

  // Resolve edge types, if any.
  if (ast_stmt->edge_type_list() != nullptr) {
    for (const ASTGraphElementType* ast_edge_type :
         ast_stmt->edge_type_list()->element_types()) {
      if (!element_type_names.insert(ast_edge_type->name()->GetAsStringView())
               .second) {
        return MakeSqlErrorAt(ast_edge_type->name())
               << "Duplicate element type name "
               << ToSingleQuotedStringLiteral(
                      ast_edge_type->name()->GetAsString());
      }
      // Validate FROM/TO references against the declared node types before
      // building, so the user-facing error points at the offending reference.
      for (const ASTGraphNodeTypeReference* ref :
           ast_edge_type->node_type_references()) {
        if (!node_type_names.contains(
                ref->node_type_name()->GetAsStringView())) {
          return MakeSqlErrorAt(ref->node_type_name())
                 << "Edge type "
                 << ToSingleQuotedStringLiteral(
                        ast_edge_type->name()->GetAsString())
                 << " references undefined node type "
                 << ToSingleQuotedStringLiteral(
                        ref->node_type_name()->GetAsString());
        }
      }
      GOOGLESQL_ASSIGN_OR_RETURN(
          ElementTypeWithLabelAndProperties resolved_edge,
          ResolveGraphElementType(ast_edge_type,
                                  PropertyGraphElementType::Kind::kEdge));
      CollectElementType(std::move(resolved_edge.element_type),
                         std::move(resolved_edge.label),
                         std::move(resolved_edge.property_decls), edge_types,
                         labels, property_decls);
    }
  }

  // De-duplicate property declarations at graph-type scope: a property name
  // reused across element types must resolve to a consistent type.
  GOOGLESQL_RETURN_IF_ERROR(
      Dedupe(property_decls, &PropertyDeclarationWithIsMeasure::name));
  // Labels are keyed by their (element type) name, which is already unique
  // across the statement; Dedupe keeps the collection canonical.
  GOOGLESQL_RETURN_IF_ERROR(Dedupe(labels, &ResolvedGraphElementLabel::name));

  std::vector<std::unique_ptr<const ResolvedGraphPropertyDeclaration>>
      property_declarations;
  property_declarations.reserve(property_decls.size());
  absl::c_transform(
      property_decls, std::back_inserter(property_declarations),
      [](auto& property_decl) { return property_decl->release_declaration(); });

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<const ResolvedCreatePropertyGraphTypeStmt>
          create_property_graph_type_stmt,
      ResolvedCreatePropertyGraphTypeStmtBuilder()
          .set_name_path(ast_stmt->name()->ToIdentifierVector())
          .set_create_scope(create_scope)
          .set_create_mode(create_mode)
          .set_node_type_list(std::move(node_types))
          .set_edge_type_list(std::move(edge_types))
          .set_label_list(std::move(labels))
          .set_property_declaration_list(std::move(property_declarations))
          .set_option_list(std::move(option_list))
          .Build());

  // Validates name collision. Property graph types share a single catalog
  // namespace with property graphs and tables (see
  // SimpleCatalog::global_names_), so the name must be unique across all three;
  // otherwise catalog registration would fail. Mirrors the checks in
  // ResolveCreatePropertyGraphStmt.
  //
  // Only the same-kind check honors CREATE OR REPLACE / CREATE IF NOT EXISTS: a
  // graph type may replace/coexist with an existing graph type. A name already
  // used by a property graph or a table is a cross-kind clash that those
  // modifiers cannot satisfy, so it is rejected regardless of create mode.
  GOOGLESQL_RETURN_IF_ERROR((ValidateCreationAgainstCatalog<PropertyGraphType>(
      ast_stmt->name(), *create_property_graph_type_stmt, *resolver_.catalog_,
      resolver_.analyzer_options_.find_options(),
      "A property graph type with the same name is already defined. "
      "Please use a different name")));
  GOOGLESQL_RETURN_IF_ERROR((ValidateNoCrossKindCollision<PropertyGraph>(
      ast_stmt->name(), *create_property_graph_type_stmt, *resolver_.catalog_,
      resolver_.analyzer_options_.find_options(),
      "The property graph type's name is used by a property graph under the "
      "same name. Please use a different name")));
  GOOGLESQL_RETURN_IF_ERROR((ValidateNoCrossKindCollision<Table>(
      ast_stmt->name(), *create_property_graph_type_stmt, *resolver_.catalog_,
      resolver_.analyzer_options_.find_options(),
      "The property graph type's name is used by a table under the same name. "
      "Please use a different name")));

  *output = absl::WrapUnique(const_cast<ResolvedCreatePropertyGraphTypeStmt*>(
      create_property_graph_type_stmt.release()));
  return absl::OkStatus();
}

absl::Status GraphDdlResolver::ResolveGraphMeasurePropertyRecursive(
    const ASTGraphDerivedProperty* property, const NameScope* input_scope,
    CaseInsensitiveStringLinkedHashSet& visited,
    const CaseInsensitiveStringHashMap<const ASTGraphDerivedProperty*>&
        measure_properties,
    ResolvedGraphPropertyDefinitionMap& resolved_properties) const {
  GOOGLESQL_ASSIGN_OR_RETURN(std::string measure_name,
                   GetPropertyDeclarationName(property));

  if (resolved_properties.contains(property)) {
    return absl::OkStatus();
  }

  if (visited.contains(measure_name)) {
    if (visited.size() == 1) {
      return MakeSqlErrorAt(property)
             << "The column " << measure_name << " is recursive";
    }
    std::string visited_str = absl::StrJoin(visited, ", ");
    return MakeSqlErrorAt(property)
           << "Recursive dependencies detected when resolving column "
           << *visited.begin() << ", which include objects (" << visited_str
           << ")";
  }

  visited.insert(measure_name);
  auto cleanup_visited = absl::MakeCleanup(
      [&visited, &measure_name] { visited.erase(measure_name); });

  if (visited.size() > kMaxGraphMeasureRecursionDepth) {
    return MakeSqlErrorAt(property)
           << "Graph measure dependency chain is too deep: " << visited.size()
           << " (limit: " << kMaxGraphMeasureRecursionDepth << ")";
  }

  static constexpr char kPropertiesClause[] = "PROPERTIES clause";

  int dependency_count = 0;
  while (true) {
    if (++dependency_count > kMaxGraphMeasureDependencies) {
      return MakeSqlErrorAt(property) << "Graph measure " << measure_name
                                      << " has too many dependencies (limit: "
                                      << kMaxGraphMeasureDependencies << ")";
    }
    resolver_.unrecognized_graph_measure_name_.clear();

    auto query_resolution_info =
        std::make_unique<QueryResolutionInfo>(&resolver_);
    auto measure_expr_resolution_info = std::make_unique<ExprResolutionInfo>(
        query_resolution_info.get(), input_scope,
        ExprResolutionInfoOptions{
            .allows_aggregation = true,
            .allows_analytic = false,
            .clause_name = kPropertiesClause,
            .is_graph_measure_expression = true,
        });

    absl::StatusOr<std::unique_ptr<const ResolvedGraphPropertyDefinition>>
        property_def_status =
            ResolveGraphProperty(property, measure_expr_resolution_info.get());

    if (property_def_status.ok()) {
      std::unique_ptr<const ResolvedGraphPropertyDefinition> property_def =
          std::move(property_def_status).value();

      // Validate the resolved measure expression.
      GOOGLESQL_RETURN_IF_ERROR(
          ValidateMeasureExpression(property_def->sql(), *property_def->expr(),
                                    resolver_.language(),
                                    property_def->property_declaration_name()))
          .With(LocationOverride(property));

      // Register the resolved type in options for subsequent derived measures.
      GOOGLESQL_ASSIGN_OR_RETURN(const Type* measure_type,
                       resolver_.type_factory()->MakeMeasureType(
                           property_def->expr()->type()));
      bool is_x_as_x = false;
      if (property_def->expr()->Is<ResolvedColumnRef>()) {
        auto* ref = property_def->expr()->GetAs<ResolvedColumnRef>();
        if (googlesql_base::CaseEqual(ref->column().name(),
                                   property_def->property_declaration_name())) {
          is_x_as_x = true;
        }
      }
      GOOGLESQL_RETURN_IF_ERROR(InternalAnalyzerOptions::AddGraphProperty(
                          resolver_.analyzer_options(),
                          property_def->property_declaration_name(),
                          measure_type, is_x_as_x))
          .With(LocationOverride(property));

      resolved_properties[property] = GetResolvedElementWithLocation(
          std::move(property_def), property->location());
      return absl::OkStatus();
    }

    // If there is no unrecognized name recorded, it is a real error, so return
    // it.
    if (resolver_.unrecognized_graph_measure_name_.empty()) {
      return property_def_status.status();
    }

    // Find the dependency in the list of measure properties.
    const ASTGraphDerivedProperty* const* dependency_property =
        googlesql_base::FindOrNull(measure_properties,
                        resolver_.unrecognized_graph_measure_name_.ToString());
    if (dependency_property == nullptr) {
      // The unrecognized name is not a measure property declared, so it cannot
      // be an unresolved forward reference. Return the original resolution
      // error.
      return property_def_status.status();
    }

    // Recursively resolve the dependency.
    GOOGLESQL_RETURN_IF_ERROR(ResolveGraphMeasurePropertyRecursive(
        *dependency_property, input_scope, visited, measure_properties,
        resolved_properties));
  }
}

}  // namespace googlesql
