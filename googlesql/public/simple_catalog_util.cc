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

#include "googlesql/public/simple_catalog_util.h"

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "googlesql/common/internal_property_graph.h"
#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/constant_evaluator.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/simple_property_graph.h"
#include "googlesql/public/sql_constant.h"
#include "googlesql/public/sql_function.h"
#include "googlesql/public/sql_tvf.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/templated_sql_function.h"
#include "googlesql/public/templated_sql_tvf.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

static absl::StatusOr<std::unique_ptr<Function>>
MakeFunctionFromCreateFunctionImpl(
    const ResolvedCreateFunctionStmt& create_function_stmt,
    const FunctionOptions* /*absl_nullable*/ function_options,
    bool legacy_joined_name_path = false) {
  FunctionOptions options;
  if (function_options != nullptr) {
    options = *function_options;
  } else {
    // User-defined functions often use CamelCase. Upper casing that makes it
    // unreadable.
    options.set_uses_upper_case_sql_name(false);
  }
  FunctionEnums::Mode function_mode = create_function_stmt.is_aggregate()
                                          ? FunctionEnums::AGGREGATE
                                          : FunctionEnums::SCALAR;
  std::unique_ptr<Function> function;
  std::vector<std::string> name_path = create_function_stmt.name_path();
  if (legacy_joined_name_path) {
    name_path = {absl::StrJoin(create_function_stmt.name_path(), ".")};
  }
  if (create_function_stmt.function_expression() != nullptr) {
    std::unique_ptr<SQLFunction> sql_function;
    GOOGLESQL_ASSIGN_OR_RETURN(function,
                     SQLFunction::Create(
                         std::move(name_path), function_mode,
                         create_function_stmt.signature(), std::move(options),
                         create_function_stmt.function_expression(),
                         create_function_stmt.argument_name_list(),
                         &create_function_stmt.aggregate_expression_list(),
                         /*parse_resume_location=*/std::nullopt));
  } else if (create_function_stmt.language() == "SQL") {
    function = std::make_unique<TemplatedSQLFunction>(
        create_function_stmt.name_path(), create_function_stmt.signature(),
        create_function_stmt.argument_name_list(),
        ParseResumeLocation::FromStringView(create_function_stmt.code()),
        function_mode, options);
  } else {
    // The group arg is just used for distinguishing classes of functions
    // in some error messages.
    std::vector<FunctionSignature> signatures = {
        create_function_stmt.signature()};
    function = std::make_unique<Function>(create_function_stmt.name_path(),
                                          /*group=*/"External_function",
                                          function_mode, signatures, options);
  }

  function->set_sql_security(create_function_stmt.sql_security());
  return function;
}

absl::Status AddFunctionFromCreateFunction(
    absl::string_view create_sql_stmt, const AnalyzerOptions& analyzer_options,
    bool allow_persistent_function,
    const FunctionOptions* /*absl_nullable*/ function_options,
    std::unique_ptr<const AnalyzerOutput>& analyzer_output,
    Catalog& resolving_catalog, SimpleCatalog& catalog) {
  GOOGLESQL_RET_CHECK(analyzer_options.language().SupportsStatementKind(
      RESOLVED_CREATE_FUNCTION_STMT));
  GOOGLESQL_RETURN_IF_ERROR(AnalyzeStatement(create_sql_stmt, analyzer_options,
                                   &resolving_catalog, catalog.type_factory(),
                                   &analyzer_output))
      << create_sql_stmt;
  const ResolvedStatement* resolved = analyzer_output->resolved_statement();
  GOOGLESQL_RET_CHECK(resolved->Is<ResolvedCreateFunctionStmt>());
  const ResolvedCreateFunctionStmt* resolved_create =
      resolved->GetAs<ResolvedCreateFunctionStmt>();
  if (!allow_persistent_function) {
    GOOGLESQL_RET_CHECK_EQ(resolved_create->create_scope(),
                 ResolvedCreateStatementEnums::CREATE_TEMP);
  }
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<Function> function,
      MakeFunctionFromCreateFunctionImpl(*resolved_create, function_options,
                                         /*legacy_joined_name_path=*/true));

  GOOGLESQL_RET_CHECK(catalog.AddOwnedFunctionIfNotPresent(&function))
      << absl::StrJoin(resolved_create->name_path(), ".");
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<Function>> MakeFunctionFromCreateFunction(
    const ResolvedCreateFunctionStmt& create_function_stmt,
    const FunctionOptions* /*absl_nullable*/ function_options) {
  return MakeFunctionFromCreateFunctionImpl(create_function_stmt,
                                            function_options);
}

absl::Status AddTVFFromCreateTableFunction(
    absl::string_view create_tvf_stmt, const AnalyzerOptions& analyzer_options,
    bool allow_persistent,
    std::unique_ptr<const AnalyzerOutput>& analyzer_output,
    SimpleCatalog& catalog) {
  GOOGLESQL_RET_CHECK(analyzer_options.language().SupportsStatementKind(
      RESOLVED_CREATE_TABLE_FUNCTION_STMT));
  GOOGLESQL_RETURN_IF_ERROR(AnalyzeStatement(create_tvf_stmt, analyzer_options, &catalog,
                                   catalog.type_factory(), &analyzer_output))
      << create_tvf_stmt;
  const ResolvedStatement* resolved = analyzer_output->resolved_statement();
  GOOGLESQL_RET_CHECK(resolved->Is<ResolvedCreateTableFunctionStmt>());
  const ResolvedCreateTableFunctionStmt* stmt =
      resolved->GetAs<ResolvedCreateTableFunctionStmt>();
  if (!allow_persistent) {
    GOOGLESQL_RET_CHECK_EQ(stmt->create_scope(),
                 ResolvedCreateStatementEnums::CREATE_TEMP);
  }

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<TableValuedFunction> tvf,
                   MakeTVFFromCreateTableFunction(*stmt));

  GOOGLESQL_RET_CHECK(catalog.AddOwnedTableValuedFunctionIfNotPresent(&tvf));

  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<TableValuedFunction>>
MakeTVFFromCreateTableFunction(const ResolvedCreateTableFunctionStmt& stmt) {
  std::unique_ptr<TableValuedFunction> tvf;
  if (stmt.query() != nullptr) {
    GOOGLESQL_RET_CHECK(!stmt.signature().IsTemplated());
    GOOGLESQL_RET_CHECK_EQ(stmt.language(), "SQL");
    std::unique_ptr<SQLTableValuedFunction> sql_tvf;
    GOOGLESQL_RETURN_IF_ERROR(SQLTableValuedFunction::Create(&stmt, &sql_tvf));
    tvf = std::move(sql_tvf);
  } else if (stmt.language() == "SQL" && !stmt.code().empty()) {
    GOOGLESQL_RET_CHECK(stmt.signature().IsTemplated());
    tvf = std::make_unique<TemplatedSQLTVF>(
        stmt.name_path(), stmt.signature(), stmt.argument_name_list(),
        ParseResumeLocation::FromStringView(stmt.code()));
  } else {
    const FunctionArgumentType& result_type = stmt.signature().result_type();
    GOOGLESQL_RET_CHECK(result_type.IsRelation());
    GOOGLESQL_RET_CHECK(result_type.options().has_relation_input_schema())
        << "Only TVFs with fixed output table schemas are supported";

    tvf = std::make_unique<FixedOutputSchemaTVF>(
        stmt.name_path(), std::vector<FunctionSignature>{stmt.signature()},
        result_type.options().relation_input_schema());
  }

  // Give an error if any options were set that weren't handled above.
  // Some fields are okay to ignore here so we mark them accessed.
  for (const auto& col : stmt.output_column_list()) {
    col->MarkFieldsAccessed();
  }
  if (stmt.query() != nullptr) {
    stmt.query()->MarkFieldsAccessed();
  }
  GOOGLESQL_RETURN_IF_ERROR(stmt.CheckFieldsAccessed());

  return tvf;
}

absl::Status AddViewFromCreateView(
    absl::string_view create_view_stmt, const AnalyzerOptions& analyzer_options,
    bool allow_non_temp, std::unique_ptr<const AnalyzerOutput>& analyzer_output,
    SimpleCatalog& catalog) {
  GOOGLESQL_RET_CHECK(analyzer_options.language().SupportsStatementKind(
      RESOLVED_CREATE_VIEW_STMT));
  GOOGLESQL_RETURN_IF_ERROR(AnalyzeStatement(create_view_stmt, analyzer_options, &catalog,
                                   catalog.type_factory(), &analyzer_output))
      << create_view_stmt;
  const ResolvedStatement* resolved = analyzer_output->resolved_statement();
  GOOGLESQL_RET_CHECK(resolved->Is<ResolvedCreateViewStmt>());
  const ResolvedCreateViewStmt* stmt =
      resolved->GetAs<ResolvedCreateViewStmt>();
  if (!allow_non_temp) {
    GOOGLESQL_RET_CHECK_EQ(stmt->create_scope(),
                 ResolvedCreateStatementEnums::CREATE_TEMP);
  }
  std::vector<SimpleSQLView::NameAndType> columns;
  for (int i = 0; i < stmt->output_column_list_size(); ++i) {
    const ResolvedOutputColumn* col = stmt->output_column_list(i);
    columns.push_back({.name = col->name(), .type = col->column().type()});
  }
  SimpleSQLView::SqlSecurity security = stmt->sql_security();
  // GoogleSQL defines the default SQL security to be "DEFINER"
  if (security == SQLView::kSecurityUnspecified) {
    security = SQLView::kSecurityDefiner;
  }
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<SimpleSQLView> sql_view,
      SimpleSQLView::Create(absl::StrJoin(stmt->name_path(), "."), columns,
                            security, stmt->is_value_table(), stmt->query()));
  std::string view_name = sql_view->Name();
  GOOGLESQL_RET_CHECK(catalog.AddOwnedTableIfNotPresent(view_name, std::move(sql_view)));
  return absl::OkStatus();
}

absl::Status AddTableFromCreateTable(
    absl::string_view create_table_stmt,
    const AnalyzerOptions& analyzer_options, bool allow_non_temp,
    std::unique_ptr<const AnalyzerOutput>& analyzer_output, SimpleTable*& table,
    SimpleCatalog& catalog) {
  table = nullptr;

  GOOGLESQL_RET_CHECK(analyzer_options.language().SupportsStatementKind(
      RESOLVED_CREATE_TABLE_STMT));
  GOOGLESQL_RETURN_IF_ERROR(AnalyzeStatement(create_table_stmt, analyzer_options,
                                   &catalog, catalog.type_factory(),
                                   &analyzer_output))
      << create_table_stmt;
  const ResolvedStatement* resolved = analyzer_output->resolved_statement();
  GOOGLESQL_RET_CHECK(resolved->Is<ResolvedCreateTableStmt>());
  const ResolvedCreateTableStmt* stmt =
      resolved->GetAs<ResolvedCreateTableStmt>();
  if (!allow_non_temp) {
    GOOGLESQL_RET_CHECK_EQ(stmt->create_scope(),
                 ResolvedCreateStatementEnums::CREATE_TEMP);
  }

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<SimpleTable> created_table,
                   MakeTableFromCreateTable(*stmt));

  SimpleTable* table_ptr = created_table.get();
  const std::string table_name = created_table->Name();
  GOOGLESQL_RET_CHECK(
      catalog.AddOwnedTableIfNotPresent(table_name, std::move(created_table)));

  // Give an error if any options were set that weren't handled above.
  GOOGLESQL_RETURN_IF_ERROR(stmt->CheckFieldsAccessed());

  table = table_ptr;
  return absl::OkStatus();
}

static absl::Status SetPrimaryKey(const ResolvedCreateTableStmtBase& stmt,
                                  SimpleTable* table) {
  if (stmt.primary_key() == nullptr) {
    return absl::OkStatus();
  }

  GOOGLESQL_RETURN_IF_ERROR(
      table->SetPrimaryKey(stmt.primary_key()->column_offset_list()));
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<SimpleTable>> MakeTableFromCreateTable(
    const ResolvedCreateTableStmtBase& stmt) {
  GOOGLESQL_RET_CHECK_EQ(stmt.name_path().size(), 1)
      << "Table names with sub-catalogs are not supported";

  std::vector<SimpleTable::NameAndType> columns;
  for (const auto& column_def : stmt.column_definition_list()) {
    columns.push_back({column_def->name(), column_def->type()});
  }
  auto created_table = std::make_unique<SimpleTable>(
      absl::StrJoin(stmt.name_path(), "."), columns);

  GOOGLESQL_RETURN_IF_ERROR(SetPrimaryKey(stmt, created_table.get()));

  // Give an error if any options were set that weren't handled above.
  GOOGLESQL_RETURN_IF_ERROR(stmt.CheckFieldsAccessed());

  return created_table;
}

absl::StatusOr<std::unique_ptr<SQLConstant>> MakeConstantFromCreateConstant(
    const ResolvedCreateConstantStmt& stmt) {
  std::unique_ptr<SQLConstant> constant;
  GOOGLESQL_RETURN_IF_ERROR(SQLConstant::Create(&stmt, &constant));
  return constant;
}

// Returns the catalog column and column name from a table, given the column
// reference.
static absl::Status GetColumnAndNameFromColumnRef(
    const ResolvedExpr& column_ref, const Table* table, const Column*& column,
    std::string& name) {
  if (column_ref.Is<ResolvedColumnRef>()) {
    name = column_ref.GetAs<ResolvedColumnRef>()->column().name();
    column = table->FindColumnByName(name);
  } else if (column_ref.Is<ResolvedCatalogColumnRef>()) {
    column = column_ref.GetAs<ResolvedCatalogColumnRef>()->column();
  } else {
    GOOGLESQL_RET_CHECK_FAIL() << column_ref.DebugString();
  }
  GOOGLESQL_RET_CHECK(column != nullptr);
  return absl::OkStatus();
}

// Returns a list of column indices in the `table`, given a list of column
// references.
template <typename T>
static absl::StatusOr<std::vector<int>> GetColumnIndices(
    const Table* table, const std::vector<T>& column_refs) {
  absl::flat_hash_map<const Column*, int> column_to_index_map;
  for (int i = 0; i < table->NumColumns(); ++i) {
    column_to_index_map.emplace(table->GetColumn(i), i);
  }

  std::vector<int> column_indices;
  column_indices.reserve(column_refs.size());
  for (const auto& ref : column_refs) {
    std::string name;
    const Column* column = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(GetColumnAndNameFromColumnRef(*ref, table, column, name));
    column_indices.push_back(column_to_index_map[column]);
  }
  return column_indices;
}

static absl::Status ValidatePropertyDefinition(
    const GraphPropertyDeclaration& property_decl) {
  GOOGLESQL_RET_CHECK(property_decl.kind() != GraphPropertyDeclaration::Kind::kInvalid)
      << property_decl.Name();
  if (property_decl.kind() == GraphPropertyDeclaration::Kind::kMeasure) {
    return absl::UnimplementedError(
        absl::StrFormat("Measure property definitions are not yet supported. "
                        "Property: %s",
                        property_decl.Name()));
  }
  return absl::OkStatus();
}

template <typename ElementTableType>
static absl::Status AddElementTable(
    const ResolvedGraphElementTable* resolved_element_table,
    SimplePropertyGraph& graph) {
  GOOGLESQL_RET_CHECK(resolved_element_table->input_scan()->Is<ResolvedTableScan>());
  const Table* table =
      resolved_element_table->input_scan()->GetAs<ResolvedTableScan>()->table();
  GOOGLESQL_ASSIGN_OR_RETURN(std::vector<int> key_indices,
                   GetColumnIndices(table, resolved_element_table->key_list()));

  // Static properties.
  std::vector<std::unique_ptr<const GraphPropertyDefinition>> property_defs;
  for (const auto& def : resolved_element_table->property_definition_list()) {
    const GraphPropertyDeclaration* property_decl;
    GOOGLESQL_RETURN_IF_ERROR(graph.FindPropertyDeclarationByName(
        def->property_declaration_name(), property_decl));
    GOOGLESQL_RETURN_IF_ERROR(ValidatePropertyDefinition(*property_decl));
    auto property_def = std::make_unique<SimpleGraphPropertyDefinition>(
        property_decl, def->sql());
    property_defs.push_back(std::move(property_def));
  }

  // Static labels.
  absl::flat_hash_set<const GraphElementLabel*> labels;
  for (const auto& label_name : resolved_element_table->label_name_list()) {
    const GraphElementLabel* label;
    GOOGLESQL_RETURN_IF_ERROR(graph.FindLabelByName(label_name, label));
    labels.insert(label);
  }

  // Dynamic properties.
  std::unique_ptr<SimpleGraphDynamicProperties>
      element_table_dynamic_properties;
  if (resolved_element_table->dynamic_properties() != nullptr) {
    const ResolvedExpr* dynamic_properties_expr =
        resolved_element_table->dynamic_properties()->property_expr();
    GOOGLESQL_RET_CHECK(dynamic_properties_expr->Is<ResolvedColumnRef>());
    GOOGLESQL_RET_CHECK(dynamic_properties_expr->type()->IsJson());

    // CAVEAT: If the original properties specification in the DDL is a path
    // expression, we only restore the last name in the path as the SQL
    // expression of the dynamic properties.
    // Note that column name can not be internal here as it must be catalog
    // table column.
    std::string dynamic_properties_col_name;
    const Column* unused = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(GetColumnAndNameFromColumnRef(
        *dynamic_properties_expr, table, unused, dynamic_properties_col_name));
    element_table_dynamic_properties =
        std::make_unique<SimpleGraphDynamicProperties>(
            dynamic_properties_col_name);
  }

  // Dynamic labels.
  std::unique_ptr<SimpleGraphDynamicLabel> element_table_dynamic_label;
  if (resolved_element_table->dynamic_label() != nullptr) {
    const ResolvedExpr* dynamic_label_expr =
        resolved_element_table->dynamic_label()->label_expr();
    GOOGLESQL_RET_CHECK(dynamic_label_expr->Is<ResolvedColumnRef>());
    GOOGLESQL_RET_CHECK(
        dynamic_label_expr->type()->IsString() ||
        (dynamic_label_expr->type()->IsArray() &&
         dynamic_label_expr->type()->AsArray()->element_type()->IsString()));

    // CAVEAT: If the original label specification in the DDL is a path
    // expression, we only restore the last name in the path as the SQL
    // expression of the dynamic label.
    // Note that column name can not be internal here as it must be catalog
    // table column.
    std::string dynamic_label_col_name;
    const Column* unused = nullptr;
    GOOGLESQL_RETURN_IF_ERROR(GetColumnAndNameFromColumnRef(
        *dynamic_label_expr, table, unused, dynamic_label_col_name));
    element_table_dynamic_label =
        std::make_unique<SimpleGraphDynamicLabel>(dynamic_label_col_name);
  }

  std::unique_ptr<const ElementTableType> element_table;
  if constexpr (std::is_same_v<ElementTableType, GraphNodeTable>) {
    element_table = std::make_unique<SimpleGraphNodeTable>(
        resolved_element_table->alias(), graph.NamePath(), table,
        std::move(key_indices), std::move(labels), std::move(property_defs),
        std::move(element_table_dynamic_label),
        std::move(element_table_dynamic_properties)
    );
    graph.AddNodeTable(std::move(element_table));
    return absl::OkStatus();
  }
  if constexpr (std::is_same_v<ElementTableType, GraphEdgeTable>) {
    auto reference_builder =
        [&](const ResolvedGraphNodeTableReference* reference)
        -> absl::StatusOr<std::unique_ptr<const GraphNodeTableReference>> {
      const GraphElementTable* referenced_node_table;
      GOOGLESQL_RETURN_IF_ERROR(graph.FindElementTableByName(
          reference->node_table_identifier(), referenced_node_table));
      GOOGLESQL_RET_CHECK(referenced_node_table->Is<GraphNodeTable>());
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::vector<int> edge_key_indices,
          GetColumnIndices(table, reference->edge_table_column_list()));
      GOOGLESQL_ASSIGN_OR_RETURN(std::vector<int> node_key_indices,
                       GetColumnIndices(referenced_node_table->GetTable(),
                                        reference->node_table_column_list()));
      return std::make_unique<const SimpleGraphNodeTableReference>(
          referenced_node_table->GetAs<GraphNodeTable>(),
          std::move(edge_key_indices), std::move(node_key_indices));
    };
    GOOGLESQL_ASSIGN_OR_RETURN(
        auto source_node,
        reference_builder(resolved_element_table->source_node_reference()));
    GOOGLESQL_ASSIGN_OR_RETURN(
        auto dest_node,
        reference_builder(resolved_element_table->dest_node_reference()));
    element_table = std::make_unique<SimpleGraphEdgeTable>(
        resolved_element_table->alias(), graph.NamePath(), table,
        std::move(key_indices), std::move(labels), std::move(property_defs),
        std::move(source_node), std::move(dest_node),
        std::move(element_table_dynamic_label),
        std::move(element_table_dynamic_properties)
    );
    graph.AddEdgeTable(std::move(element_table));
    return absl::OkStatus();
  }
  GOOGLESQL_RET_CHECK_FAIL() << "Unknown element table type";
}

static absl::StatusOr<std::unique_ptr<SimplePropertyGraph>>
PopulatePropertyGraph(
    const ResolvedCreatePropertyGraphStmt* create_graph_stmt) {
  auto graph =
      std::make_unique<SimplePropertyGraph>(create_graph_stmt->name_path());
  // Add all property declarations.
  for (const auto& resolved_property_decl :
       create_graph_stmt->property_declaration_list()) {
    graph->AddPropertyDeclaration(
        std::make_unique<SimpleGraphPropertyDeclaration>(
            resolved_property_decl->name(), graph->NamePath(),
            resolved_property_decl->type()));
  }
  // Add all labels.
  for (const auto& resolved_label : create_graph_stmt->label_list()) {
    absl::flat_hash_set<const GraphPropertyDeclaration*> property_declarations;
    absl::Status find_status;
    for (const auto& property_name :
         resolved_label->property_declaration_name_list()) {
      const GraphPropertyDeclaration* property_declaration;
      GOOGLESQL_RETURN_IF_ERROR(graph->FindPropertyDeclarationByName(
          property_name, property_declaration));
      property_declarations.insert(property_declaration);
    }
    graph->AddLabel(std::make_unique<SimpleGraphElementLabel>(
        resolved_label->name(), graph->NamePath(), property_declarations));
  }
  // Add all node tables.
  for (const auto& node_table : create_graph_stmt->node_table_list()) {
    GOOGLESQL_RETURN_IF_ERROR(AddElementTable<GraphNodeTable>(node_table.get(), *graph));
  }
  // Add all edge tables.
  for (const auto& edge_table : create_graph_stmt->edge_table_list()) {
    GOOGLESQL_RETURN_IF_ERROR(AddElementTable<GraphEdgeTable>(edge_table.get(), *graph));
  }

  return std::move(graph);
}

static absl::Status ResolveGraphPropertyDefinitions(
    LanguageOptions language_options, const PropertyGraph* graph,
    SimpleCatalog* catalog,
    std::vector<std::unique_ptr<const AnalyzerOutput>>& artifacts) {
  // Force graph features on for schema setup.
  language_options.EnableLanguageFeature(FEATURE_SQL_GRAPH);
  GOOGLESQL_RETURN_IF_ERROR(language_options.EnableReservableKeyword("GRAPH_TABLE"));
  AnalyzerOptions options(language_options);
  options.CreateDefaultArenasIfNotSet();

  absl::flat_hash_set<const GraphNodeTable*> node_tables;
  GOOGLESQL_RET_CHECK_OK(graph->GetNodeTables(node_tables));
  absl::flat_hash_set<const GraphEdgeTable*> edge_tables;
  GOOGLESQL_RET_CHECK_OK(graph->GetEdgeTables(edge_tables));

  std::vector<const GraphElementTable*> element_tables;
  for (const GraphNodeTable* node_table : node_tables) {
    element_tables.push_back(node_table);
  }
  for (const GraphEdgeTable* edge_table : edge_tables) {
    element_tables.push_back(edge_table);
  }

  for (const GraphElementTable* element_table : element_tables) {
    options.SetLookupCatalogColumnCallback(
        [element_table](
            const std::string& column_name) -> absl::StatusOr<const Column*> {
          const Column* column =
              element_table->GetTable()->FindColumnByName(column_name);
          if (column == nullptr) {
            return absl::NotFoundError(
                absl::StrCat("Cannot find column named ", column_name));
          }
          return column;
        });

    absl::flat_hash_set<const GraphPropertyDefinition*> property_definitions;
    GOOGLESQL_RETURN_IF_ERROR(
        element_table->GetPropertyDefinitions(property_definitions));
    for (const GraphPropertyDefinition* definition : property_definitions) {
      GOOGLESQL_RET_CHECK(definition->Is<SimpleGraphPropertyDefinition>());
      std::unique_ptr<const AnalyzerOutput> analyzer_output;
      GOOGLESQL_RETURN_IF_ERROR(AnalyzeExpression(definition->expression_sql(), options,
                                        catalog, catalog->type_factory(),
                                        &analyzer_output));

      InternalPropertyGraph::InternalSetResolvedExpr(
          const_cast<SimpleGraphPropertyDefinition*>(
              definition->GetAs<SimpleGraphPropertyDefinition>()),
          analyzer_output->resolved_expr());
      artifacts.push_back(std::move(analyzer_output));
    }

    if (element_table->HasDynamicProperties()) {
      const GraphDynamicProperties* dynamic_prop = nullptr;
      GOOGLESQL_RETURN_IF_ERROR(element_table->GetDynamicProperties(dynamic_prop));
      GOOGLESQL_RET_CHECK_NE(dynamic_prop, nullptr);
      GOOGLESQL_RET_CHECK(dynamic_prop->Is<SimpleGraphDynamicProperties>());

      std::unique_ptr<const AnalyzerOutput> analyzer_output;
      GOOGLESQL_RETURN_IF_ERROR(AnalyzeExpression(
          dynamic_prop->properties_expression(), options, catalog,
          catalog->type_factory(), &analyzer_output));
      InternalPropertyGraph::InternalSetResolvedExpr(
          const_cast<SimpleGraphDynamicProperties*>(
              dynamic_prop->GetAs<SimpleGraphDynamicProperties>()),
          analyzer_output->resolved_expr());
      artifacts.push_back(std::move(analyzer_output));
    }

    if (element_table->HasDynamicLabel()) {
      const GraphDynamicLabel* dynamic_label = nullptr;
      GOOGLESQL_RETURN_IF_ERROR(element_table->GetDynamicLabel(dynamic_label));
      GOOGLESQL_RET_CHECK_NE(dynamic_label, nullptr);
      GOOGLESQL_RET_CHECK(dynamic_label->Is<SimpleGraphDynamicLabel>());

      std::unique_ptr<const AnalyzerOutput> analyzer_output;
      GOOGLESQL_RETURN_IF_ERROR(
          AnalyzeExpression(dynamic_label->label_expression(), options, catalog,
                            catalog->type_factory(), &analyzer_output));
      InternalPropertyGraph::InternalSetResolvedExpr(
          const_cast<SimpleGraphDynamicLabel*>(
              dynamic_label->GetAs<SimpleGraphDynamicLabel>()),
          analyzer_output->resolved_expr());
      artifacts.push_back(std::move(analyzer_output));
    }
  }

  return absl::OkStatus();
}

absl::Status AddPropertyGraphFromCreatePropertyGraphStmt(
    absl::string_view create_property_graph_stmt,
    const AnalyzerOptions& analyzer_options,
    std::vector<std::unique_ptr<const AnalyzerOutput>>& artifacts,
    SimpleCatalog& catalog) {
  GOOGLESQL_RET_CHECK(analyzer_options.language().SupportsStatementKind(
      RESOLVED_CREATE_PROPERTY_GRAPH_STMT));
  auto& analyzer_output = artifacts.emplace_back();
  GOOGLESQL_RETURN_IF_ERROR(AnalyzeStatement(create_property_graph_stmt, analyzer_options,
                                   &catalog, catalog.type_factory(),
                                   &analyzer_output))
      << create_property_graph_stmt;
  const ResolvedStatement* resolved = analyzer_output->resolved_statement();
  GOOGLESQL_RET_CHECK(resolved->Is<ResolvedCreatePropertyGraphStmt>());
  const auto* resolved_create =
      resolved->GetAs<ResolvedCreatePropertyGraphStmt>();
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<SimplePropertyGraph> graph,
                   PopulatePropertyGraph(resolved_create));

  GOOGLESQL_RETURN_IF_ERROR(ResolveGraphPropertyDefinitions(
      analyzer_options.language(), graph.get(), &catalog, artifacts));

  std::string graph_name = graph->Name();
  GOOGLESQL_RET_CHECK(catalog.AddOwnedPropertyGraphIfNotPresent(std::move(graph_name),
                                                      std::move(graph)))
      << absl::StrJoin(resolved_create->name_path(), ".");

  return absl::OkStatus();
}

absl::Status AddConstantFromCreateConstant(
    absl::string_view create_constant_stmt,
    const AnalyzerOptions& analyzer_options,
    std::unique_ptr<const AnalyzerOutput>& analyzer_output,
    SimpleCatalog& catalog) {
  GOOGLESQL_RET_CHECK(analyzer_options.language().SupportsStatementKind(
      RESOLVED_CREATE_CONSTANT_STMT));
  GOOGLESQL_RETURN_IF_ERROR(AnalyzeStatement(create_constant_stmt, analyzer_options,
                                   &catalog, catalog.type_factory(),
                                   &analyzer_output))
      << create_constant_stmt;
  const ResolvedStatement* resolved = analyzer_output->resolved_statement();
  GOOGLESQL_RET_CHECK(resolved->Is<ResolvedCreateConstantStmt>());
  const ResolvedCreateConstantStmt* stmt =
      resolved->GetAs<ResolvedCreateConstantStmt>();
  std::vector<std::string> name_path = stmt->name_path();
  ConstantEvaluator* evaluator = analyzer_options.constant_evaluator();
  GOOGLESQL_RET_CHECK(evaluator != nullptr);
  GOOGLESQL_ASSIGN_OR_RETURN(Value const_value, evaluator->Evaluate(*stmt->expr()));
  std::unique_ptr<SimpleConstant> constant;
  GOOGLESQL_RETURN_IF_ERROR(
      SimpleConstant::Create(std::move(name_path), const_value, &constant));
  GOOGLESQL_RET_CHECK(catalog.AddOwnedConstantIfNotPresent(std::move(constant)));
  return absl::OkStatus();
}

}  // namespace googlesql
