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

// GoogleSQL Algebrizer
// Defines the transformation from a resolved abstract syntax tree (AST) into a
// relational algebra tree.
//
// See (broken link) for more information.

#ifndef GOOGLESQL_REFERENCE_IMPL_ALGEBRIZER_H_
#define GOOGLESQL_REFERENCE_IMPL_ALGEBRIZER_H_

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <stack>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/public/anonymization_utils.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/type.h"
#include "googlesql/public/types/measure_type.h"
#include "googlesql/reference_impl/evaluation.h"
#include "googlesql/reference_impl/function.h"
#include "googlesql/reference_impl/measure_evaluation.h"
#include "googlesql/reference_impl/operator.h"
#include "googlesql/reference_impl/parameters.h"
#include "googlesql/reference_impl/tuple.h"
#include "googlesql/reference_impl/tvf_evaluation.h"
#include "googlesql/reference_impl/uda_evaluation.h"
#include "googlesql/reference_impl/variable_generator.h"
#include "googlesql/reference_impl/variable_id.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_collation.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "gtest/gtest_prod.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql {

struct AlgebrizerOptions {
  // If true, tables are represented as arrays, and the values are obtained
  // during evaluation through EvaluationContext. If false, tables are
  // represented using EvaluatorTableIterators.
  bool use_arrays_for_tables = false;

  // If true, the algebrizer arranges so that, e.g, proto_col.a and proto_col.b
  // reads both 'a' and 'b' from 'proto_col' in one shot, instead of
  // potentially deserializing it twice.
  bool consolidate_proto_field_accesses = false;

  // If true, the algebrizer attempts to use a hash join instead of a nested
  // loop join when the join condition is amenable, or when there is a
  // compatible filter immediately above the join.
  bool allow_hash_join = false;

  // If true, the algebrizer attempts to use a single operator for ORDER BY
  // LIMIT instead of LimitOp(SortOp), which saves memory.
  bool allow_order_by_limit_operator = false;

  // If true, the algebrizer attempts to push down filters into the highest
  // ancestor node that is either a join or an EvaluatorTableScanOp node. In the
  // latter case, the filter remains in its original location because
  // EvaluatorTableIterator does not have to honor the filter.
  bool push_down_filters = false;

  // True to inline references to WITH entries which are referenced at most
  // once. This causes rows in a WITH entry referenced only once to be evaluated
  // only when necessary to determine the primary query result, while also
  // avoiding the evaluation of WITH entries without any references altogether.
  //
  // If false, all WITH entry tables, whether referenced or not, will be
  // evaluated up front, and the result stored in an in-memory array, which will
  // then be dereferenced when the WITH entry is referenced.
  bool inline_with_entries = false;

  // Used to allocate column ids. New column ids are currently allocated when
  // algebrizing measure expressions.
  std::shared_ptr<int> max_seen_column_id;
};

struct AnonymizationOptions {
  std::optional<Value> epsilon;                      // double Value
  std::optional<Value> delta;                        // double Value
  std::optional<Value> max_groups_contributed;       // int64 Value
  std::optional<Value> max_rows_contributed;         // int64 Value
  std::optional<Value> group_selection_threshold;    // int64 Value
  std::optional<Value> min_privacy_units_per_group;  // int64 Value
  std::optional<Value> group_selection_strategy;     // enum Value
  anonymization::FunctionEpsilonAssigner* epsilon_assigner = nullptr;
};

class Algebrizer {
 public:
  Algebrizer(const Algebrizer&) = delete;
  Algebrizer& operator=(const Algebrizer&) = delete;

  // Returns the statement kinds that are supported by AlgebrizeStatement().
  static std::set<ResolvedNodeKind> GetSupportedStatementKinds() {
    return {RESOLVED_QUERY_STMT, RESOLVED_DELETE_STMT, RESOLVED_UPDATE_STMT,
            RESOLVED_INSERT_STMT, RESOLVED_MULTI_STMT};
  }

  // Algebrize the resolved AST for a SQL statement. 'parameters' returns either
  // a map of lower-cased parameter names appearing in the statement to variable
  // IDs, if the statement uses named parameters, or else a list of variable IDs
  // for positional parameters. 'column_map' returns a map of lower-cased column
  // parameter names to variable IDs.
  //
  // For query statements, 'output' is only valid for as long as 'type_factory'
  // is valid.
  //
  // For DML statements:
  //  * 'output' is only valid for as long as 'type_factory' and 'ast_root' are
  //     valid. Also, 'output' is always a DMLValueExpr.
  //
  // For CREATE TABLE AS SELECT statements, algebrizes the query that would
  // go into the newly created table.
  //
  // On output, <system_variables_map> is populated with a
  // name path=>variable id map for each system variable used in the
  // statement/expression.
  static absl::Status AlgebrizeStatement(
      const LanguageOptions& language_options,
      const AlgebrizerOptions& algebrizer_options, TypeFactory* type_factory,
      const ResolvedStatement* ast_root, std::unique_ptr<ValueExpr>* output,
      Parameters* parameters, ParameterMap* column_map,
      SystemVariablesAlgebrizerMap* system_variables_map);

  // Same as above, but only supports query statements and returns a
  // RelationalOp, which can be used to construct an iterator over the result
  // set (to avoid storing the entire result in memory). Populates
  // 'output_column_list' with the output columns of the query,
  // 'output_column_names' with the user-visible names, and
  // 'output_column_variables' with the corresponding VariableIds for the
  // TupleIterator returned by 'output'.
  static absl::Status AlgebrizeQueryStatementAsRelation(
      const LanguageOptions& language_options,
      const AlgebrizerOptions& algebrizer_options, TypeFactory* type_factory,
      const ResolvedQueryStmt* ast_root, ResolvedColumnList* output_column_list,
      std::unique_ptr<RelationalOp>* output,
      std::vector<std::string>* output_column_names,
      std::vector<VariableId>* output_column_variables, Parameters* parameters,
      ParameterMap* column_map,
      SystemVariablesAlgebrizerMap* system_variables_map);

  // Similar to AlgebrizeStatement(), but accepts any ResolvedExpr. 'output' is
  // only valid for as long as 'type_factory' is valid.
  static absl::Status AlgebrizeExpression(
      const LanguageOptions& language_options,
      const AlgebrizerOptions& algebrizer_options, TypeFactory* type_factory,
      const ResolvedExpr* ast_root, std::unique_ptr<ValueExpr>* output,
      Parameters* parameters, ParameterMap* column_map,
      SystemVariablesAlgebrizerMap* system_variables_map);

 private:
  friend class AlgebrizerTestBase;
  friend class AlgebrizerTestSelectColumn;
  friend class AlgebrizerTestFunctions;
  friend class AlgebrizerTestFilters;
  friend class AlgebrizerTestGroupingAggregation;
  friend class BarrierScanAlgebrizerTest;
  FRIEND_TEST(ExpressionAlgebrizerTest, Parameters);
  FRIEND_TEST(ExpressionAlgebrizerTest, PositionalParametersInExpressions);
  FRIEND_TEST(StatementAlgebrizerTest, SingleRowScan);
  FRIEND_TEST(StatementAlgebrizerTest, SingleRowSelect);
  FRIEND_TEST(StatementAlgebrizerTest, TableScanAsArrayType);
  FRIEND_TEST(AlgebrizerTestBase, TableScanAsIterator);
  FRIEND_TEST(StatementAlgebrizerTest, TableSelectAll);
  FRIEND_TEST(StatementAlgebrizerTestSelectColumn, SelectColumn);
  FRIEND_TEST(AlgebrizerTestFunctions, Functions);
  FRIEND_TEST(AlgebrizerTestFunctions, SelectFunctions);
  FRIEND_TEST(AlgebrizerTestFilters, Filters);
  FRIEND_TEST(StatementAlgebrizerTest, CrossApply);
  FRIEND_TEST(AlgebrizerTestJoins, InnerJoin);
  FRIEND_TEST(AlgebrizerTestJoins, CorrelatedInnerJoin);
  FRIEND_TEST(AlgebrizerTestGroupingAggregation, GroupByAny);
  FRIEND_TEST(AlgebrizerTestGroupingAggregation, GroupByAvg);
  FRIEND_TEST(AlgebrizerTestGroupingAggregation, GroupByCountStar);
  FRIEND_TEST(AlgebrizerTestGroupingAggregation, GroupByCountColumn);
  FRIEND_TEST(AlgebrizerTestGroupingAggregation, GroupByMax);
  FRIEND_TEST(AlgebrizerTestGroupingAggregation, GroupByMin);
  FRIEND_TEST(AlgebrizerTestGroupingAggregation, GroupBySum);
  FRIEND_TEST(StatementAlgebrizerTest, TVF);

  struct MatchRecognizeQueryParams {
    std::variant<std::vector<std::string>, std::vector<int>>
        query_parameter_keys;
    std::vector<std::unique_ptr<ValueExpr>> query_parameter_values;
  };

  Algebrizer(const LanguageOptions& options,
             const AlgebrizerOptions& algebrizer_options,
             TypeFactory* type_factory, Parameters* parameters,
             ParameterMap* column_map,
             SystemVariablesAlgebrizerMap* system_variables_map);

  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeCast(
      const ResolvedCast* cast);

  absl::StatusOr<std::unique_ptr<InlineLambdaExpr>> AlgebrizeLambda(
      const ResolvedInlineLambda* inline_lambda);

  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeFunctionCallWithLambda(
      const ResolvedFunctionCall* function_call);

  // A helper for AlgebrizeFunctionCall that algebrizes the argument exprs.
  absl::StatusOr<std::vector<std::unique_ptr<AlgebraArg>>>
  AlgebrizeFunctionArgs(
  const ResolvedFunctionCall* function_call);

  // AlgebrizeFunctionCall logic that is specific to SQL functions that provide
  // an evaluator factory to supply an evaluator.
  absl::StatusOr<std::unique_ptr<ValueExpr>>
  AlgebrizeFunctionCallWithAlgebriedArgsAndEvalFactory(
      const ResolvedFunctionCall* function_call,
      std::vector<std::unique_ptr<AlgebraArg>> arguments);

  // AlgebrizeFunctionCall logic that is specific to SQL functions.
  absl::StatusOr<std::unique_ptr<ValueExpr>>
  AlgebrizeSqlFunctionCallWithAlgebrizedArgs(
      const ResolvedFunctionCall* function_call,
      std::vector<std::unique_ptr<AlgebraArg>> arguments);

  // AlgebrizeFunctionCall logic for builtin functions (excepting those with an
  // evaluator factory),
  absl::StatusOr<std::unique_ptr<ValueExpr>>
  AlgebrizeBuiltinFunctionCallWithAlgebrizedArguments(
      const ResolvedFunctionCall* function_call,
  std::vector<std::unique_ptr<AlgebraArg>> arguments);

  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeFunctionCall(
      const ResolvedFunctionCall* function_call);

  absl::StatusOr<std::unique_ptr<NewStructExpr>> AlgebrizeMakeStruct(
      const ResolvedMakeStruct* make_struct);

  absl::StatusOr<std::unique_ptr<FieldValueExpr>> AlgebrizeGetStructField(
      const ResolvedGetStructField* get_struct_field);

  // helper method to algebrize a list of expressions into value expressions,
  // stored into `output_value_list`, which will be cleared first if not empty.
  absl::Status AlgebrizeExpressionList(
      absl::Span<const std::unique_ptr<const ResolvedExpr>> expr_list,
      std::vector<std::unique_ptr<ValueExpr>>& output_value_list);

  absl::StatusOr<std::unique_ptr<GraphGetElementPropertyExpr>>
  AlgebrizeGraphGetElementProperty(
      const ResolvedGraphGetElementProperty* get_graph_element_property);

  absl::StatusOr<std::unique_ptr<NewGraphElementExpr>>
  AlgebrizeGraphMakeElement(const ResolvedGraphMakeElement* make_graph_element);

  absl::StatusOr<std::unique_ptr<ScalarFunctionCallExpr>> AlgebrizeGetJsonField(
      const ResolvedGetJsonField* get_json_field);

  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeGetProtoField(
      const ResolvedGetProtoField* get_proto_field);

  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeFlatten(
      const ResolvedFlatten* flatten);

  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeFlattenedArg(
      const ResolvedFlattenedArg* flattened_arg);

  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeLiteral(
      const ResolvedLiteral* literal);
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeConstant(
      const ResolvedConstant* resolved_constant);
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeColumnRef(
      const ResolvedColumnRef* column_ref);
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeCatalogColumnRef(
      const ResolvedCatalogColumnRef* column_ref);
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeExpressionColumn(
      const ResolvedExpressionColumn* expr_column);
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeGetProtoOneof(
      const ResolvedGetProtoOneof* get_proto_oneof);
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeSystemVariable(
      const ResolvedSystemVariable* system_variable);
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeParameter(
      const ResolvedParameter* parameter);
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeArgumentRef(
      const ResolvedArgumentRef* argument_ref);
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeMakeProto(
      const ResolvedMakeProto* make_proto);
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeDMLDefault(
      const ResolvedDMLDefault* dml_default);
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeFilterField(
      const ResolvedFilterField* filter_fields);
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeReplaceField(
      const ResolvedReplaceField* replace_fields);
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeUpdateConstructor(
      const ResolvedUpdateConstructor* update_constructor);

  // Helper for AlgebrizeGetProtoField() for the case where we are getting a
  // proto field of an expression of the form
  // <column_or_param_expr>.<path>. <column_or_param> must be a
  // ResolvedColumnRef, a ResolvedParameter, or a ResolvedExpressionColumn.
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeGetProtoFieldOfPath(
      const ResolvedExpr* column_or_param_expr,
      absl::Span<const std::variant<const ResolvedGetProtoField*,
                                    const ResolvedGetStructField*>>
          path);

  // Algebrize specific expressions.
  // If `order_by_keys` is non-empty then use it as the order by. If `expr`
  // specifies a non empty `order_by_item_list` then `order_by_keys` must be
  // empty.
  // Currently used by ArrayAggregate to order the aggregate in array order.
  // If `measure_expr` is not null, then this is a measure aggregate function
  // call.
  absl::StatusOr<std::unique_ptr<AggregateArg>>
  AlgebrizeAggregateFnWithAlgebrizedArguments(
      const VariableId& variable,
      std::optional<AnonymizationOptions> anonymization_options,
      std::unique_ptr<ValueExpr> filter, const ResolvedExpr* expr,
      std::vector<std::unique_ptr<ValueExpr>> arguments,
      std::vector<std::unique_ptr<KeyArg>> inner_grouping_keys,
      std::vector<std::unique_ptr<AggregateArg>> inner_aggregators,
      const VariableId& side_effects_variable = VariableId(),
      std::vector<std::unique_ptr<KeyArg>> order_by_keys = {},
      const ResolvedExpr* measure_expr = nullptr);
  absl::StatusOr<std::unique_ptr<AggregateArg>> AlgebrizeAggregateFn(
      const VariableId& variable,
      std::optional<AnonymizationOptions> anonymization_options,
      std::unique_ptr<ValueExpr> filter, const ResolvedExpr* expr,
      const VariableId& side_effects_variable);
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeSubqueryExpr(
      const ResolvedSubqueryExpr* subquery_expr);
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeWithExpr(
      const ResolvedWithExpr* with_expr);
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeInArray(
      std::unique_ptr<ValueExpr> in_value,
      std::unique_ptr<ValueExpr> array_value,
      const ResolvedCollation& collation);

  // Adds a variable for a UDA argument. Requires that `argument_name` does not
  // already have a variable.
  absl::StatusOr<VariableId> AddUdaArgumentVariable(
      absl::string_view argument_name);

  // Method to algebrize a UDA or measure call. Much of the algebrization logic
  // is shared between UDAs and measures.
  static absl::StatusOr<std::unique_ptr<RelationalOp>>
  AlgebrizeUdaOrMeasureCall(
      const AnonymizationOptions* anonymization_options,
      const ResolvedExpr& function_expr,
      const std::vector<const ResolvedExpr*>& aggregate_exprs,
      const ResolvedColumnList& aggregate_expr_columns,
      std::function<absl::StatusOr<std::unique_ptr<RelationalOp>>(Algebrizer&)>
          create_input_op,
      const LanguageOptions& language_options,
      const AlgebrizerOptions& algebrizer_options, TypeFactory* type_factory);

  absl::StatusOr<std::unique_ptr<RelationalOp>> CreateInputOpForUdaCall(
      absl::Span<const UdaArgumentInfo> argument_infos);
  absl::StatusOr<std::unique_ptr<RelationalOp>> CreateInputOpForMeasureCall(
      const MeasureType* measure_type);

  static absl::Status AlgebrizeTvfCall(
      const ResolvedScan& resolved_body, bool is_value_table,
      const LanguageOptions& language_options,
      const AlgebrizerOptions& algebrizer_options, TypeFactory* type_factory,
      std::vector<TvfArgumentInfo> arg_infos,
      std::vector<TableValuedFunction::TvfEvaluatorArg> arg_evaluators,
      EvaluationContext* context,
      std::unique_ptr<RelationalOp>& out_algebrized_body,
      std::vector<int>& out_column_indices);

  // We hit this only when algebrizing the body of a TVF during a call.
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeRelationArgumentScan(
      const ResolvedRelationArgumentScan* arg_scan);

  // Adds a TVF argument for this child algebrizer.
  absl::Status AddTvfArgument(TableValuedFunction::TvfEvaluatorArg arg,
                              EvaluationContext* context);

  absl::StatusOr<std::unique_ptr<AggregateFunctionBody>>
  CreateCallbackUserDefinedAggregateFn(
      const ResolvedNonScalarFunctionCallBase* aggregate_function,
      const AnonymizationOptions* anonymization_options);

  absl::StatusOr<std::unique_ptr<AggregateFunctionBody>>
  CreateTemplatedUserDefinedAggregateFn(
      const ResolvedNonScalarFunctionCallBase* aggregate_function,
      std::vector<std::unique_ptr<ValueExpr>>& arguments,
      const AnonymizationOptions* anonymization_options);

  absl::StatusOr<std::unique_ptr<AggregateFunctionBody>>
  CreateNonTemplatedUserDefinedAggregateFn(
      const ResolvedNonScalarFunctionCallBase* aggregate_function,
      std::vector<std::unique_ptr<ValueExpr>>& arguments,
      const AnonymizationOptions* anonymization_options);

  absl::StatusOr<std::unique_ptr<AggregateFunctionBody>>
  CreateUserDefinedAggregateFn(
      const ResolvedNonScalarFunctionCallBase* aggregate_function,
      std::vector<std::unique_ptr<ValueExpr>>& arguments,
      const AnonymizationOptions* anonymization_options);

  // Creates an AggregateFunctionBody for an AGG(measure_column) function call.
  absl::StatusOr<std::unique_ptr<AggregateFunctionBody>>
  CreateMeasureAggregateFn(
      const ResolvedNonScalarFunctionCallBase* aggregate_function,
      std::vector<std::unique_ptr<ValueExpr>>& arguments,
      const ResolvedExpr* measure_expr);

  // TODO: Remove the special collation logic in this function.
  absl::StatusOr<std::unique_ptr<ValueExpr>>
  AlgebrizeScalarArrayFunctionWithCollation(
      FunctionKind kind, const Type* output_type,
      absl::string_view function_name,
      std::vector<std::unique_ptr<ValueExpr>> args,
      absl::Span<const ResolvedCollation> collation_list);

  absl::StatusOr<std::unique_ptr<ValueExpr>>
  AlgebrizeScalarArrayFunctionWithCollation(
      FunctionKind kind, const Type* output_type,
      absl::string_view function_name,
      std::vector<std::unique_ptr<AlgebraArg>> converted_arguments,
      absl::Span<const ResolvedCollation> collation_list);

  // Algebrizes IN, LIKE ANY, or LIKE ALL when the rhs is a subquery.
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeInLikeAnyLikeAllRelation(
      std::unique_ptr<ValueExpr> lhs,
      ResolvedSubqueryExpr::SubqueryType subquery_type,
      const VariableId& haystack_var,
      std::unique_ptr<RelationalOp> haystack_rel,
      const ResolvedCollation& collation);

  // Wrapper around AlgebrizeExpression() for use on standalone expressions.
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeStandaloneExpression(
      const ResolvedExpr* expr);

  // Algebrize a resolved expression. For aggregate function expressions, call
  // AlgebrizeAggregateFn.
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeExpression(
      const ResolvedExpr* expr);

  // Wraps a conjunct from a filter with some associated information.
  struct FilterConjunctInfo {
    // Describes 'conjunct'.
    enum Kind {
      kLE,  // Includes both < and <=
      kGE,  // Include both > and >=
      kEquals,
      kBetween,
      kInArray,
      kOther
    };

    static absl::StatusOr<std::unique_ptr<FilterConjunctInfo>> Create(
        const ResolvedExpr* conjunct);

    Kind kind = kOther;

    // A filter conjunct.
    const ResolvedExpr* conjunct = nullptr;

    // True if 'conjunct' is known to be non-volatile (per
    // FunctionEnums::VOLATILE).
    bool is_non_volatile = false;

    // All the columns referenced by 'conjunct'.
    absl::flat_hash_set<ResolvedColumn> referenced_columns;

    // If 'kind' is a ResolvedFunctionCall, these are the arguments.
    std::vector<const ResolvedExpr*> arguments;

    // The columns referenced by each argument. Corresponds positionally to
    // 'arguments'.
    std::vector<absl::flat_hash_set<ResolvedColumn>> argument_columns;

    // True if 'conjunct' is guaranteed to be satisfied by some node in the
    // algebrized tree. For example, consider the query:
    //   select * from KeyValue, KeyValue2 where KeyValue.Key = KeyValue2.Key
    // The naive algebrization looks like:
    //   FilterOp(KeyValue.Key = KeyValue2.Key,
    //   + JoinOp(INNER,
    //     + left_input: EvaluatorTableScan(KeyValue),
    //     + right_input: EvaluatorTableScan(KeyValue2)))
    // A better tree (for evaluation) is:
    //   JoinOp(INNER,
    //   + hash_join_equality_left_exprs: KeyValue.Key
    //   + hash_join_equality_right_exprs: KeyValue2.Key
    //   + left_input: EvaluatorTableScan(KeyValue)
    //   + right_input: EvaluatorTableScan(KeyValue2))
    // (The second plan is better because it avoids materializing all
    // combinations of tuples KeyValue and KeyValue2.) To implement this
    // approach, the algebrizer looks at the conjunct KeyValue.Key =
    // KeyValue2.Key while processing the join, notices that it can be used for
    // the hash join, and then marks it is as redundant so that the
    // algebrization of the ResolvedFilterScan corresponding to the WHERE clause
    // will just be the input scan (which is the JoinOp).
    bool redundant = false;
  };

  // Adds all the conjuncts in 'expr' to 'conjunct_infos'.
  static absl::Status AddFilterConjunctsTo(
      const ResolvedExpr* expr,
      std::vector<std::unique_ptr<FilterConjunctInfo>>* conjunct_infos);

  // Algebrize specific relational inputs. We only support filter pushdown for
  // certain scan types. For those, there is an 'active_conjuncts' argument
  // (which represents a stack) that contains the applicable FilterConjunctInfos
  // that the operator can choose to incorporate or push down. None of those
  // FilterConjunctInfos may be marked as redundant when these methods are
  // called. For example, consider this query:
  //   SELECT key FROM (SELECT key, (key + 10) AS k2 FROM KeyValue)
  //              WHERE key > 10 AND k2 > 11
  // The resolved AST looks like:
  //   ResolvedFilterScan(no active FilterConjunctInfos,
  //   + ResolvedProjectScan(active FilterConjunctInfos = {key > 10, k2 > 11},
  //     + ResolvedTableScan(active FilterConjunctInfos = {key > 10})))
  // The algebrized tree will ultimately push down the filters as far as they
  // can go.
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeSingleRowScan(
      const ResolvedSingleRowScan* single_row_scan);
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeJoinScan(
      const ResolvedJoinScan* join_scan,
      std::vector<FilterConjunctInfo*>* active_conjuncts);
  // Returns an algebrized scan with the given active conjuncts.
  using ScanAlgebrizerCallback =
      std::function<absl::StatusOr<std::unique_ptr<RelationalOp>>(
          std::vector<FilterConjunctInfo*>*)>;
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeJoinScanInternal(
      JoinOp::JoinKind join_kind,
      const ResolvedExpr* join_expr,  // May be NULL
      const ResolvedScan* left_scan,
      const std::vector<ResolvedColumn>& right_output_column_list,
      const ScanAlgebrizerCallback& right_scan_algebrizer_cb,
      std::vector<FilterConjunctInfo*>* active_conjuncts);
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeFilterScan(
      const ResolvedFilterScan* filter_scan,
      std::vector<FilterConjunctInfo*>* active_conjuncts);
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeFilterScanInternal(
      const ResolvedExpr* filter_expr,
      const ScanAlgebrizerCallback& scan_algebrizer_cb,
      std::vector<FilterConjunctInfo*>* active_conjuncts);
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeSampleScan(
      const ResolvedSampleScan* sample_scan,
      std::vector<FilterConjunctInfo*>* active_conjuncts);
  absl::StatusOr<std::unique_ptr<AggregateOp>> AlgebrizeAggregateScan(
      const ResolvedAggregateScan* aggregate_scan);
  absl::StatusOr<std::unique_ptr<AggregateOp>> AlgebrizePivotScan(
      const ResolvedPivotScan* pivot_scan);
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeUnpivotScan(
      const ResolvedUnpivotScan* unpivot_scan);
  absl::StatusOr<UnionAllOp::Input> AlgebrizeUnpivotArg(
      const ResolvedUnpivotScan* unpivot_scan, const ExprArg& input,
      int arg_index);
  absl::StatusOr<std::unique_ptr<FilterOp>> AlgebrizeNullFilterForUnpivotScan(
      const ResolvedUnpivotScan* unpivot_scan,
      std::unique_ptr<RelationalOp> input);
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeMatchRecognizeScan(
      const ResolvedMatchRecognizeScan* match_recognize_scan);
  absl::StatusOr<MatchRecognizeQueryParams> GetMatchRecognizeQueryParams(
      const ResolvedMatchRecognizeScan& scan);
  absl::StatusOr<std::unique_ptr<AggregateArg>> AlgebrizeGroupingCall(
      const ResolvedGroupingCall* grouping_call,
      std::optional<AnonymizationOptions> anonymization_options,
      absl::flat_hash_set<ResolvedColumn>& output_columns,
      absl::flat_hash_map<std::string, int>& key_to_index_map);
  absl::StatusOr<std::unique_ptr<AggregateOp>> AlgebrizeAggregateScanBase(
      const ResolvedAggregateScanBase* aggregate_scan,
      std::optional<AnonymizationOptions> anonymization_options);
  absl::StatusOr<std::unique_ptr<RelationalOp>>
  AlgebrizeAnonymizedAggregateScan(
      const ResolvedAnonymizedAggregateScan* aggregate_scan);
  absl::StatusOr<std::unique_ptr<RelationalOp>>
  AlgebrizeDifferentialPrivacyAggregateScan(
      const ResolvedDifferentialPrivacyAggregateScan* aggregate_scan);
  absl::StatusOr<std::unique_ptr<RelationalOp>>
  AlgebrizeAggregationThresholdAggregateScan(
      const ResolvedAggregationThresholdAggregateScan* aggregate_scan);
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeSetOperationScan(
      const ResolvedSetOperationScan* set_scan);
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeUnionScan(
      const ResolvedSetOperationScan* set_scan);
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeExceptIntersectScan(
      const ResolvedSetOperationScan* set_scan);
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeProjectScan(
      const ResolvedProjectScan* resolved_project,
      std::vector<FilterConjunctInfo*>* active_conjuncts);
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeProjectScanInternal(
      const ResolvedColumnList& column_list,
      absl::Span<const std::unique_ptr<const ResolvedComputedColumn>> expr_list,
      const ResolvedScan* input_scan, bool is_ordered,
      std::vector<FilterConjunctInfo*>* active_conjuncts);
  // 'limit' and 'offset' may both be NULL or both non-NULL.
  absl::StatusOr<std::unique_ptr<SortOp>> AlgebrizeOrderByScan(
      const ResolvedOrderByScan* scan, std::unique_ptr<ValueExpr> limit,
      std::unique_ptr<ValueExpr> offset);
  absl::StatusOr<std::unique_ptr<SortOp>> AlgebrizeOrderByScan(
      const ResolvedOrderByScan* scan) {
    return AlgebrizeOrderByScan(scan, /*limit=*/nullptr, /*offset=*/nullptr);
  }
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeArrayScan(
      const ResolvedArrayScan* array_scan,
      std::vector<FilterConjunctInfo*>* active_conjuncts);
  // Algebrizes 'array_scan', ignoring any input scan or join condition.
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeArrayScanWithoutJoin(
      const ResolvedArrayScan* array_scan,
      std::vector<FilterConjunctInfo*>* active_conjuncts);
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeLimitOffsetScan(
      const ResolvedLimitOffsetScan* scan);
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeWithScan(
      const ResolvedWithScan* scan);
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeWithRefScan(
      const ResolvedWithRefScan* scan);
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeGroupRowsScan(
      const ResolvedGroupRowsScan* group_rows_scan);
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeTVFScan(
      const ResolvedTVFScan* tvf_scan);
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeTableScan(
      const ResolvedTableScan* table_scan,
      std::vector<FilterConjunctInfo*>* active_conjuncts);
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizePipeIfScan(
      const ResolvedPipeIfScan* scan);

  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeGraphTableScan(
      const ResolvedGraphTableScan* graph_table_scan,
      std::vector<FilterConjunctInfo*>* active_conjuncts);
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeGraphScan(
      const ResolvedGraphScan* graph_scan,
      std::vector<FilterConjunctInfo*>* active_conjuncts);
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeGraphCompositeScan(
      const ResolvedScan* graph_composite_scan, int composite_query_index,
      std::vector<FilterConjunctInfo*>* active_conjuncts);
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeGraphLinearScan(
      const ResolvedGraphLinearScan* graph_linear_scan,
      std::vector<FilterConjunctInfo*>* active_conjuncts);
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeGraphRefScan(
      const ResolvedGraphRefScan* graph_ref_scan);
  // Algebrize paths in `path_list` starting from `from_index`.
  // Requires: `from_index` < `path_list`.size().
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeGraphPathScanList(
      const std::vector<std::unique_ptr<const ResolvedGraphPathScan>>&
          path_list,
      int from_index, std::vector<FilterConjunctInfo*>* active_conjuncts);

  // Represents a graph path context.
  struct GraphPathContext {
    // Path mode. Defaults to WALK.
    ResolvedGraphPathMode::PathMode path_mode = ResolvedGraphPathMode::WALK;

    // Path search prefix type. Defaults to PATH_SEARCH_PREFIX_TYPE_UNSPECIFIED
    // if not set.
    ResolvedGraphPathSearchPrefix::PathSearchPrefixType prefix_type =
        ResolvedGraphPathSearchPrefix::PATH_SEARCH_PREFIX_TYPE_UNSPECIFIED;

    // The count of the path that matches the search prefix. Defaults to nullptr
    // if not set. If set, `prefix_type` must not be
    // PATH_SEARCH_PREFIX_TYPE_UNSPECIFIED.
    const ResolvedExpr* path_count = nullptr;
  };
  absl::StatusOr<std::unique_ptr<GraphPathPrefixInfo>>
  CreateGraphPathPrefixInfo(const GraphPathContext& path_context);

  absl::StatusOr<std::unique_ptr<RelationalOp>>
  AlgebrizeGraphQuantifiedPathScan(
      const ResolvedGraphPathScan* path_scan,
      std::unique_ptr<RelationalOp> path_primary_op,
      std::vector<FilterConjunctInfo*>* active_conjuncts);
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeGraphPathPrimaryScan(
      const ResolvedGraphPathScan* path_scan,
      std::vector<FilterConjunctInfo*>* active_conjuncts);
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeGraphPathScan(
      const ResolvedGraphPathScan* path_scan,
      std::vector<FilterConjunctInfo*>* active_conjuncts);
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeGraphNodeScan(
      const ResolvedGraphElementScan* element_scan,
      std::vector<FilterConjunctInfo*>* active_conjuncts) {
    return AlgebrizeGraphElementScan(element_scan, active_conjuncts);
  }
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeGraphEdgeScan(
      const ResolvedGraphElementScan* element_scan,
      std::vector<FilterConjunctInfo*>* active_conjuncts) {
    return AlgebrizeGraphElementScan(element_scan, active_conjuncts);
  }
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeGraphElementScan(
      const ResolvedGraphElementScan* element_scan,
      std::vector<FilterConjunctInfo*>* active_conjuncts);
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeGraphCostExpr(
      const ResolvedExpr* cost_expr);
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeGraphCallScan(
      const ResolvedGraphCallScan* call_scan,
      std::vector<FilterConjunctInfo*>* active_conjuncts);

  // Algebrizes a scan of <graph_element_table> that computes expressions
  // with <graph_element_type> that are represented by the variable
  // <element_variable_id>.
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeGraphElementTableScan(
      VariableId element_variable_id,
      const GraphElementType* graph_element_type,
      const GraphElementTable* graph_element_table);

  // Algebrizes the <node_reference> from <edge_table> and returns the output
  // relational op as the result. <node_reference_vars> outputs the node columns
  // and variable mapping.
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeGraphNodeReference(
      const GraphEdgeTable* edge_table,
      const GraphNodeTableReference& node_reference,
      std::unique_ptr<RelationalOp> input_scan,
      absl::flat_hash_map<const Column*, VariableId>& node_reference_vars);

  // Algebrizes a relational op that projects <base_table> to <element_type>.
  // <base_table> is a scan of the underlying table of <element_table>.
  // <source_node_vars> and <dest_node_vars> contains the variable mappings
  // required for edge generation; they're only set when this is computing
  // edges.
  absl::StatusOr<std::unique_ptr<RelationalOp>> ComputeGraphElements(
      VariableId element_variable_id, std::unique_ptr<RelationalOp> base_table,
      const GraphElementTable* element_table,
      const GraphElementType* element_type,
      const absl::flat_hash_map<const Column*, VariableId>* source_node_vars =
          nullptr,
      const absl::flat_hash_map<const Column*, VariableId>* dest_node_vars =
          nullptr);

  // Algebrizes the dynamic properties expression.
  // If `element_type` is dynamic, returns a JSON value expression. Otherwise,
  // returns nullptr.
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeDynamicProperties(
      const GraphElementType* element_type, const ResolvedExpr* resolved_expr);

  // Algebrizes the dynamic label expression.
  // If `resolved_expr` is non-null, returns a STRING value expression.
  // Otherwise, returns nullptr.
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeDynamicLabel(
      const ResolvedExpr* resolved_expr);

  using ConjunctsByGraphSubpaths =
      absl::flat_hash_map<const ResolvedGraphPathScanBase*,
                          std::vector<FilterConjunctInfo*>>;
  // Return each child subpath scan's active filter conjuncts (in reversed order
  // as a stack) to be pushed down from a path scan. Based on how a predicate's
  // referenced ResolvedColumns overlap with child scans there are 3
  // cases:
  // 1. Predicates that has intersection with exactly one child scan, gets
  // passed to that child scan.
  // 2. Predicates that has intersection with no child scan, gets passed to all
  // child scans.
  // 3. Predicates that has intersection with >1 child scan stays at the
  // path level.
  static absl::StatusOr<ConjunctsByGraphSubpaths>
  GetPushableActiveConjunctsByChildScans(
      absl::Span<const std::unique_ptr<const ResolvedGraphPathScanBase>>
          child_scans,
      const std::vector<FilterConjunctInfo*>& active_conjuncts);

  // Algebrizes ArrayAggregate which is similar to a subquery of an aggregate
  // scan of an array scan in that it can access specific fields or expressions
  // on the array elements, but respects the order in the input array.
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeArrayAggregate(
      const ResolvedArrayAggregate* array_aggregate);

  // Algebrizes GraphIsLabeledPredicate
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeGraphIsLabeledPredicate(
      const ResolvedGraphIsLabeledPredicate* predicate);

  // Maps a ResolvedColumn from a table scan to its corresponding Variable and
  // index in the scan (not the Table).
  using TableScanColumnInfoMap =
      absl::flat_hash_map<ResolvedColumn, std::pair<VariableId, int>>;

  // Algebrizes group_selection_threshold_expr expression of differential
  // privacy nodes.
  absl::StatusOr<std::vector<std::unique_ptr<ValueExpr>>>
  AlgebrizeGroupSelectionThresholdExpression(
      const ResolvedExpr* group_selection_threshold_expr,
      const AnonymizationOptions& anonymization_options);

  // If 'conjunct_info' can be represented using ColumnFilterArgs with the
  // columns in 'column_info_map', appends them to 'and_filters'.
  absl::Status TryAlgebrizeFilterConjunctAsColumnFilterArgs(
      const TableScanColumnInfoMap& column_info_map,
      const FilterConjunctInfo& conjunct_info,
      std::vector<std::unique_ptr<ColumnFilterArg>>* and_filters);

  // Algebrizes the resolved AST for an AnalyticScan. The AnalyticScan is
  // converted to a sequence of AnalyticOp, one per analytic function group.
  // For each analytic function group, a SortOp is also created if it contains
  // partitioning and ordering expressions, even when the input relation has
  // been already sorted by those expressions.
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeAnalyticScan(
      const ResolvedAnalyticScan* analytic_scan);
  // This is the bulk of AlgebrizeAnalyticScan(), but it's shared with
  // AlgebrizeMatchRecognizeScan() which also has a list of analytic function
  // groups.
  // `reverse_order` is used for MATCH_RECOGNIZE, because we want to preserve
  // the order of the input rows from the first group for the downstream
  // PatternMatchOp.
  absl::StatusOr<std::unique_ptr<RelationalOp>>
  AlgebrizeAnalyticFunctionGroupList(
      const ResolvedScan* input_scan,
      absl::Span<const std::unique_ptr<const ResolvedAnalyticFunctionGroup>>
          analytic_function_groups,
      bool reverse_order);

  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeRecursiveScan(
      const ResolvedRecursiveScan* recursive_scan);

  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeRecursiveRefScan(
      const ResolvedRecursiveRefScan* recursive_ref_scan);

  // Returns an AnalyticOp for 'analytic_group'. A SortOp is also created under
  // the AnalyticOp if the partitioning or ordering expressions are not
  // empty. 'input_resolved_columns' contains the input columns including the
  // analytic columns created by the preceding analytic function
  // groups. 'input_is_from_same_analytic_scan' must be true if 'analytic_group'
  // and 'input_relation_op' correspond to the same AnalyticScan resolved AST
  // node.
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeAnalyticFunctionGroup(
      const std::set<ResolvedColumn>& input_resolved_columns,
      const ResolvedAnalyticFunctionGroup* analytic_group,
      std::unique_ptr<RelationalOp> input_relation_op,
      bool input_is_from_same_analytic_scan);

  // Returns 'input_relation_op' if all the partitioning and ordering
  // expressions are correlated column references.
  // Otherwise, creates and returns a SortOp on top of 'input_relation_op' with
  // non-correlated partitioning and ordering expressions as order keys.
  // 'input_resolved_columns' contains the input columns produced by
  // 'input_relation_op'. If 'require_stable_sort' is true, then any SortOp
  // created performs a stable sort over its input.
  absl::StatusOr<std::unique_ptr<RelationalOp>>
  MaybeCreateSortForAnalyticOperator(
      const std::set<ResolvedColumn>& input_resolved_columns,
      const ResolvedWindowPartitioning* partition_by,
      const ResolvedWindowOrdering* order_by,
      std::unique_ptr<RelationalOp> input_relation_op,
      bool require_stable_sort);

  // Converts each ResolvedOrderByItem to a KeyArg.
  // If 'drop_correlated_columns' is true, the output 'order_by_keys' does not
  // include the KeyArg for a ResolvedOrderByItem if it refers to a correlated
  // column.
  // If 'create_new_ids' is true, we create a new VariableId for each KeyArg
  // and update the class member variable 'column_to_variable_' for the
  // referenced column; otherwise, the KeyArg uses the existing VariableId of
  // the referenced column.
  // 'column_to_id_map' maps for each order by column from its column id to its
  // initial VariableId before any change has been made in this function.
  absl::Status AlgebrizeOrderByItems(
      bool drop_correlated_columns, bool create_new_ids,
      absl::Span<const std::unique_ptr<const ResolvedOrderByItem>>
          order_by_items,
      absl::flat_hash_map<int, VariableId>* column_to_id_map,
      std::vector<std::unique_ptr<KeyArg>>* order_by_keys);

  // Converts each non-correlated ResolvedColumnRef in 'partition_by' to a
  // KeyArg with an ascending order.
  // 'column_to_id_map' collects the ids of the referenced columns and their
  // VariableIds.
  absl::Status AlgebrizePartitionExpressions(
      const ResolvedWindowPartitioning* partition_by,
      absl::flat_hash_map<int, VariableId>* column_to_id_map,
      std::vector<std::unique_ptr<KeyArg>>* partition_by_keys);

  // Converts a ResolvedAnalyticFunctionCall to an AnalyticArg.
  absl::StatusOr<std::unique_ptr<AnalyticArg>> AlgebrizeAnalyticFunctionCall(
      const VariableId& variable,
      const ResolvedAnalyticFunctionCall* analytic_function_call);

  // Converts a ResolvedWindowFrame to a WindowFrameArg.
  absl::StatusOr<std::unique_ptr<WindowFrameArg>> AlgebrizeWindowFrame(
      const ResolvedWindowFrame* window_frame);

  // Converts a ResolvedWindowFrameExpr to a WindowFrameBoundaryArg.
  absl::StatusOr<std::unique_ptr<WindowFrameBoundaryArg>>
  AlgebrizeWindowFrameExpr(const ResolvedWindowFrameExpr* window_frame_expr);

  // Algebrize the resolved AST for a scan.
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeScan(
      const ResolvedScan* scan,
      std::vector<FilterConjunctInfo*>* active_conjuncts);

  using AlgebrizeScanFunc =
      std::function<absl::StatusOr<std::unique_ptr<RelationalOp>>(
          Algebrizer*, const ResolvedScan*, std::vector<FilterConjunctInfo*>*)>;

  using AlgebrizeExprFunc =
      std::function<absl::StatusOr<std::unique_ptr<ValueExpr>>(
          Algebrizer*, const ResolvedExpr*)>;

  // We use a map of function pointers initialized once and outside the
  // potentially recursively called AlgebrizeScan function to keep the stack
  // frame of recursive callstacks small.
  // which could lead to out of stack errors. See b/447813193.
  static const absl::flat_hash_map<ResolvedNodeKind, AlgebrizeScanFunc>*
  InitializeScanMap();

  // We use a map of function pointers initialized once and outside the
  // potentially recursively called AlgebrizeExpr function to keep the stack
  // frame of recursive callstacks small.
  static const absl::flat_hash_map<ResolvedNodeKind, AlgebrizeExprFunc>*
  InitializeExprMap();

  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeScan(
      const ResolvedScan* scan);

  // 'output_columns' are needed to compensate for the extra implicit
  // 'ProjectScan' in top-level queries.
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeRootScanAsValueExpr(
      const ResolvedColumnList& output_columns, bool is_value_table,
      const ResolvedScan* scan);

  // Populates 'output_column_list', 'output_column_names', and
  // 'output_column_variables' according to the output of 'query' and returns a
  // RelationalOp corresponding to the scan (which may have extra variables).
  absl::StatusOr<std::unique_ptr<RelationalOp>>
  AlgebrizeQueryStatementAsRelation(
      const ResolvedQueryStmt* query, ResolvedColumnList* output_column_list,
      std::vector<std::string>* output_column_names,
      std::vector<VariableId>* output_column_variables);

  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeDMLStatement(
      const ResolvedStatement* ast_root, IdStringPool* id_string_pool);

  absl::Status AlgebrizeStatementImpl(const ResolvedStatement* ast_root,
                                      std::unique_ptr<ValueExpr>* output);

  // Populates the ResolvedScanMap and the ResolvedExprMap corresponding to
  // 'ast_root', which must be a DML statement. If the DML statement is
  // top-level (non-nested), then 'resolved_table_scan' must not be NULL and
  // this method populates it with the ResolvedTableScan corresponding to the
  // table being modified. If the DML statement is nested, then
  // 'resolved_table_scan' must be NULL. Also adds any placeholder columns
  // (i.e., ResolvedColumns that are defined in the subtree rooted at
  // 'ast_root') to 'column_to_variable_'.
  absl::Status AlgebrizeDescendantsOfDMLStatement(
      const ResolvedStatement* ast_root, ResolvedScanMap* resolved_scan_map,
      ResolvedExprMap* resolved_expr_map, ColumnExprMap* column_expr_map,
      const ResolvedTableScan** resolved_table_scan);

  // Populates the 'returning_column_list' and 'returning_column_values' from
  // the returning clause found in this dml statement. 'returning_column_list'
  // is used to create the returning output table array type,
  // 'returning_column_values' contains the ValueExpr of the returning output
  // column list and then passed to its algebrized plan.
  absl::Status AlgebrizeDMLReturningClause(
      const ResolvedStatement* ast_root, IdStringPool* id_string_pool,
      ResolvedColumnList* returning_column_list,
      std::vector<std::unique_ptr<ValueExpr>>* returning_column_values);

  // Populates the ResolvedScanMap and the ResolvedExprMap corresponding to
  // 'update_item', which must be a DML statement. Also adds any placeholder
  // columns (i.e., ResolvedColumns that are defined in the subtree rooted at
  // 'ast_root') to 'column_to_variable_'.
  absl::Status AlgebrizeDescendantsOfUpdateItem(
      const ResolvedUpdateItem* update_item, ResolvedScanMap* resolved_scan_map,
      ResolvedExprMap* resolved_expr_map, ColumnExprMap* column_expr_map);

  // Adds the entry corresponding to 'resolved_scan' to 'resolved_scan_map'
  // (whose key is 'resolved_scan' and whose value is the algebrized scan). Note
  // that the map does not own the ResolvedScan nodes.
  absl::Status PopulateResolvedScanMap(const ResolvedScan* resolved_scan,
                                       ResolvedScanMap* resolved_scan_map);

  // Adds the entry corresponding to 'resolved_expr' to 'resolved_expr_map'
  // (whose key is 'resolved_expr' and whose value is the algebrized
  // expression). Note that the map does not own the ResolvedExpr nodes.
  absl::Status PopulateResolvedExprMap(const ResolvedExpr* resolved_expr,
                                       ResolvedExprMap* resolved_expr_map);

  // Algebrize expressions of columns with default values or generated columns
  // in 'table', and put them into the output argument, 'column_expr_map'.
  // generated_column_exprs contain the rewritten generated column expressions
  // corresponding to each generated column represented by the column id in
  // the topologically_sorted_generated_column_ids and hence have same size and
  // 1-to-1 mapping.
  absl::Status AlgebrizeDefaultAndGeneratedExpressions(
      const ResolvedTableScan* table_scan, ColumnExprMap* column_expr_map,
      absl::Span<const std::unique_ptr<const ResolvedExpr>>
          generated_column_exprs,
      std::vector<int> topologically_sorted_generated_column_ids);

  // Given a list of ResolvedComputedColumn and a column_id, return in
  // (*definition) the expression that defines that column, or nullptr if not
  // found.
  bool FindColumnDefinition(
      absl::Span<const std::unique_ptr<const ResolvedComputedColumn>> expr_list,
      int column_id, const ResolvedExpr** definition);

  // Returns a RelationalOp representing a filtered view of <input>, excluding
  // any rows which are duplicates, either with respect to <input> itself, or
  // any prior evaluation of a node created from FilterDuplicates() using the
  // same <row_set_id> value.
  //
  // <column_list> denotes the columns of each row returned by <input>. Two
  // rows are considered to be duplicates only if all of the columns in
  // <column_list> hold identical values.
  absl::StatusOr<std::unique_ptr<RelationalOp>> FilterDuplicates(
      std::unique_ptr<RelationalOp> input,
      const ResolvedColumnList& column_list, VariableId row_set_id);

  // Cap the algebra for a relation in a struct with a ArrayNestExpr.
  absl::StatusOr<std::unique_ptr<ArrayNestExpr>> NestRelationInStruct(
      const ResolvedColumnList& output_columns,
      std::unique_ptr<RelationalOp> relation, bool is_with_table);
  // Cap the algebra for a relation with a ArrayNestExpr. This is used to
  // encapsulate the result of a subquery expression which is always a single
  // column (but may be a struct containing multiple columns).
  absl::StatusOr<std::unique_ptr<ArrayNestExpr>> NestSingleColumnRelation(
      const ResolvedColumnList& output_columns,
      std::unique_ptr<RelationalOp> relation, bool is_with_table);

  // Creates a scan operator iterating over 'table_expr'.
  absl::StatusOr<std::unique_ptr<ArrayScanOp>> CreateScanOfTableAsArray(
      const ResolvedScan* scan, bool is_value_table,
      std::unique_ptr<ValueExpr> table_expr);
  // Similar to CreateScanOfTableAsArray but based on column list.
  absl::StatusOr<std::unique_ptr<ArrayScanOp>> CreateScanOfColumnsAsArray(
      const ResolvedColumnList& column_list, bool is_value_table,
      std::unique_ptr<ValueExpr> table_expr);

  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeIf(
      const Type* output_type, std::vector<std::unique_ptr<ValueExpr>> args);
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeIfError(
      std::vector<std::unique_ptr<ValueExpr>> args);
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeIsError(
      std::vector<std::unique_ptr<ValueExpr>> args);
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeIfNull(
      const Type* output_type, std::vector<std::unique_ptr<ValueExpr>> args);
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeNullIf(
      const Type* output_type, std::vector<std::unique_ptr<ValueExpr>> args);
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeCoalesce(
      const Type* output_type, std::vector<std::unique_ptr<ValueExpr>> args);
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeCaseNoValue(
      const Type* output_type, std::vector<std::unique_ptr<ValueExpr>> args);
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeCaseWithValue(
      const Type* output_type, std::vector<std::unique_ptr<ValueExpr>> args,
      absl::Span<const ResolvedCollation> collation_list);
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeWithSideEffects(
      std::vector<std::unique_ptr<ValueExpr>> args);
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeNotEqual(
      std::vector<std::unique_ptr<ValueExpr>> args);
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeIn(
      const Type* output_type, std::vector<std::unique_ptr<ValueExpr>> args);
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeBetween(
      const Type* output_type, std::vector<std::unique_ptr<ValueExpr>> args);

  // If the presence of 'conjunct_info' above the join allows us to remove the
  // outer join from one or both sides of 'join_kind', updates 'join_kind'
  // accordingly.
  static absl::Status NarrowJoinKindForFilterConjunct(
      const FilterConjunctInfo& conjunct_info,
      const absl::flat_hash_set<ResolvedColumn>& left_output_columns,
      const absl::flat_hash_set<ResolvedColumn>& right_output_columns,
      JoinOp::JoinKind* join_kind);

  // For a conjunct above the join, populates the following:
  // - 'push_down_to_join_condition' with true if the push down should stop at
  //    the join condition.
  // - 'push_down_to_left_input'/'push_down_to_right_input' if the push down can
  //    go to the left/right input.
  absl::Status CanPushFilterConjunctIntoJoin(
      const FilterConjunctInfo& conjunct_info, JoinOp::JoinKind join_kind,
      const absl::flat_hash_set<ResolvedColumn>& left_output_columns,
      const absl::flat_hash_set<ResolvedColumn>& right_output_columns,
      bool* push_down_to_join_condition, bool* push_down_to_left_input,
      bool* push_down_to_right_input);

  // Populates the output arguments based on whether/where the filter conjunct
  // can be pushed down, assuming it is currently in the join condition of an
  // inner join.
  absl::Status CanPushFilterConjunctDownFromInnerJoinCondition(
      const FilterConjunctInfo& conjunct_info,
      const absl::flat_hash_set<ResolvedColumn>& left_output_columns,
      const absl::flat_hash_set<ResolvedColumn>& right_output_columns,
      bool* push_down_to_left_input, bool* push_down_to_right_input);

  // Adds each entry in 'conjuncts_with_push_down' (all of which must be
  // non-redundant) that can be represented with a HashJoinEqualityExprs object
  // to 'hash_join_equality_exprs'.  'conjuncts_with_push_down' is passed by
  // pointer because we mark any conjuncts added to 'hash_join_equality_exprs'
  // as redundant. The iteration is done in reverse order because
  // 'conjuncts_with_push_down' is interpreted as a stack.
  absl::Status AlgebrizeJoinConditionForHashJoin(
      const absl::flat_hash_set<ResolvedColumn>& left_output_columns,
      const absl::flat_hash_set<ResolvedColumn>& right_output_columns,
      std::vector<FilterConjunctInfo*>* conjuncts_with_push_down,
      std::vector<JoinOp::HashJoinEqualityExprs>* hash_join_equality_exprs);

  // If 'conjunct_info' can be represented by a HashJoinEqualityExprs, populates
  // 'equality_exprs'. Else returns false.
  absl::StatusOr<bool> TryAlgebrizeFilterConjunctAsHashJoinEqualityExprs(
      const FilterConjunctInfo& conjunct_info,
      const absl::flat_hash_set<ResolvedColumn>& left_output_columns,
      const absl::flat_hash_set<ResolvedColumn>& right_output_columns,
      int num_previous_equality_exprs,
      JoinOp::HashJoinEqualityExprs* equality_exprs);

  // Creates a new variable for each column and returns a vector of arguments,
  // each assigning the new variable from a DerefExpr of the old variable.
  absl::Status RemapJoinColumns(const ResolvedColumnList& columns,
                                std::vector<std::unique_ptr<ExprArg>>* output);

  // If 'algebrizer_options_.push_down_filters' is false, returns
  // 'input'. Otherwise returns a RelationalOp that applies the non-redundant
  // entries in 'active_conjuncts' to 'input', marking everything in
  // 'active_conjuncts' as redundant.
  absl::StatusOr<std::unique_ptr<RelationalOp>> MaybeApplyFilterConjuncts(
      std::unique_ptr<RelationalOp> input,
      std::vector<FilterConjunctInfo*>* active_conjuncts);

  // Returns a RelationalOp corresponding to 'input' that applies
  // 'algebrized_conjuncts' as filters.
  absl::StatusOr<std::unique_ptr<RelationalOp>> ApplyAlgebrizedFilterConjuncts(
      std::unique_ptr<RelationalOp> input,
      std::vector<std::unique_ptr<ValueExpr>> algebrized_conjuncts);

  // Returns a `RelationalOp` corresponding to `resolved_assert`.
  //
  // Unlike `AlgebrizeProjectScan`, this function does not accept
  // FilterConjunctInfo to prevent query optimizations like filter pushdown,
  // which can violate the semantics of the assert statement.
  //
  // For example, consider the following query:
  //
  // FROM UNNEST([1, 2, 3]) AS k
  // |> ASSERT k > 1
  // |> WHERE k > 1
  //
  // The assertion should fail. However, if the filter `WHERE k > 1` is pushed
  // down to the array scan for [1, 2, 3], the assertion will succeed and thus
  // the result is wrong.
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeAssertScan(
      const ResolvedAssertScan* resolved_assert);

  // Returns a `RelationalOp` corresponding to `resolved_describe`.
  //
  // This makes a scan that just returns a single-row table with the
  // describe value.
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeDescribeScan(
      const ResolvedDescribeScan* resolved_describe);

  // Returns a `RelationalOp` corresponding to `resolved_static_describe`.
  //
  // This operator is a no-op so it just includes the input scan.
  // The static logging side-effect currently doesn't happen.
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeStaticDescribeScan(
      const ResolvedStaticDescribeScan* resolved_static_describe);

  // Returns a `RelationalOp` corresponding to `resolved_barrier_scan`.
  //
  // This function does not accept FilterConjunctInfo to prevent query
  // optimizations like filter pushdown.
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeBarrierScan(
      const ResolvedBarrierScan* resolved_barrier_scan);

  // Agebrize a ResolvedSubpipeline. `input_op` is the algebrized scan producing
  // the input table for the subpipeline.  It'll be used for to make the
  // table for the contained ResolvedSubpipelineInputScan.
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeSubpipeline(
      const ResolvedSubpipeline* subpipeline,
      std::unique_ptr<RelationalOp> input_op);
  absl::StatusOr<std::unique_ptr<RelationalOp>> AlgebrizeSubpipelineInputScan(
      const ResolvedSubpipelineInputScan* scan);

  // Algebrizes a ResolvedMultiStmt and returns a MultiStmtExpr.
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeMultiStmt(
      const ResolvedMultiStmt* multi_stmt);

  // Algebrizes a ResolvedCreateWithEntryStmt, and adds the WITH entry to
  // `with_map_`.
  //
  // Local with entries `with_subquery_let_assignments_` and
  // `inlined_with_entries_` must be empty before calling this function, and
  // they remain empty after calling this function.
  absl::StatusOr<std::unique_ptr<ExprArg>> AlgebrizeCreateWithEntryStmt(
      const ResolvedCreateWithEntryStmt* create_with_stmt);

  // Algebrize the WHERE filtering expression for an aggregate function call
  // `expr`.
  // `filter` is an existing filtering expression that should be ANDed with the
  // WHERE filter.
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeAggregateWhereFilterExpr(
      const ResolvedExpr* expr, std::unique_ptr<ValueExpr> filter);

  // Algebrize the HAVING (post-aggregate) filtering expression for an aggregate
  // function call `expr`.
  absl::StatusOr<std::unique_ptr<ValueExpr>> AlgebrizeAggregateHavingFilterExpr(
      const ResolvedExpr* expr);

  // Represents a named or positional parameter.
  class Parameter {
   public:
    explicit Parameter(const ResolvedParameter& param) {
      if (param.position() == 0) {
        position_or_name_ = param.name();
      } else {
        position_or_name_ = param.position();
      }
    }

    bool operator==(const Parameter& other) const {
      return position_or_name_ == other.position_or_name_;
    }

    template <typename H>
    friend H AbslHashValue(H h, const Parameter& p) {
      return H::combine(std::move(h), p.position_or_name_);
    }

    std::string DebugString() const {
      if (std::holds_alternative<int>(position_or_name_)) {
        return absl::StrCat("$", *std::get_if<int>(&position_or_name_));
      }
      return absl::StrCat("$", *std::get_if<std::string>(&position_or_name_));
    }

   private:
    std::variant<int, std::string> position_or_name_;
  };

  // Represents a column or a parameter (or a ResolvedExpressionColumn, which in
  // some ways is like a column and in some ways is like a parameter).
  class ColumnOrParameter {
   public:
    ColumnOrParameter() = default;

    explicit ColumnOrParameter(const ResolvedColumn& column)
        : column_or_param_(column) {}

    explicit ColumnOrParameter(const ResolvedParameter& param)
        : column_or_param_(Parameter(param)) {}

    explicit ColumnOrParameter(const ResolvedExpressionColumn& column)
        : column_or_param_(column.name()) {}

    bool operator==(const ColumnOrParameter& other) const {
      return column_or_param_ == other.column_or_param_;
    }

    template <typename H>
    friend H AbslHashValue(H h, const ColumnOrParameter& c) {
      return H::combine(std::move(h), c.column_or_param_);
    }

    std::string DebugString() const {
      if (std::holds_alternative<ResolvedColumn>(column_or_param_)) {
        return std::get_if<ResolvedColumn>(&column_or_param_)->DebugString();
      }
      if (std::holds_alternative<Parameter>(column_or_param_)) {
        return std::get_if<Parameter>(&column_or_param_)->DebugString();
      }
      return *std::get_if<std::string>(&column_or_param_);
    }

   private:
    // Stores a ResolvedColumn, a Parameter (for a ResolvedParameter), or the
    // name of a ResolvedExpressionColumn.
    using StorageType = std::variant<ResolvedColumn, Parameter, std::string>;
    StorageType column_or_param_;
  };

  class ProtoOrStructField {
   public:
    enum Kind { PROTO_FIELD, STRUCT_FIELD };

    ProtoOrStructField(Kind kind, int tag_or_field_idx)
        : kind_(kind), tag_or_field_idx_(tag_or_field_idx) {}

    Kind kind() const { return kind_; }

    // Requires kind() == PROTO_FIELD.
    int tag_number() const { return tag_or_field_idx_; }

    // Requires kind() == STRUCT_FIELD.
    int field_idx() const { return tag_or_field_idx_; }

    bool operator==(const ProtoOrStructField& other) const {
      return kind_ == other.kind() &&
             tag_or_field_idx_ == other.tag_or_field_idx_;
    }

    template <typename H>
    friend H AbslHashValue(H h, const ProtoOrStructField& f) {
      h = H::combine(std::move(h), f.kind_);
      return H::combine(std::move(h), f.tag_or_field_idx_);
    }

    std::string DebugString() const {
      switch (kind_) {
        case PROTO_FIELD:
          return absl::StrCat("proto_", tag_number());
        case STRUCT_FIELD:
          return absl::StrCat("struct_", field_idx());
      }
    }

   private:
    Kind kind_;
    int tag_or_field_idx_ = 0;
    // Allow copy/move/assign.
  };

  // Represents an expression of the form
  // proto_column_or_parameter.field_with_tag1.field_with_tag2...; extension
  // fields are allowed.
  struct SharedProtoFieldPath {
    ColumnOrParameter column_or_param;
    // TODO: Consider allowing struct field names at the start of the
    // path to optimize for more cases.
    std::vector<ProtoOrStructField> field_path;

    bool operator==(const SharedProtoFieldPath& other) const {
      return column_or_param == other.column_or_param &&
             field_path == other.field_path;
    }

    template <typename H>
    friend H AbslHashValue(H h, const SharedProtoFieldPath& p) {
      h = H::combine(std::move(h), p.column_or_param);
      return H::combine(std::move(h), p.field_path);
    }

    std::string DebugString() const;
  };

  // If 'id' is set, also updates 'proto_field_registry_map_'.
  absl::StatusOr<std::unique_ptr<ProtoFieldRegistry>> MakeProtoFieldRegistry(
      const std::optional<SharedProtoFieldPath>& id);

  // A ProtoFieldReader corresponding to 'access_info' and 'registry' is
  // returned. If 'id' is set and 'access_info' represents a proto-valued field,
  // also updates 'get_proto_field_reader_map_'.
  absl::StatusOr<std::unique_ptr<ProtoFieldReader>> MakeProtoFieldReader(
      const std::optional<SharedProtoFieldPath>& id,
      const ProtoFieldAccessInfo& access_info, ProtoFieldRegistry* registry);

  // Maps each column in <input_columns>, produced by <input>, into the
  // corresponding column in <output_columns>.
  //
  // The result is a ComputeOp node like the following:
  //   ComputeOp
  //     input: <input>
  //     map:
  //      <output_columns[0]>: DerefExpr(<input_columns[0]>)
  //      <output_columns[1]>: DerefExpr(<input_columns[1]>)
  //      ...
  absl::StatusOr<std::unique_ptr<RelationalOp>> MapColumns(
      std::unique_ptr<RelationalOp> input,
      const ResolvedColumnList& input_columns,
      const ResolvedColumnList& output_columns);

  // Utilities to handle filter push down.
  //
  // Pushes <conjunct_infos> into <active_conjuncts> in reverse order (because
  // it's a stack).
  static void PushConjuncts(
      absl::Span<const std::unique_ptr<FilterConjunctInfo>> conjunct_infos,
      std::vector<FilterConjunctInfo*>* active_conjuncts);

  // Restores <active_conjuncts> by popping conjuncts. Returns error if the
  // conjuncts popped are different from <conjunct_infos>.
  static absl::Status PopConjuncts(
      absl::Span<const std::unique_ptr<FilterConjunctInfo>> conjunct_infos,
      std::vector<FilterConjunctInfo*>* active_conjuncts);

  // Algebrizes the <conjunct_infos> which are NOT redundant to a corresponding
  // list of value expressions.
  absl::StatusOr<std::vector<std::unique_ptr<ValueExpr>>>
  AlgebrizeNonRedundantConjuncts(
      absl::Span<const std::unique_ptr<FilterConjunctInfo>> conjunct_infos);

  // LanguageOption to use when algebrizing.
  const LanguageOptions language_options_;
  const AlgebrizerOptions algebrizer_options_;

  // Maintains the mapping between column ids and variables.
  //
  // Since the same ResolvedColumn can appear in multiple places (e.g., the
  // column lists of multiple scan nodes), the Algebrizer makes extensive use of
  // ColumnToVariableMapping::GetVariableNameFromColumn() to either retrieve a
  // column or add it if it hasn't been added already. However, there are also
  // places where the Algebrizer instead calls
  // ColumnToVariableMapping::AssignNewVariableToColumn() to forcibly allocate a
  // new variable for a column that may have already been added; this technique
  // is typically used where an operator has both an input and an output
  // corresponding to a particular ResolvedColumn. Finally, there is also at
  // least one place where the Algebrizer explicitly requires that a variable
  // corresponding to a ResolvedColumn has already been added: when resolving a
  // RESOLVED_COLUMN_REF expression in the case where the Algebrizer is
  // resolving a GoogleSQL statement (not a standalone expression).
  std::unique_ptr<ColumnToVariableMapping> column_to_variable_;
  // Generates variable names corresponding to query parameters or columns.
  // Owned by 'column_to_variable_'.
  VariableGenerator* variable_gen_;
  // Maps MEASURE-typed ResolvedColumns to the expression used to compute the
  // measure. The expression used to compute the measure is stored in the
  // catalog (not the ResolvedColumn or the type), hence why we need to track it
  // here.
  MeasureColumnToExprMapping measure_column_to_expr_;
  // Maps parameters to variables for named parameters or else contains a list
  // of positional parameters. Not owned.
  Parameters* parameters_;
  ParameterMap* column_map_;  // Maps columns to variables. Not owned.

  // Maps aggregate arguments by names to the variables.
  // Active only when algebrizing a UDA's body during a call. Usually this would
  // be a child/single-use algebrizer.
  absl::flat_hash_map<std::string, VariableId> aggregate_args_map_;

  // The active `measure_variable` and `measure_type` to use when algebrizing
  // an ExpressionColumn in a measure expression. This is only active when
  // algebrizing a measure expression, which typically happens via a
  // single-use algebrizer.
  VariableId measure_variable_;
  const MeasureType* measure_type_ = nullptr;

  // Maps system variables to variable ids.  Not owned.
  SystemVariablesAlgebrizerMap* system_variables_map_;

  // Maps named WITH subquery to an argument (variable, ValueExpr). Used to
  // algebrize WithRef scans referencing named subqueries.
  //
  // This map includes:
  // - WITH subqueries which are referenced two or more times (e.g. evaluated up
  //   front and stored in an array), and
  // - WITH entries that are registered by `ResolvedCreateWithEntryStmt`s.
  absl::flat_hash_map<std::string, ExprArg*> with_map_;  // Not owned.

  // Vector of LetOp/WithExpr assignments we need to apply for WITH clauses in
  // the query.
  std::vector<std::unique_ptr<ExprArg>> with_subquery_let_assignments_;

  // WITH entries whose definitions are to be inlined where they are referenced.
  // Only WITH entries referenced exactly once are included in this map.
  // Key = name, value = ResolvedScan of WITH entry subquery.
  //
  // Entries are removed from the map as AlgebrizeWithRefScan() consumes them.
  absl::flat_hash_map<std::string, const ResolvedScan*> inlined_with_entries_;

  // If 'algebrizer_options_.consolidate_proto_field_accesses' is true, contains
  // every GetProtoFieldExpr::FieldRegistry whose proto-valued expression can be
  // represented with a ProtoColumnAndFieldPath. The pointers are owned by
  // 'proto_field_registries_'.
  //
  // For example, the expression proto_column.a.b would result in:
  //
  // GetProtoFieldExpr
  // - GetProtoFieldExpr (corresponding to proto_column.a)
  //   - ValueExpr (corresponding to proto_column)
  //   - ProtoFieldReader (in 'get_proto_field_reader_map_'
  //                       with key proto_column.a)
  //     - ProtoFieldAccessInfo (corresponding to a)
  //     - FieldRegistry (in 'proto_field_registry_map_' with key
  //                      proto_column)
  // - ProtoFieldReader (in 'get_proto_field_reader_map_'
  //                     with key proto_column.a.b)
  //    - ProtoFieldAccessInfo (corresponding to b)
  //    - FieldRegistry (in 'proto_field_registry_map_' with key
  //                     proto_column.a)
  //
  // For more details, see the class comment for GetProtoFieldExpr in
  // operator.h.
  absl::flat_hash_map<SharedProtoFieldPath, ProtoFieldRegistry*>
      proto_field_registry_map_;

  // If 'algebrizer_options_.consolidate_proto_field_accesses' is true, contains
  // the ProtoFieldReader for every GetProtoFieldExpr node with proto type that
  // represents an expression that can be represented with a
  // ProtoColumnAndFieldPath. The pointers are owned by
  // 'get_proto_field_readers_'. Note that all the keys in this map have
  // non-empty tag_number_paths. We only cache proto-valued nodes because it's
  // possible to have two different accesses to a non-message field that have
  // slightly different meaning (e.g., get_has_bit = true vs. false).
  //
  // For more details, see the class comment for GetProtoFieldExpr in
  // operator.h.
  absl::flat_hash_map<SharedProtoFieldPath, ProtoFieldReader*>
      get_proto_field_reader_map_;

  TypeFactory* type_factory_;  // Not owned.

  // The top of the stack represents the variable id to use for the recursive
  // variable in the current RecursiveScan node being algebrized.
  std::stack<std::unique_ptr<ExprArg>> recursive_var_id_stack_;

  // The input that a FlattenedArg should read from.
  // There may be multiple in a stack as there could be Flatten used as part of
  // the input expression for another Flatten.
  std::stack<std::unique_ptr<const Value*>> flattened_arg_input_;

  // The list of variables to use when algebrizing a ResolvedCatalogColumnRef.
  // This is not a stack because we should never have a nested
  // ResolvedCatalogColumnRef.
  std::optional<absl::flat_hash_map<const Column*, VariableId>>
      catalog_column_ref_variables_;

  // The top of the stack represents the algebrized input scan for the
  // subpipeline being resolved.  It'll be used for the
  // ResolvedSubpipelineInputScan.
  std::stack<std::unique_ptr<RelationalOp>> subpipeline_input_scan_stack_;

  // Represents an algebrized graph composite scan.
  struct GraphCompositeScanInput {
    // Algebrized relation.
    std::unique_ptr<RelationalOp> op;
    // Resolved column list.
    ResolvedColumnList column_list;
    // Set to ArrayNestExpr of `op` if the current `op` is referenced by
    // multiple linear scans in the following graph composite scan. Can be
    // nullptr if otherwise. It is owned by `GraphCompositeScanInput::op` of the
    // next composite scan.
    ExprArg* array_nest_expr;
  };
  // Tracks the current relational op that can be used as input by the child
  // scan in graph composite scan. The stack size increases whenever there is a
  // new level of nested GraphLinearScan.
  std::vector<GraphCompositeScanInput>
      current_input_op_in_graph_linear_scan_stack_;

  // Tracks the current graph path context of `GraphPathOp`, which can be
  // consumed by the child path `GraphPathOp` or `QuantifiedGraphPathOp`.
  std::vector<GraphPathContext> current_graph_path_context_stack_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_REFERENCE_IMPL_ALGEBRIZER_H_
