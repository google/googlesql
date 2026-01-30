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

#include <algorithm>
#include <memory>
#include <string>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/testdata/sample_catalog.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"

namespace googlesql {
using ::testing::AnyOf;
using ::testing::ContainerEq;
using ::testing::IsSupersetOf;
using ::testing::Not;
using ::testing::Property;
using ::testing::UnorderedElementsAre;

class SampleCatalogPropertyGraphTest : public ::testing::Test {
 protected:
  void SetUp() override {
    LanguageOptions options;
    options.EnableMaximumLanguageFeatures();
    TypeFactory type_factory;
    sample_catalog_ = std::make_unique<SampleCatalog>(options, &type_factory);

    catalog_ = sample_catalog_->catalog();
  }

  std::unique_ptr<SampleCatalog> sample_catalog_;
  SimpleCatalog* catalog_;
};

void CheckLabel(const PropertyGraph* property_graph, absl::string_view name,
                absl::flat_hash_set<const GraphPropertyDeclaration*> expected) {
  const GraphElementLabel* label = nullptr;
  GOOGLESQL_ASSERT_OK(property_graph->FindLabelByName(name, label));
  absl::flat_hash_set<const GraphPropertyDeclaration*> property_declarations;
  GOOGLESQL_ASSERT_OK(label->GetPropertyDeclarations(property_declarations));
  EXPECT_THAT(property_declarations, ContainerEq(expected));
}

void CheckPropertyDefinitions(
    const GraphElementTable* element,
    absl::flat_hash_set<std::string> expected_definition_exprs) {
  absl::flat_hash_set<const GraphPropertyDefinition*> property_definitions;
  GOOGLESQL_ASSERT_OK(element->GetPropertyDefinitions(property_definitions));
  absl::flat_hash_set<std::string> definitions;
  for (const auto* property_definition : property_definitions) {
    definitions.insert(std::string(property_definition->expression_sql()));
  }
  EXPECT_THAT(definitions, ContainerEq(expected_definition_exprs));
}

TEST_F(SampleCatalogPropertyGraphTest, BasicAmlGraphElementLabel) {
  const PropertyGraph* property_graph = nullptr;
  GOOGLESQL_ASSERT_OK(catalog_->FindPropertyGraph({"aml"}, property_graph));

  const GraphPropertyDeclaration* property_dcl_name = nullptr;
  GOOGLESQL_ASSERT_OK(
      property_graph->FindPropertyDeclarationByName("name", property_dcl_name));
  EXPECT_EQ(property_dcl_name->Type()->kind(), TypeKind::TYPE_STRING);

  const GraphPropertyDeclaration* property_dcl_age = nullptr;
  GOOGLESQL_ASSERT_OK(
      property_graph->FindPropertyDeclarationByName("age", property_dcl_age));
  EXPECT_EQ(property_dcl_age->Type()->kind(), TypeKind::TYPE_UINT32);

  const GraphPropertyDeclaration* property_dcl_data = nullptr;
  GOOGLESQL_ASSERT_OK(
      property_graph->FindPropertyDeclarationByName("data", property_dcl_data));
  EXPECT_EQ(property_dcl_data->Type()->kind(), TypeKind::TYPE_BYTES);

  const GraphPropertyDeclaration* property_dcl_id = nullptr;
  GOOGLESQL_ASSERT_OK(
      property_graph->FindPropertyDeclarationByName("id", property_dcl_id));
  EXPECT_EQ(property_dcl_id->Type()->kind(), TypeKind::TYPE_INT64);

  const GraphPropertyDeclaration* property_dcl_bday = nullptr;
  GOOGLESQL_ASSERT_OK(property_graph->FindPropertyDeclarationByName("birthday",
                                                          property_dcl_bday));
  EXPECT_EQ(property_dcl_bday->Type()->kind(), TypeKind::TYPE_DATE);

  const GraphPropertyDeclaration* property_dcl_pid = nullptr;
  GOOGLESQL_ASSERT_OK(property_graph->FindPropertyDeclarationByName("personId",
                                                          property_dcl_pid));
  EXPECT_EQ(property_dcl_pid->Type()->kind(), TypeKind::TYPE_INT64);

  const GraphPropertyDeclaration* property_dcl_aid = nullptr;
  GOOGLESQL_ASSERT_OK(property_graph->FindPropertyDeclarationByName("accountId",
                                                          property_dcl_aid));
  EXPECT_EQ(property_dcl_aid->Type()->kind(), TypeKind::TYPE_INT64);

  const GraphPropertyDeclaration* property_dcl_syndicateid = nullptr;
  GOOGLESQL_ASSERT_OK(property_graph->FindPropertyDeclarationByName(
      "syndicateId", property_dcl_syndicateid));
  EXPECT_EQ(property_dcl_syndicateid->Type()->kind(), TypeKind::TYPE_INT64);

  const GraphPropertyDeclaration* property_dcl_syndicatename = nullptr;
  GOOGLESQL_ASSERT_OK(property_graph->FindPropertyDeclarationByName(
      "syndicateName", property_dcl_syndicatename));
  EXPECT_EQ(property_dcl_syndicatename->Type()->kind(), TypeKind::TYPE_STRING);

  const GraphPropertyDeclaration* property_dcl_syndicatedata = nullptr;
  GOOGLESQL_ASSERT_OK(property_graph->FindPropertyDeclarationByName(
      "syndicateData", property_dcl_syndicatedata));
  EXPECT_EQ(property_dcl_syndicatedata->Type()->kind(), TypeKind::TYPE_ARRAY);

  CheckLabel(property_graph, "Person",
             {property_dcl_name, property_dcl_bday, property_dcl_age,
              property_dcl_data});
  CheckLabel(property_graph, "Account", {property_dcl_id});
  CheckLabel(property_graph, "PersonOwnAccount",
             {property_dcl_pid, property_dcl_aid});
  CheckLabel(property_graph, "Syndicate",
             {property_dcl_syndicateid, property_dcl_syndicatename,
              property_dcl_syndicatedata});
}

TEST_F(SampleCatalogPropertyGraphTest, BasicAmlGraphNodeTable) {
  const PropertyGraph* property_graph = nullptr;
  GOOGLESQL_ASSERT_OK(catalog_->FindPropertyGraph({"aml"}, property_graph));

  absl::flat_hash_set<const GraphNodeTable*> node_tables;
  GOOGLESQL_ASSERT_OK(property_graph->GetNodeTables(node_tables));
  EXPECT_EQ(node_tables.size(), 3);

  for (auto node : node_tables) {
    EXPECT_THAT(node->Name(), AnyOf("Person", "Account", "Syndicate"));
    if (node->Name() == "Person") {
      // Check property definitions
      const GraphPropertyDefinition* property_definition_name = nullptr;
      GOOGLESQL_ASSERT_OK(
          node->FindPropertyDefinitionByName("name", property_definition_name));
      const GraphPropertyDeclaration* property_dcl_name = nullptr;
      GOOGLESQL_ASSERT_OK(property_graph->FindPropertyDeclarationByName(
          "name", property_dcl_name));
      EXPECT_EQ(&(property_definition_name->GetDeclaration()),
                property_dcl_name);

      // Check label
      const GraphElementLabel* person_label = nullptr;
      GOOGLESQL_ASSERT_OK(node->FindLabelByName("Person", person_label));
      const GraphElementLabel* person_label_expected = nullptr;
      GOOGLESQL_ASSERT_OK(
          property_graph->FindLabelByName("Person", person_label_expected));
      EXPECT_EQ(person_label, person_label_expected);

    } else if (node->Name() == "Account") {
      const GraphPropertyDefinition* property_definition_name = nullptr;
      GOOGLESQL_ASSERT_OK(
          node->FindPropertyDefinitionByName("id", property_definition_name));
      const GraphPropertyDeclaration* property_dcl_name = nullptr;
      GOOGLESQL_ASSERT_OK(property_graph->FindPropertyDeclarationByName(
          "id", property_dcl_name));
      EXPECT_EQ(&(property_definition_name->GetDeclaration()),
                property_dcl_name);

      const GraphElementLabel* account_label = nullptr;
      GOOGLESQL_ASSERT_OK(node->FindLabelByName("Account", account_label));
      const GraphElementLabel* account_label_expected = nullptr;
      GOOGLESQL_ASSERT_OK(
          property_graph->FindLabelByName("Account", account_label_expected));
      EXPECT_EQ(account_label, account_label_expected);
    }
  }
}

TEST_F(SampleCatalogPropertyGraphTest, BasicAmlGraphEdgeTable) {
  const PropertyGraph* property_graph = nullptr;
  GOOGLESQL_ASSERT_OK(catalog_->FindPropertyGraph({"aml"}, property_graph));

  absl::flat_hash_set<const GraphEdgeTable*> edge_tables;
  GOOGLESQL_ASSERT_OK(property_graph->GetEdgeTables(edge_tables));

  EXPECT_THAT(edge_tables,
              UnorderedElementsAre(
                  Pointee(Property(&GraphEdgeTable::Name, "PersonOwnAccount")),
                  Pointee(Property(&GraphEdgeTable::Name, "Transfer"))));
  auto edge_iter =
      absl::c_find_if(edge_tables, [](const GraphEdgeTable* edge_table) {
        return edge_table->Name() == "PersonOwnAccount";
      });
  ASSERT_TRUE(edge_iter != edge_tables.end());
  const GraphEdgeTable* edge = *edge_iter;
  EXPECT_EQ(edge->Name(), "PersonOwnAccount");

  // Check property definitions
  const GraphPropertyDefinition* property_definition_personId = nullptr;
  GOOGLESQL_ASSERT_OK(edge->FindPropertyDefinitionByName("personId",
                                               property_definition_personId));
  const GraphPropertyDeclaration* property_dcl_personId = nullptr;
  GOOGLESQL_ASSERT_OK(property_graph->FindPropertyDeclarationByName(
      "personId", property_dcl_personId));
  EXPECT_EQ(&(property_definition_personId->GetDeclaration()),
            property_dcl_personId);

  const GraphPropertyDefinition* property_definition_accId = nullptr;
  GOOGLESQL_ASSERT_OK(edge->FindPropertyDefinitionByName("accountId",
                                               property_definition_accId));
  const GraphPropertyDeclaration* property_dcl_accId = nullptr;
  GOOGLESQL_ASSERT_OK(property_graph->FindPropertyDeclarationByName("accountId",
                                                          property_dcl_accId));
  EXPECT_EQ(&(property_definition_accId->GetDeclaration()), property_dcl_accId);

  // Check label
  const GraphElementLabel* person_own_acc_label = nullptr;
  GOOGLESQL_ASSERT_OK(edge->FindLabelByName("PersonOwnAccount", person_own_acc_label));
  const GraphElementLabel* person_own_acc_label_expected = nullptr;
  GOOGLESQL_ASSERT_OK(property_graph->FindLabelByName("PersonOwnAccount",
                                            person_own_acc_label_expected));
  EXPECT_EQ(person_own_acc_label, person_own_acc_label_expected);

  // Check source and destination nodes
  absl::flat_hash_set<const GraphNodeTable*> all_node_tables;
  GOOGLESQL_ASSERT_OK(property_graph->GetNodeTables(all_node_tables));
  absl::flat_hash_set<const GraphNodeTable*> referenced_node_tables{
      edge->GetSourceNodeTable()->GetReferencedNodeTable(),
      edge->GetDestNodeTable()->GetReferencedNodeTable()};

  EXPECT_THAT(all_node_tables, IsSupersetOf(referenced_node_tables));
}

// Tests the shape of the "aml_enhanced" property graph schema which is
// introduced to test the BindingCandidateUtility framework.
// Compared to the basic "aml" property graph, it includes 2 new paths that
// allow for better testing of edge cases within that framework.
//
// Important paths to check for:
// 1. A disconnected path from the rest of the graph: 'City LocatedIn Country'
// 2. A new cycle in the graph schema: `Person Knows Person`
TEST_F(SampleCatalogPropertyGraphTest, EnhancedAmlVerifyAdditionalPaths) {
  const PropertyGraph* property_graph = nullptr;
  GOOGLESQL_ASSERT_OK(catalog_->FindPropertyGraph({"aml_enhanced"}, property_graph));

  // Verify that we have all the expected Node tables
  absl::flat_hash_set<const GraphNodeTable*> node_tables;
  GOOGLESQL_ASSERT_OK(property_graph->GetNodeTables(node_tables));
  EXPECT_EQ(node_tables.size(), 4);
  std::for_each(node_tables.begin(), node_tables.end(),
                [](const GraphNodeTable* node) {
                  EXPECT_THAT(node->Name(),
                              AnyOf("Person", "Account", "City", "Country"));
                });

  const GraphElementTable* person_element;
  const GraphElementTable* knows_element;
  GOOGLESQL_ASSERT_OK(property_graph->FindElementTableByName("Person", person_element));
  GOOGLESQL_ASSERT_OK(property_graph->FindElementTableByName("Knows", knows_element));

  // City LocatedIn Country must be disconnected from the rest of the graph
  const GraphElementTable* city_element;
  const GraphElementTable* locatedin_element;
  const GraphElementTable* country_element;
  GOOGLESQL_ASSERT_OK(property_graph->FindElementTableByName("City", city_element));
  GOOGLESQL_ASSERT_OK(
      property_graph->FindElementTableByName("LocatedIn", locatedin_element));
  GOOGLESQL_ASSERT_OK(property_graph->FindElementTableByName("Country", country_element));

  absl::flat_hash_set<const GraphEdgeTable*> edge_tables;
  GOOGLESQL_ASSERT_OK(property_graph->GetEdgeTables(edge_tables));
  EXPECT_EQ(edge_tables.size(), 4);

  for (auto edge : edge_tables) {
    // Verify that we have all the expected edge tables
    EXPECT_THAT(edge->Name(),
                AnyOf("Knows", "PersonOwnAccount", "Transfer", "LocatedIn"));
    absl::flat_hash_set<const GraphNodeTable*> referenced_node_tables{
        edge->GetSourceNodeTable()->GetReferencedNodeTable(),
        edge->GetDestNodeTable()->GetReferencedNodeTable()};
    if (edge->Name() == "LocatedIn") {
      // Ensure that "LocatedIn" only connects "City" and "Country"
      EXPECT_EQ(referenced_node_tables.size(), 2);
      EXPECT_THAT(referenced_node_tables,
                  UnorderedElementsAre(city_element, country_element));
    } else if (edge->Name() == "Knows") {
      // Ensure that "Knows" connects "Person" with "Person"
      EXPECT_EQ(referenced_node_tables.size(), 1);
      EXPECT_THAT(referenced_node_tables, UnorderedElementsAre(person_element));
    } else {
      // Ensure that all other edges do not touch "City" or "Person"
      EXPECT_THAT(referenced_node_tables,
                  Not(UnorderedElementsAre(city_element, country_element)));
    }
  }
}

// Check the shape of the "aml_dynamic" property graph schema.
TEST_F(SampleCatalogPropertyGraphTest,
       PropertyGraphWithDynamicLabelAndProperties) {
  const PropertyGraph* property_graph = nullptr;
  GOOGLESQL_ASSERT_OK(catalog_->FindPropertyGraph({"aml_dynamic"}, property_graph));

  absl::flat_hash_set<const GraphNodeTable*> node_tables;
  GOOGLESQL_ASSERT_OK(property_graph->GetNodeTables(node_tables));
  EXPECT_EQ(node_tables.size(), 1);
  std::for_each(node_tables.begin(), node_tables.end(),
                [](const GraphNodeTable* node) {
                  EXPECT_THAT(node->Name(), AnyOf("DynamicGraphNode"));
                });

  absl::flat_hash_set<const GraphEdgeTable*> edge_tables;
  GOOGLESQL_ASSERT_OK(property_graph->GetEdgeTables(edge_tables));
  EXPECT_EQ(edge_tables.size(), 1);
  std::for_each(edge_tables.begin(), edge_tables.end(),
                [](const GraphEdgeTable* edge) {
                  EXPECT_THAT(edge->Name(), AnyOf("DynamicGraphEdge"));
                });

  const GraphElementTable* graph_node_table = nullptr;
  GOOGLESQL_ASSERT_OK(property_graph->FindElementTableByName("DynamicGraphNode",
                                                   graph_node_table));
  const GraphElementTable* graph_edge_table = nullptr;
  GOOGLESQL_ASSERT_OK(property_graph->FindElementTableByName("DynamicGraphEdge",
                                                   graph_edge_table));

  // Validate prop declarations.
  const GraphPropertyDeclaration* property_dcl_id = nullptr;
  GOOGLESQL_ASSERT_OK(
      property_graph->FindPropertyDeclarationByName("id", property_dcl_id));
  EXPECT_EQ(property_dcl_id->Type()->kind(), TypeKind::TYPE_INT64);

  const GraphPropertyDeclaration* property_dcl_dst_id = nullptr;
  GOOGLESQL_ASSERT_OK(property_graph->FindPropertyDeclarationByName("dst_id",
                                                          property_dcl_dst_id));
  EXPECT_EQ(property_dcl_dst_id->Type()->kind(), TypeKind::TYPE_INT64);

  const GraphPropertyDeclaration* property_dcl_node_label_col = nullptr;
  GOOGLESQL_ASSERT_OK(property_graph->FindPropertyDeclarationByName(
      "nodeLabelCol", property_dcl_node_label_col));
  EXPECT_EQ(property_dcl_node_label_col->Type()->kind(), TypeKind::TYPE_STRING);

  const GraphPropertyDeclaration* property_dcl_edge_label_col = nullptr;
  GOOGLESQL_ASSERT_OK(property_graph->FindPropertyDeclarationByName(
      "edgeLabelCol", property_dcl_edge_label_col));
  EXPECT_EQ(property_dcl_edge_label_col->Type()->kind(), TypeKind::TYPE_STRING);

  const GraphPropertyDeclaration* property_dcl_node_json_prop = nullptr;
  GOOGLESQL_ASSERT_OK(property_graph->FindPropertyDeclarationByName(
      "nodeJsonProp", property_dcl_node_json_prop));
  EXPECT_EQ(property_dcl_node_json_prop->Type()->kind(), TypeKind::TYPE_JSON);

  const GraphPropertyDeclaration* property_dcl_edge_json_prop = nullptr;
  GOOGLESQL_ASSERT_OK(property_graph->FindPropertyDeclarationByName(
      "edgeJsonProp", property_dcl_edge_json_prop));
  EXPECT_EQ(property_dcl_edge_json_prop->Type()->kind(), TypeKind::TYPE_JSON);

  const GraphPropertyDeclaration* property_dcl_code = nullptr;
  GOOGLESQL_ASSERT_OK(
      property_graph->FindPropertyDeclarationByName("code", property_dcl_code));
  EXPECT_EQ(property_dcl_code->Type()->kind(), TypeKind::TYPE_INT64);

  const GraphPropertyDeclaration* property_dcl_category = nullptr;
  GOOGLESQL_ASSERT_OK(property_graph->FindPropertyDeclarationByName(
      "category", property_dcl_category));
  EXPECT_EQ(property_dcl_category->Type()->kind(), TypeKind::TYPE_STRING);

  // Check default labels.
  CheckLabel(property_graph, "DynamicGraphNode",
             {property_dcl_id, property_dcl_node_label_col, property_dcl_code,
              property_dcl_node_json_prop});
  CheckLabel(property_graph, "DynamicGraphEdge",
             {
                 property_dcl_id,
                 property_dcl_dst_id,
                 property_dcl_edge_label_col,
                 property_dcl_edge_json_prop,
                 property_dcl_category,
             });

  // Check static labels.
  CheckLabel(property_graph, "Entity", {property_dcl_code});
  CheckLabel(property_graph, "CONNECTION", {property_dcl_category});

  // Check property definitions.
  CheckPropertyDefinitions(graph_node_table,
                           {"id", "nodeLabelCol", "code", "nodeJsonProp"});
  CheckPropertyDefinitions(graph_edge_table, {"id", "dst_id", "edgeLabelCol",
                                              "edgeJsonProp", "category"});

  // Validate node and edge dynamic labels and properties.
  EXPECT_TRUE(graph_node_table->HasDynamicLabel());
  EXPECT_EQ(graph_node_table->DynamicLabelCardinality(),
            GraphElementTable::DynamicLabelCardinality::kSingle);
  const GraphDynamicLabel* dynamic_label = nullptr;
  GOOGLESQL_EXPECT_OK(graph_node_table->GetDynamicLabel(dynamic_label));
  EXPECT_EQ(dynamic_label->label_expression(), "nodeLabelCol");

  EXPECT_TRUE(graph_node_table->HasDynamicProperties());
  const GraphDynamicProperties* dynamic_properties = nullptr;
  GOOGLESQL_EXPECT_OK(graph_node_table->GetDynamicProperties(dynamic_properties));
  EXPECT_EQ(dynamic_properties->properties_expression(), "nodeJsonProp");

  EXPECT_TRUE(graph_edge_table->HasDynamicLabel());
  EXPECT_EQ(graph_edge_table->DynamicLabelCardinality(),
            GraphElementTable::DynamicLabelCardinality::kSingle);
  const GraphDynamicLabel* edge_dynamic_label = nullptr;
  GOOGLESQL_EXPECT_OK(graph_edge_table->GetDynamicLabel(edge_dynamic_label));
  EXPECT_EQ(edge_dynamic_label->label_expression(), "edgeLabelCol");

  EXPECT_TRUE(graph_edge_table->HasDynamicProperties());
  const GraphDynamicProperties* edge_dynamic_properties = nullptr;
  GOOGLESQL_EXPECT_OK(graph_edge_table->GetDynamicProperties(edge_dynamic_properties));
  EXPECT_EQ(edge_dynamic_properties->properties_expression(), "edgeJsonProp");
}

// Check the shape of the "aml_dynamic" property graph schema.
TEST_F(SampleCatalogPropertyGraphTest, PropertyGraphWithDynamicMultiLabelNode) {
  const PropertyGraph* property_graph = nullptr;
  GOOGLESQL_ASSERT_OK(catalog_->FindPropertyGraph({"aml_dynamic_multi"}, property_graph));

  absl::flat_hash_set<const GraphNodeTable*> node_tables;
  GOOGLESQL_ASSERT_OK(property_graph->GetNodeTables(node_tables));
  EXPECT_EQ(node_tables.size(), 1);
  std::for_each(
      node_tables.begin(), node_tables.end(), [](const GraphNodeTable* node) {
        EXPECT_THAT(node->Name(), AnyOf("DynamicMultiLabelGraphNode"));
      });

  absl::flat_hash_set<const GraphEdgeTable*> edge_tables;
  GOOGLESQL_ASSERT_OK(property_graph->GetEdgeTables(edge_tables));
  EXPECT_EQ(edge_tables.size(), 1);
  std::for_each(
      edge_tables.begin(), edge_tables.end(), [](const GraphEdgeTable* edge) {
        EXPECT_THAT(edge->Name(), AnyOf("DynamicSingleLabelGraphEdge"));
      });

  const GraphElementTable* graph_node_table = nullptr;
  GOOGLESQL_ASSERT_OK(property_graph->FindElementTableByName("DynamicMultiLabelGraphNode",
                                                   graph_node_table));
  const GraphElementTable* graph_edge_table = nullptr;
  GOOGLESQL_ASSERT_OK(property_graph->FindElementTableByName(
      "DynamicSingleLabelGraphEdge", graph_edge_table));

  // Validate prop declarations.
  const GraphPropertyDeclaration* property_dcl_id = nullptr;
  GOOGLESQL_ASSERT_OK(
      property_graph->FindPropertyDeclarationByName("id", property_dcl_id));
  EXPECT_EQ(property_dcl_id->Type()->kind(), TypeKind::TYPE_INT64);

  const GraphPropertyDeclaration* property_dcl_dst_id = nullptr;
  GOOGLESQL_ASSERT_OK(property_graph->FindPropertyDeclarationByName("dst_id",
                                                          property_dcl_dst_id));
  EXPECT_EQ(property_dcl_dst_id->Type()->kind(), TypeKind::TYPE_INT64);

  // Validate node label property declaration as an array.
  const GraphPropertyDeclaration* property_dcl_node_label_col = nullptr;
  GOOGLESQL_ASSERT_OK(property_graph->FindPropertyDeclarationByName(
      "nodeLabelCol", property_dcl_node_label_col));
  EXPECT_EQ(property_dcl_node_label_col->Type()->kind(), TypeKind::TYPE_ARRAY);

  const GraphPropertyDeclaration* property_dcl_edge_label_col = nullptr;
  GOOGLESQL_ASSERT_OK(property_graph->FindPropertyDeclarationByName(
      "edgeLabelCol", property_dcl_edge_label_col));
  EXPECT_EQ(property_dcl_edge_label_col->Type()->kind(), TypeKind::TYPE_STRING);

  const GraphPropertyDeclaration* property_dcl_node_json_prop = nullptr;
  GOOGLESQL_ASSERT_OK(property_graph->FindPropertyDeclarationByName(
      "nodeJsonProp", property_dcl_node_json_prop));
  EXPECT_EQ(property_dcl_node_json_prop->Type()->kind(), TypeKind::TYPE_JSON);

  const GraphPropertyDeclaration* property_dcl_edge_json_prop = nullptr;
  GOOGLESQL_ASSERT_OK(property_graph->FindPropertyDeclarationByName(
      "edgeJsonProp", property_dcl_edge_json_prop));
  EXPECT_EQ(property_dcl_edge_json_prop->Type()->kind(), TypeKind::TYPE_JSON);

  const GraphPropertyDeclaration* property_dcl_code = nullptr;
  GOOGLESQL_ASSERT_OK(
      property_graph->FindPropertyDeclarationByName("code", property_dcl_code));
  EXPECT_EQ(property_dcl_code->Type()->kind(), TypeKind::TYPE_INT64);

  const GraphPropertyDeclaration* property_dcl_category = nullptr;
  GOOGLESQL_ASSERT_OK(property_graph->FindPropertyDeclarationByName(
      "category", property_dcl_category));
  EXPECT_EQ(property_dcl_category->Type()->kind(), TypeKind::TYPE_STRING);

  // Check default labels.
  CheckLabel(property_graph, "DynamicMultiLabelGraphNode",
             {property_dcl_id, property_dcl_node_label_col, property_dcl_code,
              property_dcl_node_json_prop});
  CheckLabel(property_graph, "DynamicSingleLabelGraphEdge",
             {
                 property_dcl_id,
                 property_dcl_dst_id,
                 property_dcl_edge_label_col,
                 property_dcl_edge_json_prop,
                 property_dcl_category,
             });

  // Check static labels.
  CheckLabel(property_graph, "Entity", {property_dcl_code});
  CheckLabel(property_graph, "CONNECTION", {property_dcl_category});

  // Check property definitions.
  CheckPropertyDefinitions(graph_node_table,
                           {"id", "nodeLabelCol", "code", "nodeJsonProp"});
  CheckPropertyDefinitions(graph_edge_table, {"id", "dst_id", "edgeLabelCol",
                                              "edgeJsonProp", "category"});

  // Validate node and edge dynamic labels and properties.
  EXPECT_TRUE(graph_node_table->HasDynamicLabel());
  EXPECT_EQ(graph_node_table->DynamicLabelCardinality(),
            GraphElementTable::DynamicLabelCardinality::kMultiple);
  const GraphDynamicLabel* dynamic_label = nullptr;
  GOOGLESQL_EXPECT_OK(graph_node_table->GetDynamicLabel(dynamic_label));
  EXPECT_EQ(dynamic_label->label_expression(), "nodeLabelCol");

  EXPECT_TRUE(graph_node_table->HasDynamicProperties());
  const GraphDynamicProperties* dynamic_properties = nullptr;
  GOOGLESQL_EXPECT_OK(graph_node_table->GetDynamicProperties(dynamic_properties));
  EXPECT_EQ(dynamic_properties->properties_expression(), "nodeJsonProp");

  EXPECT_TRUE(graph_edge_table->HasDynamicLabel());
  EXPECT_EQ(graph_edge_table->DynamicLabelCardinality(),
            GraphElementTable::DynamicLabelCardinality::kSingle);
  const GraphDynamicLabel* edge_dynamic_label = nullptr;
  GOOGLESQL_EXPECT_OK(graph_edge_table->GetDynamicLabel(edge_dynamic_label));
  EXPECT_EQ(edge_dynamic_label->label_expression(), "edgeLabelCol");

  EXPECT_TRUE(graph_edge_table->HasDynamicProperties());
  const GraphDynamicProperties* edge_dynamic_properties = nullptr;
  GOOGLESQL_EXPECT_OK(graph_edge_table->GetDynamicProperties(edge_dynamic_properties));
  EXPECT_EQ(edge_dynamic_properties->properties_expression(), "edgeJsonProp");
}

}  // namespace googlesql
