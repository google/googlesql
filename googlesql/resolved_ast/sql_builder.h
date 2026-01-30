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

#ifndef GOOGLESQL_RESOLVED_AST_SQL_BUILDER_H_
#define GOOGLESQL_RESOLVED_AST_SQL_BUILDER_H_

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <stack>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/atomic_sequence_num.h"
#include "googlesql/analyzer/query_resolver_helper.h"
#include "googlesql/public/analyzer.h"  // For QueryParametersMap
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/query_expression.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_visitor.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/target_syntax.h"
#include "absl/base/attributes.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql {

class QueryExpression;
class GqlReturnOpSQLBuilder;

struct CopyableState {
  // Stores the path used to access the resolved columns, keyed by column_id.
  // This map needs to updated to reflect the path of the column in the current
  // context (e.g. when inside or outside a join or subquery) as we visit nodes
  // up the tree.
  std::map<int /* column_id */, std::string> column_paths;

  // Stores the unique alias we generate for the ResolvedComputedColumn.
  // These aliases are mostly used to replace the internal column names assigned
  // to these columns in the resolved tree. The key is column_id here.
  std::map<int /* column_id */, std::string> computed_column_alias;

  // Stores the unique scan_aliases assigned for the corresponding scan nodes.
  absl::flat_hash_map<const ResolvedScan*, std::string> scan_alias_map;

  // Stores the unique aliases assigned to tables.
  absl::flat_hash_map<const Table*, std::string> table_alias_map;

  // Stores the tables that have been used without an explicit alias.
  // This is a map from the implicit alias to the identifier (as returned by
  // TableToIdentifierLiteral).
  // This is because if we have two tables in different nested catalogs S1.T
  // and S2.T, they cannot both use the same implicit alias "t" and one of them
  // needs to be explicitly aliased.
  absl::flat_hash_map<std::string, std::string> tables_with_implicit_alias;

  // Stores the sql text of the columns, keyed by column_id and populated in
  // AggregateScan, AnalyticScan, and TVFScan. We refer to the sql text in its
  // parent ProjectScan (while building the select_list) and in
  // ResolvedColumnRef pointing to the column.
  // NOTE: This map is cleared every time we visit a ProjectScan, once it has
  // processed all the columns of its child scan.
  std::map<int /* column_id */, std::string> pending_columns;

  // Stores the sql text of pending columns that are used as parameters of sub
  // expressions.
  // NOTE: This map is initialized and cleared in each VisitResolvedSubqueryExpr
  // call. Unlike pending_columns_ it is not cleared in every ProjectScan as
  // correlated columns must be accessible to all scans inside the sub query.
  std::map<int /* column_id */, std::string> correlated_pending_columns;

  // Stores the WithScan corresponding to the with-query-name. Used in
  // WithRefScan to extract the column aliases of the relevant WithScan.
  std::map<std::string /* with_query_name */,
           const ResolvedScan* /* with_subquery */>
      with_query_name_to_scan;

  enum SubscriptModifierKind {
    kNone = 0,
    kOffset = 1,
    kKey = 2,
  };
  struct UpdateItemElementState {
    // The type of the subscript target (the container that is being
    // subscripted).
    const Type* target_type;
    // The SQL for the subscript expression.
    std::string subscript_sql;
    // A function modifier (e.g. OFFSET(), KEY()) on the subscript expression.
    SubscriptModifierKind subscript_modifier;
  };
  // A stack of stacks. An element of an inner stack corresponds to a
  // ResolvedUpdateItem node that is currently being processed, and stores the
  // target SQL for that node and the subscript expression SQL (empty if not
  // applicable) of the ResolvedUpdateItemElement child currently being
  // processed. We start a new stack just before we process a ResolvedUpdateItem
  // node from a ResolvedUpdateStmt (as opposed to a ResolvedUpdateItemElement).
  //
  // Stacks are added/removed in GetUpdateItemListSQL. Target SQL is set in
  // VisitResolvedUpdateItem. The subscript expression SQL is set in
  // VisitResolvedUpdateItemElement. An inner stack is used to construct a
  // target in VisitResolvedUpdateItem when there are no
  // ResolvedUpdateItemElement children.
  //
  // We also keep track of the offset type as a subscript expression is allowed
  // to have non-integer offsets.
  std::deque<std::deque<
      std::pair<std::string /* target_sql */,
                UpdateItemElementState /* container_element_access_sql */>>>
      update_item_targets_and_offsets;

  // A stack of dml target paths kept to parallel the nesting of dml statements.
  // Populated in VisitResolvedUpdateItem and used in
  // VisitResolved<DMLType>Statement to refer the corresponding target path.
  std::deque<
      std::pair<std::string /* target_path */, std::string /* target_alias */>>
      nested_dml_targets;

  // All column names referenced in the query.
  absl::flat_hash_set<std::string> col_ref_names;

  // True if we are unparsing the LHS of a SET statement.
  bool in_set_lhs = false;

  struct RecursiveQueryInfo {
    // Text to be inserted into the generated query to refer to the recursive
    // table being referenced. If backticks are needed, this should already be
    // present in the query name.
    std::string query_name;

    // The recursive scan being referenced.
    const ResolvedRecursiveScan* scan;
  };

  // Stack of names of recursive queries being defined. Any
  // ResolvedRecursiveRefScan node must refer to the query at the top of the
  // stack.
  std::stack<RecursiveQueryInfo> recursive_query_info;

  // MATCH_RECOGNIZE state, in order to convert back access to internal columns
  // to calls to special functions, e.g. MATCH_NUMBER().
  struct MatchRecognizeState {
    ResolvedColumn match_number_column;
    ResolvedColumn match_row_number_column;
    ResolvedColumn classifier_column;
  };
  std::optional<MatchRecognizeState> match_recognize_state;
};

// SQLBuilder takes the GoogleSQL Resolved ASTs and generates its equivalent SQL
// form.
//
// Usage:
//   SQLBuilder sql_builder;
//   const ResolvedQueryStmt* node = ...;
//   node->Accept(&sql_builder);
//   const std::string& unparsed_sql = sql_builder.sql();
//
//   NOTE: SQL for the node gets appended onto the SQLBuilder's sql.
//
// Be wary of using SQLBuilder with positional query parameters; there is no
// guarantee that they will appear in the same order as their positions in the
// resolved AST.
//
// There are some ResolvedAST nodes that have no SQL equivalent, such as
// ResolvedExecuteAsRoleScan. Such nodes result from specific rewriters as there
// is no way to generate them directly from SQL. SQLBuilder produces a
// kInvalidArgument error in this case. Engines that invoke such rewriters
// should not ask SQLBuilder to generate SQL.
class SQLBuilder : public ResolvedASTVisitor {
 public:
  using SQLAliasPairList = QueryExpression::SQLAliasPairList;
  using TargetSyntaxMode = QueryExpression::TargetSyntaxMode;

  // Options to use when generating SQL.
  struct SQLBuilderOptions {
    SQLBuilderOptions() {
      // PRODUCT_EXTERNAL is the default product mode.
      language_options.set_product_mode(PRODUCT_EXTERNAL);
    }
    explicit SQLBuilderOptions(ProductMode product_mode,
                               bool use_external_float32 = false)
        : use_external_float32(use_external_float32) {
      language_options.set_product_mode(product_mode);
    }
    explicit SQLBuilderOptions(const LanguageOptions& language_options,
                               bool use_external_float32 = false)
        : language_options(language_options),
          use_external_float32(use_external_float32) {}
    ~SQLBuilderOptions() = default;

    // Language options included enabled/disabled features and product mode,
    // whether the generated SQL is for INTERNAL or EXTERNAL mode.  For example,
    // EXTERNAL mode supports FLOAT64 only as a type name, not DOUBLE
    // (INTERNAL supports both as type names).
    LanguageOptions language_options;

    // Undeclared query parameters get wrapped in explicit CASTs. This is needed
    // because explicit CASTs may get dropped from the resolved AST upon parsing
    // but may be required for inferring the type of the undeclared parameters.
    // This member can be copied from AnalyzerOutput::undeclared_parameters().
    QueryParametersMap undeclared_parameters;

    // Same as above, but applies to undeclared positional parameters.
    // This member can be copied from
    // AnalyzerOutput::undeclared_positional_parameters().
    std::vector<const Type*> undeclared_positional_parameters;

    // Controls how the SQLBuilder outputs positional parameters. By default,
    // they are converted to ?. In named mode, which allows the caller to
    // sanity-check where they appear in the query, positional parameters are
    // converted to parameters named @param1, @param2, and so on in
    // correspondence with the position of the parameter.
    //
    // Warning: there is no attempt made to verify that the ? in the output
    // appear in the same order of the positional parameters in the resolved
    // AST.
    enum PositionalParameterOutputMode {
      kQuestionMark = 0,
      kNamed = 1,
    };
    PositionalParameterOutputMode positional_parameter_mode = kQuestionMark;

    // Optional catalog, can affect rendering in some corner cases:
    //  - Opaque enum types will always render as literals if the type isn't
    //    found, which is an indication it isn't fully supported.
    Catalog* catalog = nullptr;

    // Controls the target syntax mode of the SQLBuilder.
    // Warning: support for Pipe SQL target syntax is in-developed and not fully
    // tested, and is not guaranteed to produce correct results in some cases.
    // TODO: b/357999315 - Once all testing is added, remove this warning.
    TargetSyntaxMode target_syntax_mode = TargetSyntaxMode::kStandard;

    // Records syntax hint against specific Resolved AST node to decide what
    // SQL syntax to restore. The syntax hint is particularly useful when
    // multiple SQL syntaxes are possible.
    // WARNING: This map should only be populated by GoogleSQL resolver and RQG.
    // It's an advanced feature with specific intended use cases and misusing it
    // can cause the SQLBuilder to misbehave.
    TargetSyntaxMap target_syntax_map;

    // Setting to true will return FLOAT32 as the type name for TYPE_FLOAT,
    // for PRODUCT_EXTERNAL mode.
    // TODO: Remove once all engines are updated to use FLOAT32 in
    // the external mode.
    bool use_external_float32 = false;
  };

  explicit SQLBuilder(const SQLBuilderOptions& options = SQLBuilderOptions());
  SQLBuilder(const SQLBuilder&) = delete;
  SQLBuilder& operator=(const SQLBuilder&) = delete;
  ~SQLBuilder() override;

  // Visit all nodes in the AST tree and accumulate SQL to be returned via sql()
  // call, below.
  absl::Status Process(const ResolvedNode& ast);

  // Returns the sql string for the last visited ResolvedAST.
  // If we are in GOOGLESQL_DEBUG_MODE then we will check that all fields are accessed
  // after generating the SQL for the fragment's AST node, to ensure that SQL
  // generation didn't miss any semantically meaningful part of the related AST
  // node.
  absl::StatusOr<std::string> GetSql();

  // This deprecated version can crash in some cases.  Use GetSql() instead.
  // This one also returns successfully with "" if no nodes have been visited,
  // which GetSql() won't do any more.
  ABSL_DEPRECATED("Use GetSql(), which can handle errors without crashing")
  std::string sql();

  // Returns a map of column id to its path expression.
  std::map</* column_id */ int, std::string>& mutable_column_paths() {
    return state_.column_paths;
  }
  const std::map</* column_id */ int, std::string>& column_paths() const {
    return state_.column_paths;
  }

  // Returns a map of column id to its alias.
  std::map</* column_id */ int, std::string>& mutable_computed_column_alias() {
    return state_.computed_column_alias;
  }
  const std::map</* column_id */ int, std::string>& computed_column_alias()
      const {
    return state_.computed_column_alias;
  }

  // Returns a set of all column names referenced in the query.
  absl::flat_hash_set<std::string>& mutable_col_ref_names() {
    return state_.col_ref_names;
  }
  const absl::flat_hash_set<std::string>& col_ref_names() const {
    return state_.col_ref_names;
  }

  // Returns a map of scans to it unique aliases.
  absl::flat_hash_map<const ResolvedScan*, std::string>&
  mutable_scan_alias_map() {
    return state_.scan_alias_map;
  }
  const absl::flat_hash_map<const ResolvedScan*, std::string>& scan_alias_map()
      const {
    return state_.scan_alias_map;
  }

  // Returns a map of tables to it unique aliases.
  absl::flat_hash_map<const Table*, std::string>& mutable_table_alias_map() {
    return state_.table_alias_map;
  }
  const absl::flat_hash_map<const Table*, std::string>& table_alias_map()
      const {
    return state_.table_alias_map;
  }

  // Returns a map of pending column id to SQL string.
  std::map<int /* column_id */, std::string>& mutable_pending_columns() {
    return state_.pending_columns;
  }
  const std::map<int /* column_id */, std::string>& pending_columns() const {
    return state_.pending_columns;
  }

  // Returns a map of correlated pending column id to SQL string.
  std::map<int /* column_id */, std::string>&
  mutable_correlated_pending_columns() {
    return state_.correlated_pending_columns;
  }
  const std::map<int /* column_id */, std::string>& correlated_pending_columns()
      const {
    return state_.correlated_pending_columns;
  }

  // Returns a map of implicit alias to the table identifier.
  absl::flat_hash_map<std::string, std::string>&
  mutable_tables_with_implicit_alias() {
    return state_.tables_with_implicit_alias;
  }
  const absl::flat_hash_map<std::string, std::string>&
  tables_with_implicit_alias() const {
    return state_.tables_with_implicit_alias;
  }

  // Returns a deque of dml target path and alias.
  std::deque<
      std::pair</* target_path */ std::string, /* target_alias */ std::string>>&
  mutable_nested_dml_targets() {
    return state_.nested_dml_targets;
  }
  const std::deque<
      std::pair</* target_path */ std::string, /* target_alias */ std::string>>&
  nested_dml_targets() const {
    return state_.nested_dml_targets;
  }

  // Returns a stack of stack of update item target path and alias.
  std::deque<std::deque<std::pair<
      /* target_sql */ std::string,
      /* container_element_access_sql */ CopyableState::
          UpdateItemElementState>>>&
  mutable_update_item_targets_and_offsets() {
    return state_.update_item_targets_and_offsets;
  }
  const std::deque<std::deque<std::pair<
      /* target_sql */ std::string,
      /* container_element_access_sql */ CopyableState::
          UpdateItemElementState>>>&
  update_item_targets_and_offsets() const {
    return state_.update_item_targets_and_offsets;
  }

  // Set the max seen alias id.
  void set_max_seen_alias_id(int max_seen_alias_id);

  // Returns the max seen alias id.
  int* mutable_max_seen_alias_id();
  int max_seen_alias_id() const;

  // Visit methods for types of ResolvedStatement.
  absl::Status VisitResolvedQueryStmt(const ResolvedQueryStmt* node) override;
  absl::Status VisitResolvedGeneralizedQueryStmt(
      const ResolvedGeneralizedQueryStmt* node) override;
  absl::Status VisitResolvedMultiStmt(const ResolvedMultiStmt* node) override;
  absl::Status VisitResolvedCreateWithEntryStmt(
      const ResolvedCreateWithEntryStmt* node) override;
  absl::Status VisitResolvedExplainStmt(
      const ResolvedExplainStmt* node) override;
  absl::Status VisitResolvedCreateConnectionStmt(
      const ResolvedCreateConnectionStmt* node) override;
  absl::Status VisitResolvedCreateDatabaseStmt(
      const ResolvedCreateDatabaseStmt* node) override;
  absl::Status VisitResolvedCreateIndexStmt(
      const ResolvedCreateIndexStmt* node) override;
  absl::Status VisitResolvedCreateModelStmt(
      const ResolvedCreateModelStmt* node) override;
  absl::Status VisitResolvedCreateSchemaStmt(
      const ResolvedCreateSchemaStmt* node) override;
  absl::Status VisitResolvedCreateExternalSchemaStmt(
      const ResolvedCreateExternalSchemaStmt* node) override;
  absl::Status VisitResolvedCreateTableStmt(
      const ResolvedCreateTableStmt* node) override;
  absl::Status VisitResolvedCreateSequenceStmt(
      const ResolvedCreateSequenceStmt* node) override;
  absl::Status VisitResolvedCreateSnapshotTableStmt(
      const ResolvedCreateSnapshotTableStmt* node) override;
  absl::Status VisitResolvedCreateTableAsSelectStmt(
      const ResolvedCreateTableAsSelectStmt* node) override;
  absl::Status VisitResolvedCreateTableAsSelectStmtImpl(
      const ResolvedCreateTableAsSelectStmt* node, bool generate_as_pipe);
  absl::Status VisitResolvedCreateViewStmt(
      const ResolvedCreateViewStmt* node) override;
  absl::Status VisitResolvedCreateMaterializedViewStmt(
      const ResolvedCreateMaterializedViewStmt* node) override;
  absl::Status VisitResolvedCreateApproxViewStmt(
      const ResolvedCreateApproxViewStmt* node) override;
  absl::Status VisitResolvedCreateExternalTableStmt(
      const ResolvedCreateExternalTableStmt* node) override;
  absl::Status VisitResolvedCreatePrivilegeRestrictionStmt(
      const ResolvedCreatePrivilegeRestrictionStmt* node) override;
  absl::Status VisitResolvedCreateRowAccessPolicyStmt(
      const ResolvedCreateRowAccessPolicyStmt* node) override;
  absl::Status VisitResolvedCreateConstantStmt(
      const ResolvedCreateConstantStmt* node) override;
  absl::Status VisitResolvedCreateFunctionStmt(
      const ResolvedCreateFunctionStmt* node) override;
  absl::Status VisitResolvedCreateTableFunctionStmt(
      const ResolvedCreateTableFunctionStmt* node) override;
  absl::Status VisitResolvedCreateProcedureStmt(
      const ResolvedCreateProcedureStmt* node) override;
  absl::Status VisitResolvedArgumentDef(
      const ResolvedArgumentDef* node) override;
  absl::Status VisitResolvedArgumentRef(
      const ResolvedArgumentRef* node) override;
  absl::Status VisitResolvedCloneDataStmt(
      const ResolvedCloneDataStmt* node) override;
  absl::Status VisitResolvedExportDataStmt(
      const ResolvedExportDataStmt* node) override;
  absl::Status VisitResolvedExportDataStmtImpl(
      const ResolvedExportDataStmt* node, bool generate_as_pipe);
  absl::Status VisitResolvedExportModelStmt(
      const ResolvedExportModelStmt* node) override;
  absl::Status VisitResolvedExportMetadataStmt(
      const ResolvedExportMetadataStmt* node) override;
  absl::Status VisitResolvedCallStmt(const ResolvedCallStmt* node) override;
  absl::Status VisitResolvedDefineTableStmt(
      const ResolvedDefineTableStmt* node) override;
  absl::Status VisitResolvedDescribeStmt(
      const ResolvedDescribeStmt* node) override;
  absl::Status VisitResolvedShowStmt(const ResolvedShowStmt* node) override;
  absl::Status VisitResolvedBeginStmt(const ResolvedBeginStmt* node) override;
  absl::Status VisitResolvedSetTransactionStmt(
      const ResolvedSetTransactionStmt* node) override;
  absl::Status VisitResolvedCommitStmt(const ResolvedCommitStmt* node) override;
  absl::Status VisitResolvedRollbackStmt(
      const ResolvedRollbackStmt* node) override;
  absl::Status VisitResolvedStartBatchStmt(
      const ResolvedStartBatchStmt* node) override;
  absl::Status VisitResolvedRunBatchStmt(
      const ResolvedRunBatchStmt* node) override;
  absl::Status VisitResolvedAbortBatchStmt(
      const ResolvedAbortBatchStmt* node) override;
  absl::Status VisitResolvedDeleteStmt(const ResolvedDeleteStmt* node) override;
  absl::Status VisitResolvedUndropStmt(const ResolvedUndropStmt* node) override;
  absl::Status VisitResolvedDropStmt(const ResolvedDropStmt* node) override;
  absl::Status VisitResolvedDropFunctionStmt(
      const ResolvedDropFunctionStmt* node) override;
  absl::Status VisitResolvedDropTableFunctionStmt(
      const ResolvedDropTableFunctionStmt* node) override;
  absl::Status VisitResolvedDropMaterializedViewStmt(
      const ResolvedDropMaterializedViewStmt* node) override;
  absl::Status VisitResolvedDropPrivilegeRestrictionStmt(
      const ResolvedDropPrivilegeRestrictionStmt* node) override;
  absl::Status VisitResolvedDropRowAccessPolicyStmt(
      const ResolvedDropRowAccessPolicyStmt* node) override;
  absl::Status VisitResolvedDropSnapshotTableStmt(
      const ResolvedDropSnapshotTableStmt* node) override;
  absl::Status VisitResolvedDropIndexStmt(
      const ResolvedDropIndexStmt* node) override;
  absl::Status VisitResolvedTruncateStmt(
      const ResolvedTruncateStmt* node) override;
  absl::Status VisitResolvedUpdateStmt(const ResolvedUpdateStmt* node) override;
  absl::Status VisitResolvedInsertStmt(const ResolvedInsertStmt* node) override;
  absl::Status VisitResolvedInsertStmtImpl(const ResolvedInsertStmt* node,
                                           bool generate_as_pipe);
  absl::Status VisitResolvedMergeStmt(const ResolvedMergeStmt* node) override;
  absl::Status VisitResolvedMergeWhen(const ResolvedMergeWhen* node) override;
  absl::Status VisitResolvedGrantStmt(const ResolvedGrantStmt* node) override;
  absl::Status VisitResolvedRevokeStmt(const ResolvedRevokeStmt* node) override;
  absl::Status VisitResolvedAlterConnectionStmt(
      const ResolvedAlterConnectionStmt* node) override;
  absl::Status VisitResolvedAlterDatabaseStmt(
      const ResolvedAlterDatabaseStmt* node) override;
  absl::Status VisitResolvedAlterPrivilegeRestrictionStmt(
      const ResolvedAlterPrivilegeRestrictionStmt* node) override;
  absl::Status VisitResolvedAlterRowAccessPolicyStmt(
      const ResolvedAlterRowAccessPolicyStmt* node) override;
  absl::Status VisitResolvedAlterAllRowAccessPoliciesStmt(
      const ResolvedAlterAllRowAccessPoliciesStmt* node) override;
  absl::Status VisitResolvedAlterSchemaStmt(
      const ResolvedAlterSchemaStmt* node) override;
  absl::Status VisitResolvedAlterExternalSchemaStmt(
      const ResolvedAlterExternalSchemaStmt* node) override;
  absl::Status VisitResolvedAlterSequenceStmt(
      const ResolvedAlterSequenceStmt* node) override;
  absl::Status VisitResolvedAlterTableSetOptionsStmt(
      const ResolvedAlterTableSetOptionsStmt* node) override;
  absl::Status VisitResolvedAlterTableStmt(
      const ResolvedAlterTableStmt* node) override;
  absl::Status VisitResolvedAlterViewStmt(
      const ResolvedAlterViewStmt* node) override;
  absl::Status VisitResolvedAlterMaterializedViewStmt(
      const ResolvedAlterMaterializedViewStmt* node) override;
  absl::Status VisitResolvedAlterApproxViewStmt(
      const ResolvedAlterApproxViewStmt* node) override;
  absl::Status VisitResolvedAlterIndexStmt(
      const ResolvedAlterIndexStmt* node) override;
  absl::Status VisitResolvedAlterModelStmt(
      const ResolvedAlterModelStmt* node) override;
  absl::Status VisitResolvedRenameStmt(const ResolvedRenameStmt* node) override;
  absl::Status VisitResolvedImportStmt(const ResolvedImportStmt* node) override;
  absl::Status VisitResolvedModuleStmt(const ResolvedModuleStmt* node) override;
  absl::Status VisitResolvedAnalyzeStmt(
      const ResolvedAnalyzeStmt* node) override;
  absl::Status VisitResolvedAssertStmt(const ResolvedAssertStmt* node) override;
  absl::Status VisitResolvedAssignmentStmt(
      const ResolvedAssignmentStmt* node) override;
  absl::Status VisitResolvedExecuteImmediateStmt(
      const ResolvedExecuteImmediateStmt* node) override;

  // Visit methods for types of ResolvedExpr.
  absl::Status VisitResolvedExpressionColumn(
      const ResolvedExpressionColumn* node) override;
  absl::Status VisitResolvedCatalogColumnRef(
      const ResolvedCatalogColumnRef* node) override;
  absl::Status VisitResolvedLiteral(const ResolvedLiteral* node) override;
  absl::Status VisitResolvedConstant(const ResolvedConstant* node) override;
  absl::Status VisitResolvedFunctionCall(
      const ResolvedFunctionCall* node) override;
  absl::Status VisitResolvedAggregateFunctionCall(
      const ResolvedAggregateFunctionCall* node) override;
  absl::Status VisitResolvedAnalyticFunctionCall(
      const ResolvedAnalyticFunctionCall* node) override;
  absl::Status VisitResolvedInlineLambda(
      const ResolvedInlineLambda* node) override;
  absl::Status VisitResolvedSequence(const ResolvedSequence* node) override;
  absl::Status VisitResolvedGetProtoField(
      const ResolvedGetProtoField* node) override;
  absl::Status VisitResolvedFlatten(const ResolvedFlatten* node) override;
  absl::Status VisitResolvedFilterField(
      const ResolvedFilterField* node) override;
  absl::Status VisitResolvedReplaceField(
      const ResolvedReplaceField* node) override;
  absl::Status VisitResolvedFlattenedArg(
      const ResolvedFlattenedArg* node) override;
  absl::Status VisitResolvedColumnRef(const ResolvedColumnRef* node) override;
  absl::Status VisitResolvedCast(const ResolvedCast* node) override;
  absl::Status VisitResolvedColumnHolder(
      const ResolvedColumnHolder* node) override;
  absl::Status VisitResolvedSubqueryExpr(
      const ResolvedSubqueryExpr* node) override;
  absl::Status VisitResolvedWithExpr(const ResolvedWithExpr* node) override;
  absl::Status VisitResolvedTableAndColumnInfo(
      const ResolvedTableAndColumnInfo* node) override;
  absl::Status VisitResolvedOption(const ResolvedOption* node) override;
  absl::Status VisitResolvedParameter(const ResolvedParameter* node) override;
  absl::Status VisitResolvedSystemVariable(
      const ResolvedSystemVariable* node) override;
  absl::Status VisitResolvedMakeProto(const ResolvedMakeProto* node) override;
  absl::Status VisitResolvedMakeProtoField(
      const ResolvedMakeProtoField* node) override;
  absl::Status VisitResolvedMakeStruct(const ResolvedMakeStruct* node) override;
  absl::Status VisitResolvedGetStructField(
      const ResolvedGetStructField* node) override;
  absl::Status VisitResolvedGetJsonField(
      const ResolvedGetJsonField* node) override;
  absl::Status VisitResolvedGetRowField(
      const ResolvedGetRowField* node) override;
  absl::Status VisitResolvedOrderByItem(
      const ResolvedOrderByItem* node) override;
  absl::Status VisitResolvedComputedColumn(
      const ResolvedComputedColumn* node) override;
  absl::Status VisitResolvedDeferredComputedColumn(
      const ResolvedDeferredComputedColumn* node) override;
  absl::Status VisitResolvedAssertRowsModified(
      const ResolvedAssertRowsModified* node) override;
  absl::Status VisitResolvedReturningClause(
      const ResolvedReturningClause* node) override;
  absl::Status VisitResolvedOnConflictClause(
      const ResolvedOnConflictClause* node) override;
  absl::Status VisitResolvedDMLDefault(const ResolvedDMLDefault* node) override;
  absl::Status VisitResolvedDMLValue(const ResolvedDMLValue* node) override;
  absl::Status VisitResolvedInsertRow(const ResolvedInsertRow* node) override;
  absl::Status VisitResolvedUpdateItem(const ResolvedUpdateItem* node) override;
  absl::Status VisitResolvedUpdateItemElement(
      const ResolvedUpdateItemElement* node) override;
  absl::Status VisitResolvedPrivilege(const ResolvedPrivilege* node) override;
  absl::Status VisitResolvedAggregateHavingModifier(
      const ResolvedAggregateHavingModifier* node) override;
  absl::Status VisitResolvedGetProtoOneof(
      const ResolvedGetProtoOneof* node) override;

  // Visit methods for types of ResolvedScan.
  absl::Status VisitResolvedAnalyticScan(
      const ResolvedAnalyticScan* node) override;
  absl::Status VisitResolvedTableScan(const ResolvedTableScan* node) override;
  absl::Status VisitResolvedProjectScan(
      const ResolvedProjectScan* node) override;
  absl::Status VisitResolvedExecuteAsRoleScan(
      const ResolvedExecuteAsRoleScan* node) override;
  absl::Status VisitResolvedTVFScan(const ResolvedTVFScan* node) override;
  absl::Status VisitResolvedRelationArgumentScan(
      const ResolvedRelationArgumentScan* node) override;
  absl::Status VisitResolvedFilterScan(const ResolvedFilterScan* node) override;
  absl::Status VisitResolvedJoinScan(const ResolvedJoinScan* node) override;
  absl::Status VisitResolvedArrayScan(const ResolvedArrayScan* node) override;
  absl::Status VisitResolvedLimitOffsetScan(
      const ResolvedLimitOffsetScan* node) override;
  absl::Status VisitResolvedSetOperationScan(
      const ResolvedSetOperationScan* node) override;
  absl::Status VisitResolvedOrderByScan(
      const ResolvedOrderByScan* node) override;
  absl::Status VisitResolvedAggregateScan(
      const ResolvedAggregateScan* node) override;
  absl::Status VisitResolvedAnonymizedAggregateScan(
      const ResolvedAnonymizedAggregateScan* node) override;
  absl::Status VisitResolvedDifferentialPrivacyAggregateScan(
      const ResolvedDifferentialPrivacyAggregateScan* node) override;
  absl::Status VisitResolvedAggregationThresholdAggregateScan(
      const ResolvedAggregationThresholdAggregateScan* node) override;
  absl::Status VisitResolvedRecursiveScan(
      const ResolvedRecursiveScan* node) override;
  absl::Status VisitResolvedWithScan(const ResolvedWithScan* node) override;
  absl::Status VisitResolvedRecursiveRefScan(
      const ResolvedRecursiveRefScan* node) override;
  absl::Status VisitResolvedRecursionDepthModifier(
      const ResolvedRecursionDepthModifier* node) override;
  absl::Status VisitResolvedWithRefScan(
      const ResolvedWithRefScan* node) override;
  absl::Status VisitResolvedSampleScan(const ResolvedSampleScan* node) override;
  absl::StatusOr<std::string> GetSqlForSample(const ResolvedSampleScan* node,
                                              bool is_gql);
  absl::Status VisitResolvedSingleRowScan(
      const ResolvedSingleRowScan* node) override;
  absl::Status VisitResolvedUnsetArgumentScan(
      const ResolvedUnsetArgumentScan* node) override;
  absl::Status VisitResolvedMatchRecognizeScan(
      const ResolvedMatchRecognizeScan* node) override;
  absl::StatusOr<std::string> GeneratePatternExpression(
      const ResolvedMatchRecognizePatternExpr* node);
  absl::Status VisitResolvedPivotScan(const ResolvedPivotScan* node) override;
  absl::Status VisitResolvedUnpivotScan(
      const ResolvedUnpivotScan* node) override;
  absl::Status VisitResolvedGroupRowsScan(
      const ResolvedGroupRowsScan* node) override;
  absl::Status VisitResolvedDescribeScan(
      const ResolvedDescribeScan* node) override;
  absl::Status VisitResolvedStaticDescribeScan(
      const ResolvedStaticDescribeScan* node) override;
  absl::Status VisitResolvedAssertScan(const ResolvedAssertScan* node) override;
  absl::Status VisitResolvedLogScan(const ResolvedLogScan* node) override;
  absl::Status VisitResolvedPipeIfScan(const ResolvedPipeIfScan* node) override;
  absl::Status VisitResolvedPipeForkScan(
      const ResolvedPipeForkScan* node) override;
  absl::Status VisitResolvedPipeTeeScan(
      const ResolvedPipeTeeScan* node) override;
  absl::Status VisitResolvedPipeExportDataScan(
      const ResolvedPipeExportDataScan* node) override;
  absl::Status VisitResolvedPipeCreateTableScan(
      const ResolvedPipeCreateTableScan* node) override;
  absl::Status VisitResolvedPipeInsertScan(
      const ResolvedPipeInsertScan* node) override;
  absl::Status VisitResolvedBarrierScan(
      const ResolvedBarrierScan* node) override;
  absl::Status VisitResolvedSubpipeline(
      const ResolvedSubpipeline* node) override;
  absl::Status VisitResolvedSubpipelineInputScan(
      const ResolvedSubpipelineInputScan* node) override;
  absl::Status VisitResolvedSubpipelineStmt(
      const ResolvedSubpipelineStmt* node) override;
  absl::Status VisitResolvedStatementWithPipeOperatorsStmt(
      const ResolvedStatementWithPipeOperatorsStmt* node) override;

  // Visit methods for analytic functions related nodes.
  absl::Status VisitResolvedAnalyticFunctionGroup(
      const ResolvedAnalyticFunctionGroup* node) override;
  absl::Status VisitResolvedWindowPartitioning(
      const ResolvedWindowPartitioning* node) override;
  absl::Status VisitResolvedWindowOrdering(
      const ResolvedWindowOrdering* node) override;
  absl::Status VisitResolvedWindowFrame(
      const ResolvedWindowFrame* node) override;
  absl::Status VisitResolvedWindowFrameExpr(
      const ResolvedWindowFrameExpr* node) override;
  absl::Status VisitResolvedCreateEntityStmt(
      const ResolvedCreateEntityStmt* node) override;
  absl::Status VisitResolvedAlterEntityStmt(
      const ResolvedAlterEntityStmt* node) override;
  absl::Status VisitResolvedAuxLoadDataStmt(
      const ResolvedAuxLoadDataStmt* node) override;
  absl::Status VisitResolvedUpdateConstructor(
      const ResolvedUpdateConstructor* node) override;

  absl::Status VisitResolvedLockMode(const ResolvedLockMode* node) override;

  // Visit methods for graph related nodes.
  absl::Status VisitResolvedCreatePropertyGraphStmt(
      const ResolvedCreatePropertyGraphStmt* node) override;
  absl::Status VisitResolvedGraphTableScan(
      const ResolvedGraphTableScan* node) override;
  absl::Status VisitResolvedGraphNodeScan(
      const ResolvedGraphNodeScan* node) override;
  absl::Status VisitResolvedGraphEdgeScan(
      const ResolvedGraphEdgeScan* node) override;
  absl::Status VisitResolvedGraphPathScan(
      const ResolvedGraphPathScan* node) override;
  absl::Status VisitResolvedGraphScan(const ResolvedGraphScan* node) override;
  absl::Status VisitResolvedGraphCallScan(
      const ResolvedGraphCallScan* node) override;
  absl::Status VisitResolvedGraphGetElementProperty(
      const ResolvedGraphGetElementProperty* node) override;
  absl::Status VisitResolvedGraphIsLabeledPredicate(
      const ResolvedGraphIsLabeledPredicate* node) override;

  absl::Status VisitResolvedGraphLabel(const ResolvedGraphLabel* node) override;
  absl::Status VisitResolvedGraphLabelNaryExpr(
      const ResolvedGraphLabelNaryExpr* node) override;
  absl::Status MakeGraphLabelNaryExpressionString(
      absl::string_view op, const ResolvedGraphLabelNaryExpr* node,
      std::string& output);
  absl::Status VisitResolvedGraphWildCardLabel(
      const ResolvedGraphWildCardLabel* node) override;
  absl::Status VisitResolvedArrayAggregate(
      const ResolvedArrayAggregate* node) override;
  absl::Status ProcessResolvedGraphShape(
      absl::Span<const ResolvedColumn> column_list,
      absl::Span<const std::unique_ptr<const ResolvedComputedColumn>>
          shape_expr_list,
      absl::string_view table_alias, SQLAliasPairList* select_list,
      std::string* sql);
  absl::Status ProcessGqlSubquery(const ResolvedSubqueryExpr* node,
                                  std::string& output_sql);
  absl::Status ProcessResolvedGraphPathScan(const ResolvedGraphPathScan* node,
                                            std::string& output_sql);
  absl::Status ProcessResolvedGqlLinearScanIgnoringLastReturn(
      const ResolvedGraphLinearScan* node, std::string& output_sql,
      std::vector<std::string>& columns);
  absl::Status ProcessResolvedGqlMatchOp(const ResolvedGraphScan* node,
                                         std::string& output_sql);
  absl::Status ProcessGqlCompositeScans(const ResolvedGraphLinearScan* node,
                                        std::string& output_sql,
                                        std::vector<std::string>& columns);
  absl::Status ProcessGqlSetOp(const ResolvedSetOperationScan* node,
                               std::string& output_sql,
                               std::vector<std::string>& columns);

  // Returns GQL for a graph single linear scan.
  // `output_column_to_alias` contains an ordered map of the output column to
  //     output alias. It only has value when the single linear scan is an input
  //     of a set operation.
  // `columns` is an ordered list of the output column names.
  absl::Status ProcessResolvedGqlLinearOp(
      const ResolvedGraphLinearScan* node,
      std::optional<absl::btree_map<ResolvedColumn, std::string>>
          output_column_to_alias,
      std::string& output_sql, std::vector<std::string>& columns);
  absl::Status ProcessResolvedGqlWithOp(const ResolvedAggregateScan* node,
                                        std::string& output_sql);
  absl::Status ProcessResolvedGqlWithOp(const ResolvedProjectScan* node,
                                        std::string& output_sql);
  absl::Status ProcessResolvedGqlWithOp(const ResolvedAnalyticScan* node,
                                        std::string& output_sql);
  absl::Status ProcessResolvedGqlWithOp(const ResolvedScan* node,
                                        std::string& output_sql);
  absl::Status ProcessResolvedGqlLetOp(const ResolvedProjectScan* node,
                                       std::string& output_sql);
  absl::Status ProcessResolvedGqlLetOp(const ResolvedAnalyticScan* node,
                                       std::string& output_sql);
  absl::Status ProcessResolvedGqlFilterOp(const ResolvedFilterScan* node,
                                          std::string& output_sql);
  absl::Status ProcessResolvedGqlSampleOp(const ResolvedSampleScan* node,
                                          std::string& output_sql);
  absl::Status ProcessResolvedGqlNamedCallOp(const ResolvedTVFScan* node,
                                             std::string& output_sql);
  // Used for operations such as GQL's LET, which may have an
  // AnalyticScan (or a ProjScan whose input is an AnalyticScan) as their input.
  // Such operators need to collapse those expressions by storing their SQL in
  // pending_computed_columns_.
  absl::Status ProcessAnalyticInputsWithNoVerticalAggregation(
      const ResolvedScan* input_scan);

  absl::Status ProcessResolvedGqlForOp(const ResolvedArrayScan* node,
                                       std::string& output_sql);
  // Processes an ORDER BY clause as a primitive query statement.
  absl::Status ProcessResolvedGqlOrderByOp(const ResolvedOrderByScan* node,
                                           std::string& output_sql);
  absl::Status ProcessResolvedGqlPageOp(const ResolvedLimitOffsetScan* node,
                                        std::string& output_sql);
  absl::Status ProcessGraphPathPatternQuantifier(
      const ResolvedGraphPathPatternQuantifier* node, std::string& output_sql);
  void AppendGraphReference(const ResolvedGraphTableScan* node,
                            std::string& output_sql);
  absl::Status ProcessResolvedGraphSubquery(const ResolvedGraphTableScan* node,
                                            std::string& output_sql);

  // Helper method to process the input project scan in an OrderByScan.
  absl::Status ProcessResolvedGqlProjectScanFromOrderBy(
      const ResolvedProjectScan* scan);

  // Helper method to process an order by item in an OrderByScan.
  // `return_alias` is nullptr if the order by item is a return column.
  absl::StatusOr<std::string> ProcessResolvedGqlOrderByItem(
      const ResolvedOrderByItem* item, const std::string* return_alias);

  // Helper method to process the OFFSET clause in a LimitOffsetScan.
  absl::Status ProcessGqlOffset(const ResolvedLimitOffsetScan* node,
                                std::string& output_sql);

  // Helper method to process the LIMIT clause in a LimitOffsetScan.
  absl::Status ProcessGqlLimit(const ResolvedLimitOffsetScan* node,
                               std::string& output_sql);

 private:
  using StringToStringHashMap = absl::flat_hash_map<std::string, std::string>;

  absl::Status VisitResolvedGraphElementScanInternal(
      const ResolvedGraphElementScan* node, absl::string_view prefix,
      absl::string_view suffix);

  // Helper method to append element tables SQL strings to `sql`.
  absl::Status AppendElementTables(
      absl::Span<const std::unique_ptr<const ResolvedGraphElementTable>> tables,
      const absl::flat_hash_map<std::string, const ResolvedGraphElementLabel*>&
          name_to_label,
      std::string& sql);
  // Helper method to append a single element table's SQL string to `sql`.
  absl::Status VisitResolvedGraphElementTableInternal(
      const std::unique_ptr<const ResolvedGraphElementTable>& node,
      const absl::flat_hash_map<std::string, const ResolvedGraphElementLabel*>&
          name_to_label,
      std::string& sql);

  // Helper method to take an output column list and convert it to its
  // SQL representation, computing column aliases when necessary.
  // `column_alias_to_sql` is a map that will replace the WITH clause item
  // for a given column alias with a precomputed SQL string.
  absl::Status PopulateItemsForGqlWithOp(
      absl::Span<const ResolvedColumn> columns,
      StringToStringHashMap& column_alias_to_sql,
      std::vector<std::string>& with_item_list);

  // Helper method to linearize the sequence of scans of a GQL WITH clause.
  absl::Status LinearizeWithOpResolvedScans(
      const ResolvedScan* ret_scan,
      std::vector<const ResolvedScan*>& return_scans,
      const ResolvedLimitOffsetScan*& limit_offset_scan) const;

  // Helper method to collapse the sequence of scans of a GQL RETURN or WITH
  // clause.
  // `output_column_to_alias` contains an ordered map of the output column to
  //     output alias. It only has value if the RETURN/WITH clause belongs to a
  //     linear scan that is an input to a set operation input. If it has value,
  //     reuse the aliases. Otherwise, always generate new aliases for output
  //     columns.
  absl::Status CollapseResolvedGqlReturnScans(
      const ResolvedScan* ret_scan,
      std::optional<absl::btree_map<ResolvedColumn, std::string>>
          output_column_to_alias,
      std::string& output_sql, std::vector<std::string>& columns,
      bool generate_with = false);

  // Helper method to append a path mode to the SQL string.
  absl::Status AppendPathMode(const ResolvedGraphPathMode* path_mode,
                              std::string& sql);

  // Helper method to append a search prefix to the SQL string.
  absl::Status AppendSearchPrefix(const ResolvedGraphPathSearchPrefix* prefix,
                                  std::string& text);

  // Pushes a placeholder RecursiveQueryInfo with query_name = `query_name` and
  // scan = nullptr to `state_.recursive_query_info`.
  //
  // The placeholder scan will be updated to the real recursive scan when the
  // ResolvedRecursiveScan itself is visited.
  void AddPlaceholderRecursiveScan(absl::string_view query_name);

 public:
  absl::Status DefaultVisit(const ResolvedNode* node) override;

 protected:
  SQLBuilder(int* max_seen_alias_id_root_ptr, const SQLBuilderOptions& options,
             const CopyableState& state);

  // Allows subclasses to override the default SQL builder for GQL RETURN
  // statements.
  virtual std::unique_ptr<GqlReturnOpSQLBuilder> CreateGqlReturnOpSQLBuilder(
      const ResolvedColumnList& output_column_list,
      std::optional<absl::btree_map<ResolvedColumn, std::string>>
          output_column_to_alias,
      int* max_seen_alias_id, const SQLBuilderOptions& options,
      const CopyableState& state);

  // Holds SQL text of nodes representing expressions/subqueries and
  // QueryExpression for scan nodes.
  struct QueryFragment {
    explicit QueryFragment(const ResolvedNode* node, std::string text)
        : node(node), text(std::move(text)) {}

    // Takes ownership of the <query_expression>.
    explicit QueryFragment(const ResolvedNode* node,
                           QueryExpression* query_expression)
        : node(node), query_expression(query_expression) {}

    QueryFragment(const QueryFragment&) = delete;
    QueryFragment operator=(const QueryFragment&) = delete;

    std::string GetSQL() const;

    // Associated resolved node tree for the QueryFragment.
    const ResolvedNode* node = nullptr;

    // Populated if QueryFragment is created from a QueryExpression.
    std::unique_ptr<QueryExpression> query_expression;

   private:
    // Associated sql text for the QueryFragment.
    const std::string text;
  };

  // Dumps all the QueryFragment in query_fragments_ (if any).
  void DumpQueryFragmentStack();

  // Stack operations on query_fragments_.
  void PushQueryFragment(std::unique_ptr<QueryFragment> query_fragment);

  std::unique_ptr<QueryFragment> PopQueryFragment();

  // Helper functions which creates QueryFragment from the passed params and
  // push it on query_fragments_.
  void PushQueryFragment(const ResolvedNode* node, const std::string& text);
  void PushQueryFragment(const ResolvedNode* node,
                         QueryExpression* query_expression);

  // Helper function which Visits the given <node> and returns the corresponding
  // QueryFragment generated.
  absl::StatusOr<std::unique_ptr<QueryFragment>> ProcessNode(
      const ResolvedNode* node);
  absl::StatusOr<std::string> ProcessExecuteImmediateArgument(
      const ResolvedExecuteImmediateArgument* node);

  // Wraps the given <query_expression> corresponding to the scan <node> as a
  // subquery to the from clause, mutating the <query_expression> subsequently.
  absl::Status WrapQueryExpression(const ResolvedScan* node,
                                   QueryExpression* query_expression);

  // Helper function which fetches <select_list> for columns present in
  // <column_list>. <parent_scan> is optional; if present, it points to the
  // ResolvedScan for which we are currently fetching a select list.
  absl::Status GetSelectList(
      const ResolvedColumnList& column_list,
      const std::map<int64_t, const ResolvedExpr*>& col_to_expr_map,
      const ResolvedScan* parent_scan, QueryExpression* query_expression,
      SQLAliasPairList* select_list);
  // This overload does not expects a <col_to_expr_map> which is needed only
  // when select expressions are present in the column_list.
  absl::Status GetSelectList(const ResolvedColumnList& column_list,
                             QueryExpression* query_expression,
                             SQLAliasPairList* select_list);

  // Adds select_list for columns present in <column_list> inside the
  // <query_expression>, if not present.
  absl::Status AddSelectListIfNeeded(const ResolvedColumnList& column_list,
                                     QueryExpression* query_expression);

  // Merges the <type> and the <annotations> trees and prints the column
  // schema to <text>.
  absl::Status AppendColumnSchema(
      const Type* type, bool is_hidden,
      const ResolvedColumnAnnotations* annotations,
      const ResolvedGeneratedColumnInfo* generated_column_info,
      const ResolvedColumnDefaultValue* default_value, std::string* text);

  absl::Status AppendGeneratedColumnInfo(
      const ResolvedGeneratedColumnInfo* generated_column_info,
      std::string* text);

  absl::StatusOr<std::string> GetHintListString(
      absl::Span<const std::unique_ptr<const ResolvedOption>> hint_list);

  absl::Status AppendCloneDataSource(const ResolvedScan* source,
                                     std::string* sql);

  // Always append a (possibly empty) OPTIONS clause.
  absl::Status AppendOptions(
      absl::Span<const std::unique_ptr<const ResolvedOption>> option_list,
      std::string* sql);
  // Only append an OPTIONS clause if there is at least one option.
  absl::Status AppendOptionsIfPresent(
      absl::Span<const std::unique_ptr<const ResolvedOption>> option_list,
      std::string* sql);

  absl::Status AppendHintsIfPresent(
      absl::Span<const std::unique_ptr<const ResolvedOption>> hint_list,
      std::string* text);

  void PushSQLForQueryExpression(const ResolvedNode* node,
                                 QueryExpression* query_expression);

  // Returns fully qualified path to access the column (in scope of the caller).
  // i.e. <scan_alias>.<column_alias>
  std::string GetColumnPath(const ResolvedColumn& column);

  // Returns the alias to be used to select the column.
  std::string GetColumnAlias(const ResolvedColumn& column);

  // Create a new alias to be used for the column, replacing any existing alias.
  std::string UpdateColumnAlias(const ResolvedColumn& column);

  // Returns the sql text associated with a left/right join scan. Adds explicit
  // scan alias if necessary.
  absl::StatusOr<std::string> GetJoinOperand(const ResolvedScan* scan);

  // For USING(...) expressions track `right_to_left_column_id_mapping`. More
  // details can be found in the function comment for `GetUsingWrapper`.
  absl::StatusOr<std::string> GetJoinOperand(
      const ResolvedScan* scan, absl::flat_hash_map<int, std::vector<int>>&
                                    right_to_left_column_id_mapping);

  // Returns a list of pairs of column names and aliases for a JOIN with
  // USING(...) expression for the right side of the join to ensure that columns
  // within the USING(...) expression have the same alias.
  //
  // The following query fragment
  //
  // (select key from keyvalue) join (select key, value from keyvalue2)
  // using (key)
  //
  // would get deparsed into
  //
  // (select key as a1 from keyvalue) sq1
  // join (select sq2.a2 as a1, sq2.a3 from
  //      (select key as a2, value as a3 from keyvalue2) sq2) AS sq2
  // using (a1)
  //
  // The wrapper wraps the right side of the join to match column aliases with
  // the left side of the join (`sq2.a2 as a1`) and re-aliases these columns in
  // the case that outer parts of the query reference them.
  //
  //  We maintain the `right_to_left_column_id_mapping` from `int` to
  // `vector<int>` in the case that there are duplicate columns in the
  // USING(...) expression on the right side of the join.
  //
  // We do not support unparsing USING(...) with duplicate left columns.
  // If there is a duplicate left column then the sql_builder will build JOIN
  // ON.
  absl::StatusOr<SQLAliasPairList> GetUsingWrapper(
      absl::string_view scan_alias, const ResolvedColumnList& column_list,
      absl::flat_hash_map<int, std::vector<int>>&
          right_to_left_column_id_mapping);

  // Helper function which fetches the list of function arguments
  absl::StatusOr<std::string> GetFunctionArgListString(
      absl::Span<const std::string> arg_name_list,
      const FunctionSignature& signature);

  // Fetches the scan alias corresponding to the given scan node. If not
  // present, assigns a new unique alias to the scan node.
  std::string GetScanAlias(const ResolvedScan* scan);

  // Like GetScanAlias but for a Table, to be able to assign different aliases
  // to ResolvedTableScan and its underlying Table.
  std::string GetTableAlias(const Table* table);

  // Helper for the above. Keep generating aliases until find one that does not
  // conflict with a column name.
  std::string MakeNonconflictingAlias(absl::string_view name);

  // Checks whether the table can be used without an explicit "AS" clause, which
  // will use the last component of its table name as its alias. If we have two
  // tables with the same local name in two catalogs/nested catalogs, then we
  // cannot use both without an explicit alias (for example, S.T vs. T, or
  // S1.T vs. S2.T).
  bool CanTableBeUsedWithImplicitAlias(const Table* table);

  // Returns a unique incremental id each time this is called.
  int64_t GetUniqueId();

  // Get the sql for CreateViewStatement/CreateMaterializedViewStatement.
  absl::Status GetCreateViewStatement(const ResolvedCreateViewBase* node,
                                      bool is_value_table,
                                      const std::string& view_type);

  // Get the first part of the syntax for a CREATE command, including statement
  // hints, the <object_type> (e.g. "TABLE") and name, and CREATE modifiers,
  // and a trailing space.
  absl::Status GetCreateStatementPrefix(const ResolvedCreateStatement* node,
                                        absl::string_view object_type,
                                        std::string* sql);

  // If the view was created with explicit column names, prints the column
  // names with optional column options.
  absl::Status GetOptionalColumnNameWithOptionsList(
      const ResolvedCreateViewBase* node, std::string* sql);

  // Appends PARTITION BY or CLUSTER BY expressions to the provided string, not
  // including the "PARTITION BY " or "CLUSTER BY " prefix.
  absl::Status GetPartitionByListString(
      absl::Span<const std::unique_ptr<const ResolvedExpr>> partition_by_list,
      std::string* sql);

  // Helper function to get corresponding SQL for a list of TableAndColumnInfo
  // to be analyzed in ANALYZE STATEMENT.
  absl::Status GetTableAndColumnInfoList(
      absl::Span<const std::unique_ptr<const ResolvedTableAndColumnInfo>>
          table_and_column_info_list,
      std::string* sql);

  static std::string GetOptionalObjectType(absl::string_view object_type);

  // Get the "<privilege_list> ON <object_type> <name_path>" part of a GRANT or
  // REVOKE statement.
  absl::Status GetPrivilegesString(const ResolvedGrantOrRevokeStmt* node,
                                   std::string* sql);

  // Appends PARTITIONS(...) expressions to the provided string, including
  // "PARTITIONS" prefix.
  absl::Status GetLoadDataPartitionFilterString(
      const ResolvedAuxLoadDataPartitionFilter* partition_filter,
      std::string* sql);

  // Helper functions to save the <path> used to access the column later.
  void SetPathForColumn(const ResolvedColumn& column, const std::string& path);
  void SetPathForColumnList(const ResolvedColumnList& column_list,
                            absl::string_view scan_alias);
  absl::Status SetPathForColumnsInScan(const ResolvedScan* scan,
                                       const std::string& alias);

  // Helper functions to set up ResolvedColumn path in returning clause.
  absl::Status SetPathForColumnsInReturningExpr(const ResolvedExpr* expr);

  // Helper function to ensure:
  // - Columns in <output_column_list> matches 1:1 in order with
  //   <query_expression> select-list.
  // - Column aliases in <output_column_list> (if not internal) matches the
  //   <query_expression> select-list.
  absl::Status MatchOutputColumns(
      absl::Span<const std::unique_ptr<const ResolvedOutputColumn>>
          output_column_list,
      const ResolvedScan* query, QueryExpression* query_expression);

  // Helper function which process the given scan <query> and returns the
  // corresponding QueryExpression. Also ensures that the query produces columns
  // matching the order and aliases in <output_column_list>.
  // Caller owns the returned QueryExpression.
  absl::StatusOr<std::unique_ptr<QueryExpression>> ProcessQuery(
      const ResolvedScan* query,
      absl::Span<const std::unique_ptr<const ResolvedOutputColumn>>
          output_column_list);

  static absl::Status AddNullHandlingModifier(
      ResolvedNonScalarFunctionCallBase::NullHandlingModifier kind,
      std::string* arguments_suffix);

  // Setting the optional parameter `use_external_float32` to true will return
  // FLOAT32 as the type name for TYPE_FLOAT, for PRODUCT_EXTERNAL mode.
  // TODO: Remove `use_external_float32` once all engines are
  // updated to use FLOAT32 as the external name.
  absl::StatusOr<std::string> GetSQL(const Value& value, ProductMode mode,
                                     bool is_constant_value = false);
  absl::StatusOr<std::string> GetSQL(const Value& value, ProductMode mode,
                                     bool use_external_float32,
                                     bool is_constant_value = false);

  // Similar to the above function, but uses <annotation_map> to indicate the
  // annotation map of value type. Currently we only support collation
  // annotations for NULL values, and would return error if collation
  // annotations exist while <value> is not NULL.
  absl::StatusOr<std::string> GetSQL(const Value& value,
                                     const AnnotationMap* annotation_map,
                                     ProductMode mode,
                                     bool is_constant_value = false);
  absl::StatusOr<std::string> GetSQL(const Value& value,
                                     const AnnotationMap* annotation_map,
                                     ProductMode mode,
                                     bool use_external_float32,
                                     bool is_constant_value = false);

  absl::StatusOr<std::string> GetFunctionCallSQL(
      const ResolvedFunctionCallBase* function_call,
      std::vector<std::string> inputs, absl::string_view arguments_suffix,
      bool allow_chained_call = true);

  // Helper function to return corresponding SQL for a list of
  // ResolvedUpdateItems.
  absl::StatusOr<std::string> GetUpdateItemListSQL(
      absl::Span<const std::unique_ptr<const ResolvedUpdateItem>>
          update_item_list);

  // Helper function to return corresponding SQL for a list of columns to be
  // inserted.
  std::string GetInsertColumnListSQL(
      absl::Span<const ResolvedColumn> insert_column_list) const;

  absl::Status ProcessWithPartitionColumns(
      std::string* sql, const ResolvedWithPartitionColumns* node);

  absl::StatusOr<std::string> ProcessCreateTableStmtBase(
      const ResolvedCreateTableStmtBase* node, bool process_column_definitions,
      absl::string_view table_type);

  // Helper function for adding SQL for aggregate and group by lists.
  absl::Status ProcessAggregateScanBase(
      const ResolvedAggregateScanBase* node, bool is_differencial_privacy_scan,
      absl::flat_hash_map<int /*grouping_column_id*/,
                          int /*grouping_argument_group_by_column_id*/>
          grouping_column_id_map,
      QueryExpression* query_expression);

  // Helper function to return corresponding SQL for a list of
  // ResolvedAlterActions.
  absl::StatusOr<std::string> GetAlterActionListSQL(
      absl::Span<const std::unique_ptr<const ResolvedAlterAction>>
          alter_action_list);

  // Helper function to return corresponding SQL for a single
  // ResolvedAlterAction.
  absl::StatusOr<std::string> GetAlterActionSQL(
      const ResolvedAlterAction* alter_action);

  // Helper function to return corresponding SQL for ResolvedAlterObjectStmt
  absl::Status GetResolvedAlterObjectStmtSQL(
      const ResolvedAlterObjectStmt* node, absl::string_view object_kind);

  // Helper function to return corresponding SQL from the grantee list of
  // GRANT, REVOKE, CREATE/ALTER ROW POLICY statements.
  absl::StatusOr<std::string> GetGranteeListSQL(
      absl::string_view prefix, const std::vector<std::string>& grantee_list,
      absl::Span<const std::unique_ptr<const ResolvedExpr>> grantee_expr_list);
  // Helper function to append table_element, including column_schema and
  // table_constraints, to sql.
  absl::Status ProcessTableElementsBase(
      std::string* sql,
      absl::Span<const std::unique_ptr<const ResolvedColumnDefinition>>
          column_definition_list,
      const ResolvedPrimaryKey* resolved_primary_key,
      absl::Span<const std::unique_ptr<const ResolvedForeignKey>>
          foreign_key_list,
      absl::Span<const std::unique_ptr<const ResolvedCheckConstraint>>
          check_constraint_list);
  // Helper function to append foreign key table constraint.
  absl::StatusOr<std::string> ProcessForeignKey(
      const ResolvedForeignKey* foreign_key, bool is_if_not_exists);
  // Helper function to append primary key table constraint.
  absl::StatusOr<std::string> ProcessPrimaryKey(
      const ResolvedPrimaryKey* primary_key);
  std::string ComputedColumnAliasDebugString() const;

  // A ProjectScan is degenerate if it does not add any new information over its
  // input scan.
  virtual bool IsDegenerateProjectScan(const ResolvedProjectScan* node,
                                       const QueryExpression* query_expression);

  // If we have a recursive view, sets up internal data structures in
  // preparation for generating SQL text for a recursive view, so that the
  // columns and table references to the recursive table are correct.
  //
  // If 'node->query()' is not a ResolvedRecursiveScan, this function is a
  // no-op.
  absl::Status MaybeSetupRecursiveView(const ResolvedCreateViewBase* node);

  // A stack of QueryFragment kept to parallel the Visit call stack. Stores
  // return values of each Visit call which are then popped/used by the caller
  // of Accept.
  // NOTE: Using deque makes it possible to look at the entire stack, useful for
  // debugging purposes.
  std::deque<std::unique_ptr<QueryFragment>> query_fragments_;

  // The maximum seen alias id so far. This number is used to generate unique
  // alias ids.
  int max_seen_alias_id_ = 0;

  // Always points to `max_seen_alias_id_` of the root SQLBuilder.
  // `max_seen_alias_id_root_ptr_` is nullptr only if it's the root SQLBuilder.
  // Otherwise, it should never be nullptr.
  int* max_seen_alias_id_root_ptr_ = nullptr;

  // Stores the target table alias for DML Returning statements.
  std::string returning_table_alias_;

  // Used to generate a unique alias id.
  googlesql_base::SequenceNumber alias_id_sequence_;

  // Holds sql text for node representing an analytic function.
  class AnalyticFunctionInfo;

  // Sql for an analytic function is populated in two places.
  // AnalyticFunctionCall fills in the function-specific parts (i.e. function
  // call, window frame) and AnalyticFunctionGroup fills in the group-specific
  // parts (i.e. partition by, order by).
  //
  // <pending_analytic_function_> stores the return value (in
  // AnalyticFunctionInfo) for each Visit call to ResolvedAnalyticFunctionCall
  // which is then popped/used by the caller of Accept,
  // i.e. ResolvedAnalyticFunctionGroup.
  //
  // NOTE: We cannot use QueryFragment for this purpose. As in the sql of an
  // analytic function, the function-specific and group-specific parts don't go
  // in a left-right order where we could simply append/prepend the sql built
  // from a child node (i.e. AnalyticFunctionCall) to the sql built from a
  // parent node (i.e. AnalyticFunctionGroup).
  std::unique_ptr<AnalyticFunctionInfo> pending_analytic_function_;

  std::string sql_;

  // Options for building SQL.
  SQLBuilderOptions options_;

  // Returns the name alias for a table in VisitResolvedTableScan and appends to
  // the <from> string if necessary.
  virtual std::string GetTableAliasForVisitResolvedTableScan(
      const ResolvedTableScan& node, std::string* from);

 private:
  CopyableState state_;

  bool IsPipeSyntaxTargetMode() {
    return options_.target_syntax_mode == TargetSyntaxMode::kPipe;
  }

  bool IsStandardSyntaxTargetMode() {
    return options_.target_syntax_mode == TargetSyntaxMode::kStandard;
  }

  // Helper function to perform operation on value table's column for
  // ResolvedTableScan.
  virtual absl::Status AddValueTableAliasForVisitResolvedTableScan(
      absl::string_view table_alias, const ResolvedColumn& column,
      SQLAliasPairList* select_list);

  // Returns a GoogleSQL identifier literal for a table's name.
  // The output will be quoted (with backticks) and escaped if necessary.
  virtual std::string TableToIdentifierLiteral(const Table* table);

  // Converts a table name string to a GoogleSQL identifier literal.
  // The output will be quoted (with backticks) and escaped if necessary.
  virtual std::string TableNameToIdentifierLiteral(
      absl::string_view table_name);

  // Returns a new unique alias name. The default implementation generates the
  // name as "a_<id>".
  virtual std::string GenerateUniqueAliasName();

  // Renames the columns of each set operation item (represented by each entry
  // in `set_op_scan_list`) to the aliases in `final_column_list` when
  // - `node->column_propagation_mode` is FULL,
  // - and `node->column_match_mode` is CORRESPONDING (not CORRESPONDING_BY).
  //
  // We have a separate method specifically for (CORRESPONDING, FULL) because
  // this combination requires us to analyze the internal structure of all the
  // `output_column_list` from all the input items of `node->input_item_list` to
  // know the order of which columns should be preserved.
  //
  // For other combinations in (CORRESPONDING|CORRESPONDING_BY) *
  // (INNER|FULL|LEFT|STRICT), the columns are either the ones of the first scan
  // (for (INNER|LEFT|STRICT), CORRESPONDING) or none (for CORRESPONDING BY),
  // which is handled by method RenameSetOperationItemsNonFullCorresponding.
  absl::Status RenameSetOperationItemsFullCorresponding(
      const ResolvedSetOperationScan* node,
      absl::Span<const std::pair<std::string, std::string>> final_column_list,
      std::vector<std::unique_ptr<QueryExpression>>& set_op_scan_list);

  // Renames the columns of each set operation item (represented by each entry
  // in `set_op_scan_list`) to the aliases in `final_column_list` when
  // - `node->column_propagation_mode` is not FULL, i.e. one of INNER, LEFT, and
  //   STRICT,
  // - or `node->column_match_mode` is CORRESPONDING_BY (not CORRESPONDING).
  absl::Status RenameSetOperationItemsNonFullCorresponding(
      const ResolvedSetOperationScan* node,
      absl::Span<const std::pair<std::string, std::string>> final_column_list,
      std::vector<std::unique_ptr<QueryExpression>>& set_op_scan_list);

  // Returns the original scan from the input `scan`.
  //
  // For example, some ProjectScans are added by the resolver to wrap the
  // original scan to adjust column order or add NULL columns. This function
  // identifies those scans and returns the original scan to provide better
  // unparsed SQL.
  absl::StatusOr<const ResolvedScan*> GetOriginalInputScanForCorresponding(
      const ResolvedScan* scan);

  // In Standard SQL syntax mode, wraps the QueryExpression such that its FROM
  // clause becomes usable in Pipe syntax. In Pipe SQL syntax mode, simply wraps
  // the QueryExpression. For example, for the ResolvedScan for the query
  // "select * from table", this function would wrap the passed query expression
  // to represent the following queries:
  // - in Standard SQL syntax mode: `select * from table |> as alias`,
  // - in Pipe SQL syntax mode: `from table |> select * |> as alias`,
  // both of which are useable further in Pipe syntax.
  absl::Status MaybeWrapStandardQueryAsPipeQuery(
      const ResolvedScan* node, QueryExpression* query_expression);

  // In Standard SQL syntax mode, returns a QueryExpression that represents the
  // input `pipe_sql` converted to a regular query by wrapping it in a SELECT
  // statement. `pipe_query_node` is the ResolvedScan that corresponds to the
  // `pipe_sql`.
  //
  // In Pipe SQL syntax mode, returns a QueryExpression with the FROM clause set
  // to the given `pipe_sql`.
  absl::StatusOr<std::unique_ptr<QueryExpression>>
  MaybeWrapPipeQueryAsStandardQuery(const ResolvedScan& pipe_query_node,
                                    absl::string_view pipe_sql);

  // Mode to indicate how to general SQL for a TVF.
  enum class TVFBuildMode {
    // Whether relational or pipe SQL.
    kSql,
    // When the implicit input table is the first table arg, e.g. CALL PER().
    // The implicit input table is not printed.
    kGqlImplicit,
    // When there is no implicit input table, e.g. CALL tvf(...) without PER.
    // There may be an implicit graph input.
    // This proceeds similar to SQL, but doesn't attach an alias to the scan.
    kGqlExplicit,
  };

  // Contains the shared logic for building SQL for a TVFScan.
  // GQL CALL PER() tvf() prints the same way as pipe CALL, where the first
  // table arg isn't printed.
  // Returns the generated SQL for this TVF call.
  absl::StatusOr<std::string> ProcessResolvedTVFScan(
      const ResolvedTVFScan* node, TVFBuildMode build_mode);

  // While resolving a subquery we should start with a fresh scope, i.e. it
  // should not see any columns (except which are correlated) outside the query.
  // To ensure that, we clear the pending_columns_ after maintaining a copy of
  // it locally. We then copy it back once we have processed the subquery.
  // To give subquery access to correlated columns, scan parameter list and
  // copy current pending_columns_ and correlated_pending_columns_ from outer
  // subquery whose column ids match the parameter column.
  //
  // NOTE: Correlated columns cannot be simply retained in pending_columns_
  // because pending_columns_ get cleared in every ProjectScan and subquery can
  // have multiple of those. Correlated columns must be accessible to the whole
  // subquery instead.
  class PendingColumnsAutoRestorer {
   public:
    PendingColumnsAutoRestorer(
        SQLBuilder& sql_builder,
        const std::vector<std::unique_ptr<const ResolvedColumnRef>>&
            columns_to_expose);

    // Destructor restoring the previous state.
    ~PendingColumnsAutoRestorer();

   private:
    SQLBuilder& sql_builder_;
    std::map<int, std::string> previous_pending_columns_;
    std::map<int, std::string> previous_correlated_columns_;
  };

  // When building function call which defines a side effects scope, we may need
  // to inline some column refs to input scans. This stack keeps track of the
  // current error handling context. Note that the same node may now be visited
  // twice, e.g. an AggregateScan's aggregations would be first be visited from
  // the context of an outer IFERROR() call to collect all its inputs, before
  // being visited again free of any error handling context for the other
  // expressions.
  absl::flat_hash_set<const ResolvedScan*> scans_to_collapse_;
};

// This resolved AST visitor is used to restore the SQL for a GQL RETURN
// statement in a way that collapses the nested scans into a one liner. It
// memorizes important states during traversal of the resolved AST and only
// output the GQL text after it is done with the traversal.
class GqlReturnOpSQLBuilder : public SQLBuilder {
 public:
  explicit GqlReturnOpSQLBuilder(
      const ResolvedColumnList& output_column_list,
      std::optional<absl::btree_map<ResolvedColumn, std::string>>
          output_column_to_alias,
      int* max_seen_alias_id, const SQLBuilderOptions& options,
      const CopyableState& state);

  std::vector<std::string> GetOutputColumnAliases();

  std::string GetReturnColumnExpr(const ResolvedColumn& column);

  std::string gql();

  absl::Status VisitResolvedGraphRefScan(
      const ResolvedGraphRefScan* node) override;

  bool IsDistinctAggregateScan(const ResolvedAggregateScan* node);

  absl::Status VisitResolvedAggregateScan(
      const ResolvedAggregateScan* node) override;

  absl::Status VisitResolvedAnalyticScan(
      const ResolvedAnalyticScan* node) override;

  absl::Status VisitResolvedProjectScan(
      const ResolvedProjectScan* node) override;

  absl::Status VisitResolvedOrderByScan(
      const ResolvedOrderByScan* node) override;

  absl::Status VisitResolvedLimitOffsetScan(
      const ResolvedLimitOffsetScan* node) override;

  absl::Status VisitResolvedSubqueryExpr(
      const ResolvedSubqueryExpr* node) override;

  absl::Status ProcessInputScan(const ResolvedScan* node);

 private:
  // Final output column list of the GQL RETURN clause.
  ResolvedColumnList output_column_list_;
  absl::btree_map<ResolvedColumn, std::string> output_column_to_alias_;

  // Stores the SQL expression of computed columns computed by intermediate
  // scans. It will only be used to output the SQL for the final column list.
  // We only output their SQL after we are done with traversing the entire tree
  // that represents the RETURN clause.
  absl::flat_hash_map<int, std::string> column_id_to_sql_;

  // Final column list of GROUP BY clause.
  std::vector<ResolvedColumn> group_by_list_;

  // Stores the SQL expression of the computed columns used in the final column
  // list of ORDER BY clause.
  absl::btree_map<ResolvedColumn, std::string> order_by_column_to_sql_;

  // True if DISTINCT is present.
  bool is_distinct_ = false;

  // Non-empty if OFFSET is present.
  std::string offset_sql_;

  // Non-empty if LIMIT is present.
  std::string limit_sql_;

  // Only set to true if the current visit is under a GQL subquery context.
  bool is_subquery_context_ = false;
};

// Returns true if the array constructor should print the explicit type, which
// is always the case except when MEASURE or graph types are present because
// the type is not possible to express in SQL.
// In those cases, the array constructor needs to be implicit, but we need to
// make sure there's at least one element to force the correct type.
// Otherwise, we end up with ARRAY[] which is untyped and gets resolved as
// ARRAY<INT64>[], instead of the original type.
absl::StatusOr<bool> ShouldArrayCtorPrintExplicitType(
    const ResolvedFunctionCall* node);

}  // namespace googlesql

#endif  // GOOGLESQL_RESOLVED_AST_SQL_BUILDER_H_
