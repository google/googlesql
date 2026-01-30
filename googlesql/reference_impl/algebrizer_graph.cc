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

#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/catalog.h"
#include "googlesql/public/json_value.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/reference_impl/algebrizer.h"
#include "googlesql/reference_impl/function.h"
#include "googlesql/reference_impl/operator.h"
#include "googlesql/reference_impl/tuple.h"
#include "googlesql/reference_impl/type_helpers.h"
#include "googlesql/reference_impl/variable_generator.h"
#include "googlesql/reference_impl/variable_id.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_visitor.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "absl/algorithm/container.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/bind_front.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

// Builds a list of value expressions representing columns in <table> with
// with <indices>. Column value expressions are represented by variables that
// can be looked up by column from <variables_by_column>.
absl::StatusOr<std::vector<std::unique_ptr<ValueExpr>>> GetColumnReferences(
    const Table& table, absl::Span<const int> indices,
    const absl::flat_hash_map<const Column*, VariableId>& variables_by_column) {
  std::vector<std::unique_ptr<ValueExpr>> references;
  references.reserve(indices.size());
  const int num_columns = table.NumColumns();
  for (const int i : indices) {
    GOOGLESQL_RET_CHECK_LT(i, num_columns);
    const Column* column = table.GetColumn(i);
    GOOGLESQL_RET_CHECK_NE(column, nullptr);
    const auto iter = variables_by_column.find(column);
    GOOGLESQL_RET_CHECK(iter != variables_by_column.end());
    GOOGLESQL_ASSIGN_OR_RETURN(auto ref,
                     DerefExpr::Create(iter->second, column->GetType()));
    references.push_back(std::move(ref));
  }
  return references;
}

// Builds a value expression representing a conjunction of pairwise equality
// between <left_exprs> and <right_exprs>.
absl::StatusOr<std::unique_ptr<ValueExpr>> BuildPairwiseEquality(
    std::vector<std::unique_ptr<ValueExpr>> left_exprs,
    std::vector<std::unique_ptr<ValueExpr>> right_exprs,
    const LanguageOptions& language_options) {
  GOOGLESQL_RET_CHECK_EQ(left_exprs.size(), right_exprs.size());
  std::vector<std::unique_ptr<ValueExpr>> and_args;
  and_args.reserve(left_exprs.size());
  for (size_t i = 0; i < left_exprs.size(); ++i) {
    std::vector<std::unique_ptr<ValueExpr>> equality_args(2);
    equality_args[0] = std::move(left_exprs[i]);
    equality_args[1] = std::move(right_exprs[i]);
    GOOGLESQL_ASSIGN_OR_RETURN(
        auto and_arg,
        BuiltinScalarFunction::CreateCall(
            FunctionKind::kEqual, language_options, types::BoolType(),
            ConvertValueExprsToAlgebraArgs(std::move(equality_args)),
            ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));
    and_args.push_back(std::move(and_arg));
  }

  return BuiltinScalarFunction::CreateCall(
      FunctionKind::kAnd, language_options, types::BoolType(),
      ConvertValueExprsToAlgebraArgs(std::move(and_args)),
      ResolvedFunctionCallBase::DEFAULT_ERROR_MODE);
}

absl::StatusOr<std::unique_ptr<FilterOp>> FilterOutRowsWithNullValuedPks(
    const GraphElementTable* element_table,
    std::unique_ptr<RelationalOp> input_op,
    const LanguageOptions& language_options,
    const absl::flat_hash_map<const Column*, VariableId>& variables_by_column) {
  // Dedupes the columns because there could be overlaps between src/dest
  // referencing columns (and key columns).
  //
  // For example,
  //
  //   SOURCE      KEY(node_type, src_node_key)
  //   REFERENCES Node(node_type, node_key)
  //
  //   DESTINATION KEY(node_type, dst_node_key)
  //   REFERENCES Node(node_type, node_key)
  //
  // `node_type` shows up in both source and destination node referencing
  // columns.
  std::set<int> indices = {element_table->GetKeyColumns().begin(),
                           element_table->GetKeyColumns().end()};
  if (element_table->Is<GraphEdgeTable>()) {
    const auto* edge_table = element_table->AsEdgeTable();
    absl::c_copy(edge_table->GetSourceNodeTable()->GetEdgeTableColumns(),
                 std::inserter(indices, indices.end()));
    absl::c_copy(edge_table->GetDestNodeTable()->GetEdgeTableColumns(),
                 std::inserter(indices, indices.end()));
  }
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::vector<std::unique_ptr<ValueExpr>> keys,
      GetColumnReferences(*element_table->GetTable(),
                          std::vector<int>{indices.begin(), indices.end()},
                          variables_by_column));
  GOOGLESQL_RET_CHECK(!keys.empty());
  std::vector<std::unique_ptr<ValueExpr>> not_null_args;
  not_null_args.reserve(keys.size());
  for (std::unique_ptr<ValueExpr>& key : keys) {
    std::vector<std::unique_ptr<ValueExpr>> is_null_args;
    is_null_args.push_back(std::move(key));
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<ValueExpr> is_null_expr,
        BuiltinScalarFunction::CreateCall(
            FunctionKind::kIsNull, language_options, types::BoolType(),
            ConvertValueExprsToAlgebraArgs(std::move(is_null_args)),
            ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));
    std::vector<std::unique_ptr<ValueExpr>> not_args;
    not_args.push_back(std::move(is_null_expr));
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<ValueExpr> not_null_expr,
        BuiltinScalarFunction::CreateCall(
            FunctionKind::kNot, language_options, types::BoolType(),
            ConvertValueExprsToAlgebraArgs(std::move(not_args)),
            ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));
    not_null_args.push_back(std::move(not_null_expr));
  }
  GOOGLESQL_RET_CHECK(!not_null_args.empty());
  std::unique_ptr<ValueExpr> filter_expr;
  if (not_null_args.size() > 1) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        filter_expr,
        BuiltinScalarFunction::CreateCall(
            FunctionKind::kAnd, language_options, types::BoolType(),
            ConvertValueExprsToAlgebraArgs(std::move(not_null_args)),
            ResolvedFunctionCallBase::DEFAULT_ERROR_MODE));
  } else {
    filter_expr = std::move(not_null_args.front());
  }
  return FilterOp::Create(std::move(filter_expr), std::move(input_op));
}

// Scans <base_table> as an array scan and outputs the variable name to id
// mapping into <variables_by_column>.
absl::StatusOr<std::unique_ptr<RelationalOp>> CreateGraphElementTableScan(
    const GraphElementTable& element_table, VariableGenerator& variable_gen,
    const AlgebrizerOptions& algebrizer_options,
    const LanguageOptions& language_options, TypeFactory& type_factory,
    absl::flat_hash_map<const Column*, VariableId>&
        output_variables_by_column) {
  GOOGLESQL_RET_CHECK(element_table.GetTable() != nullptr);
  const Table& base_table = *element_table.GetTable();
  // Make a new variable for each column.
  std::vector<VariableId> variables;
  variables.reserve(base_table.NumColumns());
  std::vector<const Column*> table_column_list;
  table_column_list.reserve(base_table.NumColumns());

  for (int i = 0; i < base_table.NumColumns(); ++i) {
    const Column* column = base_table.GetColumn(i);
    VariableId variable = variable_gen.GetNewVariableName(column->Name());
    variables.emplace_back(variable);
    output_variables_by_column[column] = variable;
    table_column_list.push_back(column);
  }

  const std::string& table_name = base_table.Name();

  std::unique_ptr<RelationalOp> scan_op;
  if (algebrizer_options.use_arrays_for_tables) {
    GOOGLESQL_RET_CHECK(!base_table.IsValueTable())
        << "Base value tables not yet supported.";
    std::vector<const Column*> table_columns;
    table_columns.reserve(base_table.NumColumns());
    for (int i = 0; i < base_table.NumColumns(); ++i) {
      table_columns.push_back(base_table.GetColumn(i));
    }
    GOOGLESQL_ASSIGN_OR_RETURN(
        const ArrayType* table_type,
        CreateTableArrayType(table_columns, base_table.IsValueTable(),
                             &type_factory));
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<TableAsArrayExpr> table_as_array_expr,
                     TableAsArrayExpr::Create(table_name, table_type));

    const Type* struct_type =
        table_as_array_expr->output_type()->AsArray()->element_type();
    // List of fields emitted by the table.
    std::vector<std::pair<VariableId, int>> fields;
    GOOGLESQL_RET_CHECK_EQ(table_columns.size(), struct_type->AsStruct()->num_fields());
    fields.reserve(table_columns.size());
    for (int i = 0; i < table_columns.size(); ++i) {
      fields.emplace_back(variables[i], i);
    }

    GOOGLESQL_ASSIGN_OR_RETURN(scan_op,
                     ArrayScanOp::Create(VariableId() /* element */,
                                         VariableId() /* position */, fields,
                                         std::move(table_as_array_expr)));

  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(scan_op,
                     EvaluatorTableScanOp::Create(
                         &base_table, table_name, table_column_list, variables,
                         /*and_filters=*/{}, /*read_time=*/nullptr));
  }

  return FilterOutRowsWithNullValuedPks(&element_table, std::move(scan_op),
                                        language_options,
                                        output_variables_by_column);
}

// Joins edge table scan <edge_table_scan> of edge table <edge_table>
// with its <node_reference>.
// <edge_variables_by_column> contains the edge related variable name to id
// mappings used to build join conditions.
absl::StatusOr<std::unique_ptr<RelationalOp>> JoinNodeReferences(
    const Table& edge_table, std::unique_ptr<RelationalOp> edge_table_scan,
    const GraphNodeTableReference& node_reference,
    const AlgebrizerOptions& algebrizer_options,
    const LanguageOptions& language_options,
    const absl::flat_hash_map<const Column*, VariableId>&
        variables_by_edge_column,
    absl::flat_hash_map<const Column*, VariableId>& variables_by_node_column,
    VariableGenerator& variable_gen, TypeFactory& type_factory) {
  // Scans the node table.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> node_reference_op,
                   CreateGraphElementTableScan(
                       *node_reference.GetReferencedNodeTable(), variable_gen,
                       algebrizer_options, language_options, type_factory,
                       variables_by_node_column));

  // Builds the join conditions using the node reference referencing/referenced
  // column indices.
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::vector<std::unique_ptr<ValueExpr>> node_column_refs,
      GetColumnReferences(*node_reference.GetReferencedNodeTable()->GetTable(),
                          node_reference.GetNodeTableColumns(),
                          variables_by_node_column));

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::vector<std::unique_ptr<ValueExpr>> edge_column_refs,
      GetColumnReferences(edge_table, node_reference.GetEdgeTableColumns(),
                          variables_by_edge_column));

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ValueExpr> remaining_join_expr,
      BuildPairwiseEquality(std::move(node_column_refs),
                            std::move(edge_column_refs), language_options));

  return JoinOp::Create(JoinOp::kInnerJoin, /*equality_exprs=*/{},
                        std::move(remaining_join_expr),
                        std::move(edge_table_scan),
                        std::move(node_reference_op), /*left_outputs=*/{},
                        /*right_outputs=*/{});
}

absl::StatusOr<UnionAllOp::Input> BuildUnionAllInput(
    VariableId variable_id, const Type* type,
    std::unique_ptr<RelationalOp> input) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<DerefExpr> deref,
                   DerefExpr::Create(variable_id, type));

  std::vector<std::unique_ptr<ExprArg>> union_mapping_arg;
  union_mapping_arg.push_back(
      std::make_unique<ExprArg>(variable_id, std::move(deref)));
  return std::make_pair(std::move(input), std::move(union_mapping_arg));
}

// Returns true if any element of 'a' is in 'b'.
bool Intersects(absl::Span<const ResolvedColumn> a,
                const absl::flat_hash_set<ResolvedColumn>& b) {
  for (const ResolvedColumn& column : a) {
    if (b.contains(column)) {
      return true;
    }
  }
  return false;
}

std::vector<const ResolvedGraphPathScanBase*> GetScansWithIntersection(
    const absl::flat_hash_set<ResolvedColumn>& referenced_columns,
    absl::Span<const std::unique_ptr<const ResolvedGraphPathScanBase>>
        child_scans) {
  std::vector<const ResolvedGraphPathScanBase*> scans_with_intersection;
  for (const std::unique_ptr<const ResolvedGraphPathScanBase>& child_scan :
       child_scans) {
    if (Intersects(child_scan->column_list(), referenced_columns)) {
      scans_with_intersection.push_back(child_scan.get());
    }
  }
  return scans_with_intersection;
}

// Gets the source/destination node reference variables from the
// <catalog_column_ref_variables>.
//
// The reference variables are defined in the <endpoint_reference> of
// <edge_element_table>.
//
// Returns nullopt if we cannot get the node reference variables.
absl::StatusOr<std::optional<absl::flat_hash_map<const Column*, VariableId>>>
TryGetNodeReferencesFromEdgeReferences(
    const GraphEdgeTable* edge_element_table,
    const GraphNodeTableReference& endpoint_reference,
    const absl::flat_hash_map<const Column*, VariableId>&
        catalog_column_ref_variables) {
  const std::vector<int>& node_ref_cols =
      endpoint_reference.GetNodeTableColumns();
  const std::vector<int>& node_key_cols =
      endpoint_reference.GetReferencedNodeTable()->GetKeyColumns();
  if (node_ref_cols.size() != node_key_cols.size()) {
    return std::nullopt;
  }

  // Make them ordered vector to compare equality.
  std::vector<int> sorted_node_ref_cols = node_ref_cols;
  absl::c_sort(sorted_node_ref_cols);

  std::vector<int> sorted_node_key_cols = node_key_cols;
  absl::c_sort(sorted_node_key_cols);

  // This works when reference cols are EXACTLY the key cols:
  // one edge row corresponds to one edge in this case.
  if (!absl::c_equal(sorted_node_ref_cols, sorted_node_key_cols)) {
    return std::nullopt;
  }

  const Table* node_table =
      endpoint_reference.GetReferencedNodeTable()->GetTable();
  const Table* edge_table = edge_element_table->GetTable();

  // Finds the node reference variables from its corresponding edge references.
  const std::vector<int>& edge_ref_cols =
      endpoint_reference.GetEdgeTableColumns();
  GOOGLESQL_RET_CHECK_EQ(node_ref_cols.size(), edge_ref_cols.size());
  absl::flat_hash_map<const Column*, VariableId> result;
  for (int i = 0; i < node_ref_cols.size(); ++i) {
    const Column* edge_column = edge_table->GetColumn(edge_ref_cols[i]);
    GOOGLESQL_RET_CHECK(edge_column != nullptr);
    const Column* node_column = node_table->GetColumn(node_ref_cols[i]);
    GOOGLESQL_RET_CHECK(node_column != nullptr);
    auto iter = catalog_column_ref_variables.find(edge_column);
    GOOGLESQL_RET_CHECK(iter != catalog_column_ref_variables.end());
    GOOGLESQL_RET_CHECK(result.emplace(node_column, iter->second).second);
  }

  return result;
}
}  // namespace

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeGraphTableScan(
    const ResolvedGraphTableScan* graph_table_scan,
    std::vector<FilterConjunctInfo*>* active_conjuncts) {
  graph_table_scan->MarkFieldsAccessed();
  // TODO: We should just generate a projScan on top of the graph_table_scan,
  // and not embed the expressions in it.
  return AlgebrizeProjectScanInternal(
      graph_table_scan->column_list(), graph_table_scan->shape_expr_list(),
      graph_table_scan->input_scan(), graph_table_scan->is_ordered(),
      active_conjuncts);
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeGraphCallScan(
    const ResolvedGraphCallScan* call_scan,
    std::vector<FilterConjunctInfo*>* active_conjuncts) {
  // This is just a CrossApply or OuterApply operation.
  const ResolvedScan* right_scan = call_scan->subquery();
  auto right_scan_algebrizer_cb =
      [this, right_scan](std::vector<FilterConjunctInfo*>* active_conjuncts) {
        return AlgebrizeScan(right_scan, active_conjuncts);
      };
  return AlgebrizeJoinScanInternal(
      call_scan->optional() ? JoinOp::kOuterApply : JoinOp::kCrossApply,
      /*join_expr=*/nullptr, call_scan->input_scan(),
      call_scan->subquery()->column_list(), right_scan_algebrizer_cb,
      active_conjuncts);
}

absl::StatusOr<std::unique_ptr<RelationalOp>> Algebrizer::AlgebrizeGraphRefScan(
    const ResolvedGraphRefScan* graph_ref_scan) {
  GOOGLESQL_RET_CHECK(graph_ref_scan != nullptr);
  GOOGLESQL_RET_CHECK(!current_input_op_in_graph_linear_scan_stack_.empty());
  GraphCompositeScanInput& composite_scan_input =
      current_input_op_in_graph_linear_scan_stack_.back();
  if (composite_scan_input.array_nest_expr == nullptr) {
    GOOGLESQL_RET_CHECK(composite_scan_input.op != nullptr);
    return std::move(composite_scan_input.op);
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto deref_arg,
      DerefExpr::Create(
          composite_scan_input.array_nest_expr->variable(),
          composite_scan_input.array_nest_expr->value_expr()->output_type()));
  return CreateScanOfTableAsArray(graph_ref_scan, /*is_value_table=*/false,
                                  std::move(deref_arg));
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeGraphCompositeScan(
    const ResolvedScan* graph_composite_scan, int composite_query_index,
    std::vector<FilterConjunctInfo*>* active_conjuncts) {
  if (composite_query_index == 0 ||
      !graph_composite_scan->Is<ResolvedSetOperationScan>()) {
    return AlgebrizeScan(graph_composite_scan, active_conjuncts);
  }

  // GraphRefScan should be algebrized to a deref expr against the assignment
  // list of LetOp.
  GOOGLESQL_RET_CHECK(!current_input_op_in_graph_linear_scan_stack_.empty());
  GraphCompositeScanInput& composite_scan_input =
      current_input_op_in_graph_linear_scan_stack_.back();

  // Wrap the last composite scan as ArrayNestExpr, and put it into assignment
  // list of LetOp that represents the current composite scan.
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ArrayNestExpr> array_nest_expr,
                   NestRelationInStruct(composite_scan_input.column_list,
                                        std::move(composite_scan_input.op),
                                        /*is_with_table=*/true));
  auto arg = std::make_unique<ExprArg>(
      variable_gen_->GetNewVariableName(
          absl::StrCat("$composite_query_", composite_query_index - 1)),
      std::move(array_nest_expr));
  composite_scan_input.array_nest_expr = arg.get();
  std::vector<std::unique_ptr<ExprArg>> assignment_list;
  assignment_list.push_back(std::move(arg));

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> set_op,
                   AlgebrizeScan(graph_composite_scan, active_conjuncts));

  // Build LetOp if the set operation is not in the first composite query.
  return LetOp::Create(std::move(assignment_list), /*cpp_assign=*/{},
                       std::move(set_op));
}

class CompositeQueryVisitor : public ResolvedASTVisitor {
 public:
  bool ContainsGraphSetOperation() const {
    return contains_graph_set_operation_;
  }
  bool ContainsCorrelatedColumnRefs() const {
    return contains_correlated_column_refs_;
  }

 private:
  absl::Status VisitResolvedColumnRef(const ResolvedColumnRef* node) override {
    if (node->is_correlated()) {
      contains_correlated_column_refs_ = true;
    }
    return absl::OkStatus();
  }

  absl::Status VisitResolvedSetOperationItem(
      const ResolvedSetOperationItem* node) override {
    if (node->scan()->Is<ResolvedGraphLinearScan>()) {
      contains_graph_set_operation_ = true;
    }
    return DefaultVisit(node);
  }

  bool contains_graph_set_operation_ = false;
  bool contains_correlated_column_refs_ = false;
};

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeGraphLinearScan(
    const ResolvedGraphLinearScan* graph_linear_scan,
    std::vector<FilterConjunctInfo*>* active_conjuncts) {
  GOOGLESQL_RET_CHECK(graph_linear_scan != nullptr);
  GOOGLESQL_RET_CHECK(!graph_linear_scan->scan_list().empty());

  // TODO: b/324451111 - Support correlated column reference with set operation
  // after NEXT.
  bool should_use_let_op = false;
  bool contains_correlated_column_refs = false;
  for (int idx = 0; idx < graph_linear_scan->scan_list_size(); ++idx) {
    CompositeQueryVisitor visitor;
    GOOGLESQL_RETURN_IF_ERROR(graph_linear_scan->scan_list(idx)->Accept(&visitor));
    if (idx > 0 && visitor.ContainsGraphSetOperation()) {
      should_use_let_op = true;
    }
    if (visitor.ContainsCorrelatedColumnRefs()) {
      contains_correlated_column_refs = true;
    }
  }
  if (should_use_let_op && contains_correlated_column_refs) {
    return absl::UnimplementedError(
        "Correlated columns in GQL with set operation after NEXT is not "
        "implemented yet");
  }

  std::unique_ptr<RelationalOp> child_op;
  for (int i = 0; i < graph_linear_scan->scan_list_size(); ++i) {
    const ResolvedScan* child_scan = graph_linear_scan->scan_list(i);
    GOOGLESQL_ASSIGN_OR_RETURN(
        child_op, AlgebrizeGraphCompositeScan(child_scan, i, active_conjuncts));

    if (i == 0) {
      current_input_op_in_graph_linear_scan_stack_.emplace_back();
    }
    current_input_op_in_graph_linear_scan_stack_.back() = {
        .op = std::move(child_op), .column_list = child_scan->column_list()};
  }
  child_op = std::move(current_input_op_in_graph_linear_scan_stack_.back().op);
  current_input_op_in_graph_linear_scan_stack_.pop_back();
  return child_op;
}

absl::StatusOr<std::unique_ptr<RelationalOp>> Algebrizer::AlgebrizeGraphScan(
    const ResolvedGraphScan* graph_scan,
    std::vector<FilterConjunctInfo*>* active_conjuncts) {
  GOOGLESQL_RET_CHECK(graph_scan != nullptr);
  if (graph_scan->input_scan() == nullptr) {
    // It's a plain graph scan.
    if (graph_scan->filter_expr() == nullptr) {
      return AlgebrizeGraphPathScanList(graph_scan->input_scan_list(),
                                        /*from_index=*/0, active_conjuncts);
    }

    return AlgebrizeFilterScanInternal(
        graph_scan->filter_expr(),
        absl::bind_front(&Algebrizer::AlgebrizeGraphPathScanList, this,
                         std::ref(graph_scan->input_scan_list()),
                         /*from_index=*/0),
        active_conjuncts);
  }
  // It's a correlated join + graph scan.
  size_t lhs_size = graph_scan->input_scan()->column_list().size();
  std::vector<ResolvedColumn> right_output_columns = {
      graph_scan->column_list().begin() + lhs_size,
      graph_scan->column_list().end()};

  auto right_scan_algebrizer_cb =
      [this, graph_scan](std::vector<FilterConjunctInfo*>* active_conjuncts_arg)
      -> absl::StatusOr<std::unique_ptr<RelationalOp>> {
    for (FilterConjunctInfo* info : *active_conjuncts_arg) {
      GOOGLESQL_RET_CHECK(!info->redundant);
      GOOGLESQL_RET_CHECK(!Intersects(graph_scan->input_scan()->column_list(),
                            info->referenced_columns));
    }
    // Ignore graph_scan->filter_expr(), which will become the join condition.
    return AlgebrizeGraphPathScanList(graph_scan->input_scan_list(),
                                      /*from_index=*/0, active_conjuncts_arg);
  };

  return AlgebrizeJoinScanInternal(
      graph_scan->optional() ? JoinOp::kLeftOuterJoin : JoinOp::kCrossApply,
      graph_scan->filter_expr(), graph_scan->input_scan(), right_output_columns,
      right_scan_algebrizer_cb, active_conjuncts);
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeGraphPathScanList(
    const std::vector<std::unique_ptr<const ResolvedGraphPathScan>>& path_list,
    int from_index, std::vector<FilterConjunctInfo*>* active_conjuncts) {
  GOOGLESQL_RET_CHECK_LT(from_index, path_list.size());
  const std::unique_ptr<const ResolvedGraphPathScan>& path_scan =
      path_list.at(from_index);
  if (from_index == path_list.size() - 1) {
    return AlgebrizeScan(path_scan.get(), active_conjuncts);
  }

  std::vector<ResolvedColumn> right_output_columns;
  for (int i = from_index + 1; i < path_list.size(); i++) {
    absl::c_copy(path_list.at(i)->column_list(),
                 std::back_inserter(right_output_columns));
  }
  // Cross join between the first path scan and join scan of the rest of the
  // paths.
  return AlgebrizeJoinScanInternal(
      JoinOp::kInnerJoin, /*join_expr=*/nullptr, path_scan.get(),
      right_output_columns,
      absl::bind_front(&Algebrizer::AlgebrizeGraphPathScanList, this,
                       std::ref(path_list), from_index + 1),
      active_conjuncts);
}

absl::StatusOr<Algebrizer::ConjunctsByGraphSubpaths>
Algebrizer::GetPushableActiveConjunctsByChildScans(
    absl::Span<const std::unique_ptr<const ResolvedGraphPathScanBase>>
        child_scans,
    const std::vector<FilterConjunctInfo*>& active_conjuncts) {
  ConjunctsByGraphSubpaths pushable_conjuncts;
  // `pushable_conjuncts`'s value vectors should also be stacks, so we iterate
  // in the same order as `active_conjuncts`.
  for (FilterConjunctInfo* conjunct_info : active_conjuncts) {
    if (!conjunct_info->is_non_volatile) {
      continue;
    }
    std::vector<const ResolvedGraphPathScanBase*> scans_with_intersection =
        GetScansWithIntersection(conjunct_info->referenced_columns,
                                 child_scans);
    if (scans_with_intersection.size() > 1) {
      // Cannot push to any child if intersects with >1 children scans.
      continue;
    }
    if (scans_with_intersection.size() == 1) {
      // Exactly one child intersects with conjunct. Can push to that child.
      GOOGLESQL_RET_CHECK(scans_with_intersection[0] != nullptr);
      pushable_conjuncts[scans_with_intersection[0]].push_back(conjunct_info);
      continue;
    }
    // Intersect with 0 children, push to all of them. (E.g. where FALSE)
    for (const auto& child_scan : child_scans) {
      pushable_conjuncts[child_scan.get()].push_back(conjunct_info);
    }
  }
  return pushable_conjuncts;
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeGraphQuantifiedPathScan(
    const ResolvedGraphPathScan* quantified_path_scan,
    std::unique_ptr<RelationalOp> path_primary_op,
    std::vector<FilterConjunctInfo*>* active_conjuncts) {
  std::unique_ptr<TupleSchema> child_schema =
      path_primary_op->CreateOutputSchema();
  QuantifiedGraphPathOp::VariablesInfo variables;
  variables.head = column_to_variable_->AssignNewVariableToColumn(
      quantified_path_scan->head());
  variables.tail = column_to_variable_->AssignNewVariableToColumn(
      quantified_path_scan->tail());
  const GraphPathType* path_type = nullptr;
  if (quantified_path_scan->path() != nullptr) {
    variables.path = column_to_variable_->AssignNewVariableToColumn(
        quantified_path_scan->path()->column());
    GOOGLESQL_RET_CHECK(quantified_path_scan->path()->column().type()->IsGraphPath());
    path_type = quantified_path_scan->path()->column().type()->AsGraphPath();
  }
  const Type* cost_type = nullptr;
  if (quantified_path_scan->path_cost() != nullptr) {
    VariableId cost_variable =
        column_to_variable_->variable_generator()->GetNewVariableName("$cost");
    variables.cost = cost_variable;
    cost_type = quantified_path_scan->path_cost()->cost_supertype();
    GOOGLESQL_RET_CHECK(cost_type->IsNumerical());
  }
  for (const std::unique_ptr<const ResolvedGraphMakeArrayVariable>& make_array :
       quantified_path_scan->group_variable_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(VariableId element,
                     column_to_variable_->LookupVariableNameForColumn(
                         make_array->element()));
    std::optional<int> idx = child_schema->FindIndexForVariable(element);
    GOOGLESQL_RET_CHECK(idx.has_value())
        << "Unable to find column " << make_array->element().DebugString()
        << " in quantified path scan.";
    VariableId group =
        column_to_variable_->AssignNewVariableToColumn(make_array->array());
    GOOGLESQL_RET_CHECK(make_array->array().type()->IsArray());
    variables.group_variables.push_back(
        QuantifiedGraphPathOp::GroupVariableInfo{
            .group_variable = group,
            // `array_type` is needed to create the array.
            .array_type = make_array->array().type()->AsArray(),
            .singleton_slot_index = *idx});
  }

  std::unique_ptr<ValueExpr> lower_bound, upper_bound;
  if (quantified_path_scan->quantifier()->lower_bound() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        lower_bound,
        AlgebrizeExpression(quantified_path_scan->quantifier()->lower_bound()));
  }
  if (quantified_path_scan->quantifier()->upper_bound() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        upper_bound,
        AlgebrizeExpression(quantified_path_scan->quantifier()->upper_bound()));
  }

  auto path_prefix_info = std::make_unique<GraphPathPrefixInfo>();
  if (!current_graph_path_context_stack_.empty()) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        path_prefix_info,
        CreateGraphPathPrefixInfo(current_graph_path_context_stack_.back()));
  }

  return QuantifiedGraphPathOp::Create(
      std::move(path_primary_op), std::move(variables), std::move(lower_bound),
      std::move(upper_bound), path_type, cost_type,
      std::move(path_prefix_info));
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeGraphPathScan(
    const ResolvedGraphPathScan* graph_path_scan,
    std::vector<FilterConjunctInfo*>* active_conjuncts) {
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<RelationalOp> returning_op,
      AlgebrizeGraphPathPrimaryScan(graph_path_scan, active_conjuncts));

  if (graph_path_scan->quantifier() != nullptr) {
    return AlgebrizeGraphQuantifiedPathScan(
        graph_path_scan, std::move(returning_op), active_conjuncts);
  }
  return returning_op;
}

absl::StatusOr<std::unique_ptr<GraphPathPrefixInfo>>
Algebrizer::CreateGraphPathPrefixInfo(const GraphPathContext& path_context) {
  auto path_prefix_info = std::make_unique<GraphPathPrefixInfo>();
  path_prefix_info->path_mode = path_context.path_mode;
  path_prefix_info->prefix_type = path_context.prefix_type;
  if (path_context.path_count != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(path_prefix_info->path_count,
                     AlgebrizeExpression(path_context.path_count));
  }
  return path_prefix_info;
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeGraphPathPrimaryScan(
    const ResolvedGraphPathScan* graph_path_scan,
    std::vector<FilterConjunctInfo*>* active_conjuncts) {
  GraphPathContext path_context;
  if (graph_path_scan->path_mode() != nullptr) {
    path_context.path_mode = graph_path_scan->path_mode()->path_mode();
  }
  if (graph_path_scan->search_prefix() != nullptr) {
    path_context.prefix_type = graph_path_scan->search_prefix()->type();
    if (graph_path_scan->search_prefix()->path_count() != nullptr) {
      path_context.path_count = graph_path_scan->search_prefix()->path_count();
    }
  }
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<GraphPathPrefixInfo> path_prefix_info,
                   CreateGraphPathPrefixInfo(path_context));

  // Push the path prefix context to the stack.
  current_graph_path_context_stack_.push_back(std::move(path_context));

  // Builds conjunct infos from path filter expressions.
  std::vector<std::unique_ptr<FilterConjunctInfo>> conjunct_infos;
  if (graph_path_scan->filter_expr() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(
        AddFilterConjunctsTo(graph_path_scan->filter_expr(), &conjunct_infos));
  }
  GOOGLESQL_RET_CHECK(active_conjuncts != nullptr);
  ConjunctsByGraphSubpaths pushable_conjuncts;
  if (algebrizer_options_.push_down_filters) {
    PushConjuncts(conjunct_infos, active_conjuncts);
    GOOGLESQL_ASSIGN_OR_RETURN(pushable_conjuncts, GetPushableActiveConjunctsByChildScans(
                                             graph_path_scan->input_scan_list(),
                                             *active_conjuncts));
  }
  // Pass empty conjuncts to children when `push_down_filters` is false.
  std::vector<FilterConjunctInfo*> empty_conjuncts;

  std::vector<GraphPathFactorOpInfo> path_factor_ops;
  path_factor_ops.reserve(graph_path_scan->input_scan_list_size());
  for (const std::unique_ptr<const ResolvedGraphPathScanBase>&
           path_factor_scan : graph_path_scan->input_scan_list()) {
    const ResolvedGraphPathScanBase* path_factor_scan_ptr =
        path_factor_scan.get();
    std::unique_ptr<RelationalOp> path_factor_op;
    if (algebrizer_options_.push_down_filters) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          path_factor_op,
          AlgebrizeScan(path_factor_scan_ptr,
                        &pushable_conjuncts[path_factor_scan_ptr]));
      // Unmark conjunctions as non-redundant because they can be used by
      // multiple children.
      for (FilterConjunctInfo* info :
           pushable_conjuncts[path_factor_scan_ptr]) {
        GOOGLESQL_RET_CHECK(info->redundant);
        info->redundant = false;
      }
    } else {
      GOOGLESQL_ASSIGN_OR_RETURN(path_factor_op,
                       AlgebrizeScan(path_factor_scan_ptr, &empty_conjuncts));
    }
    // Get output variables from the path factor. If it's an element scan,
    // column_list will have len = 1.
    std::vector<VariableId> variables;
    for (const ResolvedColumn& resolved_col : path_factor_scan->column_list()) {
      if (resolved_col.type()->IsGraphPath()) {
        // If the path factor is a subpath, then skip adding its path variable
        // to the list of output variables.
        continue;
      }
      GOOGLESQL_ASSIGN_OR_RETURN(
          VariableId variable,
          column_to_variable_->LookupVariableNameForColumn(resolved_col));
      variables.push_back(std::move(variable));
    }
    std::optional<ResolvedGraphEdgeScan::EdgeOrientation> orientation =
        path_factor_scan->Is<ResolvedGraphEdgeScan>()
            ? std::optional<ResolvedGraphEdgeScan::EdgeOrientation>(
                  path_factor_scan->GetAs<ResolvedGraphEdgeScan>()
                      ->orientation())
            : std::nullopt;
    std::optional<int> cost_slot_index = std::nullopt;
    if (path_factor_scan->Is<ResolvedGraphEdgeScan>() &&
        path_factor_scan->GetAs<ResolvedGraphEdgeScan>()->cost_expr() !=
            nullptr) {
      cost_slot_index = 1;
    }
    if (path_factor_scan->Is<ResolvedGraphPathScan>()) {
      int idx = static_cast<int>(variables.size());
      if (path_factor_scan->GetAs<ResolvedGraphPathScan>()->path() != nullptr) {
        ++idx;
      }
      if (path_factor_scan->GetAs<ResolvedGraphPathScan>()->path_cost() !=
          nullptr) {
        cost_slot_index = idx;
      }
    }
    path_factor_ops.push_back({std::move(variables), std::move(path_factor_op),
                               orientation, cost_slot_index});
  }
  if (algebrizer_options_.push_down_filters) {
    // Mark all as redundant. We've done sanity checks right after AlgebrizeScan
    // so it's safe to do so.
    for (const auto& [unused_element, conjuncts] : pushable_conjuncts) {
      for (FilterConjunctInfo* conjunct_info : conjuncts) {
        conjunct_info->redundant = true;
      }
    }
    GOOGLESQL_RETURN_IF_ERROR(PopConjuncts(conjunct_infos, active_conjuncts));
  }
  VariableId path;
  const GraphPathType* path_type = nullptr;
  if (graph_path_scan->path() != nullptr) {
    path = column_to_variable_->AssignNewVariableToColumn(
        graph_path_scan->path()->column());
    GOOGLESQL_RET_CHECK(graph_path_scan->path()->column().type()->IsGraphPath());
    path_type = graph_path_scan->path()->column().type()->AsGraphPath();
  }
  VariableId cost;
  const Type* cost_type = nullptr;
  if (graph_path_scan->path_cost() != nullptr) {
    cost =
        column_to_variable_->variable_generator()->GetNewVariableName("$cost");
    cost_type = graph_path_scan->path_cost()->cost_supertype();
    GOOGLESQL_RET_CHECK(cost_type->IsNumerical());
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<RelationalOp> graph_table_op,
      GraphPathOp::Create(std::move(path_factor_ops), path, path_type, cost,
                          cost_type, std::move(path_prefix_info)));

  // Pop the path context from the stack.
  current_graph_path_context_stack_.pop_back();

  GOOGLESQL_ASSIGN_OR_RETURN(auto algebrized_conjuncts,
                   AlgebrizeNonRedundantConjuncts(conjunct_infos));

  // Algebrize the filter.
  return ApplyAlgebrizedFilterConjuncts(std::move(graph_table_op),
                                        std::move(algebrized_conjuncts));
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeGraphElementScan(
    const ResolvedGraphElementScan* element_scan,
    std::vector<FilterConjunctInfo*>* active_conjuncts) {
  GOOGLESQL_RET_CHECK_EQ(element_scan->column_list_size(), 1)
      << "We expect exactly one column, representing the graph element";

  const Type* column_type = element_scan->column_list(0).type();
  GOOGLESQL_RET_CHECK(column_type->IsGraphElement());
  const GraphElementType* element_type = column_type->AsGraphElement();

  const VariableId element_variable =
      column_to_variable_->AssignNewVariableToColumn(
          element_scan->column_list(0));

  std::vector<UnionAllOp::Input> union_members;
  union_members.reserve(element_scan->target_element_table_list_size());
  for (const GraphElementTable* element_table :
       element_scan->target_element_table_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> relational_op,
                     AlgebrizeGraphElementTableScan(
                         element_variable, element_type, element_table));
    if (element_table->HasDynamicLabel()) {
      // For element table that had dynamic label, we will need to have extra
      // runtime filtering on label expr.
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<DerefExpr> deref,
                       DerefExpr::Create(element_variable, element_type));
      GOOGLESQL_ASSIGN_OR_RETURN(auto is_labeled_predicate,
                       GraphIsLabeledExpr::Create(std::move(deref),
                                                  element_scan->label_expr(),
                                                  /*is_negated=*/false));
      GOOGLESQL_ASSIGN_OR_RETURN(relational_op,
                       FilterOp::Create(std::move(is_labeled_predicate),
                                        std::move(relational_op)));
    }
    GOOGLESQL_ASSIGN_OR_RETURN(union_members.emplace_back(),
                     BuildUnionAllInput(element_variable, element_type,
                                        std::move(relational_op)));
  }

  if (union_members.empty()) {
    GOOGLESQL_ASSIGN_OR_RETURN(auto const_expr, ConstExpr::Create(Value::Int64(0)));
    return EnumerateOp::Create(std::move(const_expr));
  }
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<RelationalOp> relational_op,
                   UnionAllOp::Create(std::move(union_members)));

  if (element_scan->filter_expr() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> filter_expr,
                     AlgebrizeExpression(element_scan->filter_expr()));
    GOOGLESQL_ASSIGN_OR_RETURN(relational_op, FilterOp::Create(std::move(filter_expr),
                                                     std::move(relational_op)));
  }

  if (element_scan->Is<ResolvedGraphEdgeScan>() &&
      element_scan->GetAs<ResolvedGraphEdgeScan>()->cost_expr() != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<ValueExpr> cost_expr,
        AlgebrizeExpression(
            element_scan->GetAs<ResolvedGraphEdgeScan>()->cost_expr()));
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> validated_cost_expr,
                     GraphCostExpr::Create(std::move(cost_expr)));
    std::vector<std::unique_ptr<ExprArg>> cost_projection_arg;
    VariableId cost_variable =
        column_to_variable_->variable_generator()->GetNewVariableName("$cost");
    cost_projection_arg.push_back(std::make_unique<ExprArg>(
        cost_variable, std::move(validated_cost_expr)));
    GOOGLESQL_ASSIGN_OR_RETURN(relational_op,
                     ComputeOp::Create(std::move(cost_projection_arg),
                                       std::move(relational_op)));
  }
  return relational_op;
}

absl::Status Algebrizer::AlgebrizeExpressionList(
    absl::Span<const std::unique_ptr<const ResolvedExpr>> expr_list,
    std::vector<std::unique_ptr<ValueExpr>>& output_value_list) {
  output_value_list.clear();
  output_value_list.reserve(expr_list.size());
  for (const std::unique_ptr<const ResolvedExpr>& expr : expr_list) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> value_expr,
                     AlgebrizeExpression(expr.get()));
    output_value_list.push_back(std::move(value_expr));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<ValueExpr>>
Algebrizer::AlgebrizeDynamicProperties(const GraphElementType* element_type,
                                       const ResolvedExpr* resolved_expr) {
  std::unique_ptr<ValueExpr> dynamic_property_expr;
  if (element_type->is_dynamic()) {
    if (resolved_expr != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(dynamic_property_expr,
                       AlgebrizeExpression(resolved_expr));
    } else {
      GOOGLESQL_ASSIGN_OR_RETURN(dynamic_property_expr,
                       ConstExpr::Create(Value::Json(JSONValue())));
    }
  }
  return dynamic_property_expr;
}

absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeDynamicLabel(
    const ResolvedExpr* resolved_expr) {
  if (resolved_expr == nullptr) {
    return nullptr;
  }
  return AlgebrizeExpression(resolved_expr);
}

absl::StatusOr<std::unique_ptr<NewGraphElementExpr>>
Algebrizer::AlgebrizeGraphMakeElement(
    const ResolvedGraphMakeElement* make_graph_element) {
  std::vector<NewGraphElementExpr::Property> properties;
  properties.reserve(make_graph_element->property_list_size());

  const GraphElementTable* element_table =
      make_graph_element->identifier()->element_table();
  for (const std::unique_ptr<const ResolvedGraphElementProperty>& property :
       make_graph_element->property_list()) {
    std::string prop_name = property->declaration()->Name();
    const GraphPropertyDefinition* property_def;
    // Checks the GraphElementTable for property existence and skip the
    // properties that do not exist.
    absl::Status status =
        element_table->FindPropertyDefinitionByName(prop_name, property_def);
    if (absl::IsNotFound(status)) {
      continue;
    }
    GOOGLESQL_RETURN_IF_ERROR(status);
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> property_expr,
                     AlgebrizeExpression(property->expr()));
    properties.push_back(NewGraphElementExpr::Property{
        .name = prop_name, .definition = std::move(property_expr)});
  }

  std::vector<std::unique_ptr<ValueExpr>> key;
  GOOGLESQL_RETURN_IF_ERROR(AlgebrizeExpressionList(
      make_graph_element->identifier()->key_list(), key));

  std::vector<std::unique_ptr<ValueExpr>> src_node_key;
  std::vector<std::unique_ptr<ValueExpr>> dest_node_key;
  if (element_table->Is<GraphEdgeTable>()) {
    GOOGLESQL_RETURN_IF_ERROR(AlgebrizeExpressionList(
        make_graph_element->identifier()->source_node_identifier()->key_list(),
        src_node_key));
    GOOGLESQL_RETURN_IF_ERROR(AlgebrizeExpressionList(
        make_graph_element->identifier()->dest_node_identifier()->key_list(),
        dest_node_key));
  }

  const GraphElementType* element_type =
      make_graph_element->type()->AsGraphElement();
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> dynamic_property_expr,
                   AlgebrizeDynamicProperties(
                       element_type, make_graph_element->dynamic_properties()));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> dynamic_label_expr,
                   AlgebrizeDynamicLabel(make_graph_element->dynamic_labels()));
  return NewGraphElementExpr::Create(
      element_type, make_graph_element->identifier()->element_table(),
      std::move(key), std::move(properties), std::move(dynamic_property_expr),
      std::move(dynamic_label_expr), std::move(src_node_key),
      std::move(dest_node_key));
}

absl::StatusOr<std::unique_ptr<GraphGetElementPropertyExpr>>
Algebrizer::AlgebrizeGraphGetElementProperty(
    const ResolvedGraphGetElementProperty* get_graph_element_property) {
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> value,
                   AlgebrizeExpression(get_graph_element_property->expr()));
  std::string property_name;
  if (get_graph_element_property->property() != nullptr) {
    property_name = get_graph_element_property->property()->Name();
    if (get_graph_element_property->property_name() != nullptr) {
      get_graph_element_property->property_name()->MarkFieldsAccessed();
    }
  } else {
    const ResolvedExpr* expr = get_graph_element_property->property_name();
    GOOGLESQL_RET_CHECK_NE(expr, nullptr);
    GOOGLESQL_RET_CHECK(expr->Is<ResolvedLiteral>());
    GOOGLESQL_RET_CHECK(expr->GetAs<ResolvedLiteral>()->type()->IsString());
    property_name = expr->GetAs<ResolvedLiteral>()->value().string_value();
  }
  return GraphGetElementPropertyExpr::Create(get_graph_element_property->type(),
                                             property_name, std::move(value));
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeGraphElementTableScan(
    VariableId element_variable_id, const GraphElementType* element_type,
    const GraphElementTable* element_table) {
  absl::Cleanup catalog_column_ref_var_cleaner = [this]() {
    catalog_column_ref_variables_.reset();
  };
  catalog_column_ref_variables_ =
      absl::flat_hash_map<const Column*, VariableId>{};

  if (element_table->kind() == GraphElementTable::Kind::kNode) {
    // Tabular property graph conversion for nodes:
    //  generate one unique node for each row in node table.
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<RelationalOp> base_node_table,
        CreateGraphElementTableScan(
            *element_table, *variable_gen_, algebrizer_options_,
            language_options_, *type_factory_, *catalog_column_ref_variables_));
    return ComputeGraphElements(element_variable_id, std::move(base_node_table),
                                element_table, element_type);
  }

  // Tabular property graph conversion for edges:
  //   For each row in edge table:
  //      1) Find nodes that matches edge source key;
  //      2) Find nodes that matches edge destination key;
  //      3) Generates edges for each pair of source node, destination
  //         node.
  //   This translates into
  //    JOIN(edge table scan,
  //         source node table scan,
  //         destination node table scan).
  //
  // There is a shortcut when we can skip joining source/destination node table
  // scan when the node reference is EXACTLY the node keys.
  const GraphEdgeTable* edge_table = element_table->AsEdgeTable();

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<RelationalOp> edge_scan,
      CreateGraphElementTableScan(
          *edge_table, *variable_gen_, algebrizer_options_, language_options_,
          *type_factory_, *catalog_column_ref_variables_));

  absl::flat_hash_map<const Column*, VariableId> source_node_variables,
      dest_node_variables;
  GOOGLESQL_ASSIGN_OR_RETURN(edge_scan, AlgebrizeGraphNodeReference(
                                  edge_table, *edge_table->GetSourceNodeTable(),
                                  std::move(edge_scan), source_node_variables));
  GOOGLESQL_ASSIGN_OR_RETURN(edge_scan, AlgebrizeGraphNodeReference(
                                  edge_table, *edge_table->GetDestNodeTable(),
                                  std::move(edge_scan), dest_node_variables));
  return ComputeGraphElements(element_variable_id, std::move(edge_scan),
                              element_table, element_type,
                              &source_node_variables, &dest_node_variables);
}

absl::StatusOr<std::unique_ptr<RelationalOp>> Algebrizer::ComputeGraphElements(
    VariableId element_variable_id, std::unique_ptr<RelationalOp> base_table,
    const GraphElementTable* element_table,
    const GraphElementType* element_type,
    const absl::flat_hash_map<const Column*, VariableId>* source_node_vars,
    const absl::flat_hash_map<const Column*, VariableId>* dest_node_vars) {
  // Algebrize static property definitions
  std::vector<NewGraphElementExpr::Property> properties;
  properties.reserve(element_type->property_types().size());

  // Use the same ordering of properties for all node tables
  for (const PropertyType& property : element_type->property_types()) {
    const GraphPropertyDefinition* property_definition;
    const absl::Status property_lookup_status =
        element_table->FindPropertyDefinitionByName(property.name,
                                                    property_definition);
    if (property_lookup_status.ok()) {
      GOOGLESQL_ASSIGN_OR_RETURN(const ResolvedExpr* resolved_expr,
                       property_definition->GetValueExpression());
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> property_expr,
                       AlgebrizeExpression(resolved_expr));

      properties.push_back(
          {.name = property.name, .definition = std::move(property_expr)});
    } else if (!absl::IsNotFound(property_lookup_status)) {
      // If property is not found for this element table. The corresponding
      // graph element value will not contain this property.
      return property_lookup_status;
    }
  }

  GOOGLESQL_RET_CHECK(!element_table->GetKeyColumns().empty());
  GOOGLESQL_ASSIGN_OR_RETURN(std::vector<std::unique_ptr<ValueExpr>> keys,
                   GetColumnReferences(*element_table->GetTable(),
                                       element_table->GetKeyColumns(),
                                       *catalog_column_ref_variables_));

  std::vector<std::unique_ptr<ValueExpr>> src_node_key;
  std::vector<std::unique_ptr<ValueExpr>> dest_node_key;
  if (element_table->Is<GraphEdgeTable>()) {
    GOOGLESQL_RET_CHECK(source_node_vars != nullptr);
    GOOGLESQL_RET_CHECK(dest_node_vars != nullptr);
    const GraphEdgeTable* edge_table = element_table->AsEdgeTable();
    GOOGLESQL_RET_CHECK(!edge_table->GetSourceNodeTable()->GetEdgeTableColumns().empty());
    GOOGLESQL_RET_CHECK(!edge_table->GetDestNodeTable()->GetEdgeTableColumns().empty());
    GOOGLESQL_ASSIGN_OR_RETURN(src_node_key,
                     GetColumnReferences(*edge_table->GetSourceNodeTable()
                                              ->GetReferencedNodeTable()
                                              ->GetTable(),
                                         edge_table->GetSourceNodeTable()
                                             ->GetReferencedNodeTable()
                                             ->GetKeyColumns(),
                                         *source_node_vars));
    GOOGLESQL_ASSIGN_OR_RETURN(dest_node_key,
                     GetColumnReferences(*edge_table->GetDestNodeTable()
                                              ->GetReferencedNodeTable()
                                              ->GetTable(),
                                         edge_table->GetDestNodeTable()
                                             ->GetReferencedNodeTable()
                                             ->GetKeyColumns(),
                                         *dest_node_vars));
  }

  const ResolvedExpr* resolved_dynamic_prop_expr = nullptr;
  if (element_table->HasDynamicProperties()) {
    const GraphDynamicProperties* dynamic_properties;
    GOOGLESQL_RETURN_IF_ERROR(element_table->GetDynamicProperties(dynamic_properties));
    GOOGLESQL_ASSIGN_OR_RETURN(resolved_dynamic_prop_expr,
                     dynamic_properties->GetValueExpression());
  }
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ValueExpr> dynamic_property_expr,
      AlgebrizeDynamicProperties(element_type, resolved_dynamic_prop_expr));
  const ResolvedExpr* resolved_dynamic_label_expr = nullptr;
  if (element_table->HasDynamicLabel()) {
    const GraphDynamicLabel* dynamic_label;
    GOOGLESQL_RETURN_IF_ERROR(element_table->GetDynamicLabel(dynamic_label));
    GOOGLESQL_ASSIGN_OR_RETURN(resolved_dynamic_label_expr,
                     dynamic_label->GetValueExpression());
  }
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> dynamic_label_expr,
                   AlgebrizeDynamicLabel(resolved_dynamic_label_expr));
  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ValueExpr> make_graph_element_expr,
      NewGraphElementExpr::Create(
          element_type, element_table, std::move(keys), std::move(properties),
          std::move(dynamic_property_expr), std::move(dynamic_label_expr),
          std::move(src_node_key), std::move(dest_node_key)));

  std::vector<std::unique_ptr<ExprArg>> element_projection_arg;
  element_projection_arg.push_back(std::make_unique<ExprArg>(
      element_variable_id, std::move(make_graph_element_expr)));
  return ComputeOp::Create(std::move(element_projection_arg),
                           std::move(base_table));
}

absl::StatusOr<std::unique_ptr<RelationalOp>>
Algebrizer::AlgebrizeGraphNodeReference(
    const GraphEdgeTable* edge_table,
    const GraphNodeTableReference& node_reference,
    std::unique_ptr<RelationalOp> input_scan,
    absl::flat_hash_map<const Column*, VariableId>& node_reference_vars) {
  // If we know that source or destination node table is referenced by its
  // element key, we don't need to join the source or destination node table
  // to compute edges.
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto node_vars_from_edge_references,
      TryGetNodeReferencesFromEdgeReferences(edge_table, node_reference,
                                             *catalog_column_ref_variables_));
  if (node_vars_from_edge_references.has_value()) {
    node_reference_vars = *std::move(node_vars_from_edge_references);
    return input_scan;
  }
  return JoinNodeReferences(
      *edge_table->GetTable(), std::move(input_scan), node_reference,
      algebrizer_options_, language_options_, *catalog_column_ref_variables_,
      node_reference_vars, *variable_gen_, *type_factory_);
}

absl::StatusOr<std::unique_ptr<ValueExpr>> Algebrizer::AlgebrizeArrayAggregate(
    const ResolvedArrayAggregate* array_aggregate) {
  absl::Cleanup restore_original_column_to_variable_map =
      [this, original_column_to_variable = column_to_variable_->map()] {
        column_to_variable_->set_map(original_column_to_variable);
      };
  // Element variable needs to be created before it's referenced in
  // computed_column_list.
  VariableId element_variable = column_to_variable_->AssignNewVariableToColumn(
      array_aggregate->element_column());
  std::vector<std::unique_ptr<ExprArg>> expr_list;
  for (const std::unique_ptr<const ResolvedComputedColumn>& computed_column :
       array_aggregate->pre_aggregate_computed_column_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> argument,
                     AlgebrizeExpression(computed_column->expr()));
    expr_list.push_back(std::make_unique<ExprArg>(
        column_to_variable_->AssignNewVariableToColumn(
            computed_column->column()),
        std::move(argument)));
  }
  std::vector<std::unique_ptr<ValueExpr>> arguments;
  for (const std::unique_ptr<const ResolvedExpr>& argument_expr :
       array_aggregate->aggregate()->argument_list()) {
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> argument,
                     AlgebrizeExpression(argument_expr.get()));
    arguments.push_back(std::move(argument));
  }
  VariableId position_variable =
      column_to_variable_->variable_generator()->GetNewVariableName("$order");
  std::vector<std::unique_ptr<KeyArg>> order_by_key_override;
  if (array_aggregate->aggregate()->function()->SupportsOrderingArguments() &&
      array_aggregate->aggregate()->order_by_item_list().empty()) {
    // We want the ORDER BY of the aggregate to match the array's order. We
    // can't change the ResolvedAggregateFunctionCall directly because it's
    // immutable and even if we made a copy we'd have to gin up a new
    // ResolvedColumn for the position. Instead, we supply the ORDER BY list out
    // of band.
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> deref_position,
                     DerefExpr::Create(position_variable, types::Int64Type()));
    order_by_key_override.push_back(
        std::make_unique<KeyArg>(position_variable, std::move(deref_position)));
  }
  VariableId agg_variable =
      column_to_variable_->variable_generator()->GetNewVariableName("$agg");
  GOOGLESQL_RET_CHECK(array_aggregate->aggregate()->group_by_list().empty() &&
            array_aggregate->aggregate()->group_by_aggregate_list().empty());
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<AggregateArg> aggregate,
                   AlgebrizeAggregateFnWithAlgebrizedArguments(
                       agg_variable, /*anonymization_options=*/{},
                       /*filter=*/nullptr, array_aggregate->aggregate(),
                       std::move(arguments), /*inner_grouping_keys=*/{},
                       /*inner_aggregators=*/{},
                       /*side_effects_variable=*/VariableId(),
                       std::move(order_by_key_override)));
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> input_array_expr,
                   AlgebrizeExpression(array_aggregate->array()));
  return ArrayAggregateExpr::Create(std::move(input_array_expr),
                                    element_variable, position_variable,
                                    std::move(expr_list), std::move(aggregate));
}

absl::StatusOr<std::unique_ptr<ValueExpr>>
Algebrizer::AlgebrizeGraphIsLabeledPredicate(
    const ResolvedGraphIsLabeledPredicate* predicate) {
  predicate->MarkFieldsAccessed();
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<ValueExpr> element,
                   AlgebrizeExpression(predicate->expr()));
  return GraphIsLabeledExpr::Create(std::move(element), predicate->label_expr(),
                                    predicate->is_not());
}

}  // namespace googlesql
