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

#include "googlesql/public/simple_property_graph.h"

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/type.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast.pb.h"
#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

template <typename T>
static std::vector<T> ToVector(const google::protobuf::RepeatedPtrField<T>& proto_field) {
  return std::vector<T>(proto_field.begin(), proto_field.end());
}

SimplePropertyGraph::SimplePropertyGraph(
    std::vector<std::string> name_path,
    std::vector<std::unique_ptr<const GraphNodeTable>> node_tables,
    std::vector<std::unique_ptr<const GraphEdgeTable>> edge_tables,
    std::vector<std::unique_ptr<const GraphElementLabel>> labels,
    std::vector<std::unique_ptr<const GraphPropertyDeclaration>>
        property_declarations)
    : name_path_(std::move(name_path)) {
  for (auto& node_table : node_tables) {
    AddNodeTable(std::move(node_table));
  }
  for (auto& edge_table : edge_tables) {
    AddEdgeTable(std::move(edge_table));
  }
  for (auto& label : labels) {
    AddLabel(std::move(label));
  }
  for (auto& property_declaration : property_declarations) {
    AddPropertyDeclaration(std::move(property_declaration));
  }
}

absl::Status SimplePropertyGraph::FindLabelByName(
    absl::string_view name, const GraphElementLabel*& label) const {
  label = nullptr;
  auto found = labels_map_.find(absl::AsciiStrToLower(name));
  if (found != labels_map_.end()) {
    label = found->second.get();
    return absl::OkStatus();
  }
  return absl::NotFoundError(absl::StrFormat("Label '%s' not found.", name));
}

absl::Status SimplePropertyGraph::FindElementTableByName(
    absl::string_view name, const GraphElementTable*& element_table) const {
  element_table = nullptr;
  const std::string lowercase_name = absl::AsciiStrToLower(name);
  auto node_itr = node_tables_map_.find(lowercase_name);
  if (node_itr != node_tables_map_.end()) {
    element_table = node_itr->second.get();
    return absl::OkStatus();
  }

  auto edge_itr = edge_tables_map_.find(lowercase_name);
  if (edge_itr != edge_tables_map_.end()) {
    element_table = edge_itr->second.get();
    return absl::OkStatus();
  }
  return absl::NotFoundError(
      absl::StrFormat("Element table '%s' not found.", name));
}

absl::Status SimplePropertyGraph::FindPropertyDeclarationByName(
    absl::string_view name,
    const GraphPropertyDeclaration*& property_declaration) const {
  property_declaration = nullptr;
  auto found = property_dcls_map_.find(absl::AsciiStrToLower(name));
  if (found != property_dcls_map_.end()) {
    property_declaration = found->second.get();
    return absl::OkStatus();
  }
  return absl::NotFoundError(
      absl::StrFormat("No declaration found for property '%s'.", name));
}

absl::Status SimplePropertyGraph::GetNodeTables(
    absl::flat_hash_set<const GraphNodeTable*>& output) const {
  GOOGLESQL_RET_CHECK(output.empty());
  output.reserve(node_tables_map_.size());
  for (const auto& pair : node_tables_map_) {
    output.emplace(pair.second.get());
  }
  return absl::OkStatus();
}

absl::Status SimplePropertyGraph::GetEdgeTables(
    absl::flat_hash_set<const GraphEdgeTable*>& output) const {
  GOOGLESQL_RET_CHECK(output.empty());
  output.reserve(edge_tables_map_.size());
  for (const auto& pair : edge_tables_map_) {
    output.emplace(pair.second.get());
  }
  return absl::OkStatus();
}

absl::Status SimplePropertyGraph::GetLabels(
    absl::flat_hash_set<const GraphElementLabel*>& output) const {
  GOOGLESQL_RET_CHECK(output.empty());
  output.reserve(labels_map_.size());
  for (const auto& pair : labels_map_) {
    output.emplace(pair.second.get());
  }
  return absl::OkStatus();
}

absl::Status SimplePropertyGraph::GetPropertyDeclarations(
    absl::flat_hash_set<const GraphPropertyDeclaration*>& output) const {
  GOOGLESQL_RET_CHECK(output.empty());
  for (const auto& pair : property_dcls_map_) {
    output.emplace(pair.second.get());
  }
  return absl::OkStatus();
}

absl::Status SimplePropertyGraph::Serialize(
    FileDescriptorSetMap* file_descriptor_set_map,
    SimplePropertyGraphProto* proto) const {
  proto->Clear();
  proto->set_name(Name());
  for (absl::string_view name : name_path_) {
    proto->add_name_path(name);
  }

  // Always keep ordered for serialization
  absl::btree_map<std::string, const GraphElementLabel*> labels_map;
  for (const auto& entry : labels_map_) {
    labels_map.try_emplace(entry.first, entry.second.get());
  }
  for (const auto& entry : labels_map) {
    absl::string_view name = entry.first;
    const auto& label = entry.second;
    if (!label->Is<SimpleGraphElementLabel>()) {
      return ::googlesql_base::UnknownErrorBuilder()
             << "Cannot serialize non-SimpleGraphElementLabel " << name;
    }
    GOOGLESQL_RETURN_IF_ERROR(label->GetAs<SimpleGraphElementLabel>()->Serialize(
        proto->add_labels()));
  }

  // Always keep ordered for serialization
  absl::btree_map<std::string, const GraphNodeTable*> node_tables_map;
  for (const auto& entry : node_tables_map_) {
    node_tables_map.try_emplace(entry.first, entry.second.get());
  }
  for (const auto& entry : node_tables_map) {
    absl::string_view name = entry.first;
    const auto& node_table = entry.second;
    if (!node_table->Is<SimpleGraphNodeTable>()) {
      return ::googlesql_base::UnknownErrorBuilder()
             << "Cannot serialize non-SimpleGraphNodeTable " << name;
    }
    GOOGLESQL_RETURN_IF_ERROR(node_table->GetAs<SimpleGraphNodeTable>()->Serialize(
        file_descriptor_set_map, proto->add_node_tables()));
  }

  // Always keep ordered for serialization
  absl::btree_map<std::string, const GraphEdgeTable*> edge_tables_map;
  for (const auto& entry : edge_tables_map_) {
    edge_tables_map.try_emplace(entry.first, entry.second.get());
  }
  for (const auto& entry : edge_tables_map) {
    absl::string_view name = entry.first;
    const auto edge_table = entry.second;
    if (!edge_table->Is<SimpleGraphEdgeTable>()) {
      return ::googlesql_base::UnknownErrorBuilder()
             << "Cannot serialize non-SimpleGraphEdgeTable " << name;
    }
    GOOGLESQL_RETURN_IF_ERROR(edge_table->GetAs<SimpleGraphEdgeTable>()->Serialize(
        file_descriptor_set_map, proto->add_edge_tables()));
  }

  // Always keep ordered for serialization
  absl::btree_map<std::string, const GraphPropertyDeclaration*>
      property_dcls_map;
  for (const auto& entry : property_dcls_map_) {
    property_dcls_map.try_emplace(entry.first, entry.second.get());
  }
  for (const auto& entry : property_dcls_map) {
    absl::string_view name = entry.first;
    const auto property_dcl = entry.second;
    if (!property_dcl->Is<SimpleGraphPropertyDeclaration>()) {
      return ::googlesql_base::UnknownErrorBuilder()
             << "Cannot serialize non-SimpleGraphPropertyDeclaration " << name;
    }
    GOOGLESQL_RETURN_IF_ERROR(
        property_dcl->GetAs<SimpleGraphPropertyDeclaration>()->Serialize(
            file_descriptor_set_map, proto->add_property_declarations()));
  }

  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<SimplePropertyGraph>>
SimplePropertyGraph::Deserialize(const SimplePropertyGraphProto& proto,
                                 const TypeDeserializer& type_deserializer,
                                 SimpleCatalog* catalog) {
  std::vector<std::unique_ptr<const GraphNodeTable>> node_tables;
  std::vector<std::unique_ptr<const GraphEdgeTable>> edge_tables;
  std::vector<std::unique_ptr<const GraphElementLabel>> labels;
  std::vector<std::unique_ptr<const GraphPropertyDeclaration>>
      property_declarations;
  absl::flat_hash_map<std::string, const SimpleGraphNodeTable*>
      unowned_node_tables;
  absl::flat_hash_map<std::string, const SimpleGraphElementLabel*>
      unowned_labels;
  absl::flat_hash_map<std::string, const SimpleGraphPropertyDeclaration*>
      unowned_property_declarations;

  // Deserialize property declarations and labels first,
  // then node table and edge table can use the same set of labels.
  for (const auto& property_dcl_proto : proto.property_declarations()) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<SimpleGraphPropertyDeclaration> property_dcl,
        SimpleGraphPropertyDeclaration::Deserialize(property_dcl_proto,
                                                    type_deserializer));
    unowned_property_declarations.try_emplace(property_dcl->Name(),
                                              property_dcl.get());
    property_declarations.push_back(std::move(property_dcl));
  }

  for (const auto& label_proto : proto.labels()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<SimpleGraphElementLabel> label,
                     SimpleGraphElementLabel::Deserialize(
                         label_proto, unowned_property_declarations));
    unowned_labels.try_emplace(label->Name(), label.get());
    labels.push_back(std::move(label));
  }

  for (const auto& node_table_proto : proto.node_tables()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<SimpleGraphNodeTable> node_table,
                     SimpleGraphNodeTable::Deserialize(
                         node_table_proto, catalog, type_deserializer,
                         unowned_labels, unowned_property_declarations));
    unowned_node_tables.try_emplace(node_table->Name(), node_table.get());
    node_tables.push_back(std::move(node_table));
  }

  for (const auto& edge_table_proto : proto.edge_tables()) {
    auto source_node = unowned_node_tables.find(
        edge_table_proto.source_node_table().node_table_name());
    GOOGLESQL_RET_CHECK(source_node != unowned_node_tables.end());
    auto dest_node = unowned_node_tables.find(
        edge_table_proto.dest_node_table().node_table_name());
    GOOGLESQL_RET_CHECK(dest_node != unowned_node_tables.end());

    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<SimpleGraphEdgeTable> edge_table,
                     SimpleGraphEdgeTable::Deserialize(
                         edge_table_proto, catalog, type_deserializer,
                         unowned_labels, unowned_property_declarations,
                         source_node->second, dest_node->second));
    edge_tables.push_back(std::move(edge_table));
  }

  return std::make_unique<SimplePropertyGraph>(
      ToVector(proto.name_path()), std::move(node_tables),
      std::move(edge_tables), std::move(labels),
      std::move(property_declarations));
}

// This class stores common internal data for nodes and edges.
class ElementTableCommonInternal {
 public:
  ElementTableCommonInternal(
      absl::string_view name,
      absl::Span<const std::string> property_graph_name_path,
      const Table* input_table, const std::vector<int>& key_cols,
      const absl::flat_hash_set<const GraphElementLabel*>& labels,
      std::vector<std::unique_ptr<const GraphPropertyDefinition>>
          property_definitions,
      std::unique_ptr<const GraphDynamicLabel> dynamic_label,
      std::unique_ptr<const GraphDynamicProperties> dynamic_properties
  );

  std::string Name() const { return name_; }

  absl::Span<const std::string> PropertyGraphNamePath() const {
    return property_graph_name_path_;
  }

  const Table* GetTable() const { return input_table_; }

  const std::vector<int>& GetKeyColumns() const { return key_cols_; }

  absl::Status FindPropertyDefinitionByName(
      absl::string_view property_name,
      const GraphPropertyDefinition*& property_definition) const;

  absl::Status GetPropertyDefinitions(
      absl::flat_hash_set<const GraphPropertyDefinition*>& output) const;

  absl::Status FindLabelByName(absl::string_view name,
                               const GraphElementLabel*& label) const;

  absl::Status GetLabels(
      absl::flat_hash_set<const GraphElementLabel*>& output) const;

  void AddLabel(const GraphElementLabel* label);

  void AddPropertyDefinition(
      std::unique_ptr<const GraphPropertyDefinition> property_definition);

  bool HasDynamicLabel() const { return dynamic_label_ != nullptr; }
  absl::Status GetDynamicLabel(const GraphDynamicLabel*& dynamic_label) const {
    if (dynamic_label_ != nullptr) {
      dynamic_label = dynamic_label_.get();
      return absl::OkStatus();
    }
    return absl::NotFoundError("No dynamic label defined.");
  }
  enum GraphElementTable::DynamicLabelCardinality DynamicLabelCardinality()
      const {
    if (dynamic_label_ == nullptr) {
      return GraphElementTable::DynamicLabelCardinality::kUnknown;
    }
    absl::string_view column_name = dynamic_label_->label_expression();
    const auto& type_kind =
        input_table_->FindColumnByName(std::string(column_name))
            ->GetType()
            ->kind();
    if (type_kind == TypeKind::TYPE_STRING) {
      return GraphElementTable::DynamicLabelCardinality::kSingle;
    }
    if (type_kind == TypeKind::TYPE_ARRAY) {
      return GraphElementTable::DynamicLabelCardinality::kMultiple;
    }
    return GraphElementTable::DynamicLabelCardinality::kUnknown;
  }

  bool HasDynamicProperties() const { return dynamic_properties_ != nullptr; }
  absl::Status GetDynamicProperties(
      const GraphDynamicProperties*& dynamic_properties) const {
    if (dynamic_properties_ != nullptr) {
      dynamic_properties = dynamic_properties_.get();
      return absl::OkStatus();
    }
    return absl::NotFoundError("No dynamic properties defined.");
  }

  static absl::Status Deserialize(
      const SimpleGraphElementTableProto& proto, SimpleCatalog* catalog,
      const TypeDeserializer& type_deserializer,
      const absl::flat_hash_map<std::string, const SimpleGraphElementLabel*>&
          unowned_labels,
      const absl::flat_hash_map<std::string,
                                const SimpleGraphPropertyDeclaration*>&
          unowned_property_declarations,
      const Table*& input_table,
      absl::flat_hash_set<const GraphElementLabel*>& labels,
      std::vector<std::unique_ptr<const GraphPropertyDefinition>>&
          property_definitions,
      std::unique_ptr<const GraphDynamicLabel>& dynamic_label,
      std::unique_ptr<const GraphDynamicProperties>& dynamic_properties
  );

 private:
  const std::string name_;
  const std::vector<std::string> property_graph_name_path_;
  const Table* input_table_;
  const std::vector<int> key_cols_;

  absl::flat_hash_map<std::string, const GraphElementLabel*> labels_map_;
  absl::flat_hash_map<std::string,
                      std::unique_ptr<const GraphPropertyDefinition>>
      property_definitions_map_;
  std::unique_ptr<const GraphDynamicLabel> dynamic_label_ = nullptr;
  std::unique_ptr<const GraphDynamicProperties> dynamic_properties_ = nullptr;
};

ElementTableCommonInternal::ElementTableCommonInternal(
    absl::string_view name,
    absl::Span<const std::string> property_graph_name_path,
    const Table* input_table, const std::vector<int>& key_cols,
    const absl::flat_hash_set<const GraphElementLabel*>& labels,
    std::vector<std::unique_ptr<const GraphPropertyDefinition>>
        property_definitions,
    std::unique_ptr<const GraphDynamicLabel> dynamic_label,
    std::unique_ptr<const GraphDynamicProperties> dynamic_properties
    )
    : name_(std::move(name)),
      property_graph_name_path_(property_graph_name_path.begin(),
                                property_graph_name_path.end()),
      input_table_(input_table),
      key_cols_(key_cols) {
  labels_map_.reserve(labels.size());
  for (auto label : labels) {
    AddLabel(label);
  }
  property_definitions_map_.reserve(property_definitions.size());
  for (auto& property_definition : property_definitions) {
    AddPropertyDefinition(std::move(property_definition));
  }
  if (dynamic_label != nullptr) {
    dynamic_label_ = std::move(dynamic_label);
  }
  if (dynamic_properties != nullptr) {
    dynamic_properties_ = std::move(dynamic_properties);
  }
}

void ElementTableCommonInternal::AddLabel(const GraphElementLabel* label) {
  labels_map_.try_emplace(absl::AsciiStrToLower(label->Name()), label);
}

void ElementTableCommonInternal::AddPropertyDefinition(
    std::unique_ptr<const GraphPropertyDefinition> property_definition) {
  property_definitions_map_.try_emplace(
      absl::AsciiStrToLower(property_definition->GetDeclaration().Name()),
      std::move(property_definition));
}

absl::Status ElementTableCommonInternal::FindPropertyDefinitionByName(
    absl::string_view property_name,
    const GraphPropertyDefinition*& property_definition) const {
  property_definition = nullptr;
  auto found =
      property_definitions_map_.find(absl::AsciiStrToLower(property_name));
  if (found != property_definitions_map_.end()) {
    property_definition = found->second.get();
    return absl::OkStatus();
  }
  return absl::NotFoundError(absl::StrFormat(
      "No definition found for property '%s' on element table '%s'.",
      property_name, name_));
}

absl::Status ElementTableCommonInternal::GetPropertyDefinitions(
    absl::flat_hash_set<const GraphPropertyDefinition*>& output) const {
  GOOGLESQL_RET_CHECK(output.empty());
  output.reserve(property_definitions_map_.size());
  for (const auto& pair : property_definitions_map_) {
    output.emplace(pair.second.get());
  }
  return absl::OkStatus();
}

absl::Status ElementTableCommonInternal::FindLabelByName(
    absl::string_view name, const GraphElementLabel*& label) const {
  label = nullptr;
  auto found = labels_map_.find(absl::AsciiStrToLower(name));
  if (found != labels_map_.end()) {
    label = found->second;
    return absl::OkStatus();
  }
  return absl::NotFoundError(absl::StrFormat("Label '%s' not found.", name));
}

absl::Status ElementTableCommonInternal::GetLabels(
    absl::flat_hash_set<const GraphElementLabel*>& output) const {
  GOOGLESQL_RET_CHECK(output.empty());
  output.reserve(labels_map_.size());
  for (const auto& pair : labels_map_) {
    output.emplace(pair.second);
  }
  return absl::OkStatus();
}

absl::Status SerializeElementTable(
    const ElementTableCommonInternal* element_table,
    FileDescriptorSetMap* file_descriptor_set_map,
    SimpleGraphElementTableProto* proto) {
  proto->Clear();
  proto->set_name(element_table->Name());
  for (absl::string_view path_name : element_table->PropertyGraphNamePath()) {
    proto->add_property_graph_name_path(path_name);
  }
  proto->set_input_table_name(element_table->GetTable()->Name());

  for (const auto col : element_table->GetKeyColumns()) {
    proto->add_key_columns(col);
  }

  absl::flat_hash_set<const GraphElementLabel*> labels_output;
  GOOGLESQL_RETURN_IF_ERROR(element_table->GetLabels(labels_output));
  // Always keep ordered for serialization
  absl::btree_set<std::string> label_names;
  for (const auto label : labels_output) {
    label_names.emplace(label->Name());
  }
  for (const auto& label_name : label_names) {
    proto->add_label_names(label_name);
  }

  absl::flat_hash_set<const GraphPropertyDefinition*> property_defs_output;
  GOOGLESQL_RETURN_IF_ERROR(element_table->GetPropertyDefinitions(property_defs_output));
  // Always keep ordered for serialization
  absl::btree_map<std::string, const GraphPropertyDefinition*>
      property_defs_map;
  for (const auto property_def : property_defs_output) {
    property_defs_map.try_emplace(property_def->GetDeclaration().Name(),
                                  property_def);
  }
  for (const auto& property_def : property_defs_map) {
    if (!property_def.second->Is<SimpleGraphPropertyDefinition>()) {
      return ::googlesql_base::UnknownErrorBuilder()
             << "Cannot serialize non-SimpleGraphPropertyDeclaration "
             << property_def.first;
    }
    GOOGLESQL_RETURN_IF_ERROR(
        property_def.second->GetAs<SimpleGraphPropertyDefinition>()->Serialize(
            file_descriptor_set_map, proto->add_property_definitions()));
  }
  if (element_table->HasDynamicLabel()) {
    const GraphDynamicLabel* dynamic_label = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(element_table->GetDynamicLabel(dynamic_label));
    if (dynamic_label != nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(
          dynamic_label->GetAs<SimpleGraphDynamicLabel>()->Serialize(
              proto->mutable_dynamic_label()));
    }
  }
  if (element_table->HasDynamicProperties()) {
    const GraphDynamicProperties* dynamic_properties = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(element_table->GetDynamicProperties(dynamic_properties));
    if (dynamic_properties != nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(
          dynamic_properties->GetAs<SimpleGraphDynamicProperties>()->Serialize(
              proto->mutable_dynamic_properties()));
    }
  }
  return absl::OkStatus();
}

SimpleGraphNodeTable::SimpleGraphNodeTable(
    absl::string_view name,
    absl::Span<const std::string> property_graph_name_path,
    const Table* input_table, const std::vector<int>& key_cols,
    const absl::flat_hash_set<const GraphElementLabel*>& labels,
    std::vector<std::unique_ptr<const GraphPropertyDefinition>>
        property_definitions,
    std::unique_ptr<const GraphDynamicLabel> dynamic_label,
    std::unique_ptr<const GraphDynamicProperties> dynamic_properties
    )
    : element_internal_(std::make_unique<ElementTableCommonInternal>(
          std::move(name), property_graph_name_path, input_table, key_cols,
          labels, std::move(property_definitions), std::move(dynamic_label),
          std::move(dynamic_properties)
          )) {}

SimpleGraphNodeTable::~SimpleGraphNodeTable() = default;

std::string SimpleGraphNodeTable::Name() const {
  return element_internal_->Name();
}

absl::Span<const std::string> SimpleGraphNodeTable::PropertyGraphNamePath()
    const {
  return element_internal_->PropertyGraphNamePath();
}

const Table* SimpleGraphNodeTable::GetTable() const {
  return element_internal_->GetTable();
}

const std::vector<int>& SimpleGraphNodeTable::GetKeyColumns() const {
  return element_internal_->GetKeyColumns();
}

absl::Status SimpleGraphNodeTable::FindPropertyDefinitionByName(
    absl::string_view property_name,
    const GraphPropertyDefinition*& property_definition) const {
  return element_internal_->FindPropertyDefinitionByName(property_name,
                                                         property_definition);
}

absl::Status SimpleGraphNodeTable::GetPropertyDefinitions(
    absl::flat_hash_set<const GraphPropertyDefinition*>& output) const {
  return element_internal_->GetPropertyDefinitions(output);
}

absl::Status SimpleGraphNodeTable::FindLabelByName(
    absl::string_view name, const GraphElementLabel*& label) const {
  return element_internal_->FindLabelByName(name, label);
}

absl::Status SimpleGraphNodeTable::GetLabels(
    absl::flat_hash_set<const GraphElementLabel*>& output) const {
  return element_internal_->GetLabels(output);
}

absl::Status SimpleGraphNodeTable::Serialize(
    FileDescriptorSetMap* file_descriptor_set_map,
    SimpleGraphElementTableProto* proto) const {
  GOOGLESQL_RETURN_IF_ERROR(SerializeElementTable(element_internal_.get(),
                                        file_descriptor_set_map, proto));
  proto->set_kind(SimpleGraphElementTableProto::NODE);
  return absl::OkStatus();
}

absl::Status ElementTableCommonInternal::Deserialize(
    const SimpleGraphElementTableProto& proto, SimpleCatalog* catalog,
    const TypeDeserializer& type_deserializer,
    const absl::flat_hash_map<std::string, const SimpleGraphElementLabel*>&
        unowned_labels,
    const absl::flat_hash_map<std::string,
                              const SimpleGraphPropertyDeclaration*>&
        unowned_property_declarations,
    const Table*& input_table,
    absl::flat_hash_set<const GraphElementLabel*>& labels,
    std::vector<std::unique_ptr<const GraphPropertyDefinition>>&
        property_definitions,
    std::unique_ptr<const GraphDynamicLabel>& dynamic_label,
    std::unique_ptr<const GraphDynamicProperties>& dynamic_properties
) {
  // for labels in property graph with same name as the labels in
  // element table proto, use these labels instead of creating new ones
  for (const auto& label_name : proto.label_names()) {
    const auto found = unowned_labels.find(label_name);
    if (found != unowned_labels.end()) {
      labels.insert(found->second);
    }
  }

  IdStringPool string_pool;
  std::vector<const google::protobuf::DescriptorPool*> pools(
      type_deserializer.descriptor_pools().begin(),
      type_deserializer.descriptor_pools().end());
  const ResolvedNode::RestoreParams params(
      pools, catalog, type_deserializer.type_factory(), &string_pool);
  for (const auto& property_def_proto : proto.property_definitions()) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<SimpleGraphPropertyDefinition> property_def,
        SimpleGraphPropertyDefinition::Deserialize(
            property_def_proto, params, unowned_property_declarations));
    property_definitions.push_back(std::move(property_def));
  }

  const std::vector<std::string> path =
      absl::StrSplit(proto.input_table_name(), '.');
  GOOGLESQL_RETURN_IF_ERROR(catalog->FindTable(path, &input_table));
  if (proto.has_dynamic_label()) {
    GOOGLESQL_ASSIGN_OR_RETURN(dynamic_label, SimpleGraphDynamicLabel::Deserialize(
                                        proto.dynamic_label()));
  }
  if (proto.has_dynamic_properties()) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        dynamic_properties,
        SimpleGraphDynamicProperties::Deserialize(proto.dynamic_properties()));
  }

  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<SimpleGraphNodeTable>>
SimpleGraphNodeTable::Deserialize(
    const SimpleGraphElementTableProto& proto, SimpleCatalog* catalog,
    const TypeDeserializer& type_deserializer,
    const absl::flat_hash_map<std::string, const SimpleGraphElementLabel*>&
        unowned_labels,
    const absl::flat_hash_map<std::string,
                              const SimpleGraphPropertyDeclaration*>&
        unowned_property_declarations) {
  const Table* input_table;
  absl::flat_hash_set<const GraphElementLabel*> labels;
  std::vector<std::unique_ptr<const GraphPropertyDefinition>> property_defs;
  std::unique_ptr<const GraphDynamicLabel> dynamic_label;
  std::unique_ptr<const GraphDynamicProperties> dynamic_properties;

  GOOGLESQL_RET_CHECK_OK(ElementTableCommonInternal::Deserialize(
      proto, catalog, type_deserializer, unowned_labels,
      unowned_property_declarations, input_table, labels, property_defs,
      dynamic_label,
      dynamic_properties
      ));

  return std::make_unique<SimpleGraphNodeTable>(
      proto.name(), ToVector(proto.property_graph_name_path()), input_table,
      std::vector<int>(proto.key_columns().begin(), proto.key_columns().end()),
      labels, std::move(property_defs), std::move(dynamic_label),
      std::move(dynamic_properties)
  );
}

bool SimpleGraphNodeTable::HasDynamicLabel() const {
  return element_internal_->HasDynamicLabel();
}
absl::Status SimpleGraphNodeTable::GetDynamicLabel(
    const GraphDynamicLabel*& dynamic_label) const {
  return element_internal_->GetDynamicLabel(dynamic_label);
}
enum GraphElementTable::DynamicLabelCardinality
SimpleGraphNodeTable::DynamicLabelCardinality() const {
  return element_internal_->DynamicLabelCardinality();
}

bool SimpleGraphNodeTable::HasDynamicProperties() const {
  return element_internal_->HasDynamicProperties();
}
absl::Status SimpleGraphNodeTable::GetDynamicProperties(
    const GraphDynamicProperties*& dynamic_properties) const {
  return element_internal_->GetDynamicProperties(dynamic_properties);
}

SimpleGraphEdgeTable::SimpleGraphEdgeTable(
    absl::string_view name,
    absl::Span<const std::string> property_graph_name_path,
    const Table* input_table, const std::vector<int>& key_cols,
    const absl::flat_hash_set<const GraphElementLabel*>& labels,
    std::vector<std::unique_ptr<const GraphPropertyDefinition>>
        property_definitions,
    std::unique_ptr<const GraphNodeTableReference> source_node,
    std::unique_ptr<const GraphNodeTableReference> destination_node,
    std::unique_ptr<const GraphDynamicLabel> dynamic_label,
    std::unique_ptr<const GraphDynamicProperties> dynamic_properties
    )
    : element_internal_(std::make_unique<const ElementTableCommonInternal>(
          std::move(name), property_graph_name_path, input_table, key_cols,
          labels, std::move(property_definitions), std::move(dynamic_label),
          std::move(dynamic_properties)
          )),
      source_node_(std::move(source_node)),
      destination_node_(std::move(destination_node)) {}

SimpleGraphEdgeTable::~SimpleGraphEdgeTable() = default;

std::string SimpleGraphEdgeTable::Name() const {
  return element_internal_->Name();
}

absl::Span<const std::string> SimpleGraphEdgeTable::PropertyGraphNamePath()
    const {
  return element_internal_->PropertyGraphNamePath();
}

const Table* SimpleGraphEdgeTable::GetTable() const {
  return element_internal_->GetTable();
}

const std::vector<int>& SimpleGraphEdgeTable::GetKeyColumns() const {
  return element_internal_->GetKeyColumns();
}

absl::Status SimpleGraphEdgeTable::FindPropertyDefinitionByName(
    absl::string_view property_name,
    const GraphPropertyDefinition*& property_definition) const {
  return element_internal_->FindPropertyDefinitionByName(property_name,
                                                         property_definition);
}

absl::Status SimpleGraphEdgeTable::GetPropertyDefinitions(
    absl::flat_hash_set<const GraphPropertyDefinition*>& output) const {
  return element_internal_->GetPropertyDefinitions(output);
}

absl::Status SimpleGraphEdgeTable::FindLabelByName(
    absl::string_view name, const GraphElementLabel*& label) const {
  return element_internal_->FindLabelByName(name, label);
}

absl::Status SimpleGraphEdgeTable::GetLabels(
    absl::flat_hash_set<const GraphElementLabel*>& output) const {
  return element_internal_->GetLabels(output);
}

const GraphNodeTableReference* SimpleGraphEdgeTable::GetSourceNodeTable()
    const {
  return source_node_.get();
}

const GraphNodeTableReference* SimpleGraphEdgeTable::GetDestNodeTable() const {
  return destination_node_.get();
}

absl::Status SimpleGraphEdgeTable::Serialize(
    FileDescriptorSetMap* file_descriptor_set_map,
    SimpleGraphElementTableProto* proto) const {
  GOOGLESQL_RETURN_IF_ERROR(SerializeElementTable(element_internal_.get(),
                                        file_descriptor_set_map, proto));
  proto->set_kind(SimpleGraphElementTableProto::EDGE);

  GOOGLESQL_RETURN_IF_ERROR(
      source_node_->GetAs<SimpleGraphNodeTableReference>()->Serialize(
          proto->mutable_source_node_table()));

  GOOGLESQL_RETURN_IF_ERROR(
      destination_node_->GetAs<SimpleGraphNodeTableReference>()->Serialize(
          proto->mutable_dest_node_table()));

  return absl::OkStatus();
}

bool SimpleGraphEdgeTable::HasDynamicLabel() const {
  return element_internal_->HasDynamicLabel();
}
absl::Status SimpleGraphEdgeTable::GetDynamicLabel(
    const GraphDynamicLabel*& dynamic_label) const {
  return element_internal_->GetDynamicLabel(dynamic_label);
}

bool SimpleGraphEdgeTable::HasDynamicProperties() const {
  return element_internal_->HasDynamicProperties();
}

enum GraphElementTable::DynamicLabelCardinality
SimpleGraphEdgeTable::DynamicLabelCardinality() const {
  return element_internal_->DynamicLabelCardinality();
}

absl::Status SimpleGraphEdgeTable::GetDynamicProperties(
    const GraphDynamicProperties*& dynamic_properties) const {
  return element_internal_->GetDynamicProperties(dynamic_properties);
}

absl::StatusOr<std::unique_ptr<SimpleGraphEdgeTable>>
SimpleGraphEdgeTable::Deserialize(
    const SimpleGraphElementTableProto& proto, SimpleCatalog* catalog,
    const TypeDeserializer& type_deserializer,
    const absl::flat_hash_map<std::string, const SimpleGraphElementLabel*>&
        unowned_labels,
    const absl::flat_hash_map<std::string,
                              const SimpleGraphPropertyDeclaration*>&
        unowned_property_declarations,
    const SimpleGraphNodeTable* source_node,
    const SimpleGraphNodeTable* dest_node) {
  const Table* input_table;
  absl::flat_hash_set<const GraphElementLabel*> labels;
  std::vector<std::unique_ptr<const GraphPropertyDefinition>> property_defs;
  std::unique_ptr<const GraphDynamicLabel> dynamic_label;
  std::unique_ptr<const GraphDynamicProperties> dynamic_properties;

  GOOGLESQL_RET_CHECK(ElementTableCommonInternal::Deserialize(
                proto, catalog, type_deserializer, unowned_labels,
                unowned_property_declarations, input_table, labels,
                property_defs, dynamic_label,
                dynamic_properties
                )
                .ok());

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const SimpleGraphNodeTableReference> source,
                   SimpleGraphNodeTableReference::Deserialize(
                       proto.source_node_table(), source_node));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const SimpleGraphNodeTableReference> dest,
                   SimpleGraphNodeTableReference::Deserialize(
                       proto.dest_node_table(), dest_node));

  return std::make_unique<SimpleGraphEdgeTable>(
      proto.name(), ToVector(proto.property_graph_name_path()), input_table,
      std::vector<int>(proto.key_columns().begin(), proto.key_columns().end()),
      labels, std::move(property_defs), std::move(source), std::move(dest),
      std::move(dynamic_label),
      std::move(dynamic_properties)
  );
}

std::string SimpleGraphElementLabel::Name() const { return name_; }

absl::Span<const std::string> SimpleGraphElementLabel::PropertyGraphNamePath()
    const {
  return property_graph_name_path_;
}

absl::Status SimpleGraphElementLabel::Serialize(
    SimpleGraphElementLabelProto* proto) const {
  proto->Clear();
  proto->set_name(name_);
  for (absl::string_view path_name : property_graph_name_path_) {
    proto->add_property_graph_name_path(path_name);
  }
  absl::btree_set<std::string> property_dcl_names;
  // keep ordered
  for (const auto property_dcl : property_declarations_) {
    property_dcl_names.emplace(property_dcl->Name());
  }
  for (const auto& name : property_dcl_names) {
    proto->add_property_declaration_names(name);
  }
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<SimpleGraphElementLabel>>
SimpleGraphElementLabel::Deserialize(
    const SimpleGraphElementLabelProto& proto,
    const absl::flat_hash_map<std::string,
                              const SimpleGraphPropertyDeclaration*>&
        unowned_property_declarations) {
  absl::flat_hash_set<const GraphPropertyDeclaration*> property_declarations;

  for (const auto& name : proto.property_declaration_names()) {
    auto found = unowned_property_declarations.find(name);
    GOOGLESQL_RET_CHECK(found != unowned_property_declarations.end());
    property_declarations.insert(found->second);
  }

  return std::make_unique<SimpleGraphElementLabel>(
      proto.name(), ToVector(proto.property_graph_name_path()),
      property_declarations);
}

absl::Status SimpleGraphNodeTableReference::Serialize(
    SimpleGraphNodeTableReferenceProto* proto) const {
  proto->Clear();
  proto->set_node_table_name(table_->Name());
  for (auto col : edge_table_columns_) {
    proto->add_edge_table_columns(col);
  }
  for (auto col : node_table_columns_) {
    proto->add_node_table_columns(col);
  }
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<SimpleGraphNodeTableReference>>
SimpleGraphNodeTableReference::Deserialize(
    const SimpleGraphNodeTableReferenceProto& proto,
    const SimpleGraphNodeTable* referenced_node_table) {
  return std::make_unique<SimpleGraphNodeTableReference>(
      referenced_node_table,
      std::vector<int>(proto.edge_table_columns().begin(),
                       proto.edge_table_columns().end()),
      std::vector<int>(proto.node_table_columns().begin(),
                       proto.node_table_columns().end()));
}

std::string SimpleGraphPropertyDeclaration::Name() const { return name_; }

GraphPropertyDeclaration::Kind SimpleGraphPropertyDeclaration::kind() const {
  return kind_;
}

absl::Span<const std::string>
SimpleGraphPropertyDeclaration::PropertyGraphNamePath() const {
  return property_graph_name_path_;
}

static SimpleGraphPropertyDeclarationProto::Kind
PropertyDeclarationCppKindToProtoKind(GraphPropertyDeclaration::Kind kind) {
  switch (kind) {
    case GraphPropertyDeclaration::Kind::kInvalid:
      return SimpleGraphPropertyDeclarationProto::KIND_UNSPECIFIED;
    case GraphPropertyDeclaration::Kind::kScalar:
      return SimpleGraphPropertyDeclarationProto::SCALAR;
    case GraphPropertyDeclaration::Kind::kMeasure:
      return SimpleGraphPropertyDeclarationProto::MEASURE;
    default:
      return SimpleGraphPropertyDeclarationProto::KIND_UNSPECIFIED;
  }
}

static GraphPropertyDeclaration::Kind PropertyDeclarationProtoKindToCppKind(
    SimpleGraphPropertyDeclarationProto::Kind kind) {
  switch (kind) {
    case SimpleGraphPropertyDeclarationProto::KIND_UNSPECIFIED:
      return GraphPropertyDeclaration::Kind::kInvalid;
    case SimpleGraphPropertyDeclarationProto::SCALAR:
      return GraphPropertyDeclaration::Kind::kScalar;
    case SimpleGraphPropertyDeclarationProto::MEASURE:
      return GraphPropertyDeclaration::Kind::kMeasure;
    default:
      return GraphPropertyDeclaration::Kind::kInvalid;
  }
}

absl::Status SimpleGraphPropertyDeclaration::Serialize(
    FileDescriptorSetMap* file_descriptor_set_map,
    SimpleGraphPropertyDeclarationProto* proto) const {
  proto->set_name(Name());
  proto->set_kind(PropertyDeclarationCppKindToProtoKind(kind_));
  for (absl::string_view path_name : property_graph_name_path_) {
    proto->add_property_graph_name_path(path_name);
  }
  GOOGLESQL_RETURN_IF_ERROR(Type()->SerializeToProtoAndDistinctFileDescriptors(
      proto->mutable_type(), file_descriptor_set_map));
  if (TypeAnnotationMap() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(
        TypeAnnotationMap()->Serialize(proto->mutable_annotation_map()));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<SimpleGraphPropertyDeclaration>>
SimpleGraphPropertyDeclaration::Deserialize(
    const SimpleGraphPropertyDeclarationProto& proto,
    const TypeDeserializer& type_deserializer) {
  GOOGLESQL_ASSIGN_OR_RETURN(const googlesql::Type* type,
                   type_deserializer.Deserialize(proto.type()));
  const AnnotationMap* annotation_map = nullptr;
  if (proto.has_annotation_map()) {
    GOOGLESQL_RETURN_IF_ERROR(type_deserializer.type_factory()->DeserializeAnnotationMap(
        proto.annotation_map(), &annotation_map));
  }
  return std::make_unique<SimpleGraphPropertyDeclaration>(
      proto.name(), ToVector(proto.property_graph_name_path()), type,
      annotation_map, PropertyDeclarationProtoKindToCppKind(proto.kind()));
}

absl::Status SimpleGraphPropertyDefinition::Serialize(
    FileDescriptorSetMap* file_descriptor_set_map,
    SimpleGraphPropertyDefinitionProto* proto) const {
  proto->set_property_declaration_name(GetDeclaration().Name());
  proto->set_value_expression_sql(expression_sql());
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<SimpleGraphPropertyDefinition>>
SimpleGraphPropertyDefinition::Deserialize(
    const SimpleGraphPropertyDefinitionProto& proto,
    const ResolvedNode::RestoreParams& params,
    const absl::flat_hash_map<std::string,
                              const SimpleGraphPropertyDeclaration*>&
        unowned_property_declarations) {
  const auto found =
      unowned_property_declarations.find(proto.property_declaration_name());
  GOOGLESQL_RET_CHECK(found != unowned_property_declarations.end());

  return std::make_unique<SimpleGraphPropertyDefinition>(
      found->second, proto.value_expression_sql());
}

absl::Status SimpleGraphDynamicLabel::Serialize(
    SimpleGraphElementDynamicLabelProto* proto) const {
  proto->set_label_expression(label_expression_);
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<SimpleGraphDynamicLabel>>
SimpleGraphDynamicLabel::Deserialize(
    const SimpleGraphElementDynamicLabelProto& proto) {
  return std::make_unique<SimpleGraphDynamicLabel>(proto.label_expression());
}

absl::Status SimpleGraphDynamicProperties::Serialize(
    SimpleGraphElementDynamicPropertiesProto* proto) const {
  proto->set_properties_expression(properties_expression_);
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<SimpleGraphDynamicProperties>>
SimpleGraphDynamicProperties::Deserialize(
    const SimpleGraphElementDynamicPropertiesProto& proto) {
  return std::make_unique<SimpleGraphDynamicProperties>(
      proto.properties_expression());
}

}  // namespace googlesql
