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

#include "googlesql/public/table_name_resolver.h"

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/thread_stack.h"
#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parse_tree_decls.h"
#include "googlesql/parser/parse_tree_errors.h"
#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "absl/container/btree_set.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status.h"
#include "googlesql/base/status_macros.h"

// TODO This implementation probably doesn't cover all edge cases for
// table name extraction.  It should be tested more and tuned for the final
// name scoping rules once those are implemented in the full resolver.

namespace googlesql {
namespace table_name_resolver {
namespace {

#define RETURN_ERROR_IF_OUT_OF_STACK_SPACE()                      \
  GOOGLESQL_RETURN_IF_NOT_ENOUGH_STACK(                           \
      "Out of stack space due to deeply nested query expression " \
      "during table name extraction")

// Each instance should be used only once.
class TableNameResolver {
 public:
  // <*analyzer_options> must outlive the created TableNameResolver. It must
  // have all arenas initialized.
  // If 'type_factory' and 'catalog' are not null, their contents must
  // outlive the created TableNameResolver as well.
  //
  TableNameResolver(absl::string_view sql,
                    const AnalyzerOptions* analyzer_options,
                    TypeFactory* type_factory, Catalog* catalog,
                    TableNamesSet* table_names,
                    TableResolutionTimeInfoMap* table_resolution_time_info_map,
                    TableNamesSet* tvf_names)
      : sql_(sql),
        analyzer_options_(analyzer_options),
        for_system_time_as_of_feature_enabled_(
            analyzer_options->language().LanguageFeatureEnabled(
                FEATURE_FOR_SYSTEM_TIME_AS_OF)),
        type_factory_(type_factory),
        catalog_(catalog),
        table_names_(table_names),
        tvf_names_(tvf_names),
        table_resolution_time_info_map_(table_resolution_time_info_map) {
    ABSL_DCHECK(analyzer_options_->AllArenasAreInitialized());
  }

  TableNameResolver(const TableNameResolver&) = delete;
  TableNameResolver& operator=(const TableNameResolver&) = delete;

  absl::Status FindTableNamesAndTemporalReferences(
      const ASTStatement& statement);

  absl::Status FindTableNames(const ASTScript& script);

 private:
  typedef absl::btree_set<std::string> AliasSet;  // Always lowercase.

  absl::Status FindInStatement(const ASTStatement* statement);

  // Consumes either an ASTScript, ASTStatementList, or ASTScriptStatement.
  absl::Status FindInScriptNode(const ASTNode* node);

  absl::Status FindInQueryStatement(const ASTQueryStatement* statement);
  absl::Status FindInSubpipelineStatement(
      const ASTSubpipelineStatement* statement);

  absl::Status FindInCreateViewStatement(
      const ASTCreateViewStatement* statement);

  absl::Status FindInCreateMaterializedViewStatement(
      const ASTCreateMaterializedViewStatement* statement);

  absl::Status FindInCreateApproxViewStatement(
      const ASTCreateApproxViewStatement* statement);

  absl::Status FindInCreateTableFunctionStatement(
      const ASTCreateTableFunctionStatement* statement);

  absl::Status FindInCloneDataStatement(const ASTCloneDataStatement* statement);

  absl::Status FindInExportDataStatement(
      const ASTExportDataStatement* statement);

  absl::Status FindInDeleteStatement(const ASTDeleteStatement* statement);

  absl::Status FindInTruncateStatement(const ASTTruncateStatement* statement);

  absl::Status FindInInsertStatement(const ASTInsertStatement* statement);

  absl::Status FindInUpdateStatement(const ASTUpdateStatement* statement);

  absl::Status FindInMergeStatement(const ASTMergeStatement* statement);

  // Process the ASTWithClause that's part of an ASTQuery or ASTPipeWith.
  // This adds CTE names to `local_table_aliases_`.
  // The caller is responsible for saving the old state of that list before,
  // and then restoring it later, when these CTEs go out of scope.
  absl::Status HandleWithClause(const ASTWithClause* with_clause,
                                const AliasSet& visible_aliases);

  // 'visible_aliases' includes things like the table name we are inserting
  // into or deleting from.  It does *not* include WITH table aliases or TVF
  // table-valued argument names (which are both tracked separately in
  // 'local_table_aliases_').
  // Range variables this query exports will be added to 'new_aliases'.
  // The default value can be used when those aliases are ignored.
  absl::Status FindInQuery(
      const ASTQuery* query, const AliasSet& visible_aliases,
      AliasSet* new_aliases = std::make_unique<AliasSet>().get());

  // If non-null, range variables this query exports will be added to
  // 'new_aliases'.
  absl::Status FindInFromQuery(const ASTFromQuery* from_query,
                               const AliasSet& visible_aliases,
                               AliasSet* new_aliases);

  // 'visible_aliases' are the aliases that can be resolved inside the query.
  // Range variables this query exports will be added to 'new_aliases'.
  absl::Status FindInQueryExpression(const ASTQueryExpression* query_expr,
                                     const ASTOrderBy* order_by,
                                     const AliasSet& visible_aliases,
                                     AliasSet* new_aliases);

  absl::Status FindInAliasedQueryExpression(
      const ASTAliasedQueryExpression* aliased_query,
      const AliasSet& visible_aliases, AliasSet* new_aliases);

  absl::Status FindInSelect(const ASTSelect* select, const ASTOrderBy* order_by,
                            const AliasSet& orig_visible_aliases);

  absl::Status FindInSetOperation(const ASTSetOperation* set_operation,
                                  const AliasSet& visible_aliases);

  // When resolving the FROM clause, <external_visible_aliases> is the set
  // of names visible in the query without any names from the FROM clause
  // (excluding WITH names and TVF table-valued argument names).
  // <local_visible_aliases> includes all names visible in
  // <external_visible_alaises> plus names earlier in the same FROM clause
  // that are visible.  See corresponding methods in resolver.cc.
  absl::Status FindInTableExpression(const ASTTableExpression* table_expr,
                                     const AliasSet& external_visible_aliases,
                                     AliasSet* local_visible_aliases);

  absl::Status FindInJoin(const ASTJoin* join,
                          const AliasSet& external_visible_aliases,
                          AliasSet* local_visible_aliases);

  absl::Status FindInParenthesizedJoin(
      const ASTParenthesizedJoin* parenthesized_join,
      const AliasSet& external_visible_aliases,
      AliasSet* local_visible_aliases);

  absl::Status FindInTVF(const ASTTVF* tvf,
                         const AliasSet& external_visible_aliases,
                         AliasSet* local_visible_aliases);

  absl::Status FindInCreatePropertyGraphStatement(
      const ASTCreatePropertyGraphStatement* statement);

  absl::Status FindInGraphTableQuery(const ASTGraphTableQuery* graph_query,
                                     const AliasSet& external_visible_aliases,
                                     AliasSet* local_visible_aliases);
  absl::Status FindInGraphPatternQuery(
      const ASTGqlGraphPatternQuery* graph_pattern_query,
      const AliasSet& external_visible_aliases,
      AliasSet* local_visible_aliases);
  absl::Status FindInGraphLinearOpsQuery(
      const ASTGqlLinearOpsQuery* graph_linear_ops_query,
      const AliasSet& external_visible_aliases,
      AliasSet* local_visible_aliases);
  absl::Status FindInTableClause(const ASTTableClause* table_clause,
                                 const AliasSet& external_visible_aliases,
                                 AliasSet* local_visible_aliases);

  absl::Status FindInGqlCallsOnNamedTvfsUnder(
      const ASTGraphTableQuery* graph_query,
      const AliasSet& external_visible_aliases,
      AliasSet* local_visible_aliases);
  absl::Status FindInTableSubquery(const ASTTableSubquery* table_subquery,
                                   const AliasSet& external_visible_aliases,
                                   AliasSet* local_visible_aliases);

  absl::Status FindInTablePathExpression(
      const ASTTablePathExpression* table_ref, AliasSet* visible_aliases);

  absl::Status ResolveTablePath(const std::vector<std::string>& path,
                                const ASTForSystemTime* for_system_time);

  absl::Status FindInTableElements(const ASTTableElementList* elements);

  // Traverse all expressions attached as descendants of <root>.
  // Unlike other methods above, may be called with NULL.
  absl::Status FindInExpressionsUnder(const ASTNode* root,
                                      const AliasSet& visible_aliases);

  // Traverse all options_list node as descendants of <root>.
  // May be called with NULL.
  absl::Status FindInOptionsListUnder(const ASTNode* root,
                                      const AliasSet& visible_aliases);

  // Collects table names from a pipe recursive union.
  absl::Status FindInPipeRecursiveUnion(
      const ASTPipeRecursiveUnion* recursive_union,
      const AliasSet& visible_aliases, AliasSet* new_aliases);

  // Collects table names from the given list of pipe operators
  // `pipe_operator_list`.
  //
  // `visible_aliases` are the aliases that can be resolved inside the query.
  // `new_aliases`: the output variable that contains the new aliases introduced
  // by the pipe operators.
  absl::Status FindInPipeOperatorList(
      absl::Span<const ASTPipeOperator* const> pipe_operator_list,
      const AliasSet& visible_aliases, AliasSet* new_aliases);

  // Root level SQL statement we are extracting table names or temporal
  // references from.
  const absl::string_view sql_;

  const AnalyzerOptions* analyzer_options_;  // Not owned.

  const bool for_system_time_as_of_feature_enabled_;

  TypeFactory* type_factory_;

  Catalog* catalog_;

  // The set of table names we are building up in this call to FindTables.
  // NOTE: The raw pointer is not owned.  We just cache the output parameter
  // to FindTables/FindTableNamesAndTemporalReferences to simplify sharing
  // across recursive calls.
  TableNamesSet* table_names_ = nullptr;

  // The set of TVF names we are building up in this call to FindTables.
  // NOTE: This is also a borrowed reference. The raw pointer is not owned.
  // We just cache the output parameter to FindTables/
  // FindTableNamesAndTemporalReferences to simplify sharing
  // across recursive calls.
  TableNamesSet* tvf_names_ = nullptr;

  // The set of temporal table references we are building up
  // in this call to FindTemporalTableReferencess.
  // NOTE: The raw pointer is not owned.  We just cache the output parameter
  // to FindTableNamesAndTemporalReferences to simplify sharing
  // across recursive calls.
  TableResolutionTimeInfoMap* table_resolution_time_info_map_ = nullptr;

  // The set of local table aliases, including TVF table-valued argument
  // aliases and in-scope WITH aliases.
  AliasSet local_table_aliases_;

  // When inside a CREATE RECURSIVE VIEW statement, the name of the view; such
  // names should be treated similar to a WITH alias and not be considered an
  // external reference. In all other cases, this field is an empty vector.
  std::vector<std::string> recursive_view_name_;
};

absl::Status TableNameResolver::FindTableNamesAndTemporalReferences(
    const ASTStatement& statement) {
  table_names_->clear();
  if (tvf_names_ != nullptr) tvf_names_->clear();
  if (table_resolution_time_info_map_ != nullptr) {
    GOOGLESQL_RET_CHECK_EQ((type_factory_ == nullptr), (catalog_ == nullptr));
    table_resolution_time_info_map_->clear();
  }

  GOOGLESQL_RETURN_IF_ERROR(FindInStatement(&statement));
  // Sanity check - these should get popped.
  GOOGLESQL_RET_CHECK(local_table_aliases_.empty());
  return absl::OkStatus();
}

absl::Status TableNameResolver::FindTableNames(const ASTScript& script) {
  table_names_->clear();
  if (tvf_names_ != nullptr) {
    tvf_names_->clear();
  }
  GOOGLESQL_RETURN_IF_ERROR(FindInScriptNode(&script));
  // Sanity check - these should get popped.
  GOOGLESQL_RET_CHECK(local_table_aliases_.empty());
  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInScriptNode(const ASTNode* node) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  for (int i = 0; i < node->num_children(); ++i) {
    const ASTNode* child = node->child(i);
    if (child->IsExpression()) {
      GOOGLESQL_RETURN_IF_ERROR(FindInExpressionsUnder(child, /*visible_aliases=*/{}));
    } else if (child->IsSqlStatement()) {
      GOOGLESQL_RETURN_IF_ERROR(FindInStatement(child->GetAs<ASTStatement>()));
    } else {
      GOOGLESQL_RETURN_IF_ERROR(FindInScriptNode(child));
    }
  }
  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInPipeRecursiveUnion(
    const ASTPipeRecursiveUnion* recursive_union,
    const AliasSet& visible_aliases, AliasSet* new_aliases) {
  // Register the local alias, if provided.
  bool inserted = false;
  std::string recursive_alias;
  if (recursive_union->alias() != nullptr) {
    recursive_alias =
        absl::AsciiStrToLower(recursive_union->alias()->GetAsStringView());
    inserted = local_table_aliases_.insert(recursive_alias).second;
  }

  // Recursively find table names in the input subquery or input subpipeline.
  if (recursive_union->input_subquery() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(FindInExpressionsUnder(recursive_union, visible_aliases));
  } else {
    GOOGLESQL_RET_CHECK(recursive_union->input_subpipeline() != nullptr);
    GOOGLESQL_RETURN_IF_ERROR(FindInPipeOperatorList(
        recursive_union->input_subpipeline()->pipe_operator_list(),
        visible_aliases, new_aliases));
  }

  // If not inserted, it means `recursive_alias` shadows an outer alias. Do not
  // remove it.
  if (inserted) {
    local_table_aliases_.erase(recursive_alias);
  }
  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInStatement(const ASTStatement* statement) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  // Find table name under OPTIONS (...) clause for any type of statement.
  GOOGLESQL_RETURN_IF_ERROR(FindInOptionsListUnder(statement, /*visible_aliases=*/{}));
  switch (statement->node_kind()) {
    case AST_QUERY_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_QUERY_STMT)) {
        return FindInQueryStatement(
            static_cast<const ASTQueryStatement*>(statement));
      }
      break;

    case AST_SUBPIPELINE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_SUBPIPELINE_STMT)) {
        return FindInSubpipelineStatement(
            static_cast<const ASTSubpipelineStatement*>(statement));
      }
      break;

    case AST_EXPLAIN_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_EXPLAIN_STMT)) {
        const ASTExplainStatement* explain =
            static_cast<const ASTExplainStatement*>(statement);
        return FindInStatement(explain->statement());
      }
      break;

    case AST_STATEMENT_WITH_PIPE_OPERATORS:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_STATEMENT_WITH_PIPE_OPERATORS_STMT)) {
        const ASTStatementWithPipeOperators* ast_stmt =
            static_cast<const ASTStatementWithPipeOperators*>(statement);
        return FindInStatement(ast_stmt->statement());
      }
      break;

    case AST_CREATE_CONNECTION_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_CONNECTION_STMT)) {
        return absl::OkStatus();
      }
      break;
    case AST_CREATE_DATABASE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_DATABASE_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_CREATE_INDEX_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_INDEX_STMT)) {
        const ASTCreateIndexStatement* create_index =
            static_cast<const ASTCreateIndexStatement*>(statement);
        googlesql_base::InsertIfNotPresent(
            table_names_, create_index->table_name()->ToIdentifierVector());
        return absl::OkStatus();
      }
      break;

    case AST_ALTER_INDEX_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ALTER_INDEX_STMT)) {
        const ASTAlterIndexStatement* alter_index =
            static_cast<const ASTAlterIndexStatement*>(statement);
        if (alter_index->table_name() != nullptr) {
          googlesql_base::InsertIfNotPresent(
              table_names_, alter_index->table_name()->ToIdentifierVector());
        }
        return absl::OkStatus();
      }
      break;

    case AST_CREATE_SCHEMA_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_SCHEMA_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_CREATE_EXTERNAL_SCHEMA_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_EXTERNAL_SCHEMA_STMT) &&
          analyzer_options_->language().LanguageFeatureEnabled(
              FEATURE_EXTERNAL_SCHEMA_DDL)) {
        return absl::OkStatus();
      }
      break;

    case AST_CREATE_SNAPSHOT_TABLE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_SNAPSHOT_TABLE_STMT)) {
        const ASTCreateSnapshotTableStatement* stmt =
            statement->GetAs<ASTCreateSnapshotTableStatement>();
        const ASTCloneDataSource* clone_data_source = stmt->clone_data_source();
        GOOGLESQL_RETURN_IF_ERROR(ResolveTablePath(
            clone_data_source->path_expr()->ToIdentifierVector(),
            clone_data_source->for_system_time()));
        return absl::OkStatus();
      }
      break;
    case AST_CREATE_TABLE_STATEMENT: {
      const ASTCreateTableStatement* stmt =
          statement->GetAs<ASTCreateTableStatement>();

      const ASTTableElementList* table_elements = stmt->table_element_list();
      if (table_elements != nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(FindInTableElements(table_elements));
      }

      const ASTPathExpression* like_table_name = stmt->like_table_name();
      if (like_table_name != nullptr) {
        googlesql_base::InsertIfNotPresent(table_names_,
                                like_table_name->ToIdentifierVector());
      }

      const ASTTableDataSource* clone_data_source = stmt->clone_data_source();
      if (clone_data_source != nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(ResolveTablePath(
            clone_data_source->path_expr()->ToIdentifierVector(),
            clone_data_source->for_system_time()));
      }
      const ASTTableDataSource* copy_data_source = stmt->copy_data_source();
      if (copy_data_source != nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(ResolveTablePath(
            copy_data_source->path_expr()->ToIdentifierVector(),
            copy_data_source->for_system_time()));
      }

      const ASTQuery* query = stmt->query();
      if (query == nullptr) {
        // We allow either of these because we hit this case when analyzing
        // queries using FEATURE_PIPE_CREATE_TABLE even if
        // RESOLVED_CREATE_TABLE_STMT is not supported.
        if (analyzer_options_->language().SupportsStatementKind(
                RESOLVED_CREATE_TABLE_STMT) ||
            analyzer_options_->language().LanguageFeatureEnabled(
                FEATURE_PIPE_CREATE_TABLE)) {
          return absl::OkStatus();
        }
      } else {
        if (analyzer_options_->language().SupportsStatementKind(
                RESOLVED_CREATE_TABLE_AS_SELECT_STMT)) {
          return FindInQuery(query, /*visible_aliases=*/{});
        }
      }
      break;
    }
    case AST_CREATE_MODEL_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_MODEL_STMT)) {
        const ASTQuery* query =
            statement->GetAs<ASTCreateModelStatement>()->query();
        const ASTAliasedQueryList* aliased_query_list =
            statement->GetAs<ASTCreateModelStatement>()->aliased_query_list();
        if (query != nullptr) {
          return FindInQuery(query, /*visible_aliases=*/{});
        }
        if (analyzer_options_->language().LanguageFeatureEnabled(
                FEATURE_CREATE_MODEL_WITH_ALIASED_QUERY_LIST) &&
            aliased_query_list != nullptr) {
          for (const ASTAliasedQuery* aliased_query :
               aliased_query_list->aliased_query_list()) {
            GOOGLESQL_RETURN_IF_ERROR(
                FindInQuery(aliased_query->query(), /*visible_aliases=*/{}));
          }
        }
        return absl::OkStatus();
      }
      break;
    case AST_CREATE_VIEW_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_VIEW_STMT)) {
        return FindInCreateViewStatement(
            statement->GetAs<ASTCreateViewStatement>());
      }
      break;
    case AST_CREATE_MATERIALIZED_VIEW_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_MATERIALIZED_VIEW_STMT)) {
        return FindInCreateMaterializedViewStatement(
            statement->GetAs<ASTCreateMaterializedViewStatement>());
      }
      break;
    case AST_CREATE_APPROX_VIEW_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_APPROX_VIEW_STMT)) {
        return FindInCreateApproxViewStatement(
            statement->GetAs<ASTCreateApproxViewStatement>());
      }
      break;

    case AST_CREATE_EXTERNAL_TABLE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_EXTERNAL_TABLE_STMT)) {
        const ASTCreateExternalTableStatement* stmt =
            statement->GetAs<ASTCreateExternalTableStatement>();

        const ASTTableElementList* table_elements = stmt->table_element_list();
        if (table_elements != nullptr) {
          GOOGLESQL_RETURN_IF_ERROR(FindInTableElements(table_elements));
        }
        return absl::OkStatus();
      }
      break;

    case AST_CREATE_PRIVILEGE_RESTRICTION_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_PRIVILEGE_RESTRICTION_STMT)) {
        const ASTCreatePrivilegeRestrictionStatement* stmt =
            statement->GetAsOrDie<ASTCreatePrivilegeRestrictionStatement>();
        googlesql_base::InsertIfNotPresent(table_names_,
                                stmt->name_path()->ToIdentifierVector());
        return absl::OkStatus();
      }
      break;

    case AST_CREATE_ROW_ACCESS_POLICY_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_ROW_ACCESS_POLICY_STMT)) {
        const ASTCreateRowAccessPolicyStatement* stmt =
            statement->GetAsOrDie<ASTCreateRowAccessPolicyStatement>();
        googlesql_base::InsertIfNotPresent(table_names_,
                                stmt->target_path()->ToIdentifierVector());
        return FindInExpressionsUnder(stmt->filter_using()->predicate(),
                                      /*visible_aliases=*/{});
      }
      break;

    case AST_CREATE_CONSTANT_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_CONSTANT_STMT)) {
        GOOGLESQL_RETURN_IF_ERROR(FindInExpressionsUnder(
            static_cast<const ASTCreateConstantStatement*>(statement)->expr(),
            /*visible_aliases=*/{}));
        return absl::OkStatus();
      }
      break;

    case AST_CREATE_FUNCTION_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_FUNCTION_STMT)) {
        GOOGLESQL_RETURN_IF_ERROR(FindInExpressionsUnder(
            static_cast<const ASTCreateFunctionStatement*>(statement)
                ->sql_function_body(),
            /*visible_aliases=*/{}));
        return absl::OkStatus();
      }
      break;

    case AST_CREATE_TABLE_FUNCTION_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_TABLE_FUNCTION_STMT)) {
        return FindInCreateTableFunctionStatement(
            statement->GetAs<ASTCreateTableFunctionStatement>());
      }
      break;

    case AST_CREATE_PROCEDURE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_PROCEDURE_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_CREATE_PROPERTY_GRAPH_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_PROPERTY_GRAPH_STMT)) {
        return FindInCreatePropertyGraphStatement(
            statement->GetAs<ASTCreatePropertyGraphStatement>());
      }
      break;

    case AST_CREATE_SEQUENCE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_SEQUENCE_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_CLONE_DATA_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CLONE_DATA_STMT)) {
        return FindInCloneDataStatement(
            statement->GetAs<ASTCloneDataStatement>());
      }
      break;

    case AST_EXPORT_DATA_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_EXPORT_DATA_STMT)) {
        return FindInExportDataStatement(
            statement->GetAs<ASTExportDataStatement>());
      }
      break;

    case AST_EXPORT_METADATA_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_EXPORT_METADATA_STMT)) {
        // TODO: Extract table name from export metadata statement.
        return absl::OkStatus();
      }
      break;

    case AST_EXPORT_MODEL_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_EXPORT_MODEL_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_CALL_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CALL_STMT)) {
        const ASTCallStatement* call =
            statement->GetAsOrDie<ASTCallStatement>();
        for (const ASTTVFArgument* arg : call->arguments()) {
          GOOGLESQL_RETURN_IF_ERROR(
              FindInExpressionsUnder(arg->expr(), /*visible_aliases=*/{}));
        }
        return absl::OkStatus();
      }
      break;

    case AST_DEFINE_TABLE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_DEFINE_TABLE_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_DESCRIBE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_DESCRIBE_STMT)) {
        // Note that for a DESCRIBE TABLE statement, the table name is not
        // inserted into table_names_. Engines that need to know about a table
        // referenced by DESCRIBE TABLE should handle that themselves.
        return absl::OkStatus();
      }
      break;

    case AST_SHOW_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_SHOW_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_BEGIN_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_BEGIN_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_SET_TRANSACTION_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_SET_TRANSACTION_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_COMMIT_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_COMMIT_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_ROLLBACK_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ROLLBACK_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_START_BATCH_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_START_BATCH_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_RUN_BATCH_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_RUN_BATCH_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_ABORT_BATCH_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ABORT_BATCH_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_DELETE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_DELETE_STMT)) {
        return FindInDeleteStatement(statement->GetAs<ASTDeleteStatement>());
      }
      break;

    case AST_UNDROP_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_UNDROP_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_DROP_STATEMENT:
    case AST_DROP_ENTITY_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_DROP_STMT)) {
        // Note that for a DROP TABLE statement, the table name is not
        // inserted into table_names_. Engines that need to know about a table
        // referenced by DROP TABLE should handle that themselves.
        return absl::OkStatus();
      }
      break;

    case AST_TRUNCATE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_TRUNCATE_STMT)) {
        return FindInTruncateStatement(
            statement->GetAsOrDie<ASTTruncateStatement>());
      }
      break;

    case AST_DROP_MATERIALIZED_VIEW_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_DROP_MATERIALIZED_VIEW_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_ALTER_MODEL_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ALTER_MODEL_STMT)) {
        // Note that for an ALTER MODEL statement, the model name is not
        // inserted into table_names_. Engines that need to know about a model
        // referenced by ALTER MODEL should handle that themselves.
        return absl::OkStatus();
      }
      break;

    case AST_DROP_SNAPSHOT_TABLE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_DROP_SNAPSHOT_TABLE_STMT)) {
        // Note that for a DROP SNAPSHOT TABLE statement, the table name is not
        // inserted into table_names_. Engines that need to know about a table
        // referenced by DROP TABLE should handle that themselves.
        return absl::OkStatus();
      }
      break;

    case AST_DROP_FUNCTION_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_DROP_FUNCTION_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_DROP_TABLE_FUNCTION_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_DROP_TABLE_FUNCTION_STMT)) {
        return absl::OkStatus();
      }
      break;

    case AST_DROP_PRIVILEGE_RESTRICTION_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_DROP_PRIVILEGE_RESTRICTION_STMT)) {
        const ASTDropPrivilegeRestrictionStatement* stmt =
            statement->GetAs<ASTDropPrivilegeRestrictionStatement>();
        googlesql_base::InsertIfNotPresent(table_names_,
                                stmt->name_path()->ToIdentifierVector());
        return absl::OkStatus();
      }
      break;

    case AST_DROP_ROW_ACCESS_POLICY_STATEMENT:
    case AST_DROP_ALL_ROW_ACCESS_POLICIES_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_DROP_ROW_ACCESS_POLICY_STMT)) {
        // Note that for a DROP [ALL] ROW (ACCESS POLICY|[ACCESS] POLICIES)
        // statement, the table name is not inserted into table_names_. Engines
        // that need to know about the target table should handle that
        // themselves.
        return absl::OkStatus();
      }
      break;

    case AST_DROP_SEARCH_INDEX_STATEMENT:
    case AST_DROP_VECTOR_INDEX_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_DROP_INDEX_STMT)) {
        // For a "DROP [SEARCH|VECTOR] INDEX <name> [ON <table>]" statement, the
        // table name is not inserted into table_names_. Engines that need to
        // know about the target table should handle that themselves.
        return absl::OkStatus();
      }
      break;

    case AST_RENAME_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_RENAME_STMT)) {
        // Note that for a RENAME TABLE statement, the table names are not
        // inserted into table_names_. Engines that need to know about a table
        // referenced by RENAME TABLE should handle that themselves.
        return absl::OkStatus();
      }
      break;

    case AST_INSERT_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_INSERT_STMT)) {
        return FindInInsertStatement(statement->GetAs<ASTInsertStatement>());
      }
      break;

    case AST_UPDATE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_UPDATE_STMT)) {
        return FindInUpdateStatement(statement->GetAs<ASTUpdateStatement>());
      }
      break;

    case AST_MERGE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_MERGE_STMT)) {
        return FindInMergeStatement(statement->GetAs<ASTMergeStatement>());
      }
      break;

    case AST_GRANT_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_GRANT_STMT)) {
        // Note that for a GRANT statement, the table name is not inserted
        // into table_names_. Engines that need to know about a table
        // referenced by GRANT statement should handle that themselves.
        return absl::OkStatus();
      }
      break;

    case AST_REVOKE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_REVOKE_STMT)) {
        // Note that for a REVOKE statement, the table name is not inserted
        // into table_names_. Engines that need to know about a table
        // referenced by REVOKE statement should handle that themselves.
        return absl::OkStatus();
      }
      break;
    case AST_ALTER_CONNECTION_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ALTER_CONNECTION_STMT)) {
        return absl::OkStatus();
      }
      break;
    case AST_ALTER_PRIVILEGE_RESTRICTION_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ALTER_PRIVILEGE_RESTRICTION_STMT)) {
        const ASTAlterPrivilegeRestrictionStatement* stmt =
            statement->GetAs<ASTAlterPrivilegeRestrictionStatement>();
        googlesql_base::InsertIfNotPresent(table_names_,
                                stmt->path()->ToIdentifierVector());
        return absl::OkStatus();
      }
      break;
    case AST_ALTER_ROW_ACCESS_POLICY_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ALTER_ROW_ACCESS_POLICY_STMT)) {
        const ASTAlterRowAccessPolicyStatement* stmt =
            statement->GetAs<ASTAlterRowAccessPolicyStatement>();
        googlesql_base::InsertIfNotPresent(table_names_,
                                stmt->path()->ToIdentifierVector());
        return absl::OkStatus();
      }
      break;
    case AST_ALTER_ALL_ROW_ACCESS_POLICIES_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ALTER_ALL_ROW_ACCESS_POLICIES_STMT)) {
        const ASTAlterAllRowAccessPoliciesStatement* stmt =
            statement->GetAs<ASTAlterAllRowAccessPoliciesStatement>();
        googlesql_base::InsertIfNotPresent(table_names_,
                                stmt->table_name_path()->ToIdentifierVector());
        return absl::OkStatus();
      }
      break;
    case AST_ALTER_DATABASE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ALTER_DATABASE_STMT)) {
        return absl::OkStatus();
      }
      break;
    case AST_ALTER_SCHEMA_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ALTER_SCHEMA_STMT)) {
        return absl::OkStatus();
      }
      break;
    case AST_ALTER_EXTERNAL_SCHEMA_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ALTER_EXTERNAL_SCHEMA_STMT) &&
          analyzer_options_->language().LanguageFeatureEnabled(
              FEATURE_EXTERNAL_SCHEMA_DDL)) {
        return absl::OkStatus();
      }
      break;
    case AST_ALTER_SEQUENCE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ALTER_SEQUENCE_STMT)) {
        return absl::OkStatus();
      }
      break;
    case AST_ALTER_TABLE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ALTER_TABLE_SET_OPTIONS_STMT) ||
          analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ALTER_TABLE_STMT)) {
        // Note that for a ALTER TABLE statement, the table name is not
        // inserted into table_names_. Engines that need to know about a table
        // referenced by ALTER TABLE should handle that themselves.
        //
        // ALTER TABLE statements may reference tables if statement is adding
        // a foreign key.
        const auto* alter = statement->GetAsOrDie<ASTAlterTableStatement>();
        for (const auto* action : alter->action_list()->actions()) {
          if (action->node_kind() != AST_ADD_CONSTRAINT_ACTION) {
            continue;
          }
          const auto* constraint =
              action->GetAsOrDie<ASTAddConstraintAction>()->constraint();
          if (constraint->node_kind() != AST_FOREIGN_KEY) {
            continue;
          }
          const auto* foreign_key = constraint->GetAsOrDie<ASTForeignKey>();
          googlesql_base::InsertIfNotPresent(
              table_names_,
              foreign_key->reference()->table_name()->ToIdentifierVector());
        }
        return absl::OkStatus();
      }
      break;
    case AST_ALTER_VIEW_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ALTER_VIEW_STMT)) {
        // Note that for a ALTER VIEW statement, the table name is not
        // inserted into table_names_. Engines that need to know about a table
        // referenced by ALTER VIEW should handle that themselves.
        return absl::OkStatus();
      }
      break;
    case AST_ALTER_MATERIALIZED_VIEW_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ALTER_MATERIALIZED_VIEW_STMT)) {
        // Note that for a ALTER MATERIALIZED VIEW statement, the table name is
        // not inserted into table_names_. Engines that need to know about a
        // table referenced by ALTER MATERIALIZED VIEW should handle that
        // themselves.
        return absl::OkStatus();
      }
      break;
    case AST_ALTER_APPROX_VIEW_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ALTER_APPROX_VIEW_STMT)) {
        // Note that for a ALTER APPROX VIEW statement, the table name is
        // not inserted into table_names_. Engines that need to know about a
        // table referenced by ALTER APPROX VIEW should handle that
        // themselves.
        return absl::OkStatus();
      }
      break;

    case AST_HINTED_STATEMENT:
      return FindInStatement(
          statement->GetAs<ASTHintedStatement>()->statement());
    case AST_IMPORT_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_IMPORT_STMT)) {
        // There are no table names in an IMPORT statement.
        return absl::OkStatus();
      }
      break;
    case AST_MODULE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_MODULE_STMT)) {
        // There are no table names in a MODULE statement.
        return absl::OkStatus();
      }
      break;
    case AST_ANALYZE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ANALYZE_STMT)) {
        return FindInExpressionsUnder(statement->GetAs<ASTAnalyzeStatement>()
                                          ->table_and_column_info_list(),
                                      /*visible_aliases=*/{});
      }
      break;
    case AST_ASSERT_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ASSERT_STMT)) {
        return FindInExpressionsUnder(
            statement->GetAs<ASTAssertStatement>()->expr(),
            /*visible_aliases=*/{});
      }
      break;
    case AST_SYSTEM_VARIABLE_ASSIGNMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ASSIGNMENT_STMT)) {
        // The LHS, a system variable, cannot reference any tables.  But, the
        // RHS expression can.
        return FindInExpressionsUnder(
            statement->GetAs<ASTSystemVariableAssignment>()->expression(),
            /*visible_aliases=*/{});
      }
      break;
    case AST_EXECUTE_IMMEDIATE_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_EXECUTE_IMMEDIATE_STMT)) {
        const ASTExecuteImmediateStatement* stmt =
            statement->GetAs<ASTExecuteImmediateStatement>();
        GOOGLESQL_RETURN_IF_ERROR(FindInExpressionsUnder(stmt->using_clause(),
                                               /*visible_aliases=*/{}));
        return FindInExpressionsUnder(stmt->sql(), /*visible_aliases=*/{});
      }
      break;
    case AST_CREATE_ENTITY_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_CREATE_ENTITY_STMT)) {
        return absl::OkStatus();
      }
      break;
    case AST_ALTER_ENTITY_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ALTER_ENTITY_STMT)) {
        return absl::OkStatus();
      }
      break;
    case AST_VARIABLE_DECLARATION: {
      const ASTVariableDeclaration* decl =
          statement->GetAsOrDie<ASTVariableDeclaration>();
      if (decl->default_value() != nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(FindInExpressionsUnder(decl->default_value(),
                                               /*visible_aliases=*/{}));
      }
      return absl::OkStatus();
    }
    case AST_SINGLE_ASSIGNMENT: {
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ASSIGNMENT_STMT)) {
        const ASTSingleAssignment* assign =
            statement->GetAsOrDie<ASTSingleAssignment>();
        return FindInExpressionsUnder(assign->expression(),
                                      /*visible_aliases=*/{});
      }
      break;
    }
    case AST_ASSIGNMENT_FROM_STRUCT: {
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_ASSIGNMENT_STMT)) {
        const ASTAssignmentFromStruct* assign =
            statement->GetAsOrDie<ASTAssignmentFromStruct>();
        return FindInExpressionsUnder(assign->struct_expression(),
                                      /*visible_aliases=*/{});
      }
      break;
    }
    case AST_RAISE_STATEMENT: {
      const ASTRaiseStatement* raise =
          statement->GetAsOrDie<ASTRaiseStatement>();
      if (raise->message() != nullptr) {
        return FindInExpressionsUnder(raise->message(), /*visible_aliases=*/{});
      }
      return absl::OkStatus();
    }
    case AST_AUX_LOAD_DATA_STATEMENT:
      if (analyzer_options_->language().SupportsStatementKind(
              RESOLVED_AUX_LOAD_DATA_STMT)) {
        const ASTAuxLoadDataStatement* stmt =
            statement->GetAs<ASTAuxLoadDataStatement>();
        const ASTTableElementList* table_elements = stmt->table_element_list();
        if (table_elements != nullptr) {
          GOOGLESQL_RETURN_IF_ERROR(FindInTableElements(table_elements));
        }
        return absl::OkStatus();
      }
      break;
    default:
      break;
  }

  // This statement is not currently supported so we return an error here.
  return MakeSqlErrorAt(statement)
         << "Statement not supported: " << statement->GetNodeKindString();
}  // NOLINT(readability/fn_size)

absl::Status TableNameResolver::FindInQueryStatement(
    const ASTQueryStatement* statement) {
  return FindInQuery(statement->query(), /*visible_aliases=*/{});
}

absl::Status TableNameResolver::FindInSubpipelineStatement(
    const ASTSubpipelineStatement* statement) {
  AliasSet visible_aliases;
  const Table* input_table =
      analyzer_options_->default_table_for_subpipeline_stmt();
  if (input_table != nullptr) {
    // The input table is scanned, and is available as a range variable.
    visible_aliases.insert(absl::AsciiStrToLower(input_table->Name()));

    // We don't have a FullName() that returns a path so we have to split it
    // on the dots.  This isn't correct if there are embedded dots but it's the
    // best we have right now.
    std::vector<std::string> table_path =
        absl::StrSplit(input_table->FullName(), '.');
    googlesql_base::InsertIfNotPresent(table_names_, table_path);
  }

  AliasSet new_aliases = visible_aliases;
  GOOGLESQL_RETURN_IF_ERROR(
      FindInPipeOperatorList(statement->subpipeline()->pipe_operator_list(),
                             visible_aliases, &new_aliases));
  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInCreateViewStatement(
    const ASTCreateViewStatement* statement) {
  if (statement->recursive()) {
    recursive_view_name_ = statement->name()->ToIdentifierVector();
  }
  GOOGLESQL_RETURN_IF_ERROR(FindInQuery(statement->query(), /*visible_aliases=*/{}));
  recursive_view_name_.clear();
  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInCreateMaterializedViewStatement(
    const ASTCreateMaterializedViewStatement* statement) {
  if (statement->recursive()) {
    recursive_view_name_ = statement->name()->ToIdentifierVector();
  }
  if (statement->query() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(FindInQuery(statement->query(), /*visible_aliases=*/{}));
  }
  if (statement->replica_source() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(
        ResolveTablePath(statement->replica_source()->ToIdentifierVector(),
                         /*for_system_time=*/nullptr));
  }
  recursive_view_name_.clear();
  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInCreateApproxViewStatement(
    const ASTCreateApproxViewStatement* statement) {
  if (statement->recursive()) {
    recursive_view_name_ = statement->name()->ToIdentifierVector();
  }
  GOOGLESQL_RETURN_IF_ERROR(FindInQuery(statement->query(), /*visible_aliases=*/{}));
  recursive_view_name_.clear();
  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInCreateTableFunctionStatement(
    const ASTCreateTableFunctionStatement* statement) {
  if (statement->query() == nullptr) {
    return absl::OkStatus();
  }
  GOOGLESQL_RET_CHECK(local_table_aliases_.empty());
  for (const ASTFunctionParameter* const parameter :
       statement->function_declaration()->parameters()->parameter_entries()) {
    if (parameter->name() == nullptr) {
      continue;
    }
    // If it's a non-templated table parameter or is ANY TABLE then it is a name
    // that we should ignore.
    if (parameter->IsTableParameter() ||
        (parameter->IsTemplated() &&
         parameter->templated_parameter_type()->kind() ==
             ASTTemplatedParameterType::ANY_TABLE)) {
      googlesql_base::InsertIfNotPresent(
          &local_table_aliases_,
          absl::AsciiStrToLower(parameter->name()->GetAsString()));
    }
  }
  GOOGLESQL_RETURN_IF_ERROR(FindInQuery(statement->query(), /*visible_aliases=*/{}));
  local_table_aliases_.clear();
  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInCloneDataStatement(
    const ASTCloneDataStatement* statement) {
  googlesql_base::InsertIfNotPresent(table_names_,
                          statement->target_path()->ToIdentifierVector());
  for (const ASTCloneDataSource* data_source :
       statement->data_source_list()->data_sources()) {
    googlesql_base::InsertIfNotPresent(table_names_,
                            data_source->path_expr()->ToIdentifierVector());
  }
  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInExportDataStatement(
    const ASTExportDataStatement* statement) {
  return FindInQuery(statement->query(), /*visible_aliases=*/{});
}

absl::Status TableNameResolver::FindInDeleteStatement(
    const ASTDeleteStatement* statement) {
  GOOGLESQL_ASSIGN_OR_RETURN(const ASTPathExpression* path_expr,
                   statement->GetTargetPathForNonNested());
  std::vector<std::string> path = path_expr->ToIdentifierVector();
  googlesql_base::InsertIfNotPresent(table_names_, path);

  AliasSet visible_aliases;
  googlesql_base::InsertIfNotPresent(table_names_, path);
  const absl::string_view alias = statement->alias() == nullptr
                                      ? path.back()
                                      : statement->alias()->GetAsStringView();
  googlesql_base::InsertIfNotPresent(&visible_aliases, absl::AsciiStrToLower(alias));

  GOOGLESQL_RETURN_IF_ERROR(FindInExpressionsUnder(statement->where(), visible_aliases));
  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInTruncateStatement(
    const ASTTruncateStatement* statement) {
  AliasSet visible_aliases;

  GOOGLESQL_ASSIGN_OR_RETURN(const ASTPathExpression* path_expr,
                   statement->GetTargetPathForNonNested());
  std::vector<std::string> path = path_expr->ToIdentifierVector();
  googlesql_base::InsertIfNotPresent(table_names_, path);
  googlesql_base::InsertIfNotPresent(&visible_aliases, absl::AsciiStrToLower(path.back()));

  return FindInExpressionsUnder(statement->where(), visible_aliases);
}

absl::Status TableNameResolver::FindInInsertStatement(
    const ASTInsertStatement* statement) {
  AliasSet visible_aliases;

  GOOGLESQL_ASSIGN_OR_RETURN(const ASTPathExpression* path_expr,
                   statement->GetTargetPathForNonNested());
  std::vector<std::string> path = path_expr->ToIdentifierVector();
  googlesql_base::InsertIfNotPresent(table_names_, path);
  googlesql_base::InsertIfNotPresent(&visible_aliases, absl::AsciiStrToLower(path.back()));

  if (statement->rows() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(FindInExpressionsUnder(statement->rows(), visible_aliases));
  }

  if (statement->query() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(FindInQuery(statement->query(), visible_aliases));
  }
  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInUpdateStatement(
    const ASTUpdateStatement* statement) {
  AliasSet visible_aliases;

  GOOGLESQL_ASSIGN_OR_RETURN(const ASTPathExpression* path_expr,
                   statement->GetTargetPathForNonNested());
  const std::vector<std::string> path = path_expr->ToIdentifierVector();

  googlesql_base::InsertIfNotPresent(table_names_, path);
  const absl::string_view alias = statement->alias() == nullptr
                                      ? path.back()
                                      : statement->alias()->GetAsStringView();
  googlesql_base::InsertIfNotPresent(&visible_aliases, absl::AsciiStrToLower(alias));

  if (statement->from_clause() != nullptr) {
    GOOGLESQL_RET_CHECK(statement->from_clause()->table_expression() != nullptr);
    GOOGLESQL_RETURN_IF_ERROR(FindInTableExpression(
        statement->from_clause()->table_expression(),
        /*external_visible_aliases=*/{}, &visible_aliases));
  }

  GOOGLESQL_RETURN_IF_ERROR(FindInExpressionsUnder(statement->where(), visible_aliases));
  GOOGLESQL_RETURN_IF_ERROR(
      FindInExpressionsUnder(statement->update_item_list(), visible_aliases));
  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInMergeStatement(
    const ASTMergeStatement* statement) {
  AliasSet visible_aliases;

  const ASTPathExpression* path_expr = statement->target_path();
  std::vector<std::string> path = path_expr->ToIdentifierVector();
  googlesql_base::InsertIfNotPresent(table_names_, path);
  googlesql_base::InsertIfNotPresent(&visible_aliases, absl::AsciiStrToLower(path.back()));

  GOOGLESQL_RETURN_IF_ERROR(FindInTableExpression(statement->table_expression(),
                                        /*external_visible_aliases=*/{},
                                        &visible_aliases));
  GOOGLESQL_RETURN_IF_ERROR(
      FindInExpressionsUnder(statement->merge_condition(), visible_aliases));
  GOOGLESQL_RETURN_IF_ERROR(
      FindInExpressionsUnder(statement->when_clauses(), visible_aliases));

  return absl::OkStatus();
}

absl::Status TableNameResolver::HandleWithClause(
    const ASTWithClause* with_clause, const AliasSet& visible_aliases) {
  if (with_clause->recursive()) {
    // In WITH RECURSIVE, any entry can access an alias defined in any other
    // entry, regardless of declaration order.
    for (const ASTWithClauseEntry* entry : with_clause->entries()) {
      if (entry->aliased_query() != nullptr) {
        const std::string with_alias = absl::AsciiStrToLower(
            entry->aliased_query()->alias()->GetAsStringView());
        googlesql_base::InsertIfNotPresent(&local_table_aliases_, with_alias);
      }
    }
    for (const ASTWithClauseEntry* entry : with_clause->entries()) {
      if (entry->aliased_query() != nullptr) {
        const ASTAliasedQuery* aliased_query = entry->aliased_query();
        GOOGLESQL_RETURN_IF_ERROR(FindInQuery(aliased_query->query(), visible_aliases));
      }
    }
  } else {
    // In WITH without RECURSIVE, entries can only access with aliases defined
    // in prior entries.
    for (const ASTWithClauseEntry* entry : with_clause->entries()) {
      if (entry->aliased_query() != nullptr) {
        const ASTAliasedQuery* aliased_query = entry->aliased_query();
        GOOGLESQL_RETURN_IF_ERROR(FindInQuery(aliased_query->query(), visible_aliases));
        const std::string with_alias =
            absl::AsciiStrToLower(aliased_query->alias()->GetAsStringView());
        googlesql_base::InsertIfNotPresent(&local_table_aliases_, with_alias);
      }
    }
  }
  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInQuery(const ASTQuery* query,
                                            const AliasSet& visible_aliases,
                                            AliasSet* new_aliases) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  AliasSet old_local_table_aliases;
  if (query->with_clause() != nullptr) {
    // Record the set of local table aliases visible in the outer scope so we
    // can restore that after processing the local query.
    old_local_table_aliases = local_table_aliases_;

    GOOGLESQL_RETURN_IF_ERROR(HandleWithClause(query->with_clause(), visible_aliases));
  }

  AliasSet local_visible_aliases = visible_aliases;

  GOOGLESQL_RETURN_IF_ERROR(FindInQueryExpression(
      query->query_expr(), query->order_by(), visible_aliases,
      /*new_aliases=*/&local_visible_aliases));

  // We need to handle any pipe operators that can include table references
  // outside expressions.  Operators with just expressions are handled by
  // the default case.
  GOOGLESQL_RETURN_IF_ERROR(FindInPipeOperatorList(
      query->pipe_operator_list(), visible_aliases, &local_visible_aliases));

  new_aliases->insert(local_visible_aliases.begin(),
                      local_visible_aliases.end());

  // Restore local table alias set if we modified it.
  if (query->with_clause() != nullptr) {
    local_table_aliases_ = old_local_table_aliases;
  }
  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInPipeOperatorList(
    absl::Span<const ASTPipeOperator* const> pipe_operator_list,
    const AliasSet& visible_aliases, AliasSet* new_aliases) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  if (pipe_operator_list.empty()) {
    return absl::OkStatus();
  }

  // Record the CTE set visible in the outer scope so we can restore that
  // after processing the local query.
  AliasSet old_local_table_aliases = local_table_aliases_;

  for (const ASTPipeOperator* pipe_operator : pipe_operator_list) {
    switch (pipe_operator->node_kind()) {
      case AST_PIPE_CALL:
        GOOGLESQL_RETURN_IF_ERROR(FindInTVF(pipe_operator->GetAs<ASTPipeCall>()->tvf(),
                                  visible_aliases, new_aliases));
        break;
      case AST_PIPE_JOIN:
        GOOGLESQL_RETURN_IF_ERROR(FindInJoin(pipe_operator->GetAs<ASTPipeJoin>()->join(),
                                   visible_aliases, new_aliases));
        break;
      case AST_PIPE_CREATE_TABLE:
        // This is needed because the CREATE TABLE can have references
        // to tables in its FOREIGN KEY definitions.
        GOOGLESQL_RETURN_IF_ERROR(
            FindInStatement(pipe_operator->GetAs<ASTPipeCreateTable>()
                                ->create_table_statement()));
        break;
      case AST_PIPE_INSERT:
        GOOGLESQL_RETURN_IF_ERROR(FindInInsertStatement(
            pipe_operator->GetAs<ASTPipeInsert>()->insert_statement()));
        break;
      case AST_PIPE_RECURSIVE_UNION:
        GOOGLESQL_RETURN_IF_ERROR(FindInPipeRecursiveUnion(
            pipe_operator->GetAsOrDie<ASTPipeRecursiveUnion>(), visible_aliases,
            new_aliases));
        break;
      case AST_PIPE_WITH:
        GOOGLESQL_RETURN_IF_ERROR(HandleWithClause(
            pipe_operator->GetAsOrDie<ASTPipeWith>()->with_clause(),
            visible_aliases));
        break;
      default:
        GOOGLESQL_RETURN_IF_ERROR(FindInExpressionsUnder(pipe_operator, visible_aliases));
        break;
    }
  }

  local_table_aliases_ = old_local_table_aliases;
  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInQueryExpression(
    const ASTQueryExpression* query_expr, const ASTOrderBy* order_by,
    const AliasSet& visible_aliases, AliasSet* new_aliases) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  switch (query_expr->node_kind()) {
    case AST_SELECT:
      GOOGLESQL_RETURN_IF_ERROR(FindInSelect(query_expr->GetAs<ASTSelect>(), order_by,
                                   visible_aliases));
      break;
    case AST_SET_OPERATION:
      GOOGLESQL_RETURN_IF_ERROR(FindInSetOperation(query_expr->GetAs<ASTSetOperation>(),
                                         visible_aliases));
      break;
    case AST_QUERY:
      GOOGLESQL_RETURN_IF_ERROR(
          FindInQuery(query_expr->GetAs<ASTQuery>(), visible_aliases));
      break;
    case AST_ALIASED_QUERY_EXPRESSION:
      GOOGLESQL_RETURN_IF_ERROR(FindInAliasedQueryExpression(
          query_expr->GetAs<ASTAliasedQueryExpression>(), visible_aliases,
          new_aliases));
      break;
    case AST_FROM_QUERY:
      GOOGLESQL_RETURN_IF_ERROR(FindInFromQuery(query_expr->GetAs<ASTFromQuery>(),
                                      visible_aliases, new_aliases));
      break;
    case AST_GQL_QUERY:
      return FindInGraphTableQuery(
          query_expr->GetAs<ASTGqlQuery>()->graph_table(), visible_aliases,
          new_aliases);
      break;
    case AST_GQL_GRAPH_PATTERN_QUERY:
      return FindInGraphPatternQuery(
          query_expr->GetAs<ASTGqlGraphPatternQuery>(), visible_aliases,
          new_aliases);
      break;
    case AST_GQL_LINEAR_OPS_QUERY:
      return FindInGraphLinearOpsQuery(
          query_expr->GetAs<ASTGqlLinearOpsQuery>(), visible_aliases,
          new_aliases);
      break;
    case AST_TABLE_CLAUSE:
      return FindInTableClause(query_expr->GetAs<ASTTableClause>(),
                               visible_aliases, new_aliases);
    default:
      return MakeSqlErrorAt(query_expr) << "Unhandled query_expr:\n"
                                        << query_expr->DebugString();
  }

  if (query_expr->node_kind() != AST_SELECT) {
    GOOGLESQL_RETURN_IF_ERROR(FindInExpressionsUnder(order_by, visible_aliases));
  }
  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInAliasedQueryExpression(
    const ASTAliasedQueryExpression* aliased_query,
    const AliasSet& visible_aliases, AliasSet* new_aliases) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  // Ignore the new_aliases from the contained query.  Produce the
  // assigned range variable instead.
  GOOGLESQL_RETURN_IF_ERROR(FindInQuery(aliased_query->query(), visible_aliases));

  new_aliases->insert(aliased_query->alias()->GetAsString());

  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInFromQuery(const ASTFromQuery* from_query,
                                                const AliasSet& visible_aliases,
                                                AliasSet* new_aliases) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  const ASTFromClause* from_clause = from_query->from_clause();
  GOOGLESQL_RET_CHECK(from_clause != nullptr);
  GOOGLESQL_RET_CHECK(from_clause->table_expression() != nullptr);
  AliasSet local_visible_aliases = visible_aliases;
  GOOGLESQL_RETURN_IF_ERROR(FindInTableExpression(from_clause->table_expression(),
                                        visible_aliases,
                                        &local_visible_aliases));
  new_aliases->insert(local_visible_aliases.begin(),
                      local_visible_aliases.end());
  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInSelect(
    const ASTSelect* select, const ASTOrderBy* order_by,
    const AliasSet& orig_visible_aliases) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  AliasSet visible_aliases = orig_visible_aliases;
  if (select->from_clause() != nullptr) {
    GOOGLESQL_RET_CHECK(select->from_clause()->table_expression() != nullptr);
    GOOGLESQL_RETURN_IF_ERROR(
        FindInTableExpression(select->from_clause()->table_expression(),
                              orig_visible_aliases, &visible_aliases));
  }
  GOOGLESQL_RETURN_IF_ERROR(
      FindInExpressionsUnder(select->select_list(), visible_aliases));
  GOOGLESQL_RETURN_IF_ERROR(
      FindInExpressionsUnder(select->where_clause(), visible_aliases));
  GOOGLESQL_RETURN_IF_ERROR(FindInExpressionsUnder(select->group_by(), visible_aliases));
  GOOGLESQL_RETURN_IF_ERROR(FindInExpressionsUnder(select->having(), visible_aliases));
  GOOGLESQL_RETURN_IF_ERROR(FindInExpressionsUnder(order_by, visible_aliases));
  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInSetOperation(
    const ASTSetOperation* set_operation, const AliasSet& visible_aliases) {
  for (const ASTQueryExpression* input : set_operation->inputs()) {
    AliasSet new_aliases;
    GOOGLESQL_RETURN_IF_ERROR(FindInQueryExpression(input, nullptr /* order_by */,
                                          visible_aliases, &new_aliases));
  }
  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInTableExpression(
    const ASTTableExpression* table_expr,
    const AliasSet& external_visible_aliases, AliasSet* local_visible_aliases) {
  switch (table_expr->node_kind()) {
    case AST_TABLE_PATH_EXPRESSION:
      return FindInTablePathExpression(
          table_expr->GetAs<ASTTablePathExpression>(), local_visible_aliases);

    case AST_TABLE_SUBQUERY:
      return FindInTableSubquery(table_expr->GetAs<ASTTableSubquery>(),
                                 external_visible_aliases,
                                 local_visible_aliases);

    case AST_JOIN:
      return FindInJoin(table_expr->GetAs<ASTJoin>(), external_visible_aliases,
                        local_visible_aliases);

    case AST_PARENTHESIZED_JOIN:
      return FindInParenthesizedJoin(table_expr->GetAs<ASTParenthesizedJoin>(),
                                     external_visible_aliases,
                                     local_visible_aliases);

    case AST_TVF:
      return FindInTVF(table_expr->GetAs<ASTTVF>(), external_visible_aliases,
                       local_visible_aliases);
    case AST_PIPE_JOIN_LHS_PLACEHOLDER:
      return absl::OkStatus();
    case AST_GRAPH_TABLE_QUERY:
      return FindInGraphTableQuery(table_expr->GetAs<ASTGraphTableQuery>(),
                                   external_visible_aliases,
                                   local_visible_aliases);
    default:
      return MakeSqlErrorAt(table_expr)
             << "Unhandled node type in from clause: "
             << table_expr->GetNodeKindString();
  }
}

absl::Status TableNameResolver::FindInJoin(
    const ASTJoin* join, const AliasSet& external_visible_aliases,
    AliasSet* local_visible_aliases) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  GOOGLESQL_RETURN_IF_ERROR(FindInTableExpression(join->lhs(), external_visible_aliases,
                                        local_visible_aliases));
  GOOGLESQL_RETURN_IF_ERROR(FindInTableExpression(join->rhs(), external_visible_aliases,
                                        local_visible_aliases));
  GOOGLESQL_RETURN_IF_ERROR(
      FindInExpressionsUnder(join->on_clause(), *local_visible_aliases));
  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInParenthesizedJoin(
    const ASTParenthesizedJoin* parenthesized_join,
    const AliasSet& external_visible_aliases, AliasSet* local_visible_aliases) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  const ASTJoin* join = parenthesized_join->join();
  // In parenthesized joins, we can't see names from outside the parentheses.
  std::unique_ptr<AliasSet> join_visible_aliases =
      std::make_unique<AliasSet>(external_visible_aliases);
  GOOGLESQL_RETURN_IF_ERROR(
      FindInJoin(join, external_visible_aliases, join_visible_aliases.get()));
  for (const std::string& alias : *join_visible_aliases) {
    googlesql_base::InsertIfNotPresent(local_visible_aliases, alias);
  }
  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInTVF(
    const ASTTVF* tvf, const AliasSet& external_visible_aliases,
    AliasSet* local_visible_aliases) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  // The 'tvf' here is the TVF parse node. Each TVF argument may be a scalar, a
  // relation, or a TABLE clause. We've parsed all of the TVF arguments as
  // expressions by this point, so the FindInExpressionsUnder call will descend
  // into the relation arguments as expression subqueries. For TABLE clause
  // arguments, we add each named table to the set of referenced table names in
  // a separate step.
  //
  // Note about correlation: if a TVF argument is a scalar, it should resolve
  // like a correlated subquery and be able to see 'local_visible_aliases'. On
  // the other hand, if the argument is a relation, it should be uncorrelated,
  // and so those aliases should not be visible. Because we don't know whether
  // the argument should be a scalar or a relation yet, we allow correlation
  // here and examine the arguments again during resolving.

  // On encountering an ASTTVF node, add the name of the TVF represented by
  // an identifier path to the optional tvf_names parameter if it has been set.
  if (tvf_names_ != nullptr) {
    googlesql_base::InsertIfNotPresent(tvf_names_, tvf->name()->ToIdentifierVector());
  }

  GOOGLESQL_RETURN_IF_ERROR(FindInExpressionsUnder(tvf, *local_visible_aliases));
  for (const ASTTVFArgument* arg : tvf->argument_entries()) {
    if (arg->table_clause() != nullptr) {
      // Single path names are table references, to either WITH clause
      // tables or table-typed arguments to the TVF.  Multi-path names
      // cannot be related to WITH clause tables or TVF arguments, so those
      // must be table references.
      if (arg->table_clause()->table_path() != nullptr &&
          arg->table_clause()->table_path()->ToIdentifierVector() !=
              recursive_view_name_) {
        if (arg->table_clause()->table_path()->num_names() > 1) {
          googlesql_base::InsertIfNotPresent(
              table_names_,
              arg->table_clause()->table_path()->ToIdentifierVector());
        } else {
          // This is a single-part name.
          const std::string lower_name =
              absl::AsciiStrToLower(arg->table_clause()
                                        ->table_path()
                                        ->first_name()
                                        ->GetAsStringView());
          if (!local_table_aliases_.contains(lower_name)) {
            googlesql_base::InsertIfNotPresent(
                table_names_,
                arg->table_clause()->table_path()->ToIdentifierVector());
          }
        }
      }
      if (arg->table_clause()->tvf() != nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(FindInTVF(arg->table_clause()->tvf(),
                                  external_visible_aliases,
                                  local_visible_aliases));
      }
    }
  }

  for (const auto* op : tvf->postfix_operators()) {
    GOOGLESQL_RETURN_IF_ERROR(FindInExpressionsUnder(op, external_visible_aliases));
  }

  if (tvf->alias() != nullptr) {
    local_visible_aliases->insert(
        absl::AsciiStrToLower(tvf->alias()->GetAsString()));
  }

  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInTableSubquery(
    const ASTTableSubquery* table_subquery,
    const AliasSet& external_visible_aliases, AliasSet* local_visible_aliases) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  AliasSet new_aliases;
  GOOGLESQL_RETURN_IF_ERROR(FindInQuery(table_subquery->subquery(),
                              external_visible_aliases, &new_aliases));

  for (const auto* op : table_subquery->postfix_operators()) {
    GOOGLESQL_RETURN_IF_ERROR(FindInExpressionsUnder(op, external_visible_aliases));
  }

  if (table_subquery->alias() != nullptr) {
    // If the table subquery has a table alias, that alias becomes visible and
    // aliases from the inner query are hidden.
    googlesql_base::InsertIfNotPresent(
        local_visible_aliases,
        absl::AsciiStrToLower(table_subquery->alias()->GetAsStringView()));
  } else {
    // Otherwise, the inner table aliases propagate out.
    local_visible_aliases->insert(new_aliases.begin(), new_aliases.end());
  }
  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInTableElements(
    const ASTTableElementList* elements) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  std::vector<const ASTNode*> foreign_references;
  elements->GetDescendantSubtreesWithKinds({AST_FOREIGN_KEY_REFERENCE},
                                           &foreign_references);
  for (const ASTNode* node : foreign_references) {
    const ASTForeignKeyReference* reference =
        node->GetAsOrDie<ASTForeignKeyReference>();
    const std::vector<std::string> path =
        reference->table_name()->ToIdentifierVector();
    googlesql_base::InsertIfNotPresent(table_names_, path);
  }

  return absl::OkStatus();
}

absl::Status TableNameResolver::ResolveTablePath(
    const std::vector<std::string>& path,
    const ASTForSystemTime* for_system_time) {
  googlesql_base::InsertIfNotPresent(table_names_, path);
  if (table_resolution_time_info_map_ == nullptr) {
    return absl::OkStatus();
  }
  // Lookup for or insert a set of temporal expressions for 'path'.
  TableResolutionTimeInfo& temporal_expressions_set =
      (*table_resolution_time_info_map_)[path];
  if (for_system_time != nullptr) {
    if (!for_system_time_as_of_feature_enabled_) {
      return MakeSqlErrorAt(for_system_time)
             << "FOR SYSTEM_TIME AS OF is not supported";
    }

    const ASTExpression* expr = for_system_time->expression();
    GOOGLESQL_RET_CHECK(expr != nullptr);
    std::unique_ptr<const AnalyzerOutput> analyzed;

    if (catalog_ != nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(::googlesql::AnalyzeExpressionFromParserAST(
          *expr, *analyzer_options_, sql_, type_factory_, catalog_, &analyzed));
    }
    temporal_expressions_set.exprs.push_back({expr, std::move(analyzed)});
  } else {
    temporal_expressions_set.has_default_resolution_time = true;
  }
  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInTablePathExpression(
    const ASTTablePathExpression* table_ref, AliasSet* visible_aliases) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  std::string alias;
  if (table_ref->alias() != nullptr) {
    alias = table_ref->alias()->GetAsString();
  }

  if (table_ref->path_expr() != nullptr) {
    const ASTPathExpression* path_expr = table_ref->path_expr();
    std::vector<std::string> path = path_expr->ToIdentifierVector();
    GOOGLESQL_RET_CHECK(!path.empty());

    // Single identifiers are always table names, not range variable references,
    // but could be WITH table references or references to TVF arguments that
    // should be ignored.
    //
    // For paths, check if the first identifier is a known alias.  This allows
    // us to ignore correlated alias references like:
    //   SELECT ... FROM table AS t1, t1.arraycol
    // However, we do not want to ignore this table name if the alias matches
    // a WITH alias or TVF argument name, since multi-part table names never
    // resolve to either of these (so the full multi-part name is a reference
    // to an actual table).
    const std::string first_identifier = absl::AsciiStrToLower(path[0]);

    if ((path != recursive_view_name_) &&
        (path.size() == 1 ? (!local_table_aliases_.contains(first_identifier))
                          : (!visible_aliases->contains(first_identifier)))) {
      GOOGLESQL_RETURN_IF_ERROR(ResolveTablePath(path, table_ref->for_system_time()));
    }

    for (const auto* op : table_ref->postfix_operators()) {
      GOOGLESQL_RETURN_IF_ERROR(FindInExpressionsUnder(op, *visible_aliases));
    }

    if (alias.empty()) {
      alias = path.back();
    }
  }

  GOOGLESQL_RETURN_IF_ERROR(
      FindInExpressionsUnder(table_ref->unnest_expr(), *visible_aliases));

  if (!alias.empty()) {
    visible_aliases->insert(absl::AsciiStrToLower(alias));
  }

  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInGraphTableQuery(
    const ASTGraphTableQuery* graph_query,
    const AliasSet& external_visible_aliases, AliasSet* local_visible_aliases) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  // Find any TVFs called through CALL, as these are not on a relational
  // subquery and won't show up in the usual path looking for them as table
  // expressions in a FROM clause.
  GOOGLESQL_RETURN_IF_ERROR(FindInGqlCallsOnNamedTvfsUnder(
      graph_query, external_visible_aliases, local_visible_aliases));

  // Find in any nested subqueries
  return FindInExpressionsUnder(graph_query, external_visible_aliases);
}

absl::Status TableNameResolver::FindInGraphPatternQuery(
    const ASTGqlGraphPatternQuery* graph_pattern_query,
    const AliasSet& external_visible_aliases, AliasSet* local_visible_aliases) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  // Find in any nested subqueries
  return FindInExpressionsUnder(
      graph_pattern_query->graph_pattern()->where_clause(),
      external_visible_aliases);
}

absl::Status TableNameResolver::FindInGraphLinearOpsQuery(
    const ASTGqlLinearOpsQuery* linear_ops_query,
    const AliasSet& external_visible_aliases, AliasSet* local_visible_aliases) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  // Find in any nested subqueries within each linear op
  const auto& operators = linear_ops_query->linear_ops()->operators();
  for (const auto& op : operators) {
    GOOGLESQL_RETURN_IF_ERROR(FindInExpressionsUnder(op, external_visible_aliases));
  }
  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInTableClause(
    const ASTTableClause* table_clause,
    const AliasSet& external_visible_aliases, AliasSet* local_visible_aliases) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  if (table_clause->table_path() != nullptr) {
    const ASTPathExpression* path_expr = table_clause->table_path();
    std::vector<std::string> path = path_expr->ToIdentifierVector();
    GOOGLESQL_RET_CHECK(!path.empty());
    const std::string first_identifier = absl::AsciiStrToLower(path[0]);
    // A path in a TABLE clause should be extracted only if it refers to a
    // catalog table. We ignore paths that resolve to WITH aliases or TVF
    // arguments (from `local_table_aliases_`). We also ignore multi-part
    // paths that start with an alias from an outer scope (from
    // `external_visible_aliases`), because such paths cannot be catalog table
    // references.
    if ((path != recursive_view_name_) &&
        (path.size() == 1
             ? (!local_table_aliases_.contains(first_identifier))
             : (!external_visible_aliases.contains(first_identifier)))) {
      GOOGLESQL_RETURN_IF_ERROR(ResolveTablePath(path, /*for_system_time=*/nullptr));
    }
  }
  if (table_clause->tvf() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(FindInTVF(table_clause->tvf(), external_visible_aliases,
                              local_visible_aliases));
  }
  return FindInExpressionsUnder(table_clause->where_clause(),
                                external_visible_aliases);
}

absl::Status TableNameResolver::FindInGqlCallsOnNamedTvfsUnder(
    const ASTGraphTableQuery* graph_query,
    const AliasSet& external_visible_aliases, AliasSet* local_visible_aliases) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  GOOGLESQL_RET_CHECK(graph_query != nullptr);

  std::vector<const ASTNode*> call_named_tvf_nodes;
  graph_query->GetDescendantSubtreesWithKinds({AST_GQL_NAMED_CALL},
                                              &call_named_tvf_nodes);

  for (const ASTNode* call_named_tvf_node : call_named_tvf_nodes) {
    GOOGLESQL_RETURN_IF_ERROR(FindInTVF(
        call_named_tvf_node->GetAsOrDie<ASTGqlNamedCall>()->tvf_call(),
        external_visible_aliases, local_visible_aliases));
  }

  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInCreatePropertyGraphStatement(
    const ASTCreatePropertyGraphStatement* statement) {
  GOOGLESQL_RET_CHECK_NE(statement, nullptr);
  GOOGLESQL_RET_CHECK_NE(statement->node_table_list(), nullptr);

  // source table references for nodes
  for (const auto& ast_graph_element_table :
       statement->node_table_list()->element_tables()) {
    googlesql_base::InsertIfNotPresent(
        table_names_, ast_graph_element_table->name()->ToIdentifierVector());
  }

  // source table references for edges, if they exist
  if (statement->edge_table_list() != nullptr) {
    for (const auto& ast_graph_element_table :
         statement->edge_table_list()->element_tables()) {
      googlesql_base::InsertIfNotPresent(
          table_names_, ast_graph_element_table->name()->ToIdentifierVector());
    }
  }

  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInExpressionsUnder(
    const ASTNode* root, const AliasSet& visible_aliases) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  if (root == nullptr) return absl::OkStatus();

  // The only thing that matters inside expressions are expression subqueries,
  // which can be either ASTExpressionSubquery or ASTIn, both of which have
  // the subquery in an ASTQuery child.
  std::vector<const ASTNode*> subquery_nodes;
  root->GetDescendantSubtreesWithKinds({AST_QUERY}, &subquery_nodes);

  for (const ASTNode* subquery_node : subquery_nodes) {
    GOOGLESQL_RETURN_IF_ERROR(
        FindInQuery(subquery_node->GetAs<ASTQuery>(), visible_aliases));
  }

  return absl::OkStatus();
}

absl::Status TableNameResolver::FindInOptionsListUnder(
    const ASTNode* root, const AliasSet& visible_aliases) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  if (root == nullptr) return absl::OkStatus();

  std::vector<const ASTNode*> options_list_nodes;
  root->GetDescendantSubtreesWithKinds({AST_OPTIONS_LIST}, &options_list_nodes);

  for (const ASTNode* options_list : options_list_nodes) {
    GOOGLESQL_RETURN_IF_ERROR(FindInExpressionsUnder(options_list, visible_aliases));
  }
  return absl::OkStatus();
}
}  // namespace

absl::Status FindTableNamesAndResolutionTime(
    absl::string_view sql, const ASTStatement& statement,
    const AnalyzerOptions& analyzer_options, TypeFactory* type_factory,
    Catalog* catalog, TableNamesSet* table_names,
    TableResolutionTimeInfoMap* table_resolution_time_info_map,
    TableNamesSet* tvf_names) {
  return TableNameResolver(sql, &analyzer_options, type_factory, catalog,
                           table_names, table_resolution_time_info_map,
                           tvf_names)
      .FindTableNamesAndTemporalReferences(statement);
}

absl::Status FindTableNamesInScript(absl::string_view sql,
                                    const ASTScript& script,
                                    const AnalyzerOptions& analyzer_options,
                                    TableNamesSet* table_names,
                                    TableNamesSet* tvf_names) {
  return TableNameResolver(sql, &analyzer_options, /*type_factory=*/nullptr,
                           /*catalog=*/nullptr, table_names,
                           /*table_resolution_time_info_map=*/nullptr,
                           tvf_names)
      .FindTableNames(script);
}

}  // namespace table_name_resolver
}  // namespace googlesql
