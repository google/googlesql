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

#include "googlesql/analyzer/substitute.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/analyzer/analyzer_impl.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/internal_analyzer_options.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/multi_catalog.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_deep_copy_visitor.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/rewrite_utils.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {

// A constant that represents the context_id used to build function signatures
// for lambda functions used in AnalyzeSubstitute.
static const int kSubstitutionLambdaContextId = -1000;

// A ColumnRefLayer represents the columns, keyed by column id, referred to by
// subqueries below a certain subquery. These references end up getting added to
// the parameter list of the corresponding subquery.
// These ColumnRef pointers point to ColumnRefs in parameter_list of lambdas,
// they are valid during the AnalyzeSubstituteCall.
using ColumnRefLayer = absl::flat_hash_map<int, const ResolvedColumnRef*>;

// Adds a new ColumnRefLayer to a stack of such layers, representing the columns
// referenced by a stack of subqueries. The new layer represents the innermost
// subquery. On destruction, if the subquery is not the outermost subquery,
// propagates any new column refs from the inner layers to the next layer out.
// This is done because any columns referenced by inner subqueries which are not
// created in those subqueries must also be referenced in the parameter lists of
// subqueries higher in the stack.
class ScopedColumnRefLayer {
 public:
  explicit ScopedColumnRefLayer(std::vector<ColumnRefLayer>& column_refs_stack)
      : column_refs_stack_(column_refs_stack) {
    // Adds a new layer to the column refs stack.
    column_refs_stack_.emplace_back();
  }

  // Pops the top layer of the column refs stack.
  ~ScopedColumnRefLayer() {
    ABSL_DCHECK(!column_refs_stack_.empty());
    if (column_refs_stack_.empty()) {
      return;
    }
    auto layer_count = column_refs_stack_.size();
    if (layer_count > 1) {
      // Move column refs in the current layer into the next upper layer.
      const ColumnRefLayer& current_layer = column_refs_stack_[layer_count - 1];
      ColumnRefLayer& upper_layer = column_refs_stack_[layer_count - 2];
      for (auto& col : current_layer) {
        upper_layer.insert({col.first, col.second});
      }
    }
    column_refs_stack_.pop_back();
  }

  std::vector<ColumnRefLayer>& column_refs_stack_;
};

// A visitor that manages replacement of column refs and columns in a lambda
// body ResolvedAST node. It replaces column refs with the
// corresponding arguments from the substitution template, and remaps columns
// not present in 'column_map' with new column ids based on 'column_factory'.
// This is separate from the VariableReplacementInserter to avoid unintended
// interactions.
// TODO: Migrate to ResolvedASTRewriteVisitor.
class ColumnRefReplacer : public ResolvedASTDeepCopyVisitor {
 public:
  // 'column_ref_map' is a map of column IDs to the column that should replace
  // references to that column ID. The column IDs will be the IDs of the
  // lambda's args columns. The ResolvedColumns will be the corresponding args
  // to the named lambda function from the expression.
  explicit ColumnRefReplacer(
      const absl::flat_hash_map<int, const ResolvedColumnRef*>& column_ref_map,
      ColumnReplacementMap& column_map, ColumnFactory& column_factory)
      : lambda_column_ref_map_(column_ref_map),
        column_map_(column_map),
        column_factory_(column_factory) {}

  absl::StatusOr<ResolvedColumn> CopyResolvedColumn(
      const ResolvedColumn& column) override {
    if (!column_map_.contains(column)) {
      column_map_[column] = column_factory_.MakeCol(
          column.table_name(), column.name(), column.annotated_type());
    }
    return column_map_[column];
  }

  absl::Status VisitResolvedColumnRef(const ResolvedColumnRef* node) override;

 private:
  const absl::flat_hash_map<int, const ResolvedColumnRef*>&
      lambda_column_ref_map_;
  // Map from the column ID in the input ResolvedAST to the column allocated
  // from column_factory_.
  ColumnReplacementMap& column_map_;

  // All ResolvedColumns in the copied ResolvedAST will have new column ids
  // allocated by column_factory_.
  ColumnFactory& column_factory_;
};

absl::Status ColumnRefReplacer::VisitResolvedColumnRef(
    const ResolvedColumnRef* node) {
  const ResolvedColumn& column = node->column();
  auto it = lambda_column_ref_map_.find(column.column_id());
  if (it == lambda_column_ref_map_.end()) {
    return CopyVisitResolvedColumnRef(node);
  }

  // Map lambda argument columns to columns that they are representing.
  // If either node or target_column is correlated, the new column ref is
  // correlated.
  const ResolvedColumnRef* target_column_ref = it->second;
  const ResolvedColumn& target_column = target_column_ref->column();
  PushNodeToStack(MakeResolvedColumnRef(
      target_column.type(), target_column,
      node->is_correlated() || target_column_ref->is_correlated()));
  return absl::OkStatus();
}

// This function makes a deep copy of the ResolvedAST 'node' associated with a
// lambda body. All of the ResolvedColumns beneath 'node' are replaced with new
// columns with IDs allocated by 'column_factory' or directly remapped if such a
// mapping exists in 'column_map'. All of the ResolvedColumnReferences beneath
// 'node' will be replaced according to 'column_ref_map', which is a map
// of lambda argument column IDs to the column references they represent in
// the overall query.
absl::StatusOr<std::unique_ptr<ResolvedExpr>> ReplaceColumnRefsAndRemapColumns(
    const ResolvedExpr* node,
    absl::flat_hash_map<int, const ResolvedColumnRef*>& column_ref_map,
    ColumnReplacementMap& column_map, ColumnFactory& column_factory) {
  ColumnRefReplacer column_map_replacer(column_ref_map, column_map,
                                        column_factory);
  GOOGLESQL_RETURN_IF_ERROR(node->Accept(&column_map_replacer));
  return column_map_replacer.ConsumeRootNode<ResolvedExpr>();
}

// TODO: Migrate to ResolvedASTRewriteVisitor.
class VariableReplacementInserter : public ResolvedASTDeepCopyVisitor {
 public:
  // 'referenced_columns' is inserted into the parameter list for the
  // outermost subquery.
  //
  // This deep copy visitor does that replacement work. We use
  // AnalyzerOptions::lookup_expression_callback_ for in-place ResolvedExpr
  // substitution based on column name lookup. We use in-place substitution for
  // 'lambdas' as they need to be evaluated at the location specified by
  // 'expression'.
  VariableReplacementInserter(
      const std::vector<std::unique_ptr<const ResolvedColumnRef>>&
          referenced_columns,
      const absl::flat_hash_map<std::string, const ResolvedInlineLambda*>&
          lambdas,
      ColumnFactory* column_factory);

  absl::Status VisitResolvedSubqueryExpr(
      const ResolvedSubqueryExpr* node) override {
    return ProcessCorrelatedContext(node);
  }

  absl::Status VisitResolvedInlineLambda(
      const ResolvedInlineLambda* node) override {
    return ProcessCorrelatedContext(node);
  }

  absl::Status VisitResolvedFunctionCall(
      const ResolvedFunctionCall* node) override;

 private:
  // Returns if the enclosing SubqueryExpr is the outermost.
  bool InOutermostSubquery() { return column_refs_stack_.size() == 1; }

  // Template specialization for the 3 special nodes so that we can use a single
  // template containing CopyVisitXyz().
  template <typename ResolvedNodeType>
  absl::Status CopyVisitNode(const ResolvedNodeType* node) {
    GOOGLESQL_RET_CHECK_FAIL() << "Unexpected node type: "
                     << ResolvedNodeKind_Name(node->node_kind());
  }
  template <>
  absl::Status CopyVisitNode(const ResolvedSubqueryExpr* node) {
    return CopyVisitResolvedSubqueryExpr(node);
  }
  template <>
  absl::Status CopyVisitNode(const ResolvedInlineLambda* node) {
    return CopyVisitResolvedInlineLambda(node);
  }
  template <>
  absl::Status CopyVisitNode(const ResolvedJoinScan* node) {
    return CopyVisitResolvedJoinScan(node);
  }

  template <typename ResolvedNodeType>
  absl::Status ProcessCorrelatedContext(const ResolvedNodeType* node) {
    ScopedColumnRefLayer scoped_column_ref_layer(column_refs_stack_);

    // Leverage template specialization to dispatch to the correct
    // CopyVisitXyz() method.
    GOOGLESQL_RETURN_IF_ERROR(CopyVisitNode(node->template GetAs<ResolvedNodeType>()));
    std::unique_ptr<ResolvedNodeType> subquery_expr =
        ConsumeTopOfStack<ResolvedNodeType>();
    if (InOutermostSubquery()) {
      // Add column refs in projected 'variables' to outermost SubqueryExpr.
      for (const auto& ref : referenced_columns_) {
        GOOGLESQL_RETURN_IF_ERROR(CopyVisitResolvedColumnRef(ref.get()));
        subquery_expr->add_parameter_list(
            ConsumeTopOfStack<ResolvedColumnRef>());
      }
    }

    GOOGLESQL_RETURN_IF_ERROR(AddLambdaColumnRefsToParameterList(subquery_expr.get()));

    PushNodeToStack(std::move(subquery_expr));
    return absl::OkStatus();
  }

  // Adds collected lambda column refs to parameter_list of `node`, which can be
  // either a SubqueryExpr or an InlineLambda.
  // If `node` is the outermost, column refs are added unchanged from the
  // original lambda parameter_list.
  // If 'node' is not the outermost, column refs are added to
  // parameter_list as correlated.
  template <typename ResolvedNodeType>
  absl::Status AddLambdaColumnRefsToParameterList(ResolvedNodeType* node) {
    // Build a set of added column ids to avoid duplicates.
    absl::flat_hash_set<int> added_column_id_set;
    for (const auto& ref : node->parameter_list()) {
      added_column_id_set.emplace(ref->column().column_id());
    }

    for (const auto& id_and_column : column_refs_stack_.back()) {
      if (!added_column_id_set.insert(id_and_column.first).second) {
        continue;
      }

      GOOGLESQL_ASSIGN_OR_RETURN(auto copy, ProcessNode(id_and_column.second));
      if (!InOutermostSubquery()) {
        copy->set_is_correlated(true);
      }
      node->add_parameter_list(std::move(copy));
    }
    return absl::OkStatus();
  }

  const std::vector<std::unique_ptr<const ResolvedColumnRef>>&
      referenced_columns_;
  const absl::flat_hash_map<std::string, const ResolvedInlineLambda*>& lambdas_;

  // A stack of ColumnRef collection for recording lambda column refs.
  // One layer is created for each SubqueryExpr. ColumnRefs collected are
  // added to the parameter_list of the SubqueryExpr.
  std::vector<ColumnRefLayer> column_refs_stack_;

  ColumnFactory* column_factory_;
};

// This object holds options, catalog and type_factory so the interior functions
// don't each have to take them as params.
class ExpressionSubstitutor {
 public:
  ExpressionSubstitutor(AnalyzerOptions options, Catalog& catalog,
                        ColumnFactory* column_factory,
                        TypeFactory& type_factory);

  absl::StatusOr<std::unique_ptr<ResolvedExpr>> Substitute(
      absl::string_view expression,
      const absl::flat_hash_map<std::string, const ResolvedExpr*>& variables,
      const absl::flat_hash_map<std::string, const ResolvedInlineLambda*>&
          lambdas,
      AnnotatedType target_type);

 private:
  // Sets up the catalog with injected lambda functions.
  absl::Status SetupLambdasCatalog(
      const absl::flat_hash_map<std::string, const ResolvedInlineLambda*>&
          lambdas);

  AnalyzerOptions options_;
  TypeFactory& type_factory_;

  // The following are used to setup a catalog with lambda functions.
  // They are not initialized when there is no lambda involved.
  std::unique_ptr<SimpleCatalog> lambdas_catalog_;
  std::unique_ptr<MultiCatalog> multi_catalog_with_lambdas_;

  // Points the catalog in for methods of this class.
  // Points to the input catalog if there are no lambdas or the lazily
  // initialized multi_catalog_with_lambdas_ if there is any lambda.
  Catalog* catalog_;

  ColumnFactory* column_factory_;
};

ExpressionSubstitutor::ExpressionSubstitutor(AnalyzerOptions options,
                                             Catalog& catalog,
                                             ColumnFactory* column_factory,
                                             TypeFactory& type_factory)
    : options_(std::move(options)),
      type_factory_(type_factory),
      catalog_(&catalog),
      column_factory_(column_factory) {
  options_.set_parameter_mode(ParameterMode::PARAMETER_NAMED);

  // We expect options_ to come from the outer analysis of the overall query.
  // It would not be unusual for the analyzer to have some parameters, but we
  // don't need them here, because this API disallows expressions with
  // parameters.
  options_.clear_positional_query_parameters();
  options_.clear_query_parameters();

  // Do not record location info. The SQL is generated so any location info
  // returned would be meaningless to the end user.
  options_.set_record_parse_locations(false);
}

// Lambdas are invoked by name in the 'expression' parameter of
// AnalyzeSubstitute. Since these lambdas are not built in functions, a catalog
// has to be setup that injects these lambdas as named functions before
// analyzing the input expression.
absl::Status ExpressionSubstitutor::SetupLambdasCatalog(
    const absl::flat_hash_map<std::string, const ResolvedInlineLambda*>&
        lambdas) {
  lambdas_catalog_ = std::make_unique<SimpleCatalog>("lambdas_catalog");
  for (const auto& [name, lambda] : lambdas) {
    // The function group is set to 'SubstitutionLambda' to differentiate it
    // as an injected lambda function.
    auto lambda_function = std::make_unique<Function>(
        /*name=*/name, /*group=*/"SubstitutionLambda", Function::SCALAR,
        FunctionOptions());

    // When the lambda body has a type annotation map, that type annotation map
    // needs to be conveyed to the fake function we're creating as a body
    // placeholder.
    FunctionSignatureOptions signature_options;

    // Always set the custom callback. We already did the propagation, and know
    // the annotation of the output. More importantly, we cannot always express
    // the correct templated relationships in the general case.
    signature_options.set_compute_result_annotations_callback(
        [map = lambda->body()->type_annotation_map()](
            const ResolvedFunctionCallBase& call,
            TypeFactory& type_factory) -> absl::StatusOr<const AnnotationMap*> {
          return map;
        });

    // We add a signature for the injected lambda function with the following
    // properties. The context_id is set to kSubstitutionLambdaContextId
    // in order to differentiate it as an injected lambda function. The result
    // type is set to the type of the lambda body expression.
    lambda_function->AddSignature(FunctionSignature(
        /*result_type=*/{lambda->body()->type()},
        /*arguments=*/
        {{SignatureArgumentKind::ARG_TYPE_ARBITRARY,
          FunctionArgumentType::REPEATED}},
        /*context_id=*/kSubstitutionLambdaContextId, signature_options));
    lambdas_catalog_->AddOwnedFunction(std::move(lambda_function));
  }

  GOOGLESQL_RETURN_IF_ERROR(MultiCatalog::Create(catalog_->FullName(),
                                       {lambdas_catalog_.get(), catalog_},
                                       &multi_catalog_with_lambdas_));
  // Point the catalog in use to the new catalog.
  catalog_ = multi_catalog_with_lambdas_.get();

  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<ResolvedExpr>> ExpressionSubstitutor::Substitute(
    absl::string_view expression,
    const absl::flat_hash_map<std::string, const ResolvedExpr*>& variables,
    const absl::flat_hash_map<std::string, const ResolvedInlineLambda*>&
        lambdas,
    AnnotatedType target_type) {
  // Set up catalog with function signatures added for each named lambda.
  if (!lambdas.empty()) {
    GOOGLESQL_RETURN_IF_ERROR(SetupLambdasCatalog(lambdas));
  }

  // This select list introduces the aliases for the 'variables' that are in
  // turn used in 'expression' as columns.
  std::vector<std::string> select_list;
  select_list.reserve(variables.size());

  // Any referenced column from 'select_list' will need to be inserted into
  // the outermost subquery in the resulting expression, so that it can be
  // used in the project scan.
  std::vector<std::unique_ptr<const ResolvedColumnRef>> referenced_columns;

  for (const auto& var : variables) {
    if (options_.expression_columns().find(var.first) !=
        options_.expression_columns().end()) {
      return MakeSqlError()
             << "name must not appear in both "
             << "AnalyzerOptions.expression_columns and "
             << "AnalyzeSubstitute variable parameter: " << var.first;
    }

    GOOGLESQL_RETURN_IF_ERROR(CollectColumnRefs(*var.second, &referenced_columns));

    // We represent each variable as a column in the inner query.
    // When resolving path expression, we will first look up the variables map
    // to get corresponding resolved expr processed by rewriter, then perform a
    // deep copy on the expression while marking all free columns as correlated.
    select_list.push_back(var.first);
  }

  // During resolution, each of the column placeholders that we set up in
  // 'select_list' will be replaced by the associated argument expression.
  InternalAnalyzerOptions::SetLookupExpressionCallback(
      options_,
      [&](absl::string_view column_name,
          std::unique_ptr<const ResolvedExpr>& expr) -> absl::Status {
        const ResolvedExpr* const* replaced_expr =
            googlesql_base::FindOrNull(variables, column_name);
        if (replaced_expr != nullptr) {
          // CorrelateColumnRefs calls a deep copy visitor internally and marks
          // all columns that are non-local to `replaced_expr` as correlated.
          GOOGLESQL_ASSIGN_OR_RETURN(expr, CorrelateColumnRefs(**replaced_expr));
        }
        return absl::OkStatus();
      });

  // We want sorted/unique column refs because these are going to end up
  // populating a expression subquery parameter list which must not have
  // duplicates.
  SortUniqueColumnRefs(referenced_columns);

  // Sort <select_list> so that the AST we generate is stable.
  std::sort(select_list.begin(), select_list.end());
  // Produces:
  // ( SELECT <expression> FROM ( SELECT <joined_select_list> ) )
  // The inner select sees the aliases introduced in the select list. After
  // analysis, we will replace the positional parameters with the
  // corresponding ResolvedExprs from variables.
  std::string sql;
  if (select_list.empty()) {
    sql = absl::StrCat("( SELECT ", expression, " )");
  } else {
    sql = absl::StrCat("( SELECT ", expression, " FROM ( SELECT ",
                       absl::StrJoin(select_list, ", "), " ) )");
  }

  // Without setting prune_unused_columns, the columns generated by rewritten
  // lambda functions, e.g. `$array.element#x` and
  // `$array_offset.off#y` in <column_list> section does not belong to
  // Resolver::referenced_column_access_ and will be removed from table scan by
  // Resolver::PruneColumnLists at the end of expression analysis.
  absl::Cleanup pruning_cleaner = [&, prune_unused_columns =
                                          options_.prune_unused_columns()] {
    options_.set_prune_unused_columns(prune_unused_columns);
  };
  options_.set_prune_unused_columns(false);

  // When Substitute calls InternalAnalyzeExpression, the resolver returns a
  // subtree with references to externally defined columns. Such a subtree will
  // fail to validate.
  // AnalyzerOptions::validate_resolved_ast_ needs to be disabled regardless of
  // what the original value of this global flag is.
  bool validate_ast = InternalAnalyzerOptions::GetValidateResolvedAST(options_);
  absl::Cleanup validator_cleaner = [&] {
    InternalAnalyzerOptions::SetValidateResolvedAST(options_, validate_ast);
  };
  InternalAnalyzerOptions::SetValidateResolvedAST(options_, false);
  std::unique_ptr<AnalyzerOutput> output;
  GOOGLESQL_RETURN_IF_ERROR(InternalAnalyzeExpression(
      sql, options_, catalog_, &type_factory_, target_type, &output))
      << "while parsing substitution sql: " << sql;
  GOOGLESQL_VLOG(1) << "Initial ast: " << output->resolved_expr()->DebugString();

  VariableReplacementInserter replacer(referenced_columns, lambdas,
                                       column_factory_);
  GOOGLESQL_RETURN_IF_ERROR(output->resolved_expr()->Accept(&replacer));
  return replacer.ConsumeRootNode<ResolvedExpr>();
}

VariableReplacementInserter::VariableReplacementInserter(
    const std::vector<std::unique_ptr<const ResolvedColumnRef>>&
        referenced_columns,
    const absl::flat_hash_map<std::string, const ResolvedInlineLambda*>&
        lambdas,
    ColumnFactory* column_factory)
    : referenced_columns_(referenced_columns),
      lambdas_(lambdas),
      column_factory_(column_factory) {}

absl::Status VariableReplacementInserter::VisitResolvedFunctionCall(
    const ResolvedFunctionCall* node) {
  if (lambdas_.empty()) {
    return CopyVisitResolvedFunctionCall(node);
  }
  // We use a function group name of "SubstitutionLambda" and a
  // fixed context_id to denote lambda functions present
  // in the substitution template that were added to the catalog.
  bool is_lambda_function =
      node->function()->signatures().size() == 1 &&
      node->function()->signatures()[0].context_id() ==
          kSubstitutionLambdaContextId &&
      node->function()->GetGroup() == "SubstitutionLambda";
  if (!is_lambda_function) {
    return CopyVisitResolvedFunctionCall(node);
  }

  auto it = lambdas_.find(node->function()->Name());
  GOOGLESQL_RET_CHECK(it != lambdas_.end())
      << "No lambda named " << node->function()->Name() << " is found";
  const ResolvedInlineLambda* lambda = it->second;

  // Record lambda column refs for the enclosing SubqueryExpr.
  // The recorded ColumnRefs would be put into parameter_list of the enclosing
  // SubqueryExpr.
  GOOGLESQL_RET_CHECK(!column_refs_stack_.empty()) << node->DebugString();
  ColumnRefLayer& ref_layer = column_refs_stack_.back();
  for (const auto& ref : lambda->parameter_list()) {
    ref_layer.emplace(ref->column().column_id(), ref.get());
  }

  GOOGLESQL_RET_CHECK_GE(node->argument_list_size(), lambda->argument_list_size());
  // Build map from lambda argument column to column reference in
  // the substitution SQL.
  absl::flat_hash_map<int, const ResolvedColumnRef*> lambda_columns_map;
  for (int i = 0; i < lambda->argument_list_size(); i++) {
    GOOGLESQL_RET_CHECK(node->argument_list(i)->Is<ResolvedColumnRef>())
        << "Lambda arguments must be a ColumnRef.";
    const ResolvedColumnRef* substitution_column =
        node->argument_list(i)->GetAs<ResolvedColumnRef>();
    lambda_columns_map.emplace(lambda->argument_list(i).column_id(),
                               substitution_column);
  }

  // We want to make sure not to copy any correlated columns
  // external to the node, as that would result in an invalid reference.
  ColumnReplacementMap column_map;
  GOOGLESQL_ASSIGN_OR_RETURN(absl::flat_hash_set<ResolvedColumn> correlated_columns,
                   GetCorrelatedColumnSet(*lambda->body()));
  for (const ResolvedColumn& correlated_column : correlated_columns) {
    column_map.emplace(correlated_column, correlated_column);
  }

  // We also want to exclude copying of ResolvedColumns underlying the column
  // references associated with the lambda argument column from above.
  // By adding these to ColumnReplacementMap we can avoid modification.
  for (const auto& [_, column_ref] : lambda_columns_map) {
    column_map.emplace(column_ref->column(), column_ref->column());
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      std::unique_ptr<ResolvedExpr> body,
      ReplaceColumnRefsAndRemapColumns(lambda->body(), lambda_columns_map,
                                       column_map, *column_factory_));

  PushNodeToStack(std::move(body));
  return absl::OkStatus();
}

}  // namespace

absl::Status ExpectAnalyzeSubstituteSuccess(
    googlesql_base::StatusBuilder status_builder) {
  GOOGLESQL_RET_CHECK_OK(status_builder) << "Unexpected error in AnalyzeSubstitute()";
  return status_builder;
}

absl::StatusOr<std::unique_ptr<ResolvedExpr>> AnalyzeSubstitute(
    AnalyzerOptions options, Catalog& catalog, TypeFactory& type_factory,
    absl::string_view expression,
    const absl::flat_hash_map<std::string, const ResolvedExpr*>& variables,
    const absl::flat_hash_map<std::string, const ResolvedInlineLambda*>&
        lambdas,
    AnnotatedType target_type) {
  constexpr absl::string_view arenas_msg =
      "AnalyzeSubstitute: All arenas and the column sequence number must be "
      "set on the input options, and they must be the same arenas used to "
      "generate the variables and lambdas. Otherwise, the result of "
      "substitution will be subtly broken";
  GOOGLESQL_RET_CHECK(options.id_string_pool()) << arenas_msg;
  GOOGLESQL_RET_CHECK(options.arena()) << arenas_msg;
  GOOGLESQL_RET_CHECK(options.column_id_sequence_number()) << arenas_msg;

  ColumnFactory column_factory(0, options.id_string_pool().get(),
                               options.column_id_sequence_number());

  // Don't rewrite 'expression' when we analyze it.
  // We don't need to reset the rewriters here as 'option' is copied from
  // main Analyzer pass already. Substitutor is the end of the current pass.
  options.set_enabled_rewrites({});
  return ExpressionSubstitutor(std::move(options), catalog, &column_factory,
                               type_factory)
      .Substitute(expression, variables, lambdas, target_type);
}

}  // namespace googlesql
