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

#ifndef GOOGLESQL_RESOLVED_AST_VALIDATOR_H_
#define GOOGLESQL_RESOLVED_AST_VALIDATOR_H_

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/templated_sql_function.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/with_modifier_mode.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/base/case.h"
#include "gtest/gtest_prod.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/base/status.h"
#include "googlesql/base/status_builder.h"
namespace googlesql {

// Options to disable certain validations. Options are used to retroactively
// add validations of invariants even if some client code still needs to be
// cleaned up. A non-default ValidatorOptions in client code signals a cleanup
// is needed to resolve broken invariants.
struct ValidatorOptions {
  // When set to false disables checking that all parameter columns of a
  // ResolvedSubquery are referenced somewhere in that subquery.
  bool validate_no_unreferenced_subquery_params = true;

  // This specifies the set of allowed hints and options, their expected
  // types, and whether to give errors on unrecognized names.
  // See the class definition for details. Currently only anonymization options
  // are checked.
  // TODO: Add validation for non anonymization options and hints.
  AllowedHintsAndOptions allowed_hints_and_options;
};

// Used to validate generated Resolved AST structures.
//  * verifies that any column reference  within the resolved tree should be
//    either from the column_list of one of the child nodes of the parent scan
//    or columns added in this node.
class Validator {
 public:
  Validator();
  explicit Validator(const LanguageOptions& language_options,
                     ValidatorOptions validator_options = {});
  Validator(const Validator&) = delete;
  Validator& operator=(const Validator&) = delete;
  ~Validator();

  absl::Status ValidateResolvedStatement(const ResolvedStatement* statement);

  absl::Status ValidateStandaloneResolvedExpr(const ResolvedExpr* expr);

 private:
  FRIEND_TEST(ValidatorTest, InvalidDynamicGraphGetElementProperty);
  FRIEND_TEST(ValidatorTest, CaseInsensitiveDynamicGraphGetElementProperty);

  // Which ArgumentKinds are allowed in the current expression.
  // Set using scoped VarSetters for field `allowed_argument_kinds_`.
  typedef absl::flat_hash_set<ResolvedArgumentDefEnums::ArgumentKind>
      ArgumentKindSet;

  // Validate a statement.
  // `in_multi_stmt` indicates this is a sub-statement inside a
  // ResolvedMultiStmt.
  absl::Status ValidateResolvedStatement(const ResolvedStatement* statement,
                                         bool in_multi_stmt);

  // Called at the end of each entry point, ValidateResolvedStatement() and
  // ValidateStandaloneExpr().
  // `in_multi_stmt` indicates this is being called for a sub-statement inside a
  // ResolvedMultiStmt, where some partial state should remain.
  absl::Status ValidateFinalState(bool in_multi_stmt = false);

  // Statements.
  absl::Status ValidateResolvedStatementInternal(
      const ResolvedStatement* statement);
  absl::Status ValidateResolvedQueryStmt(const ResolvedQueryStmt* query);
  absl::Status ValidateResolvedGeneralizedQueryStmt(
      const ResolvedGeneralizedQueryStmt* query);
  absl::Status ValidateResolvedGeneralizedQuerySubpipeline(
      const ResolvedGeneralizedQuerySubpipeline* subpipeline,
      const ResolvedScan* input_scan,
      const std::set<ResolvedColumn>& visible_parameters);
  absl::Status ValidateResolvedMultiStmt(const ResolvedMultiStmt* multi);
  absl::Status ValidateCreateWithEntryStmt(
      const ResolvedCreateWithEntryStmt* stmt);
  absl::Status ValidateResolvedCreateDatabaseStmt(
      const ResolvedCreateDatabaseStmt* stmt);
  absl::Status ValidateResolvedIndexStmt(const ResolvedCreateIndexStmt* stmt);
  absl::Status ValidateResolvedCreateModelStmt(
      const ResolvedCreateModelStmt* stmt);
  absl::Status ValidateResolvedCreateSnapshotTableStmt(
      const ResolvedCreateSnapshotTableStmt* stmt);
  absl::Status ValidateResolvedCreateTableStmt(
      const ResolvedCreateTableStmt* stmt);
  absl::Status ValidateResolvedCloneDataSource(const ResolvedScan* source,
                                               const Table* target = nullptr);
  absl::Status ValidateSingleCloneDataSource(const ResolvedScan* source,
                                             const Table* target);
  absl::Status ValidateResolvedGeneratedColumnInfo(
      const ResolvedColumnDefinition* column_definition,
      const std::set<ResolvedColumn>& visible_columns);

  // When <skip_check_type_match> is true, skip checking that the identity
  // column attribute types match the column type. This is used when validating
  // ALTER COLUMN IF EXIST column SET GENERATED.
  absl::Status ValidateResolvedIdentityColumnInfo(
      const Type* column_type,
      const ResolvedIdentityColumnInfo* identity_column_info,
      bool skip_check_type_match = false);
  absl::Status ValidateResolvedCreateTableAsSelectStmt(
      const ResolvedCreateTableAsSelectStmt* stmt,
      const std::set<ResolvedColumn>& pipe_visible_parameters = {});
  absl::Status ValidateResolvedCreateViewBase(
      const ResolvedCreateViewBase* stmt);
  absl::Status ValidateResolvedCreateViewStmt(
      const ResolvedCreateViewStmt* stmt);
  absl::Status ValidateResolvedCreateMaterializedViewStmt(
      const ResolvedCreateMaterializedViewStmt* stmt);
  absl::Status ValidateResolvedCreateApproxViewStmt(
      const ResolvedCreateApproxViewStmt* stmt);
  absl::Status ValidateResolvedCreateExternalTableStmt(
      const ResolvedCreateExternalTableStmt* stmt);
  absl::Status ValidateResolvedCreatePrivilegeRestrictionStmt(
      const ResolvedCreatePrivilegeRestrictionStmt* stmt);
  absl::Status ValidateResolvedCreateRowAccessPolicyStmt(
      const ResolvedCreateRowAccessPolicyStmt* stmt);
  absl::Status ValidateResolvedCreateConnectionStmt(
      const ResolvedCreateConnectionStmt* stmt);
  absl::Status ValidateResolvedCreateConstantStmt(
      const ResolvedCreateConstantStmt* stmt);
  absl::Status ValidateResolvedCreateFunctionStmt(
      const ResolvedCreateFunctionStmt* stmt);
  absl::Status ValidateResolvedCreateTableFunctionStmt(
      const ResolvedCreateTableFunctionStmt* stmt);
  absl::Status ValidateResolvedCreateProcedureStmt(
      const ResolvedCreateProcedureStmt* stmt);
  absl::Status ValidateResolvedCreateEntityStmt(
      const ResolvedCreateEntityStmt* stmt);
  absl::Status ValidateResolvedAlterEntityStmt(
      const ResolvedAlterEntityStmt* stmt);
  absl::Status ValidateResolvedCloneDataStmt(const ResolvedCloneDataStmt* stmt);
  absl::Status ValidateResolvedExportDataStmt(
      const ResolvedExportDataStmt* stmt,
      const std::set<ResolvedColumn>& pipe_visible_parameters = {});
  absl::Status ValidateResolvedExportModelStmt(
      const ResolvedExportModelStmt* stmt);
  absl::Status ValidateResolvedExportMetadataStmt(
      const ResolvedExportMetadataStmt* stmt);
  absl::Status ValidateResolvedCallStmt(const ResolvedCallStmt* stmt);
  absl::Status ValidateResolvedDefineTableStmt(
      const ResolvedDefineTableStmt* stmt);
  absl::Status ValidateResolvedDescribeStmt(const ResolvedDescribeStmt* stmt);
  absl::Status ValidateResolvedShowStmt(const ResolvedShowStmt* stmt);
  absl::Status ValidateResolvedBeginStmt(const ResolvedBeginStmt* stmt);
  absl::Status ValidateResolvedSetTransactionStmt(
      const ResolvedSetTransactionStmt* stmt);
  absl::Status ValidateResolvedCommitStmt(const ResolvedCommitStmt* stmt);
  absl::Status ValidateResolvedRollbackStmt(const ResolvedRollbackStmt* stmt);
  absl::Status ValidateResolvedStartBatchStmt(
      const ResolvedStartBatchStmt* stmt);
  absl::Status ValidateResolvedRunBatchStmt(const ResolvedRunBatchStmt* stmt);
  absl::Status ValidateResolvedAbortBatchStmt(
      const ResolvedAbortBatchStmt* stmt);
  absl::Status ValidateResolvedUndropStmt(const ResolvedUndropStmt* stmt);
  absl::Status ValidateResolvedDropStmt(const ResolvedDropStmt* stmt);
  absl::Status ValidateResolvedDropMaterializedViewStmt(
      const ResolvedDropMaterializedViewStmt* stmt);
  absl::Status ValidateResolvedDropFunctionStmt(
      const ResolvedDropFunctionStmt* stmt);
  absl::Status ValidateResolvedDropTableFunctionStmt(
      const ResolvedDropTableFunctionStmt* stmt);
  absl::Status ValidateResolvedDropPrivilegeRestrictionStmt(
      const ResolvedDropPrivilegeRestrictionStmt* stmt);
  absl::Status ValidateResolvedDropRowAccessPolicyStmt(
      const ResolvedDropRowAccessPolicyStmt* stmt);
  absl::Status ValidateResolvedDropSnapshotTableStmt(
      const ResolvedDropSnapshotTableStmt* stmt);
  absl::Status ValidateResolvedDropIndexStmt(const ResolvedDropIndexStmt* stmt);
  absl::Status ValidateResolvedGrantStmt(const ResolvedGrantStmt* stmt);
  absl::Status ValidateResolvedRevokeStmt(const ResolvedRevokeStmt* stmt);
  absl::Status ValidateResolvedAlterPrivilegeRestrictionStmt(
      const ResolvedAlterPrivilegeRestrictionStmt* stmt);
  absl::Status ValidateResolvedAlterRowAccessPolicyStmt(
      const ResolvedAlterRowAccessPolicyStmt* stmt);
  absl::Status ValidateResolvedAlterAllRowAccessPoliciesStmt(
      const ResolvedAlterAllRowAccessPoliciesStmt* stmt);
  absl::Status ValidateResolvedAlterTableSetOptionsStmt(
      const ResolvedAlterTableSetOptionsStmt* stmt);
  absl::Status ValidateResolvedAlterIndexStmt(
      const ResolvedAlterIndexStmt* stmt);
  absl::Status ValidateResolvedRenameStmt(const ResolvedRenameStmt* stmt);
  absl::Status ValidateResolvedImportStmt(const ResolvedImportStmt* stmt);
  absl::Status ValidateResolvedModuleStmt(const ResolvedModuleStmt* stmt);
  absl::Status ValidateResolvedAnalyzeStmt(const ResolvedAnalyzeStmt* stmt);
  absl::Status ValidateResolvedAssertStmt(const ResolvedAssertStmt* stmt);
  absl::Status ValidateResolvedAssignmentStmt(
      const ResolvedAssignmentStmt* stmt);

  // DML Statements, which can also be used as nested operations inside UPDATEs.
  // When used nested, they take a non-NULL <array_element_column> and
  // <outer_visible_columns>.
  absl::Status ValidateResolvedInsertStmt(
      const ResolvedInsertStmt* stmt,
      const std::set<ResolvedColumn>* outer_visible_columns = nullptr,
      const ResolvedColumn* array_element_column = nullptr,
      const std::set<ResolvedColumn>& pipe_visible_parameters = {});
  absl::Status ValidateResolvedDeleteStmt(
      const ResolvedDeleteStmt* stmt,
      const std::set<ResolvedColumn>* outer_visible_columns = nullptr,
      const ResolvedColumn* array_element_column = nullptr);
  absl::Status ValidateResolvedUpdateStmt(
      const ResolvedUpdateStmt* stmt,
      const std::set<ResolvedColumn>* outer_visible_columns = nullptr,
      const ResolvedColumn* array_element_column = nullptr);

  // Can occur as a child of a ResolvedUpdateStmt or a
  // ResolvedUpdateItemElement. In the latter case, <element_column> is
  // non-NULL and is in <target_visible_columns> but not
  // <offset_and_where_visible_columns>.
  absl::Status ValidateResolvedUpdateItem(
      const ResolvedUpdateItem* item, bool allow_nested_statements,
      const ResolvedColumn* element_column,
      const std::set<ResolvedColumn>& target_visible_columns,
      const std::set<ResolvedColumn>& offset_and_where_visible_columns);

  // <element_column> is not in <target_visible_columns> or
  // <offset_and_where_visible_columns>.
  // <subscripted_target_type> is the type of the target being subscripted, i.e.
  // the target type of the parent ResolvedUpdateItem.
  absl::Status ValidateResolvedUpdateItemElement(
      const ResolvedUpdateItemElement* item,
      const ResolvedColumn& element_column,
      const std::set<ResolvedColumn>& target_visible_columns,
      const std::set<ResolvedColumn>& offset_and_where_visible_columns,
      const Type* subscripted_target_type);

  // Merge statement is not supported in nested-DML.
  absl::Status ValidateResolvedMergeStmt(const ResolvedMergeStmt* stmt);
  // The source_visible_columns and target_visible_columns are visible columns
  // from source and target tables.
  // The all_visible_columns is union of source and target columns. Passed as a
  // parameter to avoid re-computing every time.
  absl::Status ValidateResolvedMergeWhen(
      const ResolvedMergeWhen* merge_when,
      const std::set<ResolvedColumn>& all_visible_columns,
      const std::set<ResolvedColumn>& source_visible_columns,
      const std::set<ResolvedColumn>& target_visible_columns);

  absl::Status ValidateResolvedTruncateStmt(const ResolvedTruncateStmt* stmt);

  // Templated common code for all DML statements.
  template <class STMT>
  absl::Status ValidateResolvedDMLStmt(
      const STMT* stmt, const ResolvedColumn* array_element_column,
      std::set<ResolvedColumn>* visible_columns);

  // Validation calls for various subtypes of ResolvedScan operations.
  // If we are validating a potentially-recursive query, <recursive_query_name>
  // specifies the query name. Otherwise, <recursive_query_name> is empty.
  absl::Status ValidateResolvedScan(
      const ResolvedScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);
  absl::Status ValidateResolvedAggregateScanBase(
      const ResolvedAggregateScanBase* scan,
      const std::set<ResolvedColumn>& visible_parameters,
      std::set<ResolvedColumn>* input_scan_visible_columns);
  absl::Status ValidateResolvedAggregateScan(
      const ResolvedAggregateScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);
  absl::Status ValidateResolvedAnonymizedAggregateScan(
      const ResolvedAnonymizedAggregateScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);
  absl::Status ValidateResolvedDifferentialPrivacyAggregateScan(
      const ResolvedDifferentialPrivacyAggregateScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);
  absl::Status ValidateResolvedAggregationThresholdAggregateScan(
      const ResolvedAggregationThresholdAggregateScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);
  absl::Status ValidateResolvedTableScan(
      const ResolvedTableScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);

  // Performs the following validations for measure columns:
  // 1. FEATURE_ENABLE_MEASURES is enabled.
  // 2. The table supports NumColumns and GetColumn(i).
  // 3. If a measure column does not have column-level row identity columns,
  //    the table must have table-level row identity columns.
  absl::Status ValidateMeasureColumns(const ResolvedScan* scan,
                                      const Table* table);
  absl::Status ValidateResolvedJoinScan(
      const ResolvedJoinScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);
  absl::Status ValidateResolvedArrayScan(
      const ResolvedArrayScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);
  absl::Status ValidateTableNameArrayPathArrayScan(
      const ResolvedArrayScan* node);
  absl::Status ValidateResolvedFilterScan(
      const ResolvedFilterScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);
  absl::Status ValidateResolvedSetOperationScan(
      const ResolvedSetOperationScan* set_op_scan,
      const std::set<ResolvedColumn>& visible_parameters);
  absl::Status ValidateResolvedSetOperationItem(
      const ResolvedSetOperationItem* input_item,
      const ResolvedColumnList& output_column_list,
      const std::set<ResolvedColumn>& visible_parameters);
  absl::Status ValidateResolvedProjectScan(
      const ResolvedProjectScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);
  absl::Status ValidateTableValuedFunction(const TableValuedFunction* tvf);
  absl::Status ValidateResolvedTVFScan(
      const ResolvedTVFScan* resolved_tvf_scan,
      const std::set<ResolvedColumn>& visible_parameters);
  absl::Status ValidateResolvedExecuteAsRoleScan(
      const ResolvedExecuteAsRoleScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);
  absl::Status ValidateResolvedRelationArgumentScan(
      const ResolvedRelationArgumentScan* arg_ref,
      const std::set<ResolvedColumn>& visible_parameters);
  absl::Status ValidateResolvedOrderByScan(
      const ResolvedOrderByScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);
  absl::Status ValidateResolvedLimitOffsetScan(
      const ResolvedLimitOffsetScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);
  absl::Status ValidateResolvedAnalyticScan(
      const ResolvedAnalyticScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);
  absl::Status ValidateResolvedSampleScan(
      const ResolvedSampleScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);
  absl::Status ValidateResolvedUnsetArgumentScan(
      const ResolvedUnsetArgumentScan* scan, bool allow_unset_argument_scan);

  // For a scan with is_ordered=true, validate that this scan can legally
  // produce ordered output.
  absl::Status ValidateResolvedScanOrdering(const ResolvedScan* scan);

  absl::Status ValidateResolvedWithScan(
      const ResolvedWithScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateResolvedWithRefScan(const ResolvedWithRefScan* scan);

  absl::Status ValidateGroupRowsScan(const ResolvedGroupRowsScan* scan);

  absl::Status ValidateResolvedReturningClause(
      const ResolvedReturningClause* returning,
      std::set<ResolvedColumn>& visible_columns);

  absl::Status ValidateResolvedOnConflictClause(
      const ResolvedOnConflictClause* on_conflict_clause,
      const std::set<ResolvedColumn>& visible_columns);

  absl::Status ValidateOrderByAndLimitClausesOfAggregateFunctionCall(
      const std::set<ResolvedColumn>& input_scan_visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedAggregateFunctionCall* aggregate_function_call);

  absl::Status ValidateResolvedAggregateComputedColumn(
      const ResolvedComputedColumnImpl* computed_column,
      const std::set<ResolvedColumn>& input_scan_visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const std::set<ResolvedColumn>& available_side_effect_columns);

  // Verifies that all the internal references in <expr> are present in
  // the <visible_columns> scope.
  absl::Status ValidateResolvedExpr(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedExpr* expr);

  absl::Status ValidateResolvedAggregateFunctionCall(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedAggregateFunctionCall* aggregate_function);

  absl::Status ValidateResolvedAnalyticFunctionCall(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedAnalyticFunctionCall* call);

  absl::Status ValidateResolvedMakeStruct(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedMakeStruct* expr);

  absl::Status ValidateResolvedGetProtoFieldExpr(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedGetProtoField* get_proto_field);

  absl::Status ValidateResolvedGetJsonFieldExpr(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedGetJsonField* get_json_field);

  absl::Status ValidateResolvedGetRowFieldExpr(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedGetRowField* get_row_field);

  absl::Status ValidateResolvedFlatten(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedFlatten* flatten);
  absl::Status ValidateResolvedFlattenedArg(
      const ResolvedFlattenedArg* flattened_arg);

  absl::Status ValidateResolvedReplaceField(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedReplaceField* replace_field);

  absl::Status ValidateResolvedGetProtoOneof(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedGetProtoOneof* get_proto_oneof);

  absl::Status ValidateResolvedInlineLambda(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedInlineLambda* resolved_lambda);

  absl::Status ValidateResolvedSubqueryExpr(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedSubqueryExpr* resolved_subquery_expr);

  absl::Status ValidateResolvedWithExpr(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedWithExpr* resolved_with_expr);

  // Verifies that all the internal references in <expr_list> are present
  // in the <visible_columns> scope.
  absl::Status ValidateResolvedExprList(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      absl::Span<const std::unique_ptr<const ResolvedExpr>> expr_list);

  absl::Status ValidateResolvedFunctionArgumentList(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      absl::Span<const std::unique_ptr<const ResolvedFunctionArgument>>
          expr_list);

  absl::Status ValidateResolvedComputedColumn(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedComputedColumnBase* computed_column);

  absl::Status ValidateGroupingFunctionCallList(
      const std::set<ResolvedColumn>& visible_columns,
      absl::Span<const std::unique_ptr<const ResolvedGroupingCall>>
          grouping_call_list,
      const std::set<ResolvedColumn>& group_by_columns);

  absl::Status ValidateResolvedComputedColumnList(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      absl::Span<const std::unique_ptr<const ResolvedComputedColumn>>
          computed_column_list);
  absl::Status ValidateResolvedComputedColumnList(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      absl::Span<const std::unique_ptr<const ResolvedComputedColumnBase>>
          computed_column_list);

  absl::Status ValidateResolvedOutputColumn(
      const std::set<ResolvedColumn>& visible_columns,
      const ResolvedOutputColumn* output_column);

  absl::Status ValidateResolvedOutputColumnList(
      absl::Span<const ResolvedColumn> visible_columns,
      absl::Span<const std::unique_ptr<const ResolvedOutputColumn>>
          output_column_list,
      bool is_value_table);

  absl::Status ValidateResolvedOutputSchema(
      absl::Span<const ResolvedColumn> visible_columns,
      const ResolvedOutputSchema* output_schema);

  absl::Status ValidateResolvedCreateSchemaStmt(
      const ResolvedCreateSchemaStmt* stmt);

  absl::Status ValidateResolvedCreateExternalSchemaStmt(
      const ResolvedCreateExternalSchemaStmt* stmt);

  absl::Status ValidateResolvedCreateTableStmtBase(
      const ResolvedCreateTableStmtBase* stmt,
      std::set<ResolvedColumn>* visible_columns);

  absl::Status ValidateResolvedCast(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedCast* resolved_cast);

  absl::Status ValidateResolvedConstant(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedConstant* resolved_constant);

  absl::Status ValidateResolvedFilterField(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedFilterField* filter_field);

  absl::Status ValidateTemplatedSqlFunctionCall(
      const TemplatedSQLFunctionCall& templated_info,
      const FunctionSignature& signature,
      const ArgumentKindSet& allowed_argument_kinds);

  absl::Status ValidateResolvedFunctionCallBase(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedFunctionCallBase* resolved_function_call);

  absl::Status ValidateArgumentAliases(
      const FunctionSignature& signature,
      absl::Span<const std::unique_ptr<const ResolvedFunctionArgument>>
          arguments);

  absl::Status ValidateOptionsList(
      absl::Span<const std::unique_ptr<const ResolvedOption>> list);

  template <class MapType>
  absl::Status ValidateOptionsList(
      absl::Span<const std::unique_ptr<const ResolvedOption>> list,
      const MapType& allowed_options,
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      absl::string_view option_type);

  absl::Status ValidateHintList(
      absl::Span<const std::unique_ptr<const ResolvedOption>> list);

  absl::Status ValidateResolvedTableAndColumnInfo(
      const ResolvedTableAndColumnInfo* table_and_column_info);

  absl::Status ValidateResolvedTableAndColumnInfoList(
      absl::Span<const std::unique_ptr<const ResolvedTableAndColumnInfo>>
          table_and_column_info_list);

  absl::Status ValidateCollateExpr(const ResolvedExpr* resolved_collate);

  absl::Status ValidateColumnAnnotations(
      const ResolvedColumnAnnotations* annotations);

  absl::Status ValidateUpdatedAnnotations(
      const ResolvedColumnAnnotations* annotations);

  absl::Status ValidateColumnDefinitions(
      absl::Span<const std::unique_ptr<const ResolvedColumnDefinition>>
          column_definitions,
      std::set<ResolvedColumn>* visible_columns);

  // When <skip_check_type_match> is true, skip checking default value type
  // can be coerced to column type. This is used when validating ALTER COLUMN IF
  // EXIST column SET DEFAULT.
  absl::Status ValidateResolvedColumnDefaultValue(
      const ResolvedColumnDefaultValue* default_value, const Type* column_type,
      bool skip_check_type_match = false);

  absl::Status ValidatePercentArgument(const ResolvedExpr* expr);

  // Verifies that only one of the parameter name and position is set.
  absl::Status ValidateResolvedParameter(
      const ResolvedParameter* resolved_param);

  absl::Status ValidateResolvedArgumentRef(
      const googlesql::ResolvedArgumentRef* arg_ref);

  absl::Status ValidateResolvedExpressionColumn(
      const ResolvedExpressionColumn* expression_column);

  absl::Status ValidateResolvedFunctionArgument(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedFunctionArgument* resolved_arg,
      bool allow_unset_argument_scan = false);

  // Validates TVF relation argument schema against the required input schema
  // in function signature.
  absl::Status ValidateRelationSchemaInResolvedFunctionArgument(
      const TVFRelation& required_input_schema,
      const TVFRelation& input_relation,
      const ResolvedFunctionArgument* resolved_arg);

  absl::Status CheckColumnIsPresentInColumnSet(
      const ResolvedColumn& column,
      const std::set<ResolvedColumn>& visible_columns);

  // Verifies that the scan column list only contains column from the visible
  // set.
  absl::Status CheckColumnList(const ResolvedScan* scan,
                               const std::set<ResolvedColumn>& visible_columns);

  absl::Status AddColumnList(const ResolvedColumnList& column_list,
                             std::set<ResolvedColumn>* visible_columns);
  absl::Status AddColumnList(
      const ResolvedColumnList& column_list,
      absl::flat_hash_set<ResolvedColumn>* visible_columns);
  absl::Status AddColumnFromComputedColumn(
      const ResolvedComputedColumnBase* computed_column,
      std::set<ResolvedColumn>* visible_columns);
  absl::Status AddGroupingFunctionCallColumn(
      ResolvedColumn grouping_call_column,
      std::set<ResolvedColumn>* visible_columns);
  absl::Status AddColumnsFromComputedColumnList(
      absl::Span<const std::unique_ptr<const ResolvedComputedColumn>>
          computed_column_list,
      std::set<ResolvedColumn>* visible_columns);
  absl::Status AddColumnsFromComputedColumnList(
      absl::Span<const std::unique_ptr<const ResolvedComputedColumnBase>>
          computed_column_list,
      std::set<ResolvedColumn>* visible_columns);

  absl::Status AddColumnsFromGroupingCallList(
      absl::Span<const std::unique_ptr<const ResolvedGroupingCall>>
          grouping_call_list,
      std::set<ResolvedColumn>* visible_columns);

  absl::Status ValidateResolvedOrderByItem(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedOrderByItem* item);

  absl::Status ValidateResolvedAnalyticFunctionGroup(
      const ResolvedAnalyticFunctionGroup* group,
      const std::set<ResolvedColumn>& input_visible_columns,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateResolvedWindowPartitioning(
      const ResolvedWindowPartitioning* partition_by,
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateResolvedWindowOrdering(
      const ResolvedWindowOrdering* order_by,
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateResolvedWindowFrame(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedWindowOrdering* window_ordering,
      const ResolvedWindowFrame* window_frame);

  absl::Status ValidateResolvedWindowFrameExpr(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedWindowOrdering* window_ordering,
      const ResolvedWindowFrame::FrameUnit& frame_unit,
      const ResolvedWindowFrameExpr* window_frame_expr);

  absl::Status ValidateResolvedWindowFrameExprType(
      const ResolvedWindowFrame::FrameUnit& frame_unit,
      const ResolvedExpr* window_ordering_expr,
      const ResolvedExpr& window_frame_expr);

  absl::Status ValidateResolvedAlterObjectStmt(
      const ResolvedAlterObjectStmt* stmt);

  // `visible_columns` is used to validate the generated columns for alter table
  // statements. It contains columns from the table_scan of the target table
  // and columns added by previous alter actions.
  absl::Status ValidateResolvedAlterAction(
      const ResolvedAlterAction* action,
      std::set<ResolvedColumn>* visible_columns);

  absl::Status ValidateAlterIndexActions(const ResolvedAlterObjectStmt* stmt);

  absl::Status ValidateResolvedExecuteImmediateStmt(
      const ResolvedExecuteImmediateStmt* stmt);

  absl::Status ValidateResolvedRecursiveScan(
      const ResolvedRecursiveScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateResolvedRecursiveRefScan(
      const ResolvedRecursiveRefScan* scan);

  absl::Status ValidateResolvedRecursionDepthModifier(
      const ResolvedRecursionDepthModifier* modifier,
      const ResolvedColumnList& recursion_column_list);

  absl::Status ValidateResolvedMatchRecognizeScan(
      const ResolvedMatchRecognizeScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateMatchRecognizeVariableName(absl::string_view name);

  absl::Status ValidateResolvedMatchRecognizePatternExpr(
      const ResolvedMatchRecognizePatternExpr* pattern,
      const absl::flat_hash_set<absl::string_view>& available_variable_names);

  absl::Status ValidateResolvedMatchRecognizePatternOperation(
      const ResolvedMatchRecognizePatternOperation* operation,
      const absl::flat_hash_set<absl::string_view>& available_variable_names);

  absl::Status ValidateResolvedMatchRecognizePatternQuantification(
      const ResolvedMatchRecognizePatternQuantification* quantification,
      const absl::flat_hash_set<absl::string_view>& available_variable_names);

  absl::Status ValidateResolvedPivotScan(
      const ResolvedPivotScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateResolvedUnpivotScan(
      const ResolvedUnpivotScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateResolvedDescribeScan(
      const ResolvedDescribeScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateResolvedStaticDescribeScan(
      const ResolvedStaticDescribeScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateResolvedAssertScan(
      const ResolvedAssertScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateResolvedLogScan(
      const ResolvedLogScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateResolvedPipeIfScan(
      const ResolvedPipeIfScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateResolvedPipeForkScan(
      const ResolvedPipeForkScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateResolvedPipeTeeScan(
      const ResolvedPipeTeeScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateResolvedPipeExportDataScan(
      const ResolvedPipeExportDataScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateResolvedPipeCreateTableScan(
      const ResolvedPipeCreateTableScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateResolvedPipeInsertScan(
      const ResolvedPipeInsertScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateResolvedSubpipeline(
      const ResolvedSubpipeline* subpipeline,
      absl::Span<const ResolvedColumn> visible_columns, bool input_is_ordered,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateResolvedSubpipelineInputScan(
      const ResolvedSubpipelineInputScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateResolvedSubpipelineStmt(
      const ResolvedSubpipelineStmt* subpipeline_stmt);

  absl::Status ValidateResolvedStatementWithPipeOperatorsStmt(
      const ResolvedStatementWithPipeOperatorsStmt* stmt);

  absl::Status ValidateResolvedWithPartitionColumns(
      const ResolvedWithPartitionColumns* with_partition_columns,
      std::set<ResolvedColumn>* visible_columns);

  absl::Status ValidateResolvedForeignKey(
      const ResolvedForeignKey* foreign_key,
      std::vector<const Type*> column_types,
      absl::flat_hash_set<std::string>* constraint_names);

  absl::Status ValidateResolvedPrimaryKey(
      const std::vector<const Type*>& resolved_column_types,
      const ResolvedPrimaryKey* primary_key,
      absl::flat_hash_set<std::string>* constraint_names);

  absl::Status ValidateAddForeignKeyAction(
      const ResolvedAddConstraintAction* action,
      absl::flat_hash_set<std::string>* constraint_names);

  absl::Status ValidateResolvedAuxLoadDataPartitionFilter(
      const std::set<ResolvedColumn>& visible_columns,
      const ResolvedAuxLoadDataPartitionFilter* partition_filter);

  absl::Status ValidateResolvedAuxLoadDataStmt(
      const ResolvedAuxLoadDataStmt* stmt);
  // Start of Graph related validations.
  // Note that all names are treated as case-insensitive within graph queries.

  absl::Status ValidateResolvedCreatePropertyGraphStmt(
      const ResolvedCreatePropertyGraphStmt* stmt);

  // REQUIRES: The underlying string_views for 'node_table_scan_map',
  // 'all_label_name_set' and 'all_property_name_set' must be valid for the
  // lifetime of the call. Similar for the remaining functions below.
  absl::Status ValidateResolvedGraphElementTable(
      const ResolvedGraphElementTable* element_table,
      const absl::flat_hash_map<absl::string_view, const ResolvedScan*,
                                googlesql_base::StringViewCaseHash,
                                googlesql_base::StringViewCaseEqual>&
          node_table_scan_map,
      const absl::flat_hash_set<
          absl::string_view, googlesql_base::StringViewCaseHash,
          googlesql_base::StringViewCaseEqual>& all_label_name_set,
      const absl::flat_hash_map<
          absl::string_view, AnnotatedType, googlesql_base::StringViewCaseHash,
          googlesql_base::StringViewCaseEqual>& all_property_name_set);

  absl::Status ValidateResolvedGraphNodeTableReference(
      const ResolvedGraphNodeTableReference* node_reference,
      const std::set<ResolvedColumn>& edge_visible_columns,
      const absl::flat_hash_map<absl::string_view, const ResolvedScan*,
                                googlesql_base::StringViewCaseHash,
                                googlesql_base::StringViewCaseEqual>&
          node_table_scan_map);

  absl::Status ValidateResolvedGraphElementLabel(
      const ResolvedGraphElementLabel* label,
      const absl::flat_hash_map<
          absl::string_view, AnnotatedType, googlesql_base::StringViewCaseHash,
          googlesql_base::StringViewCaseEqual>& property_dcl_name_set);

  absl::Status ValidateResolvedGraphPropertyDefinition(
      const ResolvedGraphPropertyDefinition* property_definition,
      const std::set<ResolvedColumn>& visible_columns,
      const absl::flat_hash_map<
          absl::string_view, AnnotatedType, googlesql_base::StringViewCaseHash,
          googlesql_base::StringViewCaseEqual>& all_property_name_set);

  absl::Status ValidateResolvedGraphDynamicLabelSpecification(
      const ResolvedGraphDynamicLabelSpecification* label_specification,
      bool is_node_table, const std::set<ResolvedColumn>& visible_columns);

  absl::Status ValidateResolvedGraphDynamicPropertiesSpecification(
      const ResolvedGraphDynamicPropertiesSpecification* property_specification,
      const std::set<ResolvedColumn>& visible_columns);

  absl::Status ValidateResolvedGraphTableScan(
      const ResolvedGraphTableScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateResolvedGraphElementScan(
      const ResolvedGraphElementScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateResolvedGraphNodeScan(
      const ResolvedGraphNodeScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateResolvedGraphEdgeScan(
      const ResolvedGraphEdgeScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateResolvedGraphPathScan(
      const ResolvedGraphPathScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateResolvedGraphScan(
      const ResolvedGraphScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateResolvedGraphLinearScan(
      const ResolvedGraphLinearScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateGraphReturnOperator(const ResolvedScan* scan);

  absl::Status ValidateTopLevelGraphLinearScanStructure(
      const ResolvedGraphLinearScan* scan);

  absl::Status ValidateInnerGraphLinearScanStructure(
      const ResolvedGraphLinearScan* scan);

  absl::Status ValidateGraphSetOperationScanStructure(
      const ResolvedSetOperationScan* scan);

  absl::Status ValidateResolvedGraphRefScan(
      const ResolvedGraphRefScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateResolvedGraphCallScan(
      const ResolvedGraphCallScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateResolvedGraphGetElementProperty(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedGraphGetElementProperty* get_element_prop_expr);

  absl::Status ValidateResolvedGraphPathPatternQuantifier(
      const ResolvedGraphPathPatternQuantifier* quantifier,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateResolvedGraphPathSearchPrefix(
      const ResolvedGraphPathSearchPrefix* search_prefix);

  absl::Status ValidateResolvedGraphLabelExpr(
      const ResolvedGraphLabelExpr* expr);

  absl::Status ValidateResolvedGraphLabel(const ResolvedGraphLabel* expr);

  absl::Status ValidateResolvedGraphWildCardLabel(
      const ResolvedGraphWildCardLabel* expr);

  absl::Status ValidateResolvedGraphLabelNaryExpr(
      const ResolvedGraphLabelNaryExpr* expr);

  absl::Status ValidateResolvedGraphElementIdentifier(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedGraphElementIdentifier* argument, bool is_edge);

  absl::Status ValidateResolvedGraphElementProperty(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedGraphElementProperty* argument);

  absl::Status ValidateResolvedGraphMakeElement(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedGraphMakeElement* make_graph_element);

  absl::Status ValidateResolvedArrayAggregate(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedArrayAggregate* array_aggregate);

  absl::Status ValidateResolvedGraphIsLabeledPredicate(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedGraphIsLabeledPredicate* predicate);

  absl::Status ValidateGraphPathMode(const ResolvedGraphPathMode* path_mode);

  // End of Graph related validations.

  absl::Status ValidateResolvedBarrierScan(
      const ResolvedBarrierScan* scan,
      const std::set<ResolvedColumn>& visible_parameters);

  absl::Status ValidateResolvedUpdateConstructor(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedUpdateConstructor* update_constructor);

  // Validates that <expr> is a valid expression of bool type.
  absl::Status ValidateBoolExpr(
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const ResolvedExpr* expr);

  // Validates group threshold expression for differential privacy scans (called
  // k threshold expression in ResolvedAnonymizedAggregateScan).
  absl::Status ValidateGroupSelectionThresholdExpr(
      const ResolvedExpr* group_threshold_expr,
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      absl::Span<const std::unique_ptr<const ResolvedOption>> scan_options,
      absl::string_view expression_name);

  // Validates the group selection threshold expression for differential privacy
  // scans (called k threshold expression in ResolvedAnonymizedAggregateScan).
  // Compared to ValidateGroupSelectionThresholdExpr above, the assumption is
  // that specification of `min_privacy_units_per_group` led to a rewriting of
  // this expression.
  absl::Status ValidateGroupSelectionThresholdExprWithMinPrivacyUnitsPerGroup(
      const ResolvedExpr* group_threshold_expr,
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      const std::unique_ptr<const ResolvedOption>&
          min_privacy_units_per_group_option,
      absl::string_view expression_name);

  // Validates the group selection threshold expression for differential
  // privacy scans (called k threshold expression in
  // ResolvedAnonymizedAggregateScan). Compared to
  // ValidateGroupSelectionThresholdExpr above, this function assumes (or else
  // returns an error) that rewriting of the group selection threshold
  // expression was not needed (for example because the
  // min_privacy_units_per_group was not specified).
  absl::Status
  ValidateGroupSelectionThresholdExprWithoutMinPrivacyUnitsPerGroup(
      const ResolvedExpr* group_threshold_expr,
      const std::set<ResolvedColumn>& visible_columns,
      const std::set<ResolvedColumn>& visible_parameters,
      absl::string_view expression_name);

  // Validates GroupingSet and grouping columns are empty.
  // This is only for the nodes that don't have grouping sets implemented yet.
  absl::Status ValidateGroupingSetListAreEmpty(
      absl::Span<const std::unique_ptr<const ResolvedGroupingSetBase>>
          grouping_set_list,
      absl::Span<const std::unique_ptr<const ResolvedColumnRef>>
          rollup_column_list);

  // Validates and extracts grouping set columns from the grouping set base.
  // `grouping_set_base` should be a ResolvedGroupingSet, ResolvedGroupingCube,
  // or ResolvedGroupingRollup.
  absl::Status ValidateAndExtractNonCompositeGroupingSetColumns(
      const std::set<ResolvedColumn>& group_by_columns,
      const ResolvedGroupingSetBase* grouping_set_base,
      std::set<ResolvedColumn>* grouping_sets_all_columns);

  // Validates and extracts grouping set columns from the grouping
  // set base. `grouping_set_base` could be ResolvedGroupingSetList,
  // ResolvedGroupingSet, ResolvedGroupingCube, or ResolvedGroupingRollup.
  absl::Status ValidateAndExtractGroupingSetColumns(
      const std::set<ResolvedColumn>& group_by_columns,
      const ResolvedGroupingSetBase* grouping_set_base,
      std::set<ResolvedColumn>* grouping_sets_all_columns);

  // Validates GroupingSet and grouping columns based on grouping conditions.
  absl::Status ValidateGroupingSetList(
      absl::Span<const std::unique_ptr<const ResolvedGroupingSetBase>>
          grouping_set_list,
      absl::Span<const std::unique_ptr<const ResolvedColumnRef>>
          rollup_column_list,
      absl::Span<const std::unique_ptr<const ResolvedComputedColumn>>
          group_by_list);

  // Checks that <expr> contains only ColumnRefs, GetProtoField, GetStructField
  // and GetJsonField expressions. Sets 'ref' to point to the leaf
  // ResolvedColumnRef.
  absl::Status CheckExprIsPath(const ResolvedExpr* expr,
                               const ResolvedColumnRef** ref);

  // Validates whether <expr> is a literal or a parameter. In either case, it
  // should be of type int64. <context_msg> represents where this validation
  // happens.
  absl::Status ValidateArgumentIsInt64(const ResolvedExpr* expr,
                                       bool validate_constant_nonnegative,
                                       absl::string_view context_msg);

  absl::Status ValidateGenericArgumentsAgainstConcreteArguments(
      const ResolvedFunctionCallBase* resolved_function_call,
      const FunctionSignature& signature);

  // Validates that CheckUniqueColumnId() was never previously called with the
  // same column id as that of <column>.
  absl::Status CheckUniqueColumnId(const ResolvedColumn& column);

  // Check that `column` was previously marked as created
  // (with CheckUniqueColumnId).
  absl::Status CheckColumnIdWasCreated(const ResolvedColumn& column);

  absl::Status ValidateCompatibleSchemaForClone(const Table* source,
                                                const Table* target);

  absl::Status CheckFunctionArgumentType(
      const FunctionArgumentTypeList& argument_type_list,
      absl::string_view statement_type);

  // Validates that the <proto_field_path> is a valid path for a proto starting
  // with <base_proto_name>.
  absl::Status ValidateProtoFieldPath(
      absl::string_view base_proto_name,
      const std::vector<const google::protobuf::FieldDescriptor*>& proto_field_path);

  // Validates that the aggregate scan has no multi-level aggregates. Used for
  // Anonymized / Differential Privacy / Aggregation Threshold aggregate scans.
  absl::Status ValidateAggregateScanHasNoMultiLevelAggregates(
      const ResolvedAggregateScanBase* scan);
  // Validates that the aggregate scan has no `AGG` function calls. Used for
  // Anonymized / Differential Privacy / Aggregation Threshold aggregate scans.
  absl::Status ValidateAggregateScanHasNoAggFunctionCall(
      const ResolvedAggregateScanBase* scan);

  // Validates that the given feature is enabled for WITH modifier.
  absl::Status ValidateFeatureIsEnabledForWithModifier(LanguageFeature feature,
                                                       WithModifierMode mode);

  absl::Status ValidateResolvedCreateSequenceStmt(
      const ResolvedCreateSequenceStmt* stmt);

  // Validates that the type of a column and the corresponding resolved_column
  // are 'equivalent'.
  //
  // Note for measure types (and potentially other function types), the
  // equivalence in this context does not refer to the behavior described by the
  // googlesql::type::Equivalent method, but a notion of equivalence specific
  // to the type. See the implementation of this method for details.
  absl::Status ValidateTableColumnTypeEquality(
      const Column& column, const ResolvedColumn& resolved_column,
      absl::string_view table_name, absl::string_view column_name);

  // Replacement for ::googlesql_base::InternalErrorBuilder(), which also records the
  // context of the error for use in the tree dump.
  googlesql_base::StatusBuilder InternalErrorBuilder() {
    RecordContext();
    return ::googlesql_base::InternalErrorBuilder();
  }

  std::string RecordContext();

  // Clears internal Validator state from prior validation.
  // `in_multi_stmt` indicates this is being called for a sub-statement inside a
  // ResolvedMultiStmt, where we want only a partial reset.
  void Reset(bool in_multi_stmt = false);

  // Helper class to push a node onto the context stack in the constructor and
  // pop the same node off the context stack in the destructor.
  //
  // An instance of this class should be created at the start of each
  // ValidateXXX() function. If an error occurs, the top of the stack, at the
  // point of the failed VALIDATOR_RET_CHECK() indicates the subtree that failed
  // validation, and will be emphasized in the tree dump.
  class PushErrorContext {
   public:
    PushErrorContext(Validator* validator, const ResolvedNode* node)
        : validator_(validator), node_(node) {
      if (node != nullptr) {
        validator->context_stack_.push_back(node);
      }
    }
    ~PushErrorContext() {
      if (node_ != nullptr) {
        ABSL_DCHECK(!validator_->context_stack_.empty());
        ABSL_DCHECK_EQ(validator_->context_stack_.back(), node_);
        validator_->context_stack_.pop_back();
      }
    }

   private:
    Validator* validator_;
    const ResolvedNode* node_;
  };

  struct RecursiveScanInfo {
    explicit RecursiveScanInfo(const ResolvedRecursiveScan* scan) {
      this->scan = scan;
    }

    const ResolvedRecursiveScan* scan;
    bool saw_recursive_ref = false;
  };

  // Options that disable validation of certain invariants.
  const ValidatorOptions options_;

  ArgumentKindSet allowed_argument_kinds_;

  // This points to the current CREATE TABLE FUNCTION statement being validated,
  // or null if no such statement is currently being validated.
  const ResolvedCreateTableFunctionStmt* current_create_table_function_stmt_ =
      nullptr;

  const LanguageOptions language_options_;

  // True if we are validating a ResolvedGeneralizedQueryStmt.
  bool in_generalized_query_stmt_ = false;

  // A stack of ResolvedRecursiveScans we are currently inside the recursive
  // term of. Used to ensure that each ResolvedRecursiveRefScan matches up
  // with a ResolvedRecursiveScan.
  std::vector<RecursiveScanInfo> nested_recursive_scans_;

  // State used while validating a ResolvedSubpipeline.
  struct SubpipelineInfo {
    explicit SubpipelineInfo(absl::Span<const ResolvedColumn> input_column_list,
                             bool input_is_ordered)
        : column_list(input_column_list.begin(), input_column_list.end()),
          is_ordered(input_is_ordered) {}

    ResolvedColumnList column_list;
    bool is_ordered;
    bool saw_subpipeline_input_scan = false;
  };

  // Stack of subpipelines being validated.
  std::vector<SubpipelineInfo> subpipeline_info_stack_;

  // Pre-aggregation columns, reflecting the columns available from the related
  // FROM clause. Captured by WITH GROUP ROWS expression, to be used by
  // GROUP_ROWS() function in it.
  std::optional<std::set<ResolvedColumn>> input_columns_for_group_rows_;

  // List of column ids seen so far. Used to ensure that every unique column
  // has a distinct id.
  absl::flat_hash_set<int> column_ids_seen_;

  // List of side effect columns that are yet to be consumed by a
  // $with_side_effects() call. At the end of validation, this list must be
  // empty.
  absl::flat_hash_set<int> unconsumed_side_effect_columns_;

  // The node at the top of the stack is the innermost node being validated.
  std::vector<const ResolvedNode*> context_stack_;

  // The list of WITH definitions that are currently visible.
  // Later definitions (in inner scopes) are added on the end of the vector.
  std::vector<const ResolvedWithEntry*> visible_with_entries_;

  // If validation fails, this is set to the innermost element being validated
  // (context_stack_.back()) at the point of the error. This allows the record
  // of the node that caused the error to be retained, even as various helper
  // functions terminate. At the root level (ValidateStandaloneExpression()/
  // ValidateResolvedStatement()), this node will be annotated in the tree dump.
  //
  // If no validation error has yet occurred, this value is nullptr.
  const ResolvedNode* error_context_ = nullptr;

  // A stack set by the current GraphLinearScan(s) under validation to track
  // their current scan that can be referenced by its child scan as input scan.
  // The stack size increases whenever there is a new level of nested graph
  // linear scan.
  std::vector<const ResolvedScan*>
      current_input_scan_in_graph_linear_scan_stack_;

  // If true, disallow references to AGGREGATE ResolvedArgumentRefs.
  // This is set to true when validating arguments to a multi-level aggregate
  // function call, or the ORDER BY clause of a multi-level aggregate function
  // call.
  bool disallow_aggregate_resolved_arg_refs_ = false;

  // If true, disallow ExpressionColumns.
  // This is set to true when validating arguments to a multi-level aggregate
  // function call, or the ORDER BY clause of a multi-level aggregate function
  // call.
  bool disallow_expression_columns_ = false;

  // If true, then we are currently validating a templated TVF call. There is
  // legacy code that doesn't require templated TVFs to produce a value table
  // with an anonymous column, so we skip validation to maintain compatibility.
  bool in_templated_tvf_call_ = false;

  // MATCH_RECOGNIZE state, in order to convert back access to internal columns
  // to calls to special functions, e.g. MATCH_NUMBER().
  struct MatchRecognizeState {
    ResolvedColumn match_number_column;
    ResolvedColumn match_row_number_column;
    ResolvedColumn classifier_column;
  };
  std::optional<MatchRecognizeState> match_recognize_state_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_RESOLVED_AST_VALIDATOR_H_
