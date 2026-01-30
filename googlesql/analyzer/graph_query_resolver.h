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

#ifndef GOOGLESQL_ANALYZER_GRAPH_QUERY_RESOLVER_H_
#define GOOGLESQL_ANALYZER_GRAPH_QUERY_RESOLVER_H_

#include <functional>
#include <memory>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include "googlesql/analyzer/name_scope.h"
#include "googlesql/analyzer/resolver.h"
#include "googlesql/analyzer/set_operation_resolver_base.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/input_argument_type.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql {

template <typename T>
struct CompareByName {
  bool operator()(const T* lhs, const T* rhs) const {
    return lhs->Name() < rhs->Name();
  }
};

// Case insensitive hash table of column names.
using ColumnNameSet =
    absl::flat_hash_set<IdString, IdStringCaseHash, IdStringCaseEqualFunc>;

// Note that PropertySet and ElementTableSet are ordered because of the file
// based analyzer tests, which performs a text based comparison and thus
// requires deterministic ResolvedASTs.
using PropertySet = std::set<const GraphPropertyDeclaration*,
                             CompareByName<GraphPropertyDeclaration>>;
using ElementTableSet =
    std::set<const GraphElementTable*, CompareByName<GraphElementTable>>;
using DynamicPropertyMap = absl::flat_hash_map<const GraphElementTable*,
                                               const GraphDynamicProperties*>;

// Returns a set of static properties exposed by <element_tables>.
absl::Status GetPropertySet(const ElementTableSet& element_tables,
                            PropertySet& static_properties,
                            DynamicPropertyMap& dynamic_properties);

// Returns a set of tables satisfying <label_expr> and which have
// the specified element_kind (node or edge).
absl::StatusOr<ElementTableSet> GetMatchingElementTables(
    const PropertyGraph& property_graph,
    const ResolvedGraphLabelExpr* label_expr,
    GraphElementTable::Kind element_kind);

// Returns a set of element tables satisfying <label_expr>.
template <typename T>
absl::StatusOr<ElementTableSet> GetTablesSatisfyingLabelExpr(
    const PropertyGraph& property_graph,
    const ResolvedGraphLabelExpr* label_expr,
    const absl::flat_hash_set<const T*>& tables);

class Resolver;
class AnalyzerOptions;
class QueryResolutionInfo;

// With support for quantified path patterns, the output name list on
// resolving any graph/path/element pattern will always contain 2 lists:
// 1) <singleton_name_list> contains all the singleton variables (variables
// that are not quantified).
// 2) <group_name_list> contains all the group variables (variables that are
// quantified).
//
// Note that whether a variable is treated as a singleton or group variable
// is context specific. For example:
// (
//  ((a) -[e]-> (b <elem_scope>) WHERE <inner_scope>){1, 3}
//  WHERE <outer_scope>
// )
// - <a>, <e>, <b> are singleton variables in the scope of <inner_scope>
// - <b> is a singleton in <elem_scope>;
// - However they are all group variables in the scope of <outer_scope>.
struct GraphTableNamedVariables {
  // Describes the context which outputs GraphTableNamedVariables.
  const ASTNode* ast_node;
  // Contains all the singleton variables for this 'ast_node'
  std::shared_ptr<NameList> singleton_name_list = std::make_shared<NameList>();
  // Contains all the group variables for this 'ast_node'
  std::shared_ptr<NameList> group_name_list = std::make_shared<NameList>();

  // Used during the resolution of a CALL subquery, to keep the correlated
  // variables from the outside scope. The subquery can have multiple linear
  // operators, so these correlated names travel all along.
  // Always nullptr (not just an empty list) unless we're inside the body of
  // of a CALL.
  //
  // INVARIANT: Names in the other lists are not allowed to conflict with
  // names in this list, unlike outer names which would simply be shadowed.
  // i.e., this list is always disjoint with the other lists.
  std::shared_ptr<NameList> correlated_name_list = nullptr;
};

// This class performs resolution for GraphTable during GoogleSQL analysis.
//
class GraphTableQueryResolver {
 public:
  GraphTableQueryResolver(Catalog* catalog, Resolver* resolver)
      : catalog_(catalog), resolver_(resolver) {}
  GraphTableQueryResolver(const GraphTableQueryResolver&) = delete;
  GraphTableQueryResolver& operator=(const GraphTableQueryResolver&) = delete;

  // Resolves a graph table query.
  // The graph_reference can be from two places:
  // 1. if graph_table_query_ above declares a graph table, then it will be set
  //    as the graph_reference_
  // 2. if graph_table_query_ omits the graph table, then the graph_reference_
  //    will be implied from the parent context
  absl::Status ResolveGraphTableQuery(
      const ASTGraphTableQuery* graph_table_query, const NameScope* scope,
      std::unique_ptr<const ResolvedScan>* output,
      NameListPtr* output_name_list);

  // Resolves a GQL EXISTS {graph_pattern} subquery.
  // This function basically does the below rewrite, from
  //   `EXISTS {$graph_pattern}`
  // to
  //   `EXISTS {MATCH $graph_pattern RETURN TRUE AS val LIMIT 1}`
  absl::Status ResolveGqlGraphPatternQuery(
      const ASTGqlGraphPatternQuery* graph_pattern_subquery,
      const NameScope* scope, std::unique_ptr<const ResolvedScan>* output,
      NameListPtr* output_name_list);

  // Resolves a GQL EXISTS {graph_linear_ops} subquery.
  // This function basically does the below rewrite, from
  //   `EXISTS {$graph_linear_ops}`
  // to
  //   `EXISTS {$graph_linear_ops RETURN TRUE AS val LIMIT 1}`
  absl::Status ResolveGqlLinearOpsQuery(
      const ASTGqlLinearOpsQuery* linear_ops_subquery, const NameScope* scope,
      std::unique_ptr<const ResolvedScan>* output,
      NameListPtr* output_name_list);

 private:
  using PathModeScansVector =
      std::vector<std::pair<ResolvedGraphPathModeEnums::PathMode,
                            const ResolvedGraphPathScan*>>;
  // Maps a scalar variable id to the tuple {R, ptr to the resolved column of
  // its corresponding array variable}, where R is the recursion level of the
  // path scan that contains the quantified path column map.
  using PathModeScalarToArrayVarMap =
      absl::flat_hash_map<int, std::tuple<int, const ResolvedColumn*>>;
  using CleanupGraphRefCb = std::function<void()>;

  // Checks if the query has graph reference, and:
  // - if true: pushes the graph ref into the stack, and returns a cleanup func
  // - if false: returns error if current stack is empty, otherwise does no-op
  absl::StatusOr<CleanupGraphRefCb> HandleGraphReference(const ASTNode* query);

  // Creates an empty GraphTableNamedVariables.
  static GraphTableNamedVariables CreateEmptyGraphNameLists(
      const ASTNode* node);

  // Creates a GraphTableNamedVariables object with a singleton namelist only.
  absl::StatusOr<GraphTableNamedVariables> CreateGraphNameListsSingletonOnly(
      const ASTNode* node, std::shared_ptr<NameList> singleton_name_list);

  // Creates a GraphTableNamedVariables object with both singleton and group
  // namelists.
  absl::StatusOr<GraphTableNamedVariables> CreateGraphNameLists(
      const ASTNode* node, std::shared_ptr<NameList> singleton_name_list,
      std::shared_ptr<NameList> group_name_list,
      std::shared_ptr<NameList> correlated_name_list);

  // Merges <child_namelist> and <parent_namelist> and outputs a new merged
  // graph namelist and a list of GraphMakeArrayVariables that record
  // the mapping between variables inside a quantification and the
  // newly introduced group variables. Used by non-leaf composite patterns (e.g.
  // PathPattern, GraphPattern).
  //
  // A composite pattern builds its GraphTableNamedVariables from merging
  // outputs from sub patterns.
  absl::StatusOr<GraphTableNamedVariables> MergeGraphNameLists(
      GraphTableNamedVariables parent_namelist,
      GraphTableNamedVariables child_namelist,
      std::vector<std::unique_ptr<const ResolvedGraphMakeArrayVariable>>&
          new_group_variables);

  // Same as the previous MergeGraphNameLists but can only be used in a
  // non-quantified context.
  absl::StatusOr<GraphTableNamedVariables> MergeGraphNameLists(
      GraphTableNamedVariables parent_namelist,
      GraphTableNamedVariables child_namelist);

  // Validates that 'graph_namelists' obeys the following:
  //  - No group variable is multiply declared
  absl::Status ValidateGraphNameLists(
      const GraphTableNamedVariables& graph_namelists);

  // Returns a new name list that contains the singleton variables outputted by
  // the node pattern in graph_namelists.
  static NameListPtr GetOutputSingletonNameList(
      const GraphTableNamedVariables& graph_namelists);

  // Returns a new name list that contains the group variables outputted by
  // 'graph_namelists' under its respective node pattern
  // (graph_namelists.node()). Also populates 'new_group_variables' with a list
  // of all new group variables and their corresponding singleton variable
  // inside the quantification.
  //
  // Variables within node()'s pattern are treated as group variables if the
  // pattern is quantified.
  absl::StatusOr<NameListPtr> GetOutputGroupNameList(
      const GraphTableNamedVariables& graph_namelists,
      std::vector<std::unique_ptr<const ResolvedGraphMakeArrayVariable>>&
          new_group_variables);

  // Represents the output of most ResolveGraph/Gql* statements
  template <typename NodeType>
  struct ResolvedGraphWithNameList {
    // The output resolved node produced by the call.
    std::unique_ptr<NodeType> resolved_node;
    // The output graph namelists produced by the call.
    GraphTableNamedVariables graph_name_lists;
  };

  // Copy the graph input scan. The `input` can only be a SingleRowScan or a
  // GraphRefScan.
  absl::StatusOr<ResolvedGraphWithNameList<const ResolvedScan>>
  BuildRefForInputScan(
      const ResolvedGraphWithNameList<const ResolvedScan>& input);

  // Represents the output of a ResolvedPathPatternList().
  struct PathPatternListResult {
    // Output vector of path scans
    std::vector<std::unique_ptr<const ResolvedGraphPathScan>>
        resolved_scan_vector;
    // Column list of the path pattern list
    ResolvedColumnList resolved_column_list;
    // Filter expression applied across the paths.
    std::unique_ptr<const ResolvedExpr> filter_expr;
    // Output name lists produced.
    GraphTableNamedVariables graph_name_lists;
  };

  // Helper function to construct the filter_expr for a 'graph_pattern' clause.
  // - 'input_named_variables' holds the name list of the input scan.
  // - 'cross_path_multiply_decl_filter_expr' holds the filter expression
  //   applied across multiple path patterns within the graph pattern.
  // - 'cross_stmt_multiply_decl_filter_expr' holds the filter expression
  //   applied across multiple MATCH statements.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
  MakeGraphPatternFilterExpr(
      const ASTGraphPattern* graph_pattern,
      const GraphTableNamedVariables& input_named_variables,
      std::unique_ptr<const ResolvedExpr> cross_path_multiply_decl_filter_expr,
      std::unique_ptr<const ResolvedExpr> cross_stmt_multiply_decl_filter_expr,
      const NameScope* scope);

  // Build a GraphRefScan from `input_name_list`.
  absl::StatusOr<std::unique_ptr<const ResolvedScan>> BuildGraphRefScan(
      const NameListPtr& input_name_list);

  // Resolves an expression that may have a horizontal aggregate.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> ResolveExpr(
      const ASTExpression* expr, const NameScope* local_scope,
      QueryResolutionInfo* query_resolution_info, bool allow_analytic,
      bool allow_horizontal_aggregate, const char* clause_name) const;

  // Resolves a GqlOperator by delegating to resolver functions of the
  // derived class. `inputs` holds the current working table and namelists.
  //
  // `current_scan_list` holds the scans in the current list of linear
  // operators. This is useful for scans like FILTER <analytic>, which needs
  // to place the new columns on a project scan because FilterScan does not
  // create any new columns.
  //  of the GQL query, it is read as input and modified as output.
  // TODO: simplify this function by just passing the current list, instead of
  // passing the last scan separately.
  absl::StatusOr<ResolvedGraphWithNameList<const ResolvedScan>>
  ResolveGqlOperator(
      const ASTGqlOperator* gql_op, const NameScope* external_scope,
      std::vector<std::unique_ptr<const ResolvedScan>>& current_scan_list,
      ResolvedGraphWithNameList<const ResolvedScan> inputs,
      bool is_first_statement_in_graph_query);

  // Verifies that the sequence of linear query ops in `primitive_ops` is valid.
  absl::Status CheckGqlLinearQuery(
      absl::Span<const ASTGqlOperator* const> primitive_ops) const;

  // Resolves a composite GraphLinearScan having linear scans as children.
  // `out_name_list` should start as an empty namelist, and would be updated by
  // this function.
  absl::StatusOr<ResolvedGraphWithNameList<const ResolvedGraphLinearScan>>
  ResolveGqlLinearQueryList(const ASTGqlOperatorList& gql_ops_list,
                            const NameScope* external_scope,
                            GraphTableNamedVariables input_graph_name_lists,
                            bool is_first_statement_in_graph_query);

  // Resolves a GraphLinearScan with primitive gql operations (MATCH, RETURN) as
  // children. `input_graph_name_lists`s namelists are consumed for the current
  // input scan. Returns the resulting scan and its namelists.
  absl::StatusOr<ResolvedGraphWithNameList<const ResolvedGraphLinearScan>>
  ResolveGqlLinearQuery(const ASTGqlOperatorList& gql_ops_list,
                        const NameScope* external_scope,
                        ResolvedGraphWithNameList<const ResolvedScan> inputs,
                        bool is_first_statement_in_graph_query);

  // Resolves a chained list of gql operators. `input_graph_name_lists`s
  // namelists are consumed for the current input scan.
  // Returns the resulting scan and its namelists.
  absl::StatusOr<ResolvedGraphWithNameList<const ResolvedGraphLinearScan>>
  ResolveGqlOperatorList(absl::Span<const ASTGqlOperator* const> gql_ops,
                         const NameScope* external_scope,
                         ResolvedGraphWithNameList<const ResolvedScan> inputs,
                         bool is_first_statement_in_graph_query);

  // Resolves a match operator in GQL syntax, which first resolves a graph
  // pattern independently using `input_scope` (the scope at the start of a
  // GRAPH_TABLE), and then uses `input_scope` plus the namelist of
  // `input_scan` to resolve the multiply declared graph column names and graph
  // pattern where clause. Outputs graph pattern cols on the right, amended by
  // `input_scan` columns on the left. More details are described in
  // (broken link):gql-linear-comp. `input_graph_name_lists`s namelists are consumed
  // for the current input scan.
  // Returns the resulting scan and its namelists.
  absl::StatusOr<ResolvedGraphWithNameList<const ResolvedScan>> ResolveGqlMatch(
      const ASTGqlMatch& match_op, const NameScope* input_scope,
      ResolvedGraphWithNameList<const ResolvedScan> inputs);

  // Resolves a LET operator in a GQL-dialect graph query and builds a
  // ProjectScan that extends the input scan with additional columns
  // corresponding to variable definitions in the LET statement. Also returns
  // those names to the return namelist.
  absl::StatusOr<ResolvedGraphWithNameList<const ResolvedScan>> ResolveGqlLet(
      const ASTGqlLet& let_op, const NameScope* local_scope,
      ResolvedGraphWithNameList<const ResolvedScan> inputs);

  // Resolves a FILTER operator in a GQL-dialect graph query and builds
  // a FilterScan on `input_scan` with the condition expressed in
  // `filter_op`. The `working_name_list` is left unmodified.
  absl::StatusOr<ResolvedGraphWithNameList<const ResolvedScan>>
  ResolveGqlFilter(
      const ASTGqlFilter& filter_op, const NameScope* local_scope,
      std::vector<std::unique_ptr<const ResolvedScan>>& current_scan_list,
      ResolvedGraphWithNameList<const ResolvedScan> inputs);

  // Resolves a GQL query's ORDER BY PAGE operator.
  absl::StatusOr<ResolvedGraphWithNameList<const ResolvedScan>>
  ResolveGqlOrderByAndPage(
      const ASTGqlOrderByAndPage& order_by_page_op,
      const NameScope* external_scope,
      ResolvedGraphWithNameList<const ResolvedScan> inputs);

  // Resolves a GQL query's WITH operator.
  absl::StatusOr<ResolvedGraphWithNameList<const ResolvedScan>> ResolveGqlWith(
      const ASTGqlWith& with_op, const NameScope* local_scope,
      ResolvedGraphWithNameList<const ResolvedScan> inputs);

  // Resolves a GQL query's FOR operator.
  absl::StatusOr<ResolvedGraphWithNameList<const ResolvedScan>> ResolveGqlFor(
      const ASTGqlFor& for_op, const NameScope* local_scope,
      ResolvedGraphWithNameList<const ResolvedScan> inputs);

  // Resolves a GQL CALL operator on a subquery.
  absl::StatusOr<ResolvedGraphWithNameList<const ResolvedScan>>
  ResolveGqlInlineSubqueryCall(
      const ASTGqlInlineSubqueryCall& call_op, const NameScope* local_scope,
      ResolvedGraphWithNameList<const ResolvedScan> input,
      bool is_first_statement_in_graph_query);

  // Resolves a GQL CALL operator on a named TVF.
  absl::StatusOr<ResolvedGraphWithNameList<const ResolvedScan>>
  ResolveGqlNamedCall(const ASTGqlNamedCall& call_op,
                      const NameScope* local_scope,
                      ResolvedGraphWithNameList<const ResolvedScan> input,
                      bool is_first_statement_in_graph_query);

  // Builds a ProjectScan from GqlReturnOp. `working_name_list` is consumed as
  // the namelist of the current input scan. Updates `working_name_list` as the
  // namelist of the resulting scan.
  absl::StatusOr<ResolvedGraphWithNameList<const ResolvedScan>>
  ResolveGqlReturn(const ASTGqlReturn& return_op, const NameScope* local_scope,
                   ResolvedGraphWithNameList<const ResolvedScan> inputs);

  // Builds a SetOperationScan from GqlSetOperation. `inputs.graph_name_lists`
  // is consumed as the incoming working table's name list.
  absl::StatusOr<ResolvedGraphWithNameList<const ResolvedScan>>
  ResolveGqlSetOperation(const ASTGqlSetOperation& set_op,
                         const NameScope* external_scope,
                         ResolvedGraphWithNameList<const ResolvedScan> inputs,
                         bool is_first_statement_in_graph_query);

  // Resolves a GQL query's SAMPLE operator.
  absl::StatusOr<ResolvedGraphWithNameList<const ResolvedScan>>
  ResolveGqlSample(const ASTGqlSample& sample_op,
                   const NameScope* external_scope,
                   ResolvedGraphWithNameList<const ResolvedScan> input);

  // Builds a graph element type for <table_kind> and <properties>.
  absl::StatusOr<const GraphElementType*> MakeGraphElementType(
      GraphElementTable::Kind table_kind, const PropertySet& properties,
      TypeFactory* type_factory, bool is_dynamic = false);

  // Resolves the graph reference in graph_table_query_.
  absl::Status ResolveGraphReference(const ASTPathExpression* graph_ref);

  // Resolves the label expressions for graph element variables within the
  // GRAPH_TABLE.
  absl::Status ResolveGraphLabelExpressions(
      const ASTGraphPattern* graph_pattern);

  // Resolves the provided lower and upper quantifier patterns and outputs a
  // ResolvedGraphPathPatternQuantifier.
  absl::StatusOr<std::unique_ptr<const ResolvedGraphPathPatternQuantifier>>
  ResolveGraphPathPatternQuantifier(const NameScope* name_scope,
                                    const ASTQuantifier* ast_quantifier);

  // Resolves element pattern:
  // - A node pattern produces a stream of single-columned rows. The column is
  //   of GraphElementType(kind = Node, properties = set of properties exposed
  //   by this node pattern);
  // - An edge pattern is similar besides the type is edge and also has an
  //   orientation specified by <ast_element_pattern>.
  absl::StatusOr<ResolvedGraphWithNameList<const ResolvedGraphElementScan>>
  ResolveElementPattern(const ASTGraphElementPattern& ast_element_pattern,
                        const NameScope* input_scope,
                        GraphTableNamedVariables input_graph_name_lists,
                        const Type* cost_supertype = nullptr);

  // Resolves path mode, which is a means to restrict the sequence of nodes and
  // edges in a path.
  absl::StatusOr<std::unique_ptr<const ResolvedGraphPathMode>> ResolvePathMode(
      const ASTGraphPathMode* ast_path_mode);

  // Resolves path search prefix, which partitions the underlying paths
  // by head and tail and selects a number of paths from each partition.
  absl::StatusOr<std::unique_ptr<const ResolvedGraphPathSearchPrefix>>
  ResolvePathSearchPrefix(
      const ASTGraphPathSearchPrefix* ast_path_search_prefix,
      const NameScope* name_scope);

  // Resolves the expression `ast_search_prefix_path_count` into a
  // ResolvedExpr.
  absl::Status GetSearchPrefixPathCountExpr(
      const ASTExpression* ast_search_prefix_path_count,
      const NameScope* input_scope,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out) const;

  // Generates a path column from the columns in `path_scan_column_list`. If the
  // path is user specified (`ast_path_name` is not null), it is inserted into
  // the singleton namelist of `multiply_decl_namelists`.
  absl::StatusOr<ResolvedColumn> GeneratePathColumn(
      const ASTIdentifier* ast_path_name,
      const ResolvedColumnList& path_scan_column_list,
      GraphTableNamedVariables multiply_decl_namelists);

  // Resolves path pattern:
  // A path pattern produces a stream of rows representing a stream of paths.
  // For each row, each column corresponds to the output of one of its input
  // element scans, which represents a graph element along the path.
  // `out_multiply_decl_forbidden_names_to_err_location` is optional and it
  // gathers names from this path that cannot be multiply declared outside of
  // it. Currently it's only required when this call is resolving a top-level
  // path pattern to deal with search prefix rules.
  // `create_path_column` indicates whether to create a column for a path
  // variable representing this path pattern. It is set to true for the path
  // that introduces a new path variable and all descendant patterns.
  // `cost_supertype` is the supertype of all cost expressions for this path.
  // It is only set if the path pattern contains at least one cost expression in
  // its nesting structure. When set:
  // - All descendant path patterns will populate `cost_type` with this type.
  // - All edge cost expressions for this path will be cast to this type.
  // Generally, this occurs via a two-pass resolution process, where
  // `ResolvedPathPattern` is first called with nullptr for `cost_supertype`,
  // then the resolvedAST is used to compute the supertype. The initial AST
  // is then discarded and a second call to `ResolvedPathPattern` is made with
  // the computed supertype populated.
  absl::StatusOr<ResolvedGraphWithNameList<const ResolvedGraphPathScan>>
  ResolvePathPattern(
      const ASTGraphPathPattern& ast_path_pattern, const NameScope* input_scope,
      GraphTableNamedVariables input_graph_name_lists, bool create_path_column,
      IdStringHashMapCase<const ASTGraphPathPattern* const>*
          out_multiply_decl_forbidden_names_to_err_location = nullptr,
      const Type* cost_supertype = nullptr);

  // Resolves the list of path patterns in a graph pattern: it produces the
  // cross product of paths produced from all path patterns it contains.
  // It does not take any input namelists. It only returns output singleton and
  // group nameslists based on the names seen in the path list.
  absl::StatusOr<PathPatternListResult> ResolvePathPatternList(
      const ASTGraphPattern& ast_graph_pattern, const NameScope* input_scope);

  // Resolves a plain graph pattern (different from ResolveGqlMatch, where the
  // current working table interacts with the graph pattern, see
  // (broken link):gql-linear-comp for more details): first resolves the path
  // pattern list, and then resolves the graph pattern where clause with the
  // namescope of path patterns (and correlated names).
  //
  // The caller is responsible for restoring multiply-declared names to map to
  // the column from the incoming working table, instead of the resolved column
  // version from the pattern. This is the case for OPTIONAL MATCH (and OPTIONAL
  // CALL) where the incoming column from the working table is kept.
  // The column choice matters as it dictates the available properties.
  absl::StatusOr<ResolvedGraphWithNameList<const ResolvedGraphScan>>
  ResolveGraphPattern(const ASTGraphPattern& ast_graph_pattern,
                      const NameScope* input_scope,
                      GraphTableNamedVariables input_graph_name_lists);

  // Given a `input_graph_name_lists`:
  // For the multiply-declared graph element singleton variable name targets
  // (which are the names showed up multiple times in the singleton part of
  // `input_graph_name_lists`), outputs only one name target to an output
  // singleton list and captures the semantics in `output_expr` with SAME
  // expression : elements are the same when they bound to the same name;
  // For other name targets, outputs them as they are.
  // Note on group variables: Multiply declared group variables are not allowed
  // and must be caught earlier. This only operates on singleton variables.
  // Names in `forbidden_singleton_names_to_err_location` cannot be multiply
  // declared.
  absl::StatusOr<ResolvedGraphWithNameList<const ResolvedExpr>>
  ResolveMultiplyDeclaredVariables(
      const ASTNode& ast_location,
      const GraphTableNamedVariables& input_graph_name_lists,
      const IdStringHashMapCase<const ASTGraphPathPattern* const>*
          forbidden_singleton_names_to_err_location = nullptr);

  // Similar functionality as above, but takes two input singleton name lists
  // whose multiply-declared variables are already resolved, concatenates them
  // and outputs the rightmost occurrence of a multiply-declared singleton
  // variable.
  absl::StatusOr<ResolvedGraphWithNameList<const ResolvedExpr>>
  ResolveMultiplyDeclaredVariables(
      const ASTNode& ast_location,
      const GraphTableNamedVariables& input_name_list,
      const GraphTableNamedVariables& local_name_list);

  // Resolves graph label filter.
  absl::StatusOr<std::unique_ptr<const ResolvedGraphLabelExpr>>
  ResolveGraphLabelFilter(const ASTGraphLabelFilter* ast_graph_label_filter,
                          GraphElementTable::Kind element_kind) const;

  // Resolves the label filter for an element variable of given kind.
  absl::StatusOr<std::unique_ptr<const ResolvedGraphLabelExpr>>
  ResolveGraphElementLabelExpr(const ASTIdentifier* ast_element_var,
                               GraphElementTable::Kind element_kind,
                               const ASTGraphLabelFilter* ast_label_filter);

  // Resolves graph table shape. Creates <expr_list> that describes how
  // <output_column_list> is produced.
  absl::Status ResolveGraphTableShape(
      const ASTSelectList* ast_select_list, const NameScope* input_scope,
      NameListPtr* output_name_list, ResolvedColumnList* output_column_list,
      std::vector<std::unique_ptr<const ResolvedComputedColumn>>* expr_list)
      const;

  // Resolves the property specification for a dynamic graph element variable.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
  ResolveGraphDynamicPropertyEquality(
      const ASTNode* error_location, const PropertyGraph* graph,
      const GraphElementType* element_type, absl::string_view property_name,
      const NameScope* input_scope,
      std::unique_ptr<const ResolvedExpr> resolved_element_expr,
      std::unique_ptr<const ResolvedExpr> resolved_property_value_expr) const;

  // Resolves the property specification for a graph element variable.
  // If `supports_dynamic_properties` is true, the element column may be a
  // dynamic graph element type. Otherwise, it must be a static element type.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
  ResolveGraphElementPropertySpecification(
      const ASTGraphPropertySpecification* ast_graph_property_specification,
      const NameScope* input_scope, const ResolvedColumn& element_column,
      bool supports_dynamic_properties) const;

  // Resolves a single <ast_select_column> using <expr_resolotuion_info>. Adds
  // new name(s) and column(s) produced to <output_name/column_list>. For each
  // new column, adds an expression to <expr_list> that describes how the column
  // is produced. DotStar, e.g. `n.*`, is expanded into all properties exposed
  // by `n`, one column per property. Inputs must not be null.
  absl::Status ResolveSelectColumn(
      const ASTSelectColumn* ast_select_column, const NameScope* input_scope,
      NameList* output_name_list, ResolvedColumnList* output_column_list,
      std::vector<std::unique_ptr<const ResolvedComputedColumn>>* expr_list)
      const;

  // If <ast_where_clause> is not null, resolves it into a bool expression using
  // <input_scope> and returns the ResolvedExpr. If <ast_where_clause> is null,
  // returns an empty ResolvedExpr.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> ResolveWhereClause(
      const ASTWhereClause* ast_where_clause, const NameScope* input_scope,
      QueryResolutionInfo* query_resolution_info, bool allow_analytic,
      bool allow_horizontal_aggregate) const;

  // if `ast_edge_cost` is not null, resolves it into a double expression using
  // `input_scope` and returns the ResolvedExpr. If `ast_edge_cost` is null,
  // returns an empty ResolvedExpr.
  // If `cost_supertype` is not null, the resolved expression will be cast to
  // this type.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> ResolveCostExpr(
      const ASTExpression& ast_edge_cost, const NameScope* input_scope,
      QueryResolutionInfo* query_resolution_info,
      const Type* cost_supertype = nullptr) const;

  // Examines all path scans in `path_scans`, and for each one, records all
  // columns that are required for implementing path modes. Specifically,
  // records all relevant array variable columns which correspond to quantified
  // path group variables.
  void RecordArrayColumnsForPathMode(
      std::vector<std::unique_ptr<const ResolvedGraphPathScan>>& path_scans);

  // Examines the ResolvedGraphPathScan in `path_scan` and all the children
  // scans of `path_scan` and returns a list of pairs of path mode and the
  // corresponding path scan, only for those path scans that implement a
  // non-WALK path mode.
  PathModeScansVector CollectPathScansForPathMode(
      const ResolvedGraphPathScan& path_scan);

  // Helper function for `RecordArrayColumnsForPathMode`. Records all columns
  // for the path scan `path_scan` that are required for implementing its path
  // mode. Specifically, records all relevant array variable columns which
  // correspond to quantified path group variables. `record_nodes == true`
  // indicates that node scans should be recorded, while `record_nodes == false`
  // indicates that edge scans should be recorded. `recursion_level` and
  // `scalar_to_array_var` are used to track the current path scan and the array
  // variable columns that exist on `group_variable_list` vectors in `path_scan`
  // or children of it. REQUIRES: `recursion_level` should be initialized to `0`
  // and `scalar_to_array_var` should be empty.
  void RecordArrayColumnsForPathModeForSinglePathScan(
      const ResolvedGraphPathScan* path_scan, int recursion_level,
      bool record_nodes, PathModeScalarToArrayVarMap& scalar_to_array_var_map);

  // Expects <resolved_element> to be GraphElementType. For each property
  // exposed by the GraphElementType:
  //   1). creates a column and appends the column to <name/column_list>.
  //   2). appends an expression to <expr_list> that describes how the column is
  //       produced.
  // Inputs must not be null.
  absl::Status AddPropertiesFromElement(
      const ASTNode* ast_location,
      std::unique_ptr<const ResolvedExpr> resolved_element, NameList* name_list,
      ResolvedColumnList* column_list,
      std::vector<std::unique_ptr<const ResolvedComputedColumn>>* expr_list)
      const;

  // Infers column name for input GRAPH_TABLE <column>. An explicit alias is
  // required and will be used as column name unless column.expression is a path
  // expression. A valid path expression is a reference to either a graph
  // element property or a correlated column. The last name in the path (i.e.
  // name of the property or correlated column) will be used as the default
  // column name if alias is not specified.
  absl::StatusOr<IdString> GetColumnName(const ASTSelectColumn* column) const;

  // Computes an alias to use for graph element.
  IdString ComputeElementAlias(const ASTIdentifier* ast_id);

  // Makes a sql error indicating that the given graph is not found.
  absl::Status MakeGraphNotFoundSqlError(
      const ASTPathExpression* graph_ref) const;

  // Builds a list of column refs over `columns`.
  std::vector<std::unique_ptr<const ResolvedExpr>> BuildColumnRefs(
      const ResolvedColumnList& columns,
      const absl::flat_hash_set<ResolvedColumn>& correlated_columns);

  // Builds an expression that is a conjunction of equality between all exprs.
  // <ast_location> and <variable> are only used for error reporting.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> MakeEqualElementExpr(
      const ASTNode& ast_location, IdString variable,
      std::vector<std::unique_ptr<const ResolvedExpr>> exprs) const;

  // Returns the list of child path base ptrs canonicalized from
  // `ast_path_pattern`'s children.
  absl::StatusOr<std::vector<const ASTGraphPathBase*>> CanonicalizePathPattern(
      const ASTGraphPathPattern& ast_path_pattern) const;

  // Function used by CanonicalizePathPattern to canonicalize consecutive pairs
  // of path bases. Requires that `left` and `right` are not null ptrs.
  absl::Status MaybeAddMissingNodePattern(
      const ASTGraphPathBase* left, const ASTGraphPathBase* right,
      std::vector<const ASTGraphPathBase*>& output_patterns) const;

  // Resolves the order_by segment of a GQL query's OrderByAndPage operator.
  absl::StatusOr<ResolvedGraphWithNameList<const ResolvedScan>>
  ResolveGqlOrderByClause(const ASTOrderBy* order_by,
                          const NameScope* external_scope,
                          ResolvedGraphWithNameList<const ResolvedScan> inputs);

  // Resolves the ASTGqlPage segment of a GQL query's OrderByAndPage operator.
  absl::StatusOr<ResolvedGraphWithNameList<const ResolvedScan>>
  ResolveGqlPageClauses(const ASTGqlPage* page, const NameScope* local_scope,
                        ResolvedGraphWithNameList<const ResolvedScan> inputs);

  // Resolves the expression `ast_quantifier_bound` into a ResolvedExpr.
  absl::Status GetQuantifierBoundExpr(
      const ASTExpression* ast_quantifier_bound, const NameScope* input_scope,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out) const;

  // Validates that path patterns obey the following rules:
  // 1) Every path pattern must contain a minimum node count > 0.
  //    Note: Minimum node count of a path-primary within a quantified path
  //    pattern is 0 if the lower_bound is 0.
  //
  //   Eg1: MATCH ((a)-[b]->(c)){0, 3}   ERROR => Min node count on 0th-iter = 0
  //   Eg2: MATCH ((a)-[b]->(c)){0, 3}(d)      => Min node count on 0th-iter = 1
  //   Eg3: MATCH (x)((a)-[b]->(c)){0, 3}(y)   => Min node count on 0th-iter = 2
  //   Eg4: MATCH ((a)-[b]->(c)){1, 3}         => Min node count on 1st-iter = 2
  //
  // 2) A path-primary contained within a quantified path pattern must have a
  //    minimum path length > 0.
  //    Note: Path length of a node = 0
  //    Note: Path length of an edge = 1
  //
  //   Eg1: MATCH ((a)-[b]->(c)){0, 3}
  //        => Min path length of quantified pattern = 1
  //
  //   Eg2: MATCH ((a)-[b]->(c)){1, 3}
  //        => Min path length of quantified pattern = 1
  //
  //   Eg3: MATCH ((a)-[b]->(c)){1, 3}((x)-[y]->(z)-[e]->(f)){2, 3}
  //        => Min path length of 1st quantified pattern = 1
  //        => Min path length of 2nd quantified pattern = 2
  //
  // 3) Nested quantifiers are not permitted.
  //
  // 4) Unbounded quantifier is only allowed when the path pattern has selective
  //    search prefix or restrictive path mode.
  //
  // If CHEAPEST is specified for a top-level path pattern, then the path
  //   must include at least one edge cost expression. Furthermore, each
  //   quantified edge or subpath pattern contained by the top-level path
  //   pattern must include at least one cost expression.
  //
  // 'struct PathInfo' maintains the current computed values of
  // each rule as it walks through and validates the pattern.
  struct PathInfo {
    // Minimum node count of the pattern.
    int min_node_count = 0;
    // Minimum path length of the pattern.
    int min_path_length = 0;
    // 'true' if there's a subpath that is already quantified.
    bool child_is_quantified = false;
    // `true` if the path pattern contains at least one edge cost expression
    // in its nesting structure. This is used in the context oF CHEAPEST path
    // search prefix, which requires at least one edge cost expression in the
    // entire path pattern, as well as at least one for every descendant
    // quantified edge or subpath pattern.
    bool has_edge_cost = false;
  };
  absl::Status ValidatePathPattern(const ASTGraphPattern* ast_graph_pattern,
                                   const NameScope* scope);

  absl::StatusOr<PathInfo> ValidatePathPatternInternal(
      const ASTGraphPathPattern* ast_path_pattern, const NameScope* scope,
      const std::vector<const ASTGraphPathBase*>& ast_path_bases,
      IdStringHashMapCase<bool>& var_has_cost_map, bool in_cheapest,
      bool parent_has_selective_search_prefix,
      bool parent_has_restrictive_path_mode);

  // Helper class used to resolve Graph set operation, aka. composite query
  // statement.
  class GraphSetOperationResolver : public SetOperationResolverBase {
   public:
    GraphSetOperationResolver(const ASTGqlSetOperation& node,
                              GraphTableQueryResolver* resolver);

    // Resolves Graph set operation. Each set operation item see the same
    // incoming working table `inputs`.
    absl::StatusOr<ResolvedGraphWithNameList<const ResolvedScan>> Resolve(
        const NameScope* external_scope,
        ResolvedGraphWithNameList<const ResolvedScan> inputs,
        bool is_first_statement_in_graph_query);

    // Validates that N-ary GQL set operation:
    // - contains the same set operator; and
    // - only has `op_type` and `all_or_distinct` in parsed metadata.
    absl::Status ValidateGqlSetOperation(const ASTGqlSetOperation& set_op);

    // Returns a list of input argument types.
    // `resolved_inputs`: The resolved input queries for the set operation.
    // `final_column_num`: The number of columns in the output of the set
    // operation.
    // `index_mapper`: Two way mapping between columns from each input query and
    // the final output columns.
    absl::StatusOr<std::vector<std::vector<InputArgumentType>>>
    BuildColumnTypeLists(
        absl::Span<ResolvedGraphWithNameList<const ResolvedGraphLinearScan>>
            resolved_inputs,
        int final_column_num, const IndexMapper& index_mapper) const;

    // Builds a list of set operation items.
    absl::StatusOr<std::vector<std::unique_ptr<const ResolvedSetOperationItem>>>
    BuildSetOperationItems(
        absl::Span<ResolvedGraphWithNameList<const ResolvedGraphLinearScan>>
            resolved_inputs,
        const ResolvedColumnList& final_column_list,
        const IndexMapper& index_mapper);

    // Wraps a type cast scan to the last scan of graph linear scan if needed
    // and then wraps a null-padded scan to the last scan of graph linear scan
    // if needed.
    //
    // `ast_location` is the AST location for error message.
    // `resolved_input` includes the resolved graph linear scan and the
    //     corresponding namelists.
    // `query_idx` is the index of the query in the set operation.
    // `final_column_list` is the output column list.
    // `index_mapper` is two way mapping between columns from each input query
    //     and the final output columns.
    absl::StatusOr<std::unique_ptr<const ResolvedGraphLinearScan>>
    MaybeTypeCastAndWrapWithNullPaddedProjectScan(
        const ASTSelect* ast_location,
        ResolvedGraphWithNameList<const ResolvedGraphLinearScan> resolved_input,
        int query_idx, const ResolvedColumnList& final_column_list,
        const IndexMapper& index_mapper);

    // Returns the final column names of the set operation based on the
    // `column_propagation_mode`.
    //
    // For LEFT / STRICT mode, returns the column names from the first input.
    // For INNER mode, returns the intersection of column names from all inputs.
    // For FULL mode, returns union of column names from all inputs.
    //
    // This function also validates that:
    // - If `column_propagation_mode` is INNER, the intersection of column names
    // from all inputs must not be empty.
    // - If `column_propagation_mode` is STRICT, all column name set from
    // different inputs should be the same.
    absl::StatusOr<std::vector<IdString>> GetFinalColumnNames(
        absl::Span<
            const ResolvedGraphWithNameList<const ResolvedGraphLinearScan>>
            resolved_inputs,
        absl::Span<IndexedColumnNames> indexed_column_names_list,
        ResolvedSetOperationScan::SetOperationColumnPropagationMode
            column_propagation_mode,
        const ASTNode* error_location);

   private:
    // Converts a list of resolved inputs to a list of IndexedColumnNames.
    static std::vector<IndexedColumnNames> ToIndexedColumnNamesList(
        absl::Span<
            const ResolvedGraphWithNameList<const ResolvedGraphLinearScan>>
            resolved_inputs);

    const ASTGqlSetOperation& node_;
    GraphTableQueryResolver* graph_resolver_;
  };

  const ASTGraphNodePattern empty_node_pattern_;
  Catalog* const catalog_;
  Resolver* const resolver_;

  // Keep track of the number of element variables declared in <graph_table>.
  // Used to create distinct internal element variable alias when required.
  int element_var_count_ = 0;

  // Resolved property graph from the input graph table.
  const PropertyGraph* graph_ = nullptr;

  // Identifier to resolved label expression map.
  IdStringHashMapCase<std::unique_ptr<const ResolvedGraphLabelExpr>>
      label_expr_map_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_GRAPH_QUERY_RESOLVER_H_
