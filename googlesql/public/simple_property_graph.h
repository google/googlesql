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

#ifndef GOOGLESQL_PUBLIC_SIMPLE_PROPERTY_GRAPH_H_
#define GOOGLESQL_PUBLIC_SIMPLE_PROPERTY_GRAPH_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/proto/simple_property_graph.pb.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/type_deserializer.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/status_builder.h"

namespace googlesql {
class SimpleGraphPropertyDeclaration;
class SimpleGraphElementLabel;
class ElementTableCommonInternal;

class SimplePropertyGraph : public PropertyGraph {
 public:
  explicit SimplePropertyGraph(std::vector<std::string> name_path)
      : name_path_(std::move(name_path)) {}

  SimplePropertyGraph(
      std::vector<std::string> name_path,
      std::vector<std::unique_ptr<const GraphNodeTable>> node_tables,
      std::vector<std::unique_ptr<const GraphEdgeTable>> edge_tables,
      std::vector<std::unique_ptr<const GraphElementLabel>> labels,
      std::vector<std::unique_ptr<const GraphPropertyDeclaration>>
          property_declarations);

  absl::Span<const std::string> NamePath() const override { return name_path_; }

  absl::Status FindLabelByName(absl::string_view name,
                               const GraphElementLabel*& label) const override;

  absl::Status FindPropertyDeclarationByName(
      absl::string_view name,
      const GraphPropertyDeclaration*& property_declaration) const override;

  absl::Status FindElementTableByName(
      absl::string_view name,
      const GraphElementTable*& element_table) const override;

  absl::Status GetNodeTables(
      absl::flat_hash_set<const GraphNodeTable*>& output) const override;

  absl::Status GetEdgeTables(
      absl::flat_hash_set<const GraphEdgeTable*>& output) const override;

  absl::Status GetLabels(
      absl::flat_hash_set<const GraphElementLabel*>& output) const override;

  absl::Status GetPropertyDeclarations(
      absl::flat_hash_set<const GraphPropertyDeclaration*>& output)
      const override;

  // Add a node table.
  void AddNodeTable(std::unique_ptr<const GraphNodeTable> node_table) {
    node_tables_map_.try_emplace(absl::AsciiStrToLower(node_table->Name()),
                                 std::move(node_table));
  }

  // Add an edge table.
  void AddEdgeTable(std::unique_ptr<const GraphEdgeTable> edge_table) {
    edge_tables_map_.try_emplace(absl::AsciiStrToLower(edge_table->Name()),
                                 std::move(edge_table));
  }

  // Add a label.
  void AddLabel(std::unique_ptr<const GraphElementLabel> label) {
    labels_map_.try_emplace(absl::AsciiStrToLower(label->Name()),
                            std::move(label));
  }

  // Add a label.
  void AddPropertyDeclaration(
      std::unique_ptr<const GraphPropertyDeclaration> property_declaration) {
    property_dcls_map_.try_emplace(
        absl::AsciiStrToLower(property_declaration->Name()),
        std::move(property_declaration));
  }

  absl::Status Serialize(FileDescriptorSetMap* file_descriptor_set_map,
                         SimplePropertyGraphProto* proto) const;

  static absl::StatusOr<std::unique_ptr<SimplePropertyGraph>> Deserialize(
      const SimplePropertyGraphProto& proto,
      const TypeDeserializer& type_deserializer, SimpleCatalog* catalog);

 private:
  const std::vector<std::string> name_path_;
  absl::flat_hash_map<std::string,
                      std::unique_ptr<const GraphPropertyDeclaration>>
      property_dcls_map_;
  absl::flat_hash_map<std::string, std::unique_ptr<const GraphElementLabel>>
      labels_map_;
  absl::flat_hash_map<std::string, std::unique_ptr<const GraphNodeTable>>
      node_tables_map_;
  absl::flat_hash_map<std::string, std::unique_ptr<const GraphEdgeTable>>
      edge_tables_map_;
};

class SimpleGraphNodeTable : public GraphNodeTable {
 public:
  SimpleGraphNodeTable(
      absl::string_view name,
      absl::Span<const std::string> property_graph_name_path,
      const Table* input_table, const std::vector<int>& key_cols,
      const absl::flat_hash_set<const GraphElementLabel*>& labels,
      std::vector<std::unique_ptr<const GraphPropertyDefinition>>
          property_definitions,
      std::unique_ptr<const GraphDynamicLabel> dynamic_label = nullptr,
      std::unique_ptr<const GraphDynamicProperties> dynamic_properties = nullptr
  );

  ~SimpleGraphNodeTable() override;

  std::string Name() const override;
  absl::Span<const std::string> PropertyGraphNamePath() const override;

  Kind kind() const override {
    return googlesql::GraphElementTable::Kind::kNode;
  }

  const Table* GetTable() const override;

  const std::vector<int>& GetKeyColumns() const override;

  absl::Status FindPropertyDefinitionByName(
      absl::string_view property_name,
      const GraphPropertyDefinition*& property_definition) const override;

  absl::Status GetPropertyDefinitions(
      absl::flat_hash_set<const GraphPropertyDefinition*>& output)
      const override;

  absl::Status FindLabelByName(absl::string_view name,
                               const GraphElementLabel*& label) const override;

  absl::Status GetLabels(
      absl::flat_hash_set<const GraphElementLabel*>& output) const override;

  bool HasDynamicLabel() const override;
  absl::Status GetDynamicLabel(
      const GraphDynamicLabel*& dynamic_label) const override;
  enum GraphElementTable::DynamicLabelCardinality DynamicLabelCardinality()
      const override;

  bool HasDynamicProperties() const override;
  absl::Status GetDynamicProperties(
      const GraphDynamicProperties*& dynamic_properties) const override;

  absl::Status Serialize(FileDescriptorSetMap* file_descriptor_set_map,
                         SimpleGraphElementTableProto* proto) const;

  static absl::StatusOr<std::unique_ptr<SimpleGraphNodeTable>> Deserialize(
      const SimpleGraphElementTableProto& proto, SimpleCatalog* catalog,
      const TypeDeserializer& type_deserializer,
      const absl::flat_hash_map<std::string, const SimpleGraphElementLabel*>&
          labels,
      const absl::flat_hash_map<std::string,
                                const SimpleGraphPropertyDeclaration*>&
          property_declarations);

 private:
  const std::unique_ptr<const ElementTableCommonInternal> element_internal_;
};

class SimpleGraphEdgeTable : public GraphEdgeTable {
 public:
  SimpleGraphEdgeTable(
      absl::string_view name,
      absl::Span<const std::string> property_graph_name_path,
      const Table* input_table, const std::vector<int>& key_cols,
      const absl::flat_hash_set<const GraphElementLabel*>& labels,
      std::vector<std::unique_ptr<const GraphPropertyDefinition>>
          property_definitions,
      std::unique_ptr<const GraphNodeTableReference> source_node,
      std::unique_ptr<const GraphNodeTableReference> destination_node,
      std::unique_ptr<const GraphDynamicLabel> dynamic_label = nullptr,
      std::unique_ptr<const GraphDynamicProperties> dynamic_properties = nullptr
  );

  ~SimpleGraphEdgeTable() override;

  std::string Name() const override;
  absl::Span<const std::string> PropertyGraphNamePath() const override;

  Kind kind() const override {
    return googlesql::GraphElementTable::Kind::kEdge;
  }

  const Table* GetTable() const override;

  const std::vector<int>& GetKeyColumns() const override;

  absl::Status FindPropertyDefinitionByName(
      absl::string_view property_name,
      const GraphPropertyDefinition*& property_definition) const override;

  absl::Status GetPropertyDefinitions(
      absl::flat_hash_set<const GraphPropertyDefinition*>& output)
      const override;

  absl::Status FindLabelByName(absl::string_view name,
                               const GraphElementLabel*& label) const override;

  absl::Status GetLabels(
      absl::flat_hash_set<const GraphElementLabel*>& output) const override;

  const GraphNodeTableReference* GetSourceNodeTable() const override;

  const GraphNodeTableReference* GetDestNodeTable() const override;

  absl::Status Serialize(FileDescriptorSetMap* file_descriptor_set_map,
                         SimpleGraphElementTableProto* proto) const;

  bool HasDynamicLabel() const override;
  absl::Status GetDynamicLabel(
      const GraphDynamicLabel*& dynamic_label) const override;
  enum GraphElementTable::DynamicLabelCardinality DynamicLabelCardinality()
      const override;

  bool HasDynamicProperties() const override;
  absl::Status GetDynamicProperties(
      const GraphDynamicProperties*& dynamic_properties) const override;
  static absl::StatusOr<std::unique_ptr<SimpleGraphEdgeTable>> Deserialize(
      const SimpleGraphElementTableProto& proto, SimpleCatalog* catalog,
      const TypeDeserializer& type_deserializer,
      const absl::flat_hash_map<std::string, const SimpleGraphElementLabel*>&
          labels,
      const absl::flat_hash_map<std::string,
                                const SimpleGraphPropertyDeclaration*>&
          property_declarations,
      const SimpleGraphNodeTable* source_node,
      const SimpleGraphNodeTable* dest_node);

 private:
  const std::unique_ptr<const ElementTableCommonInternal> element_internal_;
  const std::unique_ptr<const GraphNodeTableReference> source_node_;
  const std::unique_ptr<const GraphNodeTableReference> destination_node_;
};

class SimpleGraphElementLabel : public GraphElementLabel {
 public:
  SimpleGraphElementLabel(
      absl::string_view name,
      absl::Span<const std::string> property_graph_name_path,
      const absl::flat_hash_set<const GraphPropertyDeclaration*>&
          property_declarations)
      : name_(name),
        property_graph_name_path_(property_graph_name_path.begin(),
                                  property_graph_name_path.end()),
        property_declarations_(property_declarations) {}

  std::string Name() const override;
  absl::Span<const std::string> PropertyGraphNamePath() const override;

  absl::Status GetPropertyDeclarations(
      absl::flat_hash_set<const GraphPropertyDeclaration*>& output)
      const override {
    output.reserve(property_declarations_.size());
    for (auto property_declaration : property_declarations_) {
      output.insert(property_declaration);
    }
    return absl::OkStatus();
  }

  absl::Status Serialize(SimpleGraphElementLabelProto* proto) const;

  static absl::StatusOr<std::unique_ptr<SimpleGraphElementLabel>> Deserialize(
      const SimpleGraphElementLabelProto& proto,
      const absl::flat_hash_map<std::string,
                                const SimpleGraphPropertyDeclaration*>&
          unowned_property_declarations);

 private:
  const std::string name_;
  const std::vector<std::string> property_graph_name_path_;
  const absl::flat_hash_set<const GraphPropertyDeclaration*>
      property_declarations_;
};

class SimpleGraphNodeTableReference : public GraphNodeTableReference {
 public:
  SimpleGraphNodeTableReference(const GraphNodeTable* referenced_node_table,
                                std::vector<int> edge_table_columns,
                                std::vector<int> node_table_columns)
      : table_(referenced_node_table),
        edge_table_columns_(edge_table_columns),
        node_table_columns_(node_table_columns) {}

  const GraphNodeTable* GetReferencedNodeTable() const override {
    return table_;
  }

  const std::vector<int>& GetEdgeTableColumns() const override {
    return edge_table_columns_;
  }

  const std::vector<int>& GetNodeTableColumns() const override {
    return node_table_columns_;
  }

  absl::Status Serialize(SimpleGraphNodeTableReferenceProto* proto) const;

  static absl::StatusOr<std::unique_ptr<SimpleGraphNodeTableReference>>
  Deserialize(const SimpleGraphNodeTableReferenceProto& proto,
              const SimpleGraphNodeTable* referenced_node_table);

 private:
  const GraphNodeTable* table_;
  const std::vector<int> edge_table_columns_;
  const std::vector<int> node_table_columns_;
};

class SimpleGraphPropertyDeclaration : public GraphPropertyDeclaration {
 public:
  ABSL_DEPRECATED("Use the constructor with type annotation map instead.")
  SimpleGraphPropertyDeclaration(
      absl::string_view name,
      absl::Span<const std::string> property_graph_name_path,
      const class Type* p_type)
      : SimpleGraphPropertyDeclaration(
            name, property_graph_name_path, p_type,
            /*type_annotation_map=*/nullptr,
            GraphPropertyDeclaration::Kind::kScalar) {}

  SimpleGraphPropertyDeclaration(
      absl::string_view name,
      absl::Span<const std::string> property_graph_name_path,
      const class Type* p_type, const AnnotationMap* type_annotation_map,
      GraphPropertyDeclaration::Kind kind)
      : name_(name),
        kind_(kind),
        property_graph_name_path_(property_graph_name_path.begin(),
                                  property_graph_name_path.end()),
        type_(p_type),
        type_annotation_map_(type_annotation_map) {}

  std::string Name() const override;
  GraphPropertyDeclaration::Kind kind() const override;
  absl::Span<const std::string> PropertyGraphNamePath() const override;

  const class Type* Type() const override { return type_; }

  const AnnotationMap* TypeAnnotationMap() const override {
    return type_annotation_map_;
  }

  absl::Status Serialize(FileDescriptorSetMap* file_descriptor_set_map,
                         SimpleGraphPropertyDeclarationProto* proto) const;

  static absl::StatusOr<std::unique_ptr<SimpleGraphPropertyDeclaration>>
  Deserialize(const SimpleGraphPropertyDeclarationProto& proto,
              const TypeDeserializer& type_deserializer);

 private:
  const std::string name_;
  GraphPropertyDeclaration::Kind kind_;
  const std::vector<std::string> property_graph_name_path_;
  const class Type* type_;
  const AnnotationMap* type_annotation_map_;
};

class SimpleGraphPropertyDefinition : public GraphPropertyDefinition {
 public:
  SimpleGraphPropertyDefinition(
      const GraphPropertyDeclaration* property_declaration,
      absl::string_view expr_sql)
      : property_declaration_(property_declaration),
        expression_sql_(expr_sql) {}

  const GraphPropertyDeclaration& GetDeclaration() const override {
    return *property_declaration_;
  }

  absl::StatusOr<const ResolvedExpr*> GetValueExpression() const override {
    if (resolved_expr_ == nullptr) {
      return googlesql_base::UnimplementedErrorBuilder()
             << "SimpleCatalog does not resolve the property definitions.";
    }

    return resolved_expr_;
  }

  // The returned string_view is only valid as long as this object is alive.
  absl::string_view expression_sql() const override { return expression_sql_; }

  absl::Status Serialize(FileDescriptorSetMap* file_descriptor_set_map,
                         SimpleGraphPropertyDefinitionProto* proto) const;

  static absl::StatusOr<std::unique_ptr<SimpleGraphPropertyDefinition>>
  Deserialize(const SimpleGraphPropertyDefinitionProto& proto,
              const ResolvedNode::RestoreParams& params,
              const absl::flat_hash_map<std::string,
                                        const SimpleGraphPropertyDeclaration*>&
                  unowned_property_declarations);

 private:
  // The property declaration implemented here. Owned by the same catalog.
  const GraphPropertyDeclaration* property_declaration_;
  const std::string expression_sql_;

  // This is just a little hack used in compliance testing to avoid duplicating
  // the deserialization code.
  // TODO: remove this field once we are able to store unique_ptrs on
  // the AnalyzerOutput.
  const ResolvedExpr* resolved_expr_;
  friend class InternalPropertyGraph;
};
class SimpleGraphDynamicLabel : public GraphDynamicLabel {
 public:
  explicit SimpleGraphDynamicLabel(absl::string_view label_expression)
      : label_expression_(label_expression) {};

  absl::string_view label_expression() const override {
    return label_expression_;
  }

  absl::StatusOr<const ResolvedExpr*> GetValueExpression() const override {
    if (resolved_expr_ == nullptr) {
      return googlesql_base::UnimplementedErrorBuilder()
             << "SimpleCatalog does not resolve the dynamic label.";
    }
    return resolved_expr_;
  };

  template <class GraphDynamicLabelSubclass>
  bool Is() const {
    return dynamic_cast<const GraphDynamicLabelSubclass*>(this) != nullptr;
  }
  template <class GraphDynamicLabelSubclass>
  const GraphDynamicLabelSubclass* GetAs() const {
    return static_cast<const GraphDynamicLabelSubclass*>(this);
  }

  absl::Status Serialize(SimpleGraphElementDynamicLabelProto* proto) const;

  static absl::StatusOr<std::unique_ptr<SimpleGraphDynamicLabel>> Deserialize(
      const SimpleGraphElementDynamicLabelProto& proto);

 private:
  const std::string label_expression_;
  const ResolvedExpr* resolved_expr_;
  friend class InternalPropertyGraph;
};

class SimpleGraphDynamicProperties : public GraphDynamicProperties {
 public:
  explicit SimpleGraphDynamicProperties(absl::string_view properties_expression)
      : properties_expression_(properties_expression) {}

  absl::string_view properties_expression() const override {
    return properties_expression_;
  }

  absl::StatusOr<const ResolvedExpr*> GetValueExpression() const override {
    if (resolved_expr_ == nullptr) {
      return googlesql_base::UnimplementedErrorBuilder()
             << "SimpleCatalog does not resolve the dynamic properties.";
    }
    return resolved_expr_;
  };

  template <class GraphDynamicPropertiesSubclass>
  bool Is() const {
    return dynamic_cast<const GraphDynamicPropertiesSubclass*>(this) != nullptr;
  }
  template <class GraphDynamicPropertiesSubclass>
  const GraphDynamicPropertiesSubclass* GetAs() const {
    return static_cast<const GraphDynamicPropertiesSubclass*>(this);
  }

  absl::Status Serialize(SimpleGraphElementDynamicPropertiesProto* proto) const;

  static absl::StatusOr<std::unique_ptr<SimpleGraphDynamicProperties>>
  Deserialize(const SimpleGraphElementDynamicPropertiesProto& proto);

 private:
  const std::string properties_expression_;
  const ResolvedExpr* resolved_expr_;
  friend class InternalPropertyGraph;
};
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_SIMPLE_PROPERTY_GRAPH_H_
