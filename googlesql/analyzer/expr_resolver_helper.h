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

#ifndef GOOGLESQL_ANALYZER_EXPR_RESOLVER_HELPER_H_
#define GOOGLESQL_ANALYZER_EXPR_RESOLVER_HELPER_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/analyzer/name_scope.h"
#include "googlesql/analyzer/query_resolver_helper.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/with_modifier_mode.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "absl/status/statusor.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {

class QueryResolutionInfo;
struct ExprResolutionInfo;

// Return true if <expr> can be treated like a constant,
// meaning it has the same value for the entire query and has no
// side effects.
//
// This shouldn't fail, except for unimplemented cases.
//
// Functions like RAND() are not constant.  UDFs may not be constant.
// Functions like CURRENT_TIMESTAMP() are considered constant because they
// always return a constant within the same query.
//
// The current definition uses these rules:
// - literals and parameters are constant
// - column references are not constant
// - scalar functions are constant if FunctionOptions::volatility is
//   IMMUTABLE or STABLE, and if all arguments are constant
// - aggregate and analytic functions are not constant
// - expression subqueries are not constant
// - built-in operators like CAST and CASE and struct field access are
//   constant if all arguments are constant
absl::StatusOr<bool> IsConstantExpression(const ResolvedExpr* expr);

// Return true if `expr` is an appropriate argument for a function argument
// marked 'must_be_constant'.
//
// The current definition uses these rules.
// - literals, parameters, and CONSTANT references are appropriate.
// - a cast with an input expression that satisfies one of the above
absl::StatusOr<bool> IsConstantFunctionArg(const ResolvedExpr* expr);

// Return true if `expr` is an appropriate argument for an aggregate function
// argument that is labeled "NON AGGREGATE".
//
// The current definition uses these rules.
// - literals, parameters, and CONSTANT references are appropriate.
// - a reference to a non-aggregate arg from a containing function.
// - a cast with an input expression that satisfies one of the above
absl::StatusOr<bool> IsNonAggregateFunctionArg(const ResolvedExpr* expr);

// Helper for representing if we're allowed to flatten (ie: allowed to dot into
// the fields of a proto/struct/json array), and if so, if we're already in a
// ResolvedFlatten from having previously done so.
//
// Flattening is allowed:
// - inside an explicit FLATTEN(...) call
// - in an UNNEST
//
// Once we're allowed to flatten and we do a field access on an array, then we
// being constructing a ResolvedFlatten. The FlattenState tracks the current
// ResolvedFlatten we're building and whether it's active or not.
//
// This allows us to differentiate:
//    FLATTEN(a.b.c)[OFFSET(0)]
// from
//    FLATTEN(a.b.c[OFFSET(0)])
//
// In both cases, the input expression to the offset is the ResolvedFlatten
// but in the former case (no active flatten), the offset should be on the
// result, whereas in the latter the offset should be on the active flatten.
class FlattenState {
 public:
  FlattenState() = default;
  FlattenState(const FlattenState&) = delete;
  FlattenState& operator=(const FlattenState&) = delete;

  ~FlattenState() {
    if (parent_ != nullptr) parent_->active_flatten_ = active_flatten_;
  }

  // Sets the parent for this FlattenState. This copies state from the parent,
  // but also causes the active flatten to propagate back to parent after
  // destruction so a child ExprResolutionInfo can essentially share
  // FlattenState.
  void SetParent(FlattenState* parent) {
    ABSL_DCHECK(parent_ == nullptr) << "Parent shouldn't be set more than once";
    ABSL_CHECK(parent);
    parent_ = parent;
    can_flatten_ = parent->can_flatten_;
    active_flatten_ = parent->active_flatten_;
  }

  // Helper to allow restoring original `can_flatten` state when desired.
  class Restorer {
   public:
    ~Restorer() {
      if (can_flatten_) *can_flatten_ = original_can_flatten_;
    }
    void Activate(bool* can_flatten) {
      can_flatten_ = can_flatten;
      original_can_flatten_ = *can_flatten;
    }

   private:
    bool* can_flatten_ = nullptr;
    bool original_can_flatten_;
  };
  bool can_flatten() const { return can_flatten_; }
  void set_can_flatten(bool can_flatten, Restorer* restorer) {
    restorer->Activate(&can_flatten_);
    can_flatten_ = can_flatten;
  }

  ResolvedFlatten* active_flatten() const { return active_flatten_; }
  void set_active_flatten(ResolvedFlatten* flatten) {
    active_flatten_ = flatten;
  }

 private:
  FlattenState* parent_ = nullptr;
  bool can_flatten_ = false;
  ResolvedFlatten* active_flatten_ = nullptr;
};

struct HorizontalAggregationInfo {
  ResolvedColumn array;
  bool array_is_correlated = false;
  ResolvedColumn element;
};

// These are options for field values that can be overridden when constructing
// an initial ExprResolutionInfo, or creating a child ExprResolutionInfo
// with these fields overridden.
// Default values here indicate no-change. The class below has the field
// defaults.
struct ExprResolutionInfoOptions {
  // When constructing a new ExprResolutionInfo without a parent, set this
  // in the constructor arg rather than by using options.
  // For ExprResolutionInfo derived from a parent, these update the name scopes.
  // Normally, the only allowed update is to replace `name_scope` with
  // the parent's `aggregate_name_scope` or `analytic_name_scope`.
  const NameScope* name_scope = nullptr;
  const NameScope* aggregate_name_scope = nullptr;
  const NameScope* analytic_name_scope = nullptr;

  // Normally, when creating a child ExprResolutionInfo, name_scope is
  // only allowed to be overridden to the value of the parent's
  // `aggregate_name_scope` or `analytic_name_scope`, and those scopes cannot
  // be changed.  Unusual cases where setting brand new scopes is required are
  // supported if this is set to true.
  bool allow_new_scopes = false;

  std::optional<bool> allows_aggregation;
  std::optional<bool> allows_analytic;
  std::optional<bool> allows_horizontal_aggregation;

  std::optional<bool> use_post_grouping_columns;

  const char* clause_name = nullptr;

  // These two should only be set together.
  const ASTExpression* top_level_ast_expr = nullptr;
  IdString column_alias = IdString();

  std::optional<bool> in_match_recognize_define;
  const ASTFunctionCall* nearest_enclosing_physical_nav_op = nullptr;

  // Indicates whether the expression is an expression of a graph measure
  // property.
  bool is_graph_measure_expression = false;
};

// This contains common info needed to resolve and validate an expression.
// It includes both the constant info describing what is allowed while
// resolving the expression and mutable info that returns what it actually
// has. It is passed recursively down through all expressions.
struct ExprResolutionInfo {
  // Generic constructor for any unusual combinations of options.
  ExprResolutionInfo(const NameScope* name_scope_in,
                     ExprResolutionInfoOptions options);

  // Construct a simple ExprResolutionInfo for resolving scalar expressions.
  // Aggregation and window functions are not allowed.
  // `clause_name_in` will be used in error messages.
  ExprResolutionInfo(const NameScope* name_scope_in,
                     const char* clause_name_in);

  // Construct an ExprResolutionInfo for expressions in
  // `query_resolution_info`, setting the initial NameScope.
  // All other fields get their default value, except those overridden in
  // `options`.  This includes defaulting `allows_aggregation` and
  // `allows_analytic` to false.
  ExprResolutionInfo(QueryResolutionInfo* query_resolution_info,
                     const NameScope* name_scope_in,
                     ExprResolutionInfoOptions options);
  // This constructor allows setting alternate aggregate and analytic scopes.
  ExprResolutionInfo(QueryResolutionInfo* query_resolution_info,
                     const NameScope* name_scope_in,
                     const NameScope* aggregate_name_scope_in,
                     const NameScope* analytic_name_scope_in,
                     ExprResolutionInfoOptions options);

  // Construct an ExprResolutionInfo inheriting default options from
  // a parent expression, with overrides in `options`.
  // has_aggregation and has_analytic will be updated in parent on destruction.
  explicit ExprResolutionInfo(ExprResolutionInfo* parent);
  ExprResolutionInfo(ExprResolutionInfo* parent,
                     ExprResolutionInfoOptions options);

  // Construct an ExprResolutionInfo with this common set of args, used
  // for resolving clauses in many places is resolver_query.cc.
  // We have this shorthand rather than using the Options object because
  // this exact set of options is very common.
  // Prefer using Options rather than adding more optional args here.
  ExprResolutionInfo(const NameScope* name_scope_in,
                     const NameScope* aggregate_name_scope_in,
                     const NameScope* analytic_name_scope_in,
                     bool allows_aggregation_in, bool allows_analytic_in,
                     bool use_post_grouping_columns_in,
                     const char* clause_name_in,
                     QueryResolutionInfo* query_resolution_info_in,
                     const ASTExpression* top_level_ast_expr_in = nullptr,
                     IdString column_alias_in = IdString());

  // Construct a ExprResolutionInfo from `parent`, setting the namescope to
  // `post_grouping_name_scope` and the `QueryResolutionInfo` to
  // `new_query_resolution_info`. In general, `ExprResolutionInfo` should have
  // the same `QueryResolutionInfo*` as its parent, but in the case of
  // multi-level aggregation, we need to create a new `QueryResolutionInfo` to
  // hold nested resolved aggregate functions.
  static std::unique_ptr<ExprResolutionInfo> MakeChildForMultiLevelAggregation(
      ExprResolutionInfo* parent,
      QueryResolutionInfo* new_query_resolution_info,
      const NameScope* post_grouping_name_scope);

  ExprResolutionInfo(const ExprResolutionInfo&) = delete;
  ExprResolutionInfo& operator=(const ExprResolutionInfo&) = delete;

  ~ExprResolutionInfo();

  // Returns whether or not the current expression resolution is happening
  // after DISTINCT.
  bool is_post_distinct() const;

  WithModifierMode GetWithModifierMode() const;

  std::string DebugString() const;

  // Constant info.

  ExprResolutionInfo* const parent = nullptr;

  // NameScope to use while resolving this expression.  Never NULL.
  const NameScope* const name_scope = nullptr;

  // NameScope to use while resolving any aggregate function arguments that
  // are in this expression.  Never NULL.
  const NameScope* const aggregate_name_scope = nullptr;

  // NameScope to use while resolving any analytic function arguments that
  // are in this expression.  Never NULL.
  const NameScope* const analytic_name_scope = nullptr;

  // Indicates whether this expression allows aggregations.
  const bool allows_aggregation = false;

  // Indicates whether this expression allows analytic functions.
  const bool allows_analytic = false;

  // Indicates whether the expression is an expression of a graph measure
  // property.
  const bool is_graph_measure_expression = false;

  // <clause_name> is used to generate an error saying aggregation/analytic
  // functions are not allowed in this clause, e.g. "WHERE clause".  It is
  // also used in error messages related to path expression resolution
  // after GROUP BY.
  // This can be "" (not null) if both aggregations and analytic functions are
  // allowed, or if there is no clear clause name to use in error messages
  // (for instance when resolving correlated path expressions that are in
  // a subquery's SELECT list but the subquery itself is in the outer
  // query's ORDER BY clause).
  const char* const clause_name = "";

  // Must be non-NULL if <allows_aggregation> or <allows_analytic>.
  // If non-NULL, <query_resolution_info> gets updated during expression
  // resolution with aggregate and analytic function information present
  // in the expression.  It is unused if aggregate and analytic functions
  // are not allowed in the expression.
  // Not owned.
  QueryResolutionInfo* const query_resolution_info = nullptr;

  // Findings about this expression, e.g. `has_aggregation`, etc.
  ExprFindings findings;

  // True if this expression should be resolved against post-grouping
  // columns.  Gets set to false when resolving arguments of aggregation
  // functions.
  // TODO: Consider removing this field and rely on `grouping_context` instead.
  const bool use_post_grouping_columns = false;

  // Whether the current expression is in a pre- or post-grouping context, or
  // unknown.
  ExprGroupingContext grouping_context = ExprGroupingContext::kUnknown;
  std::vector<SelectColumnState*> columns_referenced_laterally;

  // The top-level AST expression being resolved in the current context. This
  // field is set only when resolving SELECT columns. Not owned.
  const ASTExpression* const top_level_ast_expr = nullptr;

  // The alias for `top_level_ast_expr` in the SELECT list.  This will
  // be used as the ResolvedColumn name if a column is created for that
  // top-level AST expression as output of an aggregate or an analytic function.
  // This field is set only when resolving SELECT columns.
  const IdString column_alias = IdString();

  // Context around if we can flatten and if we're currently actively doing so.
  // See FlattenState for details.
  FlattenState flatten_state;

  // True if this expression allows horizontal aggregation.
  const bool allows_horizontal_aggregation = false;

  // If present, we've seen a horizontal aggregation and we record the array and
  // element column that this horizontal aggregation can use. If not present, we
  // haven't seen an array variable in a horizontal aggregation or horizontal
  // aggregation is not allowed.
  std::optional<HorizontalAggregationInfo> horizontal_aggregation_info;

  // True if the current expression is part of a horizontal aggregation
  // expression. It can be a child or part of an argument.
  bool in_horizontal_aggregation = false;

  // True if the current expression is part of a MATCH_RECOGNIZE DEFINE
  // clause.
  bool in_match_recognize_define = false;

  // The nearest enclosing physical navigation operation.
  // Used to produce better error messages on nested PREV() and NEXT() calls.
  const ASTFunctionCall* nearest_enclosing_physical_nav_op = nullptr;

 private:
  // Specialized constructor where the `QueryResolutionInfo` is not the same as
  // the parent's. This is currently only used for resolving arguments for
  // multi-level aggregate functions, and should NOT be used anywhere else.
  ExprResolutionInfo(ExprResolutionInfo* parent,
                     QueryResolutionInfo* new_query_resolution_info,
                     ExprResolutionInfoOptions options);

  // Called only in the constructor.
  void Subscribe(const NameScope* name_scope) {
    if (name_scope != nullptr) {
      const_cast<NameScope*>(name_scope)
          ->PushLateralColumnReferenceObserver(this);
    }
  }

  // Called only in the destructor.
  void Unsubscribe(const NameScope* name_scope) {
    if (name_scope != nullptr) {
      const_cast<NameScope*>(name_scope)
          ->PopLateralColumnReferenceObserver(this);
    }
  }
};

// Create a vector<const ASTNode*> from another container.
// It would be preferable to express this as a cast without a copy but that
// is not possible with the std::vector interface.
template <class NodeContainer>
std::vector<const ASTNode*> ToASTNodes(const NodeContainer& nodes) {
  std::vector<const ASTNode*> ast_locations;
  ast_locations.reserve(nodes.size());
  for (const ASTNode* node : nodes) {
    ast_locations.push_back(node);
  }
  return ast_locations;
}

// This helper class is for resolving table-valued functions. It represents a
// resolved argument to the TVF. The argument can be either a scalar
// expression, in which case <expr> is filled, a relation, in which case
// <scan> and <name_list> are filled (where <name_list> is for <scan>), or a
// machine learning model, in which case <model> is filled.
// The public accessors provide ways to set and retrieve the fields in a type
// safe manner.
class ResolvedTVFArg {
 public:
  void SetExpr(std::unique_ptr<const ResolvedExpr> expr) {
    expr_ = std::move(expr);
    type_ = EXPR;
  }
  void SetScan(std::unique_ptr<const ResolvedScan> scan,
               std::shared_ptr<const NameList> name_list,
               bool is_pipe_input_table) {
    scan_ = std::move(scan);
    name_list_ = name_list;
    type_ = SCAN;
    is_pipe_input_table_ = is_pipe_input_table;
  }
  void SetModel(std::unique_ptr<const ResolvedModel> model) {
    model_ = std::move(model);
    type_ = MODEL;
  }
  void SetConnection(std::unique_ptr<const ResolvedConnection> connection) {
    connection_ = std::move(connection);
    type_ = CONNECTION;
  }
  void SetDescriptor(std::unique_ptr<const ResolvedDescriptor> descriptor) {
    descriptor_ = std::move(descriptor);
    type_ = DESCRIPTOR;
  }
  void SetGraph(const PropertyGraph* graph) {
    graph_ = graph;
    type_ = GRAPH;
  }

  bool IsExpr() const { return type_ == EXPR; }
  bool IsScan() const { return type_ == SCAN; }
  bool IsModel() const { return type_ == MODEL; }
  bool IsConnection() const { return type_ == CONNECTION; }
  bool IsDescriptor() const { return type_ == DESCRIPTOR; }
  bool IsGraph() const { return type_ == GRAPH; }

  absl::StatusOr<const ResolvedScan*> GetScan() const {
    GOOGLESQL_RET_CHECK(IsScan());
    return scan_.get();
  }
  absl::StatusOr<const ResolvedExpr*> GetExpr() const {
    GOOGLESQL_RET_CHECK(IsExpr());
    return expr_.get();
  }
  absl::StatusOr<const ResolvedModel*> GetModel() const {
    GOOGLESQL_RET_CHECK(IsModel());
    return model_.get();
  }
  absl::StatusOr<const ResolvedConnection*> GetConnection() const {
    GOOGLESQL_RET_CHECK(IsConnection());
    return connection_.get();
  }
  absl::StatusOr<const ResolvedDescriptor*> GetDescriptor() const {
    GOOGLESQL_RET_CHECK(IsDescriptor());
    return descriptor_.get();
  }
  absl::StatusOr<std::shared_ptr<const NameList>> GetNameList() const {
    GOOGLESQL_RET_CHECK(IsScan());
    return name_list_;
  }
  absl::StatusOr<const PropertyGraph*> GetGraph() const {
    GOOGLESQL_RET_CHECK(IsGraph());
    return graph_;
  }
  bool IsPipeInputTable() const { return is_pipe_input_table_; }

  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> MoveExpr() {
    GOOGLESQL_RET_CHECK(IsExpr());
    return std::move(expr_);
  }
  absl::StatusOr<std::unique_ptr<const ResolvedScan>> MoveScan() {
    GOOGLESQL_RET_CHECK(IsScan());
    return std::move(scan_);
  }
  absl::StatusOr<std::unique_ptr<const ResolvedModel>> MoveModel() {
    GOOGLESQL_RET_CHECK(IsModel());
    return std::move(model_);
  }
  absl::StatusOr<std::unique_ptr<const ResolvedConnection>> MoveConnection() {
    GOOGLESQL_RET_CHECK(IsConnection());
    return std::move(connection_);
  }
  absl::StatusOr<std::unique_ptr<const ResolvedDescriptor>> MoveDescriptor() {
    GOOGLESQL_RET_CHECK(IsDescriptor());
    return std::move(descriptor_);
  }

 private:
  enum {
    UNDEFINED,
    EXPR,
    SCAN,
    MODEL,
    CONNECTION,
    DESCRIPTOR,
    GRAPH,
  } type_ = UNDEFINED;

  std::unique_ptr<const ResolvedExpr> expr_;
  std::unique_ptr<const ResolvedScan> scan_;
  std::unique_ptr<const ResolvedModel> model_;
  std::unique_ptr<const ResolvedConnection> connection_;
  std::unique_ptr<const ResolvedDescriptor> descriptor_;
  const PropertyGraph* graph_ = nullptr;

  // If type is SCAN, the NameList of the table being scanned.
  std::shared_ptr<const NameList> name_list_;

  // Indicates whether this is a relation created as a pipe CALL input table.
  // Can only be true if type_ is SCAN.
  bool is_pipe_input_table_ = false;
};

// Computes the default alias to use for an expression.
// This comes from the final identifier used in a path expression.
// Returns empty string if this node doesn't have a default alias.
IdString GetAliasForExpression(const ASTNode* node);

// Returns true if the input `node` is ASTNamedArgument and its `expr` field is
// an ASTLambda.
bool IsNamedLambda(const ASTNode* node);

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_EXPR_RESOLVER_HELPER_H_
