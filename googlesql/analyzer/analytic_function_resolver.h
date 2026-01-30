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

#ifndef GOOGLESQL_ANALYZER_ANALYTIC_FUNCTION_RESOLVER_H_
#define GOOGLESQL_ANALYZER_ANALYTIC_FUNCTION_RESOLVER_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "googlesql/parser/ast_node.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/public/function.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/type.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "absl/container/btree_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace googlesql {

class Coercer;
class QueryResolutionInfo;
class Resolver;
struct ExprResolutionInfo;

// AnalyticFunctionResolver is a query-scoped stateful resolver that resolves
// OVER clauses and creates an AnalyticScan during GoogleSQL analysis.
//
// We create one AnalyticFunctionResolver per SELECT query block
// and per ORDER BY clause after set operation.
//
// For any analytic function call in SELECT or ORDER BY, the expression resolver
// performs general function resolution including resolving the function name
// and arguments, and then calls AnalyticFunctionResolver::ResolveOverClause().
// The ResolveOverClause method:
//   1. Checks that this is a semantically valid analytic function call (e.g.
//      whether the function supports the window frame and whether the window
//      frame boundaries are valid).
//   2. Flattens the window inheritance path, resolves partitioning, ordering
//      and framing expressions, and identifies alias references to
//      SELECT-list columns. The resolved result for a partitioning/ordering
//      expression is stored in a WindowExprInfo, which is inserted into
//      <ast_to_resolved_info_> with the parse tree node as the key.
//   3. Identifies the grouping window of the function, and adds or updates the
//      function group of the analytic function in
//      <grouping_window_to_function_group_>. Two analytic functions are in the
//      same group if they do not have PARTITION BY nor ORDER BY, or if they
//      share the same window PARTITION BY and ORDER BY via a common named
//      window, which we call a grouping window.
//   4. Creates a ResolvedColumn to store the output, and returns a reference
//      to the column as the ResolvedExpr.
//
// Then once all expressions have been analyzed, and the Resolver builds the
// resolved tree up to and including the GROUP BY, it calls
// AnalyticFunctionResolver::CreateAnalyticScan() to add an AnalyticScan.
//
// See (broken link) for the analytic function
// specifications.
class AnalyticFunctionResolver {
 public:
  struct FlattenedWindowInfo;

  // Map from window names to related window information.
  typedef absl::btree_map<std::string,
                          std::unique_ptr<const FlattenedWindowInfo>>
      NamedWindowInfoMap;

  // Constructor. Does not take ownership of <resolver>.  Takes ownership of
  // <named_window_info_map> if non-NULL.
  explicit AnalyticFunctionResolver(
      Resolver* resolver, NamedWindowInfoMap* named_window_info_map = nullptr);
  AnalyticFunctionResolver(const AnalyticFunctionResolver&) = delete;
  AnalyticFunctionResolver& operator=(const AnalyticFunctionResolver&) = delete;

  ~AnalyticFunctionResolver();

  // Contains the inline or inherited window subclauses (PARTITION BY, ORDER BY
  // window frame) and the grouping window.
  struct FlattenedWindowInfo {
    // Initializes PARTITION BY, ORDER BY and window frame with the inline
    // clauses. Inherited clauses must be populated by calling
    // ExtractWindowInfoFromReferencedWindow().
    explicit FlattenedWindowInfo(
        const ASTWindowSpecification* ast_window_spec_in)
        : ast_window_spec(ast_window_spec_in),
          ast_partition_by(ast_window_spec->partition_by()),
          ast_order_by(ast_window_spec->order_by()),
          ast_window_frame(ast_window_spec->window_frame()) {
      if (ast_partition_by != nullptr || ast_order_by != nullptr) {
        ast_grouping_window_spec = ast_window_spec;
      }
    }

    const ASTWindowSpecification* ast_window_spec;

    // The following three clauses include inherited ones.
    const ASTPartitionBy* ast_partition_by;
    const ASTOrderBy* ast_order_by;
    const ASTWindowFrame* ast_window_frame;

    // The window where <ast_order_by>, or <ast_partition_by> if
    // <ast_order_by> is NULL, comes from. NULL if both <ast_order_by> and
    // <ast_partition_by> are NULL. It is used to uniquely identify an
    // analytic function group for a compatible set of analytic functions
    // with the same PARTITOIN BY and ORDER BY.
    const ASTWindowSpecification* ast_grouping_window_spec = nullptr;
  };

  // Analytic functions in an analytic function group have a common
  // PARTIITON BY and ORDER BY (their grouping window), which may be shared by
  // multiple groups. The AnalyticFunctionGroupInfo stores the list of
  // ResolvedAnalyticFunctionCall in a group and the AST nodes for PARTITION BY
  // and ORDER BY. It does not contain the info for resolved
  // partitioning/ordering expressions, which is stored in the value of the map
  // <ast_to_resolved_info_> to be shared by multiple groups.
  struct AnalyticFunctionGroupInfo {
   public:
    AnalyticFunctionGroupInfo(const ASTPartitionBy* ast_partition_by_in,
                              const ASTOrderBy* ast_order_by_in)
        : ast_partition_by(ast_partition_by_in),
          ast_order_by(ast_order_by_in) {}

    AnalyticFunctionGroupInfo(const AnalyticFunctionGroupInfo&) = delete;
    AnalyticFunctionGroupInfo& operator=(const AnalyticFunctionGroupInfo&) =
        delete;

    const ASTPartitionBy* const ast_partition_by;
    const ASTOrderBy* const ast_order_by;

    // ResolvedComputedColumns of analytic function calls for output.
    std::vector<std::unique_ptr<ResolvedComputedColumn>>
        resolved_computed_columns;
  };

  // Populates <named_window_info_map_> using the named windows in
  // <window_clause>.
  absl::Status SetWindowClause(const ASTWindowClause& window_clause);

  // Releases ownership of <named_window_info_map_>.
  NamedWindowInfoMap* ReleaseNamedWindowInfoMap();

  // Disable named window references. <clause_name> is only for displaying error
  // message and cannot be empty.  We disable them before resolving the ORDER
  // BY.  We do not need to re-enable named window references once they are
  // disabled because the only other clause that supports analytic functions
  // is the SELECT list, which is resolved before ORDER BY.
  void DisableNamedWindowRefs(const char* clause_name);

  // Sets the current match recognize window context, based on the PARTITION BY
  // and ORDER BY clauses of the current MATCH_RECOGNIZE clause.
  //
  // Returns the partitioning columns (since they'll be output by the
  // ResolvedMatchRecognizeScan).
  //
  // REQUIRES: The current MATCH_RECOGNIZE window context cannot be already set.
  //
  // All window functions resolved with this AnalyticFunctionResolver will be
  // based on this window specification, unless they specify otherwise.
  // Analytic functions in the DEFINE clause should be called after setting this
  // window context to leverage it.
  absl::Status SetMatchRecognizeWindowContext(
      const ASTPartitionBy* ast_partition_by, const ASTOrderBy* ast_order_by,
      ExprResolutionInfo* expr_resolution_info,
      std::vector<ResolvedColumn>& out_partitioning_columns);

  bool in_match_recognize_window_context() const {
    return in_match_recognize_window_context_;
  }

  // Adds the given ResolvedFunctionCall to the main analytic group of the
  // current MATCH_RECOGNIZE window context and returns the resolved expression.
  // This is very similar to `ResolveOverClauseAndCreateAnalyticColumn()`, but
  // without an OVER clause, it adds the new call to the main analytic group,
  // sharing the same window of the current MATCH_RECOGNIZE window context.
  //
  // REQUIRES: The current MATCH_RECOGNIZE window context must be already set.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
  AddToMatchRecognizeMainAnalyticGroup(
      const ASTFunctionCall* ast_function_call,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedFunctionCall> resolved_call,
      std::unique_ptr<const ResolvedWindowFrame> resolved_window_frame);

  // Resolves and validates the OVER clause. The analytic function call is
  // resolved to a column reference in <resolved_expr_out>. The
  // ResolvedAnalyticFunctionCall and resolved expressions for PARTITION BY
  // and ORDER BY are stored in <analytic_function_groups_>.
  // Note that the argument list in <resolved_function_call> will be released.
  absl::Status ResolveOverClauseAndCreateAnalyticColumn(
      const ASTAnalyticFunctionCall* ast_analytic_function_call,
      std::unique_ptr<const ResolvedFunctionCall> resolved_function_call,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  // Resolves PARTITION BY that is not part of an OVER() clause.
  // This is the case in MATCH_RECOGNIZE.
  absl::StatusOr<std::unique_ptr<const ResolvedWindowPartitioning>>
  ResolveStandalonePartitionBy(const ASTPartitionBy* ast_partition_by,
                               bool force_rename_columns,
                               ExprResolutionInfo* expr_resolution_info);

  // Resolves ORDER BY that is not part of an OVER() clause.
  // This is the case in MATCH_RECOGNIZE.
  absl::StatusOr<std::unique_ptr<const ResolvedWindowOrdering>>
  ResolveStandaloneOrderBy(const ASTOrderBy* ast_order_by,
                           ExprResolutionInfo* expr_resolution_info);

  // Add a wrapper scan if there are any computed columns for partitioning or
  // ordering expressions, which have been resolved and stored on this object
  // during a call to `ResolveOverClauseAndCreateAnalyticColumn()` or
  // `ResolveStandalonePartitionByAndOrderBy()`.
  // That list of computed columns is cleared as part of this call.
  absl::StatusOr<std::unique_ptr<const ResolvedScan>>
  AddMissingPartitioningAndOrderingExpressions(
      std::unique_ptr<const ResolvedScan> input_scan);

  // Creates an AnalyticScan for all analytic functions and updates <scan> with
  // the new AnalyticScan. If computed columns are required for
  // partitioning/ordering expressions, a wrapper ProjectScan is created around
  // the original input <scan>. SELECT-list expressions in
  // <select_column_state_list> will be rewritten to
  // reference columns created for partitioning or ordering expressions by
  // the wrapper ProjectScan if the partitioning or ordering expressions
  // originally reference SELECT-list columns.
  absl::Status CreateAnalyticScan(QueryResolutionInfo* query_resolution_info,
                                  std::unique_ptr<const ResolvedScan>* scan);

  // Returns true if the analytic function resolver has resolved at least one
  // analytic function.
  bool HasAnalytic() const;

  bool HasWindowColumnsToCompute() {
    return !window_columns_to_compute_.empty();
  }

  // Returns <analytic_function_groups_> as a const reference.
  const std::vector<std::unique_ptr<AnalyticFunctionGroupInfo>>&
  analytic_function_groups() const;

 private:
  // Contains all parser and resolved expressions for an analytic function call.
  // Used in ValidateAndRewriteExprsPostAggregation() to get resolved
  // expressions to rewrite for an analytic function.
  struct AnalyticFunctionInfo;

  std::vector<std::unique_ptr<AnalyticFunctionGroupInfo>>
      analytic_function_groups_;

  // Map from grouping windows to their related analytic function group info.
  // An analytic function group is uniquely identified by a grouping window.
  // This map is used to find the group that an analytic function belongs to
  // according to the grouping window of the analytic function.
  std::map<const ASTWindowSpecification*, AnalyticFunctionGroupInfo*>
      ast_window_spec_to_function_group_map_;

  // Stores info for a partitioning or ordering expression, which is called a
  // window expression. It contains the resolved expression and the resolved
  // column, and identifies whether it is an alias reference to a
  // SELECT-list column;
  struct WindowExprInfo;

  // A list of WindowExprInfo for partitioning (ordering) expressions in
  // a window PARTITION BY (ORDER BY).
  typedef std::vector<std::unique_ptr<WindowExprInfo>> WindowExprInfoList;

  // Map from ASTPartitionBy and ASTOrderBy nodes to WindowExprInfoLists that
  // contain resolved info for partitioning/ordering expressions.
  //
  // A PARTITION BY/ORDER BY clause can be used in multiple analytic
  // function groups. We store the resolved results for all PARTITION BY/ORDER
  // BY clauses in <ast_to_resolved_info_> so that it is easy to look up the
  // resolved info of a PARTITION BY/ORDER BY clause for a function group using
  // its ASTNode (contained in the AnalyticFunctionGroupInfo).
  std::map<const ASTNode*, std::unique_ptr<WindowExprInfoList>>
      ast_to_resolved_info_map_;

  // A resolved column is created for each analytic function call.
  // This maps each ResolvedColumn to the info of the corresponding analytic
  // function. It is used in ValidateAndRewriteExprsPostAggregation()to find the
  // resolved expressions of an analytic function call from a ResolvedColumn
  // that may need rewriting.
  std::map<ResolvedColumn, AnalyticFunctionInfo>
      column_to_analytic_function_map_;

  // ResolvedComputedColumns for those partitioning/ordering expressions that
  // need to be precomputed for the analytic scan. It is populated in
  // AddColumnForWindowExpression() and then used to create a wrapper project
  // before creating an AnalyticScan.
  std::vector<std::unique_ptr<const ResolvedComputedColumn>>
      window_columns_to_compute_;

  // If not NULL and a named window reference occurs, an error will be generated
  // saying named window references are not allowed in this location, e.g.
  // "ORDER BY".
  const char* named_window_not_allowed_here_name_ = nullptr;

  // Map from the window name to the corresponding named window info.
  std::unique_ptr<NamedWindowInfoMap> named_window_info_map_;

  Resolver* resolver_;  // Not owned.

  // Used only for constructing column aliases.
  int num_analytic_functions_ = 0;
  int num_partitioning_exprs_ = 0;
  int num_ordering_items_ = 0;

  // Populates <flattened_window_info> with inherited window clauses and
  // update s the grouping window from the referenced window.
  absl::Status ExtractWindowInfoFromReferencedWindow(
      FlattenedWindowInfo* flattened_window_info) const;

  // Checks whether <window_spec> has a conflicting clause with the referenced
  // window. All window subclauses (PARTITION BY, ORDER BY, and window frame) in
  // window A must logically follow all subclauses in a referenced window B, or
  // a conflict occurs.  For example, if referenced window B contains an ORDER
  // BY clause, then any referencing window A can only have a window frame -- it
  // cannot have PARTITION BY or ORDER BY.
  absl::Status CheckForConflictsWithReferencedWindow(
      const ASTWindowSpecification* window_spec,
      const FlattenedWindowInfo* flattened_referenced_window_info) const;

  // Check whether ORDER BY and window frame must or cannot be present
  // based on the function requirements and whether there is DISTINCT.
  absl::Status CheckWindowSupport(
      const ResolvedFunctionCall* resolved_function_call,
      const ASTAnalyticFunctionCall* ast_function_call,
      const ASTOrderBy* ast_order_by,
      const ASTWindowFrame* ast_window_frame) const;

  // Performs initial resolution of window PARTITION BY. It calls
  // ResolveWindowOperationExpression() for each partitioning expression, stores
  // the resolved result in a WindowExprInfoList, and adds it to the map
  // <ast_to_resolved_info_> with <ast_partition_by> as the key.  The returned
  // <partition_by_info_out> is not owned by the caller, but by
  // <ast_to_resolved_info_map_>.
  absl::Status ResolveWindowPartitionByPreAggregation(
      const ASTPartitionBy* ast_partition_by,
      ExprResolutionInfo* expr_resolution_info, bool allow_ordinals,
      const char* clause_name, WindowExprInfoList** partition_by_info_out);

  // Performs initial resolution of window ORDER BY. It calls
  // ResolveWindowOperationExpression() for each ordering expression, stores the
  // resolved result in a WindowExprInfoList, and adds it to the map
  // <ast_to_resolved_info_> with <ast_order_by> as the key.  The returned
  // <order_by_info_out> is not owned by the caller, but by
  // <ast_to_resolved_info_map_>. If <is_in_range_window> is true and the option
  // DISALLOW_GROUP_BY_FLOAT is enabled, returns an error when there exists a
  // floating point order key.
  absl::Status ResolveWindowOrderByPreAggregation(
      const ASTOrderBy* ast_order_by, bool is_in_range_window,
      ExprResolutionInfo* expr_resolution_info, bool allow_ordinals,
      const char* clause_name, WindowExprInfoList** order_by_info_out);

  // Resolves a window (partitioning/ordering expression) expression.
  absl::Status ResolveWindowExpression(
      const ASTExpression* ast_expr, ExprResolutionInfo* expr_resolution_info,
      bool allow_ordinals, std::unique_ptr<WindowExprInfo>* resolved_item_out,
      const Type** expr_type_out);

  // Returns a ResolvedAnalyticFunctionGroup in
  // <resolved_analytic_function_group>. <analytic_column_list> is populated
  // with resolved columns for analytic functions to be output.
  absl::Status ResolveAnalyticFunctionGroup(
      QueryResolutionInfo* query_resolution_info,
      AnalyticFunctionGroupInfo* function_group_info,
      std::unique_ptr<ResolvedAnalyticFunctionGroup>*
          resolved_analytic_function_group,
      ResolvedColumnList* analytic_column_list);

  // Finish resolving partitioning expressions, and returns a
  // ResolvedWindowPartitioning in <resolved_window_partitioning_out>.
  // SELECT-list expressions in <select_column_state_list> will be rewritten to
  // reference partitioning columns if applicable.
  absl::Status ResolveWindowPartitionByPostAggregation(
      const ASTPartitionBy* ast_partition_by,
      QueryResolutionInfo* query_resolution_info, bool force_rename_columns,
      std::unique_ptr<const ResolvedWindowPartitioning>*
          resolved_window_partitioning_out);

  // Finish resolving ordering expressions, and returns a ResolvedWindowOrdering
  // in <resolved_window_ordering_out>. SELECT-list expressions in
  // <select_column_state_list> will be rewritten to reference ordering columns
  // if applicable.
  absl::Status ResolveWindowOrderByPostAggregation(
      const ASTOrderBy* ast_order_by,
      QueryResolutionInfo* query_resolution_info,
      std::unique_ptr<const ResolvedWindowOrdering>*
          resolved_window_ordering_out);

  // Creates a ResolvedColumnRef for a window (partitioning/ordering) expression
  // in <window_expr_info>. A ResolvedComputedColumn is created if:
  // 1. `expression` is not a column reference, or
  // 2. `force_rename_columns` is set (even if the expression is a column
  //    reference), or
  // 3. `expression` references a SELECT-list expression in
  //    `select_column_state_list` that is not a column reference.
  // For the latter case, the SELECT-list expression is rewritten to reference
  // the window expression.
  absl::Status AddColumnForWindowExpression(
      IdString query_alias, IdString column_alias, bool force_rename_columns,
      QueryResolutionInfo* query_resolution_info,
      WindowExprInfo* window_expr_info);

  // <target_expr_type> specifies the type of an ordering expression, and will
  // be the type of each offset boundary expression in a RANGE-based window. It
  // can be NULL if the window frame is ROWS-based.
  absl::Status ResolveWindowFrame(
      const ASTWindowFrame* ast_window_frame, const Type* target_expr_type,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedWindowFrame>* resolved_window_frame);

  absl::Status ResolveWindowFrameUnit(
      const ASTWindowFrame* ast_window_frame,
      ResolvedWindowFrame::FrameUnit* resolved_unit) const;

  absl::Status ResolveWindowFrameExpr(
      const ASTWindowFrameExpr* ast_frame_expr,
      ResolvedWindowFrame::FrameUnit frame_unit, const Type* target_expr_type,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedWindowFrameExpr>*
          resolved_window_frame_expr);

  // Resolves the expression inside the <ast_frame_expr> of type
  // OFFSET_PRECEDING or OFFSET_FOLLOWING.
  // The type of the returned <resolved_offset_expr> is INT64 if <frame_unit>
  // is ROWS. Otherwise, it is <ordering_expr_type>.
  absl::Status ResolveWindowFrameOffsetExpr(
      const ASTWindowFrameExpr* ast_frame_expr,
      ResolvedWindowFrame::FrameUnit frame_unit, const Type* ordering_expr_type,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_offset_expr);

  // Validates that the window is not always empty by comparing the boundary
  // types to check if they are compatible.
  absl::Status ValidateWindowFrameSize(
      const ASTWindowFrame* ast_window_frame,
      const ResolvedWindowFrame* resolved_window_frame) const;

  // Checks that ORDER BY is specified and there is exactly one numeric ordering
  // expression for a RANGE-based window.
  absl::Status ValidateOrderByInRangeBasedWindow(
      const ASTOrderBy* ast_order_by, const ASTWindowFrame* ast_window_frame,
      WindowExprInfoList* order_by_info);

  // Finalizes the resolved analytic function call by putting it all together,
  // resolving the final pieces like NULL handling modifier, collation, and
  // annotations. Returns the ResolvedAnalyticFunctionCall and the resolved
  // column assigned to it.
  absl::StatusOr<std::unique_ptr<const ResolvedAnalyticFunctionCall>>
  FinalizeResolvedAnalyticFunctionCall(
      const ASTNode* ast_location, const ASTFunctionCall* ast_function_call,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedFunctionCall> resolved_function_call,
      std::unique_ptr<const ResolvedExpr> resolved_where_expr,
      std::unique_ptr<const ResolvedWindowFrame> resolved_window_frame);

  // Records whether the last call to CreateAnalyticScan() was successful.
  // We need this for sanity checks, i.e. if all the resolutions were
  // successful, then ensure that all the computed window expressions are
  // attached to the final resolved tree.
  bool is_create_analytic_scan_successful_ = false;

  // Indicates whether the current resolution is in the context of a
  // MATCH_RECOGNIZE clause. If this is true, `analytic_function_groups_` will
  // not be empty and the first group is always the "main" group, defining the
  // M_R window. This group is not part of the map
  // `ast_window_spec_to_function_group_map_` as it comes from the PARTITION BY
  // and ORDER BY clauses of the MATCH_RECOGNIZE clause.
  bool in_match_recognize_window_context_ = false;
};

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_ANALYTIC_FUNCTION_RESOLVER_H_
