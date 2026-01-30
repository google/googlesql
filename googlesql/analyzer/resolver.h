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

#ifndef GOOGLESQL_ANALYZER_RESOLVER_H_
#define GOOGLESQL_ANALYZER_RESOLVER_H_

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stack>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/analyzer/annotation_propagator.h"
#include "googlesql/analyzer/column_cycle_detector.h"
#include "googlesql/analyzer/container_hash_equals.h"
#include "googlesql/analyzer/expr_matching_helpers.h"
#include "googlesql/analyzer/expr_resolver_helper.h"
#include "googlesql/analyzer/name_scope.h"
#include "googlesql/analyzer/named_argument_info.h"
#include "googlesql/analyzer/path_expression_span.h"
#include "googlesql/analyzer/query_resolver_helper.h"
#include "googlesql/analyzer/set_operation_resolver_base.h"
#include "googlesql/common/warning_sink.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output_properties.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/coercer.h"
#include "googlesql/public/deprecation_warning.pb.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/functions/datetime.pb.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/signature_match_result.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/collation.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/type_modifiers.h"
#include "googlesql/public/types/type_parameters.h"
#include "googlesql/public/value.h"
#include "googlesql/public/with_modifier_mode.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "googlesql/resolved_ast/resolved_ast_visitor.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/base/case.h"
#include "gtest/gtest_prod.h"
#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/base/general_trie.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

class FunctionArgumentInfo;
class FunctionResolver;
class QueryResolutionInfo;
class SelectColumnStateList;
class ExtendedCompositeCastEvaluator;
struct ColumnReplacements;
struct OrderByItemInfo;
struct SelectColumnState;

// Options for ResolveQuery.
// The compiler currently requires this struct to be declared outside the class.
struct ResolveQueryOptions {
  // True if this is the outermost query, and not any kind of subquery.
  bool is_outer_query = false;

  // See comment on ResolveExpr explaining inferred types.
  // Additionally for queries:
  // * This only affects queries where the select-list has a single column, and
  //   it gets selected without struct or proto construction.
  // * The <inferred_type_for_query> becomes the <inferred_type> for resolving
  //   the single select-list column expression.
  // * For ARRAY subqueries, where the expression had an expected array type,
  //   the query here will have its element type as the expected type (this
  //   happens in ResolveExprSubquery).
  // * IN subqueries work similarly to single-column queries.
  const Type* inferred_type_for_query = nullptr;

  // True if the query is an expression subquery.
  bool is_expr_subquery = false;

  // If true, the last pipe operator in the query will not be resolved. This is
  // used for recursive queries, where the last pipe operator is the recursive
  // term, which needs to be resolved by
  // SetOperationResolver::ResolveRecursive(). An error is returned if
  // `exclude_last_pipe_operator` is true but the query does not have a pipe
  // operator.
  bool exclude_last_pipe_operator = false;

  // If true, terminal pipe operators are allowed.
  // See (broken link).
  // This will be enabled for the outermost query in a query statement if
  // ResolvedGeneralizedQueryStmt is enabled in SupportedStatementKinds, and
  // then it will be turned off for nested queries.
  bool allow_terminal = false;
};

// This class contains most of the implementation of GoogleSQL analysis.
// The functions here generally traverse the AST nodes recursively,
// constructing and returning the Resolved AST nodes bottom-up.  For
// a more detailed overview, see (broken link).
// Not thread-safe.
//
// NOTE: Because this class is so large, the implementation is split up
// by category across multiple cc files:
//   resolver.cc            Common and shared methods
//   resolver_alter_stmt.cc ALTER TABLE statements
//   resolver_dml.cc        DML
//   resolver_expr.cc       Expressions
//   resolver_query.cc      SELECT statements, things that make Scans
//   resolver_stmt.cc       Statements (except DML)
class Resolver {
 public:
  enum class HintOrOptionType {
    Hint,
    Option,
    AnonymizationOption,
    DifferentialPrivacyOption,
    AggregationThresholdOption,
  };

  enum class OrderBySimpleMode {
    kNormal,
    kPipes,
    kGql,
  };

  struct GeneratedColumnIndexAndResolvedId {
    int column_index;
    int resolved_column_id;
  };

  // <*analyzer_options> should outlive the constructed Resolver. It must have
  // all arenas initialized.
  Resolver(Catalog* catalog, TypeFactory* type_factory,
           const AnalyzerOptions* analyzer_options);
  Resolver(const Resolver&) = delete;
  Resolver& operator=(const Resolver&) = delete;
  ~Resolver();

  // Resolve a parsed ASTStatement to a ResolvedStatement.
  // This fails if the statement is not of a type accepted by
  // LanguageOptions.SupportsStatementKind().
  // <sql> contains the text at which the ASTStatement points.
  absl::Status ResolveStatement(
      absl::string_view sql, const ASTStatement* statement,
      std::unique_ptr<const ResolvedStatement>* output);

  // Do final checks and updates at the end of ResolveStatement.
  // These are also applied at the end of resolve methods for terminal pipe
  // operators that correspond to statements.
  absl::Status FinishResolveStatement(const ASTNode* ast_location,
                                      ResolvedStatement* stmt);

  // Resolve a standalone expression outside a query.
  // `sql` contains the text at which `ast_expression` points.
  absl::Status ResolveStandaloneExpr(
      absl::string_view sql, const ASTExpression* ast_expr,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  // Resolve a standalone expression outside a query.
  // `sql` contains the text at which `ast_expression` points. If the type of
  // the resolved expression is different `target_type`, coerces the resolved
  // expression to `target_type` and fails if it cannot.
  absl::Status ResolveStandaloneExprAndAssignToType(
      absl::string_view sql, const ASTExpression* ast_expr,
      AnnotatedType target_type,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  // Resolve a parsed ASTExpression to a ResolvedExpr in the context of a
  // function call. Unlike ResolveExpr, this method accepts maps from the
  // argument names to their types for arguments in <function_arguments>.
  // <expr_resolution_info> is used for resolving the function call.
  //
  // TODO: Provide an overload that takes a FunctionArgumentInfo
  //     directly and deprecate this one.
  absl::Status ResolveExprWithFunctionArguments(
      absl::string_view sql, const ASTExpression* ast_expr,
      const IdStringHashMapCase<std::unique_ptr<ResolvedArgumentRef>>&
          function_arguments,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* output);

  // Resolve the ASTQueryStatement associated with a SQL TVF.  The TVF's
  // arguments are passed in through <function_arguments> (for scalar arguments)
  // and <function_table_arguments> (for table-valued arguments). Takes
  // ownership of all pointers in these arguments. If <specified_output_schema>
  // is present, calls the CheckSQLBodyReturnTypesAndCoerceIfNeeded method to
  // enforce that the schema returned by the function body matches the expected
  // schema, adding a coercion or returning an error if necessary.
  //
  // TODO: Provide an overload that takes a FunctionArgumentInfo
  //     directly and deprecate this one.
  absl::Status ResolveQueryStatementWithFunctionArguments(
      absl::string_view sql, const ASTQueryStatement* query_stmt,
      const std::optional<TVFRelation>& specified_output_schema,
      bool allow_query_parameters,
      IdStringHashMapCase<std::unique_ptr<ResolvedArgumentRef>>*
          function_arguments,
      IdStringHashMapCase<TVFRelation>* function_table_arguments,
      std::unique_ptr<const ResolvedStatement>* output_stmt,
      std::shared_ptr<const NameList>* output_name_list);

  // Creates an AnnotationMap from a given Type and TypeModifiers, which are
  // typically resolved from an explicit type declaration, like the target
  // of an explicit CAST or the explicit RETURN type of a user-defined function.
  // The returned annotation map is internalized in the TypeFactory.
  absl::StatusOr<const AnnotationMap*> CreateAnnotationMapFromTypeWithModifiers(
      const Type* type, const TypeModifiers& type_modifiers) const;

  // If a CREATE TABLE FUNCTION statement contains RETURNS TABLE to explicitly
  // specify the output schema for the function's output table, this method
  // compares it against the schema actually returned by the SQL body (if
  // present).
  //
  // If the required schema includes a column name that is not returned from the
  // SQL body, or the matching column name has a type that is not equal or
  // implicitly coercible to the required type, this method returns an error.
  // Note that the column order is not relevant: this method matches the columns
  // in the explicitly-specified schema against the output columns if the query
  // in the SQL body by name.
  //
  // Otherwise, if the names and types of columns do not match exactly, this
  // method adds a new projection to perform the necessary type coercion and/or
  // column dropping so that the names and types match from the result of the
  // projection.
  //
  // If the explicitly-specified schema is a value table, then this method only
  // checks that the query in the SQL body returns one column of a type that is
  // equal or implicitly coercible to the value-table type.
  absl::Status CheckSQLBodyReturnTypesAndCoerceIfNeeded(
      const ASTNode* statement_location, const TVFRelation& return_tvf_relation,
      const NameList* tvf_body_name_list,
      std::unique_ptr<const ResolvedScan>* resolved_query,
      std::vector<std::unique_ptr<const ResolvedOutputColumn>>*
          resolved_output_column_list);

  // Generate a customer error message when coercion is found to not be allowed
  // in one of the CoerceExprTo functions.
  using CoercionErrorMessageFunction = std::function<std::string(
      absl::string_view target_type_name, absl::string_view actual_type_name)>;

  // The different kinds of coercion supported by CoerceExprTo function family.
  // Using an enum makes calls to these functions clear and concise.
  enum CoercionMode {
    kImplicitAssignment,
    kExplicitCoercion,
    kImplicitCoercion,
  };

  // Given a resolved expression <resolved_expr>, along with the AST that
  // generated it (<ast_location>), coerces the expression to <target_type>,
  // replacing <resolved_expr> with the modified result. If the expression is
  // already the correct type, it is simply left in place, without modification.
  // If the expression cannot be coerced, an error is emitted. Errors are
  // returned with InternalErrorLocation.
  //
  // <mode> configures the Coercer to signal which kind of coercion should be
  // validated.
  //
  // The <make_error> function is called to generate an error message strinng if
  // <resolved_expr> is not coercible to <target_type> using coercion mode
  // <mode>. Two more overloads of this function take simple error message
  // template or no error message argument (in which case a generic template is
  // used).
  ABSL_DEPRECATED(
      "Use CoerceExprToType function with <annotated_target_type> argument.")
  // TODO: Refactor and remove the deprecated function in a quick
  // follow up.
  absl::Status CoerceExprToType(
      const ASTNode* ast_location, const Type* target_type, CoercionMode mode,
      CoercionErrorMessageFunction make_error,
      std::unique_ptr<const ResolvedExpr>* resolved_expr) const;

  // Same as the previous method but <annotated_target_type> is used to contain
  // both target type and its annotation information.
  absl::Status CoerceExprToType(
      const ASTNode* ast_location, AnnotatedType annotated_target_type,
      CoercionMode mode, CoercionErrorMessageFunction make_error,
      std::unique_ptr<const ResolvedExpr>* resolved_expr) const;

  // Similar to the above function, but provides an error message template
  // instead of an error message construction function. When only the type names
  // of the <target_type> and the argument (<resolved_expr>) type are needed,
  // this is more concise than specifying a full function. The name of
  // <target_type> will replace '$0' and the name of the argument type will
  // replace $1.
  ABSL_DEPRECATED(
      "Use CoerceExprToType function with <annotated_target_type> argument.")
  absl::Status CoerceExprToType(
      const ASTNode* ast_location, const Type* target_type, CoercionMode mode,
      absl::string_view error_template,
      std::unique_ptr<const ResolvedExpr>* resolved_expr) const;

  // Same as the previous method but <annotated_target_type> is used to contain
  // both target type and its annotation information.
  absl::Status CoerceExprToType(
      const ASTNode* ast_location, AnnotatedType annotated_target_type,
      CoercionMode mode, absl::string_view error_template,
      std::unique_ptr<const ResolvedExpr>* resolved_expr) const;

  // Similar to CoerceExprToType above but using a generic error message when
  // <resolved_expr> cannot be coerced to <target_type>.
  ABSL_DEPRECATED(
      "Use CoerceExprToType function with <annotated_target_type> argument.")
  absl::Status CoerceExprToType(
      const ASTNode* ast_location, const Type* target_type, CoercionMode mode,
      std::unique_ptr<const ResolvedExpr>* resolved_expr) const;

  // Same as the previous method but <annotated_target_type> is used to contain
  // both target type and its annotation information.
  absl::Status CoerceExprToType(
      const ASTNode* ast_location, AnnotatedType annotated_target_type,
      CoercionMode mode,
      std::unique_ptr<const ResolvedExpr>* resolved_expr) const;

  // Similar to the above function, but coerces to BOOL type.
  // There is no <assignment_semantics> parameter, since assignment semantics
  // do not matter when coercing to type BOOL.
  absl::Status CoerceExprToBool(
      const ASTNode* ast_location, absl::string_view clause_name,
      std::unique_ptr<const ResolvedExpr>* resolved_expr) const;

  // Resolve the Type from the <type_name>.
  absl::Status ResolveTypeName(absl::string_view type_name, const Type** type);

  // Resolve the Type and TypeModifiers from the <type_name>.
  absl::Status ResolveTypeName(absl::string_view type_name, const Type** type,
                               TypeModifiers* type_modifiers);

  // DEPRECATED: WILL BE REMOVED SOON
  // Attempt to coerce <scan>'s output types to those in <types> using
  // assignment coercion semantics.
  // If no coercion is needed, then <scan> and <output_name_list> are left
  // unmodified.
  // Otherwise, a new projection will be added to <scan> which will perform
  // the necessary type coercions. <output_name_list> will also be updated
  // to match the new <scan>.
  absl::Status CoerceQueryStatementResultToTypes(
      const ASTNode* ast_node, absl::Span<const Type* const> types,
      std::unique_ptr<const ResolvedScan>* scan,
      std::shared_ptr<const NameList>* output_name_list);

  // Return vector of warnings generated by the last input analyzed. These have
  // DeprecationWarning protos attached to them.
  absl::Span<const absl::Status> deprecation_warnings() const {
    return warning_sink_.warnings();
  }

  // Ensures that no undeclared (named) parameters have conflicting inferred
  // types, and returns the assigned type for each. If any parameter has
  // a conflict, returns an error status instead.
  absl::StatusOr<QueryParametersMap> AssignTypesToUndeclaredParameters() const;

  // Returns undeclared positional parameters found the query and their inferred
  // types. The index in the vector corresponds with the position of the
  // undeclared parameter--for example, the first element in the vector is the
  // type of the undeclared parameter at position 1 and so on.
  const std::vector<const Type*>& undeclared_positional_parameters() const {
    return undeclared_positional_parameters_;
  }

  bool has_graph_references() const {
    return !referenced_property_graphs_.empty();
  }

  absl::flat_hash_set<const PropertyGraph*>
  release_referenced_property_graphs();

  const AnalyzerOptions& analyzer_options() const { return analyzer_options_; }

  const LanguageOptions& language() const {
    return analyzer_options_.language();
  }

  const AnalyzerOutputProperties& analyzer_output_properties() const {
    return analyzer_output_properties_;
  }

  // Returns the highest column id that has been allocated.
  int max_column_id() const { return column_factory_->max_column_id(); }

  // Returns the property graph for the current innermost graph query or
  // subquery context, nullptr if none exists.
  const PropertyGraph* GetActivePropertyGraphOrNull() const {
    return graph_context_stack_.empty() ? nullptr : graph_context_stack_.top();
  }
  // Pushes a property graph onto the graph context stack.
  // Used by GraphQueryResolver.
  // REQUIRES: property_graph != nullptr
  absl::Status PushPropertyGraphContext(const PropertyGraph* property_graph) {
    GOOGLESQL_RET_CHECK(property_graph != nullptr);
    graph_context_stack_.push(property_graph);
    return absl::OkStatus();
  }
  // Pops a property graph from the graph context stack.
  // Used by GraphQueryResolver.
  // REQUIRES: graph_context_stack_ is not empty
  void PopPropertyGraphContext() { graph_context_stack_.pop(); }

  // Return the substring of `sql_` for `node`'s parse location range.
  absl::StatusOr<absl::string_view> GetSQLForASTNode(const ASTNode* node);

  // Clear state so this can be used to resolve a second statement whose text
  // is contained in <sql>.
  void Reset(absl::string_view sql);

 private:
  // Case-insensitive map of a column name to its position in a list of columns.
  typedef std::map<IdString, int, IdStringCaseLess> ColumnIndexMap;
  typedef absl::flat_hash_map<ResolvedColumn, const Column*>
      ResolvedColumnToCatalogColumnHashMap;

  // These indicate arguments that require special treatment during resolution,
  // and are related to special syntaxes in the grammar.  The grammar should
  // enforce that the corresponding argument will have the expected ASTNode
  // type.
  enum class SpecialArgumentType {
    // INTERVAL indicates the function argument is an interval, like
    // INTERVAL 5 YEAR.  This is one ASTIntervalExpr node in the AST input, and
    // will be resolved into two arguments, the numeric value ResolvedExpr and
    // the DateTimestampPart enum ResolvedLiteral.
    INTERVAL,

    // DATEPART indicates the function argument is a date part keyword like
    // YEAR.  This is an ASTIdentifier or ASTPathExpression node in the AST
    // input, and will be resolved to a DateTimestampPart enum ResolvedLiteral
    // argument.
    DATEPART,

    // NORMALIZE_MODE indicates that function argument is a normalization
    // mode keyword like NFC. This is an ASTIdentifier node in the AST
    // input, and will be resolved to a NormalizeMode enum ResolvedLiteral
    // argument.
    NORMALIZE_MODE,

    // PROPERTY_NAME indicates that function argument is an identifier to a
    // exposed property in a property graph. This is an ASTIdentifier
    // node in the AST input, and will be resolved to a ResolvedLiteral
    // argument.
    PROPERTY_NAME,
  };

  enum class PartitioningKind { PARTITION_BY, CLUSTER_BY };

  class GeneratedColumnTopoSorter : public ResolvedASTVisitor {
   public:
    explicit GeneratedColumnTopoSorter(
        const Table* table, CycleDetector* cycle_detector,
        std::vector<int>& generated_column_indices,
        absl::node_hash_map<const Column*, ResolvedColumn>&
            catalog_columns_to_resolved_columns_map)
        : table_(table),
          cycle_detector_(cycle_detector),
          generated_column_indices_(generated_column_indices),
          catalog_columns_to_resolved_columns_map_(
              catalog_columns_to_resolved_columns_map) {}
    std::vector<GeneratedColumnIndexAndResolvedId>
    release_topologically_sorted_generated_columns() {
      std::vector<GeneratedColumnIndexAndResolvedId> tmp;
      topologically_sorted_generated_columns_.swap(tmp);
      return tmp;
    }
    absl::Status TopologicallySortGeneratedColumns();

   protected:
    absl::Status DefaultVisit(const ResolvedNode* node) override;
    const Table* table_;
    CycleDetector* cycle_detector_;
    const std::vector<int>& generated_column_indices_;
    // absl::node_hash_map is used for pointer stability since this is a map
    // of a pointer vs value(ResolvedColumn) and ResolvedColumn is owned by the
    // map.
    absl::node_hash_map<const Column*, ResolvedColumn>&
        catalog_columns_to_resolved_columns_map_;
    absl::flat_hash_set<int> visited_column_indices_;
    std::vector<GeneratedColumnIndexAndResolvedId>
        topologically_sorted_generated_columns_;
  };

  absl::Status TopologicallySortGeneratedColumns(
      std::vector<int>& generated_columns, const Table* table,
      CycleDetector* cycle_detector,
      absl::node_hash_map<const Column*, ResolvedColumn>&
          catalog_columns_to_resolved_columns_map,
      std::vector<GeneratedColumnIndexAndResolvedId>&
          out_topologically_sorted_generated_columns);

  static const std::map<int, SpecialArgumentType>* const
      kEmptyArgumentOptionMap;

  // The lock mode stack is used to propagate the lock mode found at a given
  // level of the query to ResolvedTableScan nodes in subqueries.

  // Returns the currently active lock mode and nullptr if none exists.
  const ASTLockMode* GetActiveLockModeOrNull() const {
    return lock_mode_stack_.empty() ? nullptr : lock_mode_stack_.top();
  }

  // Pushes a lock mode node onto the lock mode stack. Pushing a null node is
  // ok for the purposes of not propagating a LockMode node further from
  // certain levels of the AST. E.g. if LockMode is specified in an outer
  // query, then it shouldn't propagate into the CTE query.
  void PushLockMode(const ASTLockMode* lock_mode) {
    lock_mode_stack_.push(lock_mode);
  }

  // Pops a lock mode node from the lock mode stack. No-op if the stack is
  // empty.
  void PopLockMode() {
    if (!lock_mode_stack_.empty()) {
      lock_mode_stack_.pop();
    }
  }

  std::stack<const ASTLockMode*> lock_mode_stack_;

  // Defined in resolver_query.cc.
  static const IdString& kArrayId;
  static const IdString& kOffsetAlias;
  static const IdString& kWeightAlias;
  static const IdString& kArrayOffsetId;
  static const IdString& kLambdaArgId;
  static const IdString& kWithActionId;
  static const IdString& kRecursionDepthAlias;
  static const IdString& kRecursionDepthId;

  // Input SQL query text. Set before resolving a statement, expression or
  // type.
  absl::string_view sql_;

  Catalog* catalog_;

  // Internal catalog for looking up system variables.  Content is imported
  // directly from analyzer_options_.system_variables().  This field is
  // initially set to nullptr, and is initialized the first time we encounter a
  // reference to a system variable.
  std::unique_ptr<Catalog> system_variables_catalog_;

  TypeFactory* type_factory_;
  const AnalyzerOptions& analyzer_options_;  // Not owned.
  Coercer coercer_;

  // Shared constant for an empty NameList and NameScope.
  const std::shared_ptr<const NameList> empty_name_list_;
  const std::unique_ptr<NameScope> empty_name_scope_;

  // For resolving functions.
  std::unique_ptr<FunctionResolver> function_resolver_;

  // Pool where IdStrings are allocated.  Copied from AnalyzerOptions.
  IdStringPool* const id_string_pool_;

  // Allocate the next unique column_id.
  std::unique_ptr<ColumnFactory> column_factory_;

  // Next unique subquery ID to allocate. Used for display only.
  int next_subquery_id_;

  // Next unique unnest ID to allocate. Used for display only.
  int next_unnest_id_;

  // True if we are analyzing a standalone expression rather than a statement.
  bool analyzing_expression_;

  // Either "PARTITION BY" or "CLUSTER BY" if we are analyzing one of those
  // clauses inside a DDL statement. Used for the error message if we encounter
  // an unsupported expression in the clause.
  const char* analyzing_partition_by_clause_name_ = nullptr;

  // If not empty, we are analyzing a clause that disallows query parameters,
  // such as SQL function body and view body; when encountering query
  // parameters, this field will be used as the error message.
  absl::string_view disallowing_query_parameters_with_error_;

  // For generated columns, 'cycle_detector_' is used for detecting cycles
  // between columns in a create table statement.
  // When 'generated_column_cycle_detector_' is not null,
  // Resolver::ResolvePathExpressionAsExpression() calls
  // 'cycle_detector_->AddDependencyOn(x)' whenever
  // it resolves a column 'x'.
  // The pointer will contain a local variable set in
  // Resolver::ResolveColumnDefinitionList().
  ColumnCycleDetector* generated_column_cycle_detector_ = nullptr;
  // When 'generated_column_cycle_detector_' is not null and
  // ResolvePathExpressionAsExpression() fails to resolve a column, this stores
  // the column name in 'unresolved_column_name_in_generated_column_'. A higher
  // layer can then detect that the generated column it was attempting to
  // resolve has a dependency on 'unresolved_column_name_in_generated_column_'.
  IdString unresolved_column_name_in_generated_column_;

  // True if we are analyzing an expression that is stored and non volatile,
  // either as a generated table column or as an expression stored in an index.
  bool analyzing_nonvolatile_stored_expression_columns_;

  // True if we are analyzing check constraint expression.
  bool analyzing_check_constraint_expression_;

  // When analyzing columns with a default value expression, set to the
  // NameScope containing all column names of the table being analyzed. This is
  // used to generate better error messages when the expression accesses a table
  // column.
  std::optional<const NameScope*> default_expr_access_error_name_scope_;

  // Tracks the current nesting level of side-effect scopes. A new scope starts
  // with each function call where function->MaySuppressSideEffects() is true.
  // For example, when resolving:
  //    SELECT a + IF(x, y, COALESCE(z, w)) FROM t
  // The depth is 0 when resolving a, 1 when resolving x and y, and 2 when
  // resolving z and w.
  int side_effect_scope_depth_ = 0;

  // Used when inside a MATCH_RECOGNIZE() clause, to express the state needed
  // to compute special MATCH_RECOGNIZE functions, i.e., the assigned resolved
  // columns expressing the needed information.
  struct MatchRecognizeState {
    ResolvedColumn match_number_column;
    ResolvedColumn match_row_number_column;
    ResolvedColumn classifier_column;
  };

  // The current state of the innermost MATCH_RECOGNIZE clause.
  // Not a stack because we do not allow nested MATCH_RECOGNIZE clauses.
  std::optional<MatchRecognizeState> match_recognize_state_;

  // This is true if we found any of the operators that only work in
  // ResolvedGeneralizedQueryStmt (not in ResolvedQueryStmt).
  bool needs_generalized_query_stmt_ = false;

  AnalyzerOutputProperties analyzer_output_properties_;

  // Store list of named subqueries currently visible.
  // This is updated as we traverse the query to implement scoping of
  // WITH subquery names.
  struct NamedSubquery {
    NamedSubquery(IdString unique_alias_in, bool is_recursive_in,
                  const ResolvedColumnList& column_list_in,
                  const std::shared_ptr<const NameList>& name_list_in)
        : unique_alias(unique_alias_in),
          is_recursive(is_recursive_in),
          column_list(column_list_in),
          name_list(name_list_in) {}

    NamedSubquery(const NamedSubquery&) = delete;
    NamedSubquery& operator=(const NamedSubquery&) = delete;

    // The globally uniquified alias for this table alias which we will use in
    // the resolved AST.
    const IdString unique_alias;

    // True if references to this subquery should resolve to a
    // ResolvedRecursiveRefScan, rather than a ResolvedWithRefScan.
    bool is_recursive;

    // The columns produced by the table alias.
    // These will be matched 1:1 with newly created columns in future
    // WithRefScan/RecursiveRefScan nodes.
    ResolvedColumnList column_list;

    // The name_list for the columns produced by the WITH subquery.
    // This provides the user-visible column names, which may not map 1:1
    // with column_list.
    // This also includes the is_value_table bit indicating if the WITH subquery
    // produced a value table.
    const std::shared_ptr<const NameList> name_list;

    // If not OK, represents the error that should be returned when this
    // subquery is referenced. The caller may choose to update its location
    // to the actual reference site.
    //
    // Example usage:
    // - Set this field to disallow recursive references inside the subpipline
    //   input of a pipe recursive union.
    absl::Status access_error_message = absl::OkStatus();
  };

  // Keeps track of all active named subqueries.
  // Key: Subquery name. This is a vector to allow for multi-part recursive view
  //        names, in addition to single-path WITH entry names.
  // Value: Vector of active subqueries with that name, with the innermost
  //        subquery last. This vector is never empty.
  //
  //        Note: While resolving the non-recursive term of a recursive UNION,
  //        a nullptr entry is added to this vector to indicate that any
  //        references to this alias should result in an error.
  absl::flat_hash_map<
      std::vector<IdString>, std::vector<std::unique_ptr<NamedSubquery>>,
      ContainerHash<std::vector<IdString>, IdStringCaseHash>,
      ContainerEquals<std::vector<IdString>, IdStringCaseEqualFunc>>
      named_subquery_map_;

  // Stores additional information about each ResolvedRecursiveRefScan node
  // needed by the resolver, but not persisted in the tree.
  struct RecursiveRefScanInfo {
    // The AST node representing the table reference; used for error
    // reporting only.
    const ASTNode* ast_location;

    // The path corresponding to the table reference. It is used for error
    // reporting only.
    std::vector<IdString> path;

    // Unique name of the recursive query being referenced. Used to identify
    // cases where an inner WITH alias contains a recursive reference to an
    // outer WITH query. Since such cases always result in an error, this
    // information does not need to be persisted in the resolved tree; by the
    // time the resolver completes, it is guaranteed that every recursive
    // reference points to the innermost ResolvedRecursiveScan.
    IdString recursive_query_unique_name;
  };

  // Stores additional information about each ResolvedRecursiveRefScan node
  // created, which is needed for validation checks later in the resolver, but
  // is not persisted into the resolved AST.
  //
  // All node pointers are owned externally, as part of the resolved tree being
  // generated.
  absl::flat_hash_map<const ResolvedRecursiveRefScan*, RecursiveRefScanInfo>
      recursive_ref_info_;

  // Add a CTE definition.  For each name, this maintains a stack in
  // `named_subquery_map_` storing previous definitions for that name.
  void AddNamedSubquery(const std::vector<IdString>& alias,
                        std::unique_ptr<NamedSubquery> named_subquery);

  // Removes the innermost CTE with name `alias` from `named_subquery_map_`.
  //
  // Returns an internal error if `alias` has no active definition.
  absl::Status RemoveInnermostNamedSubqueryWithAlias(IdString alias);

  bool IsPathExpressionStartingFromNamedSubquery(
      const ASTPathExpression& path_expr) const;

  // Set of unique WITH aliases seen so far.  If there are duplicate WITH
  // aliases in the query (visible in different scopes), we'll give them
  // unique names in the resolved AST.
  IdStringHashSetCase unique_with_alias_names_;

  // WarningSink tracks warnings generated by the analyzer and components it
  // invokes to analyze and modify the AST.
  WarningSink warning_sink_;

  // Store how columns have actually been referenced in the query.
  // (Note: The bottom-up resolver will initially include all possible columns
  // for each table on each ResolvedTableScan.)
  // Once we analyze the full query, this will be used to prune column_lists of
  // unreferenced columns. It is also used to populate column_access_list, which
  // indicates whether columns were read and/or written. Engines can use this
  // additional information for correct column-level ACL checking.
  std::map<ResolvedColumn, ResolvedStatement::ObjectAccess>
      referenced_column_access_;

  // Contains metadata reguarding any function arguments that are in scope and
  // can be referenced. This pointer is only set when analyzing the SQL body of
  // a function or table function.
  //
  // TODO: Maybe allow argument names in scope for the sake of
  //   the TYPEOF() operator. 'RETURNS TYPEOF(arg)' maybe a valid thing to do.
  const FunctionArgumentInfo* function_argument_info_ = nullptr;

  // Contains undeclared parameters whose type has been inferred from context.
  // It is a map associating each query parameter reference to the inferred
  // type. Keys are lowercase to achieve case-insensitive matching, and each
  // pair is a location where the parameter is referenced, as well as the
  // inferred type at that location.
  absl::flat_hash_map<std::string,
                      std::vector<std::pair<ParseLocationPoint, const Type*>>>
      undeclared_parameters_;

  // Contains undeclared positional parameters whose type has been inferred from
  // context.
  std::vector<const Type*> undeclared_positional_parameters_;
  // Maps parse locations to the names or positions of untyped occurrences of
  // undeclared parameters.
  std::map<ParseLocationPoint, std::variant<std::string, int>>
      untyped_undeclared_parameters_;

  absl::flat_hash_set<const PropertyGraph*> referenced_property_graphs_;
  // Keeps track of the graph references declared in each layer of subqueries.
  // This allows a graph subquery without a graph reference to use the reference
  // from the parent context (retrieved from this stack).
  std::stack<const PropertyGraph*> graph_context_stack_;

  // Maps ResolvedColumns produced by ResolvedTableScans to their source Columns
  // from the Catalog. This can be used to check properties like
  // Column::IsWritableColumn().
  // Note that this is filled in only for ResolvedColumns directly produced in a
  // ResolvedTableScan, not any derived columns.
  ResolvedColumnToCatalogColumnHashMap resolved_columns_from_table_scans_;

  // Maps resolved floating point literal IDs to their original textual image.
  absl::flat_hash_map<int, std::string> float_literal_images_;
  // Next ID to assign to a float literal. The ID of 0 is reserved for
  // ResolvedLiterals without a cached image.
  int next_float_literal_image_id_ = 1;

  // AnnotationPropagator is used to propagate annotations through
  // resolved nodes.
  std::unique_ptr<AnnotationPropagator> annotation_propagator_;

  // Holds active NameLists that are available for GROUP_ROWS() function
  // invoked from inside WITH GROUP ROWS(...). Contains an entry for each nested
  // usage of WITH GROUP ROWS syntax. Boolean component is used to track usage
  // of GROUP_ROWS() TVF inside WITH GROUP ROWS(...). It is enforced that
  // GROUP_ROWS() function should be used at least once.
  struct GroupRowsTvfInput {
    std::shared_ptr<const NameList> name_list;
    bool group_rows_tvf_used = false;
  };
  std::stack<GroupRowsTvfInput> name_lists_for_group_rows_;

  // Resolve the Type and TypeModifiers from the <type_name> without resetting
  // the state.
  absl::Status ResolveTypeNameInternal(absl::string_view type_name,
                                       const Type** type,
                                       TypeModifiers* type_modifiers);

  const FunctionResolver* function_resolver() const {
    return function_resolver_.get();
  }

  // Checks and propagates annotations through <resolved_node>. If there is SQL
  // error thrown, the error will be attached to the location of <error_node>.
  // <error_node> could be nullptr to indicate there is no suitable location to
  // attach the error.
  absl::Status CheckAndPropagateAnnotations(const ASTNode* error_node,
                                            ResolvedNode* resolved_node);

  ResolvedColumn MakeGroupingOutputColumn(
      const ExprResolutionInfo* expr_resolution_info, IdString grouping_id,
      AnnotatedType annotated_type);

  int AllocateColumnId();
  IdString AllocateSubqueryName();
  IdString AllocateUnnestName();

  IdString MakeIdString(absl::string_view str) const;

  // Makes a new resolved literal and records its location.
  std::unique_ptr<const ResolvedLiteral> MakeResolvedLiteral(
      const ASTNode* ast_location, const Value& value,
      bool set_has_explicit_type = false) const;

  // Same as the previous method but <annotated_type> is used to contain
  // both target type and its annotation information.
  std::unique_ptr<const ResolvedLiteral> MakeResolvedLiteral(
      const ASTNode* ast_location, AnnotatedType annotated_type,
      const Value& value, bool has_explicit_type) const;

  // Makes a new resolved float literal and records its location and original
  // image. The ResolvedLiteral will have a non-zero float_literal_id if the
  // FEATURE_NUMERIC_TYPE language feature is enabled, which associates the
  // float literal with its original image in the float_literal_images_ cache in
  // order to preserve precision for float to numeric coercion.
  std::unique_ptr<const ResolvedLiteral> MakeResolvedFloatLiteral(
      const ASTNode* ast_location, const Type* type, const Value& value,
      bool has_explicit_type, absl::string_view image);

  // Make a new resolved literal without location. Those are essentially
  // constants produced by the resolver, which don't occur in the input string
  // (e.g., NULLs for optional CASE branches) or cannot be replaced by
  // query parameters (e.g., DAY keyword in intervals).
  static std::unique_ptr<const ResolvedLiteral>
  MakeResolvedLiteralWithoutLocation(const Value& value);

  // Propagates any deprecation warnings from the body of the function call
  // corresponding to 'signature'.
  absl::Status AddAdditionalDeprecationWarningsForCalledFunction(
      const ASTNode* ast_location, const FunctionSignature& signature,
      absl::string_view function_name, bool is_tvf);

  // Adds a deprecation warning pointing at `ast_location`.
  absl::Status AddDeprecationWarning(const ASTNode* ast_location,
                                     DeprecationWarning::Kind kind,
                                     absl::string_view message);

  static ResolvedColumnList ConcatColumnLists(const ResolvedColumnList& left,
                                              const ResolvedColumnList& right);

  // Appends the ResolvedColumns in <computed_columns> to those in
  // <column_list>, returning a new ResolvedColumnList.  The returned
  // list is sorted by ResolvedColumn ids.
  // TODO: The sort is not technically required, but it helps match
  // the result plan better against the pre-refactoring plans.
  static ResolvedColumnList ConcatColumnListWithComputedColumnsAndSort(
      const ResolvedColumnList& column_list,
      absl::Span<const std::unique_ptr<const ResolvedComputedColumn>>
          computed_columns);

  // Returns the alias of the given column (if not internal). Otherwise returns
  // the column pos (1-based as visible outside).
  // <alias> - assigned alias for the column (if any).
  // <column_pos> - 0-based column position in the query.
  static std::string ColumnAliasOrPosition(IdString alias, int column_pos);

  // Make the `output_column_list` column list for a statement using the
  // columns from `name_list`.  This strips column names for value tables and
  // also calls RecordColumnAccess on each column.
  std::vector<std::unique_ptr<const ResolvedOutputColumn>> MakeOutputColumnList(
      const NameList& name_list);

  // Make the `output_schema` for a statement based on `name_list`.
  // This includes calling MakeOutputColumnList.
  std::unique_ptr<const ResolvedOutputSchema> MakeOutputSchema(
      const NameList& name_list);

  // Like the method above, but makes one output column at a time, with
  // explicitly provided `name` and `column`.
  std::unique_ptr<const ResolvedOutputColumn> MakeOneOutputColumn(
      const NameList& name_list, std::string name,
      const ResolvedColumn& column);

  // Resolve an ASTQueryStatement.
  absl::Status ResolveQueryStatement(
      const ASTQueryStatement* query_stmt,
      std::unique_ptr<ResolvedStatement>* output_stmt,
      std::shared_ptr<const NameList>* output_name_list);

  absl::Status ResolveSubpipelineStatement(
      const ASTSubpipelineStatement* ast_subpipeline_statement,
      std::unique_ptr<ResolvedStatement>* output_stmt);

  absl::Status ResolveStatementWithPipeOperators(
      const ASTStatementWithPipeOperators* ast_stmt,
      std::unique_ptr<ResolvedStatement>* output_stmt);

  // Resolve the LIKE table name from a generic CREATE statement.
  absl::Status ResolveCreateStatementLikeTableName(
      const ASTPathExpression* like_table_name,
      const IdString& table_name_id_string,
      std::vector<std::unique_ptr<const ResolvedColumnDefinition>>*
          column_definition_list,
      const Table** like_table);

  // Resolve the CreateMode from a generic CREATE statement.
  absl::Status ResolveCreateStatementOptions(
      const ASTCreateStatement* ast_statement, absl::string_view statement_type,
      ResolvedCreateStatement::CreateScope* create_scope,
      ResolvedCreateStatement::CreateMode* create_mode) const;

  // Resolves properties of ASTCreateViewStatementBase.
  // Used by ResolveCreate(|Materialized)ViewStatement functions to resolve
  // parts that are common between logical and materialized views.
  // 'column_definition_list' parameter is set to nullptr for logical views.
  // Other output arguments are always non-nulls.
  absl::Status ResolveCreateViewStatementBaseProperties(
      const ASTCreateViewStatementBase* ast_statement,
      absl::string_view statement_type, absl::string_view object_type,
      std::vector<std::string>* table_name,
      ResolvedCreateStatement::CreateScope* create_scope,
      ResolvedCreateStatement::CreateMode* create_mode,
      ResolvedCreateStatementEnums::SqlSecurity* sql_security,
      std::vector<std::unique_ptr<const ResolvedOption>>* resolved_options,
      std::vector<std::unique_ptr<const ResolvedOutputColumn>>*
          output_column_list,
      std::vector<std::unique_ptr<const ResolvedColumnDefinition>>*
          column_definition_list,
      std::unique_ptr<const ResolvedScan>* query_scan, std::string* view_sql,
      bool* is_value_table, bool* is_recursive);

  // Creates the ResolvedGeneratedColumnInfo from an ASTGeneratedColumnInfo.
  // - `ast_generated_column`: Is a pointer to the Generated Column
  // - `column_name_list`: Contains the names of the columns seen so far
  // so that they can be referenced by generated columns.
  // - `opt_type`: The optional type of this expression if provided from the
  // syntax.
  // - `skip_type_match_check`: when true, skip checking generated column type
  //   can be coerced to column type. Mainly used in ALTER COLUMN SET GENERATED
  //   when the column doesn't exist.
  // - `output`: The resolved generated column.
  absl::Status ResolveGeneratedColumnInfo(
      const ASTGeneratedColumnInfo* ast_generated_column,
      const NameList& column_name_list, const Type* opt_type,
      bool skip_type_match_check,
      std::unique_ptr<ResolvedGeneratedColumnInfo>* output);

  // Creates the ResolvedIdentityColumnInfo from an ASTIdentityColumnInfo.
  // - `ast_identity_column`: Is a pointer to the identity column.
  // - `type`: The type of the column and expected type of the expression.
  // - `skip_type_match_check`: when true, skip checking identity column type
  //   can be coerced to column type. Mainly used in ALTER COLUMN SET GENERATED
  //   when the column doesn't exist.
  // - `output`: The resolved identity column.
  absl::Status ResolveIdentityColumnInfo(
      const ASTIdentityColumnInfo* ast_identity_column,
      const NameList& column_name_list, const Type* type,
      bool skip_type_match_check,
      std::unique_ptr<ResolvedIdentityColumnInfo>* output);

  // Returns a Value from an ASTExpression representing an identity column
  // sequence attribute. The output Value is expected to be a member of
  // ResolvedIdentityColumnInfo.
  // - `attribute_expr`: A pointer to the ASTExpression object holding the
  // attribute expression.
  // - `type`: The type of the column and expected type of the expression.
  // - `skip_type_match_check`: when true, skip checking identity column type
  //   can be coerced to column type. Mainly used in ALTER COLUMN SET GENERATED
  //   when the column doesn't exist.
  // - `attribute_name`: The name of the sequence attribute.
  absl::StatusOr<Value> ResolveIdentityColumnAttribute(
      const ASTExpression* attribute_expr, const Type* type,
      bool skip_type_match_check, absl::string_view attribute_name);

  // Creates a ResolvedExpr from an ASTExpression representing a column
  // default value. The output ResolvedExpr is expected to be a member of
  // ResolvedColumnDefinition.
  // - <ast_column_default>: a pointer to the ASTExpression object holding the
  //   default expression.
  // - <opt_type>: The type of this expression provided from the syntax.
  // - <skip_type_match_check>: when true, skip checking default value type
  //   can be coerced to column type. Mainly used in ALTER COLUMN SET DEFAULT
  //   when the column doesn't exist.
  // - <default_value>: The resolved default value.
  absl::Status ResolveColumnDefaultExpression(
      const ASTExpression* ast_column_default, const Type* opt_type,
      bool skip_type_match_check,
      std::unique_ptr<ResolvedColumnDefaultValue>* default_value);

  // Resolve the column definition list from a CREATE TABLE, LOAD DATA or CREATE
  // MODEL statement.
  // - <statement_type>: the type of statement, used for error messages only.
  absl::Status ResolveColumnDefinitionList(
      IdString table_name_id_string, absl::string_view statement_type,
      absl::Span<const ASTColumnDefinition* const> ast_column_definitions,
      std::vector<std::unique_ptr<const ResolvedColumnDefinition>>*
          column_definition_list,
      ColumnIndexMap* column_indexes);

  // Creates a ResolvedColumnDefinition from an ASTTableElement.
  // Lots of complexity of this function is required because of generated
  // columns. During expression resolution, the resolver might start resolving
  // a referenced column that was not resolved yet.
  // e.g. CREATE TABLE T (a as b, b INT64);
  // When that happens, the resolver will record the pending dependency (in the
  // previous case 'b') and start resolving 'b'. Then it will retry resolving
  // 'a' again.
  //
  // The following data structures allow this to happen efficiently:
  // - <id_to_table_element_map>: Map from name of the column to the
  // ASTTableElement. This is used for finding ASTTableElement when one
  // resolution fails. The ASTColumnDefinition* are not owned.
  // - <id_to_column_def_map>: Map from name of the column to the
  // ResolvedColumnDefinition pointer. It's used for avoiding resolving the same
  // ASTTableElement more than once and also to avoid allocating a new id for a
  // ResolvedColumn. The ResolvedColumnDefinition* are not owned.
  // - <column>: The column definition to resolve.
  // - <table_name_id_string>: The name of the underlying table.
  // - <column_name_list>: Ordered list of visible column names for this column.
  // This list will also be updated with the new column being added by this
  // ResolvedColumnDefinition.
  // Note: This function requires 'generated_column_cycle_detector_' to be
  // non-NULL.
  absl::Status ResolveColumnDefinition(
      const absl::flat_hash_map<IdString, const ASTColumnDefinition*,
                                IdStringHash>& id_to_column_definition_map,
      absl::node_hash_map<IdString,
                          std::unique_ptr<const ResolvedColumnDefinition>,
                          IdStringHash>* id_to_column_def_map,
      const ASTColumnDefinition* column, const IdString& table_name_id_string,
      NameList* column_name_list);

  // Creates a ResolvedColumnDefinition from an ASTColumnDefinition.
  // - <column>: The column definition to resolve.
  // - <table_name_id_string>: The name of the underlying table.
  // - <column_name_list>: Ordered list of visible column names for this column.
  // This list will also be updated with the new column being added by this
  // ResolvedColumnDefinition.
  absl::StatusOr<std::unique_ptr<const ResolvedColumnDefinition>>
  ResolveColumnDefinitionNoCache(const ASTColumnDefinition* column,
                                 const IdString& table_name_id_string,
                                 NameList* column_name_list);

  // Resolves AS SELECT clause for CREATE TABLE/VIEW/MATERIALIZED_VIEW/MODEL
  // statements.
  // The CREATE TABLE/MODEL statement must not have a column definition list
  // (otherwise, use ResolveAndAdaptQueryAndOutputColumns instead).
  // - For regular CREATE TABLE AS SELECT,
  //     <ast_query> is the query.
  //     <pipe_input_name_list> is null.
  //     <query_scan> is unset, and will get the created scan on output.
  // - For CREATE TABLE inside ResolvedPipeCreateTableScan
  //     <ast_query> is null.
  //     <pipe_input_name_list> is the NameList for the pipe input table.
  //     <query_scan> is the initial scan for the pipe input table. It may be
  //       modified to add casts.
  // - <is_value_table> and <output_column_list> are pointers for output values
  //   and cannot be null.
  // - <internal_table_name> should be a static IdString such as
  //   kCreateAsId and kViewId; it's used as an alias of the SELECT query.
  // - <view_explicit_column_list> the list of columns with optional column
  //   options in the formal DDL declaration. This field should only be supplied
  //   for CREATE VIEW v(c1 OPTIONS(...), c2) and CREATE MATERIALIZED VIEW
  //   v(...).
  // - <is_recursive_view> is true only for views which are actually recursive.
  //   This affects the resolved tree respresentation.
  // - If <column_definition_list> is not null, then <column_definition_list>
  //   will be populated based on the output column list and
  //   <table_name_id_string> (the name of the table to be created).
  //   Currently, when this is invoked for CREATE VIEW the
  //   <column_definition_list> is null, but for CREATE
  //   TABLE/MATERIALIZED_VIEW/MODEL the <column_definition_list> is non-null.
  absl::Status ResolveQueryAndOutputColumns(
      const ASTNode* ast_location, const ASTQuery* ast_query,
      const NameList* pipe_input_name_list, absl::string_view object_type,
      bool is_recursive_view, const std::vector<IdString>& table_name_id_string,
      IdString internal_table_name,
      const ASTColumnWithOptionsList* view_explicit_column_list,
      std::unique_ptr<const ResolvedScan>* query_scan, bool* is_value_table,
      std::vector<std::unique_ptr<const ResolvedOutputColumn>>*
          output_column_list,
      std::vector<std::unique_ptr<const ResolvedColumnDefinition>>*
          column_definition_list);

  // Resolves AS SELECT clause for CREATE TABLE AS SELECT when the SQL query
  // contains a column definition list. CAST might be added to <query_scan>, to
  // ensure that the output types are the same as in <column_definition_list>.
  // No pointer in the arguments can be null.
  absl::Status ResolveAndAdaptQueryAndOutputColumns(
      const ASTNode* ast_location,
      const ASTQuery* ast_query,             // Present with AS SELECT.
      const NameList* pipe_input_name_list,  // Present with pipe input.
      const ASTPathExpression* like_table_name,
      const ASTTableElementList* table_element_list,
      absl::Span<const ASTColumnDefinition* const> ast_column_definitions,
      std::vector<std::unique_ptr<const ResolvedColumnDefinition>>&
          column_definition_list,
      std::unique_ptr<const ResolvedScan>* query_scan,
      std::vector<std::unique_ptr<const ResolvedOutputColumn>>*
          output_column_list);

  // Resolves the column schema from a column definition in a CREATE TABLE
  // statement. If annotations is null, it means annotations are disallowed.
  // generated_column_info must not be null if a generated column is present
  // on the ASTColumnSchema. Likewise, default_value must not be null if
  // a default column expression is present on the ASTColumnSchema. At most
  // one of <generated_column_info> and <default_value> may be non-null.
  absl::Status ResolveColumnSchema(
      const ASTColumnSchema* schema, const NameList& column_name_list,
      const Type** resolved_type,
      std::unique_ptr<const ResolvedColumnAnnotations>* annotations,
      std::unique_ptr<ResolvedGeneratedColumnInfo>* generated_column_info,
      std::unique_ptr<ResolvedColumnDefaultValue>* default_value);

  // Resolves a column schema for a type that is defined also by an element type
  // (eg ARRAY<T> and RANGE<T> types). If enable_nested_annotations is true,
  // adds any annotations defined on the element type to child_annotation_list.
  // Also outputs the element type in resolved_element_type.
  absl::Status ResolveElementTypeColumnSchema(
      const ASTElementTypeColumnSchema* schema, bool enable_nested_annotations,
      std::vector<std::unique_ptr<const ResolvedColumnAnnotations>>&
          child_annotation_list,
      const Type** resolved_element_type);

  // Validates the ASTColumnAttributeList, in particular looking for
  // duplicate attribute definitions (i.e. "PRIMARY KEY" "PRIMARY KEY").
  // - attribute_list is a pointer because it's an optional construct that can
  // be nullptr.
  absl::Status ValidateColumnAttributeList(
      const ASTColumnAttributeList* attribute_list) const;

  // Generates an error status if 'type' is or contains in its nesting structure
  // a Type for which SupportsReturning is false.
  absl::Status ValidateTypeIsReturnable(const Type* type,
                                        const ASTNode* error_node);
  // Validate that the statement doesn't return unreturnable types.
  absl::Status ValidateStatementIsReturnable(const ResolvedStatement* statement,
                                             const ASTNode* error_node);
  // Validate that types in `output_list` are allowed to be returned.
  // Works for lists of ResolvedOutputColumn or ResolvedColumnDefinition.
  template <typename OutputElementType>
  absl::Status ValidateColumnListIsReturnable(
      const std::vector<OutputElementType>& output_list,
      const ASTNode* error_node) {
    for (const auto& element : output_list) {
      GOOGLESQL_RETURN_IF_ERROR(
          ValidateTypeIsReturnable(element->column().type(), error_node));
    }
    return absl::OkStatus();
  }

  // Resolve the primary key from column definitions.
  absl::Status ResolvePrimaryKey(
      absl::Span<const ASTTableElement* const> table_elements,
      const ColumnIndexMap& column_indexes,
      std::unique_ptr<ResolvedPrimaryKey>* resolved_primary_key);

  // Resolve the primary key from its AST node and the column indexes of
  // resolved columns.
  absl::Status ResolvePrimaryKey(
      const ColumnIndexMap& column_indexes,
      const ASTPrimaryKey* ast_primary_key,
      std::unique_ptr<ResolvedPrimaryKey>* resolved_primary_key);

  // Resolves the column and table foreign key constraints.
  // - column_indexes: mapping column names to indices in <column_definitions>
  // - <constraint_names>: contains list of constraint names already encountered
  //   so far, for checking uniqueness of new constraint names. The method is
  //   expected to add new constraint names to the list before returning.
  absl::Status ResolveForeignKeys(
      absl::Span<const ASTTableElement* const> ast_table_elements,
      const ColumnIndexMap& column_indexes,
      absl::Span<const std::unique_ptr<const ResolvedColumnDefinition>>
          column_definitions,
      std::set<std::string, googlesql_base::CaseLess>*
          constraint_names,
      std::vector<std::unique_ptr<const ResolvedForeignKey>>* foreign_key_list);

  // Resolves a column foreign key constraint.
  absl::Status ResolveForeignKeyColumnConstraint(
      const ColumnIndexMap& column_indexes,
      const std::vector<const Type*>& column_types,
      const ASTColumnDefinition* ast_column_definition,
      const ASTForeignKeyColumnAttribute* ast_foreign_key,
      std::vector<std::unique_ptr<ResolvedForeignKey>>* resolved_foreign_keys);

  // Resolves a table foreign key constraint.
  absl::Status ResolveForeignKeyTableConstraint(
      const ColumnIndexMap& column_indexes,
      const std::vector<const Type*>& column_types,
      const ASTForeignKey* ast_foreign_key,
      std::vector<std::unique_ptr<ResolvedForeignKey>>* resolved_foreign_keys);

  // Resolves a foreign key's referencing columns and referenced table and
  // columns. <column_indexes> is used to index into <column_types>.
  absl::Status ResolveForeignKeyReference(
      const ColumnIndexMap& column_indexes,
      const std::vector<const Type*>& column_types,
      absl::Span<const ASTIdentifier* const> ast_referencing_column_identifiers,
      const ASTForeignKeyReference* ast_foreign_key_reference,
      ResolvedForeignKey* foreign_key);

  // Resolves ABSL_CHECK constraints.
  // - <name_scope>: used for resolving column names in the expression.
  // - <constraint_names>: contains list of constraint names already encountered
  //   so far, for checking uniqueness of new constraint names. The method is
  //   expected to add new constraint names to the list before returning.
  // - <check_constraint_list>: List of ResolvedCheckConstraint created.
  absl::Status ResolveCheckConstraints(
      absl::Span<const ASTTableElement* const> ast_table_elements,
      const NameScope& name_scope,
      std::set<std::string, googlesql_base::CaseLess>*
          constraint_names,
      std::vector<std::unique_ptr<const ResolvedCheckConstraint>>*
          check_constraint_list);

  // Resolves the PARTITION BY or CLUSTER BY expressions of a CREATE
  // TABLE/MATERIALIZED_VIEW statement. <clause_type> is either PARTITION_BY or
  // CLUSTER_BY. <name_scope> and <query_info> are used for name resolution.
  // <partition_by_list_out>, which may be non-empty even in error cases.
  absl::Status ResolveCreateTablePartitionByList(
      absl::Span<const ASTExpression* const> expressions,
      PartitioningKind partitioning_kind, const NameScope& name_scope,
      QueryResolutionInfo* query_info,
      std::vector<std::unique_ptr<const ResolvedExpr>>* partition_by_list_out);

  // Resolves the index path expression inside a CREATE INDEX statement.
  // `ast_statement` is the top level CREATE INDEX statement.
  // `ordering_expression` is the index ordering expression containing the path
  // to resolve.
  // `resolved_*` are the output resolved results of the CREATE INDEX statement.
  absl::Status ResolveIndexingPathExpression(
      const NameScope& name_scope, IdString table_alias,
      const ASTCreateIndexStatement* ast_statement,
      const ASTOrderingExpression* ordering_expression,
      std::set<IdString, IdStringCaseLess>& resolved_columns,
      std::vector<std::unique_ptr<const ResolvedComputedColumn>>&
          resolved_computed_columns,
      std::vector<std::unique_ptr<const ResolvedIndexItem>>&
          resolved_index_items,
      std::unique_ptr<const ResolvedTableScan>& resolved_table_scan);

  // Resolve a CREATE INDEX statement.
  absl::Status ResolveCreateIndexStatement(
      const ASTCreateIndexStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  // Validates 'resolved_expr' on an index key or storing clause of an index.
  //
  // 'resolved_columns' stores all the resolved columns in index keys and
  // storing columns. It errors out if the referred column of 'resolved_expr' is
  // already in 'resolved_columns'. If not, the column is inserted into
  // 'resolved_columns' for future usage.
  absl::Status ValidateResolvedExprForCreateIndex(
      const ASTCreateIndexStatement* ast_statement,
      const ASTExpression* ast_expression,
      std::set<IdString, IdStringCaseLess>* resolved_columns,
      const ResolvedExpr* resolved_expr);

  // Validates index key expressions for the search / vector index.
  //
  // The key expression should not have ASC or DESC option; the key expression
  // should not have the null order option.
  // The key expression should only refer to column name until b/180069278 been
  // fixed.
  // TODO: Support alias on the index key expression.
  absl::Status ValidateIndexKeyExpressionForCreateSearchOrVectorIndex(
      absl::string_view index_type,
      const ASTOrderingExpression& ordering_expression,
      const ResolvedExpr& resolved_expr);

  // A helper that resolves 'unnest_expression_list' for CREATE INDEX statement.
  //
  // 'name_list' is expected to contain the available names from the base table.
  //
  // When this function returns, populates 'name_list', and
  // 'resolved_unnest_items' accordingly.
  absl::Status ResolveIndexUnnestExpressions(
      const ASTIndexUnnestExpressionList* unnest_expression_list,
      NameList* name_list,
      std::vector<std::unique_ptr<const ResolvedUnnestItem>>*
          resolved_unnest_items);

  // Validates every ASTUnnestExpression in the given `unnest_expression_list`.
  // `unnest_expression_list` should not be nullptr.
  absl::Status ValidateASTIndexUnnestExpressionList(
      const ASTIndexUnnestExpressionList* unnest_expression_list) const;

  // Resolve a CREATE TABLE [AS SELECT] statement.
  // For pipe CREATE TABLE, `pipe_input_name_list` provides the input table
  // schema.
  absl::Status ResolveCreateTableStatement(
      const ASTCreateTableStatement* ast_statement,
      std::unique_ptr<const ResolvedScan>* pipe_input_scan,
      const NameList* pipe_input_name_list,
      std::unique_ptr<ResolvedStatement>* output);

  // Resolve a CREATE MODEL statement.
  absl::Status ResolveCreateModelStatement(
      const ASTCreateModelStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  // Resolve a CREATE CONNECTION statement.
  absl::Status ResolveCreateConnectionStatement(
      const ASTCreateConnectionStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  // Resolve a CREATE DATABASE statement.
  absl::Status ResolveCreateDatabaseStatement(
      const ASTCreateDatabaseStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  // Resolve a CREATE SCHEMA statement.
  absl::Status ResolveCreateSchemaStatement(
      const ASTCreateSchemaStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  // Resolves a CREATE EXTERNAL SCHEMA statement.
  absl::Status ResolveCreateExternalSchemaStatement(
      const ASTCreateExternalSchemaStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  // Resolves a CREATE VIEW statement
  absl::Status ResolveCreateViewStatement(
      const ASTCreateViewStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  // Resolves a CREATE MATERIALIZED VIEW statement
  absl::Status ResolveCreateMaterializedViewStatement(
      const ASTCreateMaterializedViewStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  // Resolves a CREATE APPROX VIEW statement
  absl::Status ResolveCreateApproxViewStatement(
      const ASTCreateApproxViewStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveCreateExternalTableStatement(
      const ASTCreateExternalTableStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  // Resolves a CREATE SNAPSHOT TABLE statement.
  absl::Status ResolveCreateSnapshotTableStatement(
      const ASTCreateSnapshotTableStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  // Resolves a CREATE CONSTANT statement.
  absl::Status ResolveCreateConstantStatement(
      const ASTCreateConstantStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  // Resolves a CREATE FUNCTION or CREATE AGGREGATE FUNCTION statement.
  absl::Status ResolveCreateFunctionStatement(
      const ASTCreateFunctionStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  // Resolves a CREATE TABLE FUNCTION statement.
  absl::Status ResolveCreateTableFunctionStatement(
      const ASTCreateTableFunctionStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  // Resolves a CREATE PROCEDURE statement.
  absl::Status ResolveCreateProcedureStatement(
      const ASTCreateProcedureStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  // Resolves a CREATE SEQUENCE statement.
  absl::Status ResolveCreateSequenceStatement(
      const ASTCreateSequenceStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  // This enum instructs the ResolveTVFSchema method on how to check the
  // properties of the resulting schema object.
  enum class ResolveTVFSchemaCheckPropertiesType {
    // The ResolveTVFSchema method checks if the resulting schema is valid, and
    // if not, returns an error reporting that the schema is invalid for a
    // table-valued argument for a table-valued function.
    INVALID_TABLE_ARGUMENT,

    // The ResolveTVFSchema method checks if the resulting schema is valid, and
    // if not, returns an error reporting that the schema is invalid for a
    // return table for a table-valued function.
    INVALID_OUTPUT_SCHEMA,

    // The ResolveTVFSchema method does not perform either of the above checks.
    SKIP_CHECKS
  };

  // Resolves a table-valued argument or return type for a CREATE TABLE FUNCTION
  // statement. This is only called from the ResolveCreateTableFunctionStatement
  // method. <check_type> indicates how to check the properties of the resulting
  // schema.
  absl::Status ResolveTVFSchema(const ASTTVFSchema* ast_tvf_schema,
                                ResolveTVFSchemaCheckPropertiesType check_type,
                                TVFRelation* tvf_relation);

  // Helper function that returns a customized error for unsupported (templated)
  // argument types in a function declaration.
  absl::Status UnsupportedArgumentError(const ASTFunctionParameter& argument,
                                        absl::string_view context);

  // This enum instructs the ResolveFunctionDeclaration method on what kind of
  // function it is currently resolving.
  enum class ResolveFunctionDeclarationType {
    // This is a scalar function that accepts zero or more individual values and
    // returns a single value.
    SCALAR_FUNCTION,

    // This is an aggregate function.
    AGGREGATE_FUNCTION,

    // This is a table-valued function.
    TABLE_FUNCTION,

    // This is a procedure.
    PROCEDURE,
  };

  absl::Status ResolveFunctionDeclaration(
      const ASTFunctionDeclaration* function_declaration,
      ResolveFunctionDeclarationType function_type,
      std::vector<std::string>* function_name, FunctionArgumentInfo* arg_info);

  absl::Status ResolveRelationalFunctionParameter(
      const ASTFunctionParameter& function_param,
      ResolveFunctionDeclarationType function_type,
      FunctionArgumentTypeOptions argument_type_options,
      FunctionArgumentInfo& arg_info);

  absl::Status ResolveScalarFunctionParameter(
      const ASTFunctionParameter& function_param,
      ResolvedArgumentDef::ArgumentKind arg_kind,
      FunctionArgumentTypeOptions argument_type_options,
      FunctionArgumentInfo& arg_info);

  absl::Status ResolveFunctionParameter(
      const ASTFunctionParameter& function_param,
      ResolveFunctionDeclarationType function_type,
      FunctionArgumentInfo& arg_info);

  // Resolves function parameter list and populates <arg_info>.
  absl::Status ResolveFunctionParameters(
      const ASTFunctionParameters* ast_function_parameters,
      ResolveFunctionDeclarationType function_type,
      FunctionArgumentInfo* arg_info);

  // Sets the Resolver::function_argument_info_ variable that signals what
  // argument names are in scope for expression and table name resolution.
  // Returns a cleanup object that resets the Resolver::function_argument_info_
  // variable when it goes out of scope.
  using AutoUnsetArgumentInfo =
      decltype(absl::MakeCleanup(std::function<void()>()));
  AutoUnsetArgumentInfo SetArgumentInfo(const FunctionArgumentInfo* arg_info);

  absl::Status ResolveCreatePrivilegeRestrictionStatement(
      const ASTCreatePrivilegeRestrictionStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveCreateRowAccessPolicyStatement(
      const ASTCreateRowAccessPolicyStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveCloneDataStatement(
      const ASTCloneDataStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  // For pipe EXPORT DATA `pipe_input_name_list` provides the input table
  // schema.
  absl::Status ResolveExportDataStatement(
      const ASTExportDataStatement* ast_statement,
      const NameList* pipe_input_name_list,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveExportMetadataStatement(
      const ASTExportMetadataStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveExportModelStatement(
      const ASTExportModelStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveCallStatement(const ASTCallStatement* ast_call,
                                    std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveDefineTableStatement(
      const ASTDefineTableStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveDescribeStatement(
      const ASTDescribeStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveShowStatement(const ASTShowStatement* ast_statement,
                                    std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveBeginStatement(
      const ASTBeginStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveSetTransactionStatement(
      const ASTSetTransactionStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveCommitStatement(
      const ASTCommitStatement* statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveRollbackStatement(
      const ASTRollbackStatement* statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveStartBatchStatement(
      const ASTStartBatchStatement* statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveRunBatchStatement(
      const ASTRunBatchStatement* statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveAbortBatchStatement(
      const ASTAbortBatchStatement* statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveDeleteStatement(
      const ASTDeleteStatement* ast_statement,
      std::unique_ptr<ResolvedDeleteStmt>* output);
  // <target_alias> is the alias of the target, which must be in the topmost
  // scope of <scope>.
  absl::Status ResolveDeleteStatementImpl(
      const ASTDeleteStatement* ast_statement, IdString target_alias,
      const std::shared_ptr<const NameList>& target_name_list,
      const NameScope* scope,
      std::unique_ptr<const ResolvedTableScan> table_scan,
      std::unique_ptr<ResolvedDeleteStmt>* output);

  absl::Status ResolveUndropStatement(
      const ASTUndropStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveDropStatement(const ASTDropStatement* ast_statement,
                                    std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveDropFunctionStatement(
      const ASTDropFunctionStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveDropTableFunctionStatement(
      const ASTDropTableFunctionStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveDropPrivilegeRestrictionStatement(
      const ASTDropPrivilegeRestrictionStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveDropRowAccessPolicyStatement(
      const ASTDropRowAccessPolicyStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveDropAllRowAccessPoliciesStatement(
      const ASTDropAllRowAccessPoliciesStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveDropMaterializedViewStatement(
      const ASTDropMaterializedViewStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveDropSnapshotTableStatement(
      const ASTDropSnapshotTableStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveDropIndexStatement(
      const ASTDropIndexStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveDMLTargetTable(
      const ASTPathExpression* target_path, const ASTAlias* target_path_alias,
      const ASTHint* hint, IdString* alias,
      std::unique_ptr<const ResolvedTableScan>* resolved_table_scan,
      std::shared_ptr<const NameList>* name_list,
      ResolvedColumnToCatalogColumnHashMap&
          resolved_columns_to_catalog_columns_for_target_scan);

  // Resolve INSERT statement or pipe INSERT.
  // `pipe_input_name_list` and `pipe_input_scan` provide the pipe input table
  // when resolving pipe INSERT.
  absl::Status ResolveInsertStatement(
      const ASTInsertStatement* ast_statement,
      const NameList* pipe_input_name_list,
      std::unique_ptr<const ResolvedScan> pipe_input_scan,
      std::unique_ptr<ResolvedInsertStmt>* output);
  absl::Status ResolveInsertStatementImpl(
      const ASTInsertStatement* ast_statement, IdString target_alias,
      const std::shared_ptr<const NameList>& target_name_list,
      std::unique_ptr<const ResolvedTableScan> table_scan,
      const ResolvedColumnList& insert_columns,
      const NameScope* nested_scope,  // NULL for non-nested INSERTs.
      const NameList* pipe_input_name_list,
      std::unique_ptr<const ResolvedScan> pipe_input_scan,
      ResolvedColumnToCatalogColumnHashMap&
          resolved_columns_to_catalog_columns_for_target_scan,
      std::unique_ptr<ResolvedInsertStmt>* output);

  absl::Status ResolveUpdateStatement(
      const ASTUpdateStatement* ast_statement,
      std::unique_ptr<ResolvedUpdateStmt>* output);
  // Resolves the given UPDATE statement node. The function uses two name
  // scopes: <target_scope> is used to resolve names that should appear as
  // targets in the SET clause and should come from the target table;
  // <update_scope> includes all names that can appear inside the UPDATE
  // statement and it is used to resolve names anywhere outside the target
  // expressions. <target_alias> is the alias of the target, which must be in
  // the topmost scope of both <target_scope> and <update_scope>.
  absl::Status ResolveUpdateStatementImpl(
      const ASTUpdateStatement* ast_statement, bool is_nested,
      IdString target_alias, const NameScope* target_scope,
      const std::shared_ptr<const NameList>& target_name_list,
      const NameScope* update_scope,
      std::unique_ptr<const ResolvedTableScan> table_scan,
      std::unique_ptr<const ResolvedScan> from_scan,
      ResolvedColumnToCatalogColumnHashMap&
          resolved_columns_to_catalog_columns_for_target_scan,
      std::unique_ptr<ResolvedUpdateStmt>* output);

  absl::Status ResolveMergeStatement(
      const ASTMergeStatement* statement,
      std::unique_ptr<ResolvedMergeStmt>* output);
  absl::Status ResolveMergeWhenClauseList(
      const ASTMergeWhenClauseList* when_clause_list,
      const IdStringHashMapCase<ResolvedColumn>* target_table_columns,
      const NameScope* target_name_scope, const NameScope* source_name_scope,
      const NameScope* all_name_scope, const NameList* target_name_list,
      const NameList* source_name_list,
      std::vector<std::unique_ptr<const ResolvedMergeWhen>>*
          resolved_when_clauses);
  absl::Status ResolveMergeUpdateAction(
      const ASTUpdateItemList* update_item_list,
      const NameScope* target_name_scope, const NameScope* all_name_scope,
      std::vector<std::unique_ptr<const ResolvedUpdateItem>>*
          resolved_update_item_list);
  absl::Status ResolveMergeInsertAction(
      const ASTMergeAction* merge_action,
      const IdStringHashMapCase<ResolvedColumn>* target_table_columns,
      const NameScope* target_name_scope, const NameScope* all_name_scope,
      const NameList* target_name_list, const NameList* source_name_list,
      ResolvedColumnList* resolved_insert_column_list,
      std::unique_ptr<const ResolvedInsertRow>* resolved_insert_row);

  absl::Status ResolveTruncateStatement(
      const ASTTruncateStatement* statement,
      std::unique_ptr<ResolvedTruncateStmt>* output);

  absl::Status ResolveGrantStatement(
      const ASTGrantStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveRevokeStatement(
      const ASTRevokeStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveAlterConnectionStatement(
      const ASTAlterConnectionStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveAlterPrivilegeRestrictionStatement(
      const ASTAlterPrivilegeRestrictionStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveAlterRowAccessPolicyStatement(
      const ASTAlterRowAccessPolicyStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveAlterAllRowAccessPoliciesStatement(
      const ASTAlterAllRowAccessPoliciesStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveAlterIndexStatement(
      const ASTAlterIndexStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  // `column_name_list`: Used and populated only for resolving
  //   add generated column actions in ALTER TABLE statements. Contains all
  //   existing table columns and any columns added by prior ALTER actions
  //   within the same statement. Can be nullptr for other statements or if no
  //   available table schema is provided.
  absl::Status ResolveAlterActions(
      const ASTAlterStatementBase* ast_statement,
      absl::string_view alter_statement_kind, NameList* column_name_list,
      std::unique_ptr<ResolvedStatement>* output,
      bool* has_only_set_options_action,
      std::vector<std::unique_ptr<const ResolvedAlterAction>>* alter_actions);

  // Resolves AlterActions for either top-level ALTER statements, or for
  // AlterSubEntityActions. When called to resolve AlterSubEntityAction actions,
  // this method will be called recursively, with <path> set to nullptr.
  absl::Status ResolveAlterActions(
      const ASTNode* ast_statement, absl::string_view alter_statement_kind,
      const ASTPathExpression* path,
      absl::Span<const ASTAlterAction* const> actions, bool is_if_exists,
      NameList* column_name_list, std::unique_ptr<ResolvedStatement>* output,
      bool* has_only_set_options_action,
      std::vector<std::unique_ptr<const ResolvedAlterAction>>* alter_actions);

  // `table`: Can be NULL. If the table does not exist in the catalog, we try
  //   to resolve the ALTER statement anyway.
  //
  // `column_name_list`: Used and populated only for resolving
  //   add generated column actions in ALTER TABLE statements. Contains all
  //   existing table columns and any columns added by prior ALTER actions
  //   within the same statement. Can be nullptr for other statements or if no
  //   available table schema is provided.
  absl::Status ResolveAddColumnAction(
      IdString table_name_id_string, const Table* table,
      const ASTAddColumnAction* action, NameList* column_name_list,
      IdStringSetCase* new_columns, IdStringSetCase* columns_to_drop,
      std::unique_ptr<const ResolvedAlterAction>* alter_action);

  // <table> can be NULL. If the table does not exist in the catalog, we try
  // to resolve the ALTER statement anyway.
  absl::Status ResolveDropColumnAction(
      absl::string_view alter_statement_kind, const Table* table,
      const ASTDropColumnAction* action, IdStringSetCase* new_columns,
      IdStringSetCase* columns_to_drop,
      std::unique_ptr<const ResolvedAlterAction>* alter_action);

  // <table> can be NULL. If the table does not exist in the catalog, we try
  // to resolve the ALTER statement anyway.
  absl::Status ResolveRenameColumnAction(
      const Table* table, const ASTRenameColumnAction* action,
      IdStringSetCase* columns_to_rename,
      IdStringHashMapCase<IdString>* columns_rename_map,
      std::unique_ptr<const ResolvedAlterAction>* alter_action);

  // <table> can be NULL. If the table does not exist in the catalog, we try
  // to resolve the ALTER statement anyway.
  absl::Status ResolveAlterColumnTypeAction(
      IdString table_name_id_string, const Table* table,
      const ASTAlterColumnTypeAction* action,
      std::unique_ptr<const ResolvedAlterAction>* alter_action);

  // <table> can be NULL. If the table does not exist in the catalog, we try
  // to resolve the ALTER statement anyway.
  absl::Status ResolveAlterColumnOptionsAction(
      const Table* table, const ASTAlterColumnOptionsAction* action,
      std::unique_ptr<const ResolvedAlterAction>* alter_action);

  // <table> can be NULL. If the table does not exist in the catalog, we try
  // to resolve the ALTER statement anyway.
  absl::Status ResolveAlterColumnDropNotNullAction(
      const Table* table, const ASTAlterColumnDropNotNullAction* action,
      std::unique_ptr<const ResolvedAlterAction>* alter_action);

  // <table> can be NULL. If the table does not exist in the catalog, we try
  // to resolve the ALTER statement anyway.
  absl::Status ResolveAlterColumnSetDefaultAction(
      IdString table_name_id_string, const Table* table,
      const ASTAlterColumnSetDefaultAction* action,
      std::unique_ptr<const ResolvedAlterAction>* alter_action);

  // <table> can be NULL. If the table does not exist in the catalog, we try
  // to resolve the ALTER statement anyway.
  absl::Status ResolveAlterColumnDropDefaultAction(
      const Table* table, const ASTAlterColumnDropDefaultAction* action,
      std::unique_ptr<const ResolvedAlterAction>* alter_action);

  // `table` can be nullptr. If the table does not exist in the catalog, we try
  // to resolve the ALTER statement anyway.
  absl::Status ResolveAlterColumnDropGeneratedAction(
      const Table* table, const ASTAlterColumnDropGeneratedAction& action,
      std::unique_ptr<const ResolvedAlterAction>& alter_action);

  // `table` can be nullptr. If the table does not exist in the catalog, we try
  // to resolve the ALTER statement anyway.
  absl::Status ResolveAlterColumnSetGeneratedAction(
      const Table* table, const ASTAlterColumnSetGeneratedAction& action,
      std::unique_ptr<const ResolvedAlterAction>& alter_action);

  absl::Status ResolveSetCollateClause(
      const ASTSetCollateClause* action,
      std::unique_ptr<const ResolvedAlterAction>* alter_action);

  absl::Status ResolveAlterDatabaseStatement(
      const ASTAlterDatabaseStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveAlterSchemaStatement(
      const ASTAlterSchemaStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveAlterExternalSchemaStatement(
      const ASTAlterExternalSchemaStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveAlterTableStatement(
      const ASTAlterTableStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveAlterViewStatement(
      const ASTAlterViewStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveAlterMaterializedViewStatement(
      const ASTAlterMaterializedViewStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveAlterApproxViewStatement(
      const ASTAlterApproxViewStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveAlterModelStatement(
      const ASTAlterModelStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveRenameStatement(
      const ASTRenameStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveImportStatement(
      const ASTImportStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveModuleStatement(
      const ASTModuleStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveAnalyzeStatement(
      const ASTAnalyzeStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveAssertStatement(
      const ASTAssertStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  // Resolve an ASTQuery ignoring its ASTWithClause.  This is only called from
  // inside ResolveQuery after resolving the with clause if there was one.
  //
  // <inferred_type_for_query>: See comment on ResolveQuery.
  absl::Status ResolveQueryAfterWith(
      const ASTQuery* query, const NameScope* scope, IdString query_alias,
      const Type* inferred_type_for_query,
      std::unique_ptr<const ResolvedScan>* output,
      std::shared_ptr<const NameList>* output_name_list);

  // Resolve an ASTQuery (which may have an ASTWithClause).
  //
  // <query_alias> is the table name used internally for the ResolvedColumns
  // produced as output of this query (for display only).
  //
  // Side-effect: Updates named_subquery_map_ to reflect WITH aliases currently
  // in scope so WITH references can be resolved inside <query>.
  //
  // See ResolveQueryOptions for details on options.
  //
  // If `options.allow_terminal` is true, then `*output_name_list` can be NULL
  // on return, indicating this query had a terminal pipe operator and does not
  // return a table.
  absl::Status ResolveQuery(const ASTQuery* query, const NameScope* scope,
                            IdString query_alias,
                            std::unique_ptr<const ResolvedScan>* output,
                            std::shared_ptr<const NameList>* output_name_list,
                            ResolveQueryOptions options = {});

  absl::Status ResolveFromQuery(
      const ASTFromQuery* from_query, const NameScope* scope,
      std::unique_ptr<const ResolvedScan>* output,
      std::shared_ptr<const NameList>* output_name_list);

  // Resolves a WITH entry.
  // <recursive> is true only when a WITH entry is actually recursive, as
  // opposed to merely belonging to a WITH clause with the RECURSIVE keyword.
  absl::StatusOr<std::unique_ptr<const ResolvedWithEntry>> ResolveAliasedQuery(
      const ASTAliasedQuery* with_entry, bool recursive);

  // Validates the <modifiers> to the with entry with alias <query_alias>.
  // <is_recursive> is true only when a WITH entry is actually recursive, as
  // opposed to merely belonging to a WITH clause with the RECURSIVE keyword.
  absl::Status ValidateAliasedQueryModifiers(
      IdString query_alias, const ASTAliasedQueryModifiers* modifiers,
      bool is_recursive);

  // Resolves the depth modifier to a recursive WITH entry.
  absl::StatusOr<std::unique_ptr<const ResolvedRecursionDepthModifier>>
  ResolveRecursionDepthModifier(
      const ASTRecursionDepthModifier* recursion_depth_modifier);

  // Resolve an ASTQueryExpression.
  //
  // <query_alias> is the table name used internally for the ResolvedColumns
  // produced as output of this query (for display only).
  //
  // <force_new_columns_for_projected_outputs> indicates whether or not each
  // projected output requires a new column, even if
  // <analyzer_options_.create_new_column_for_each_projected_output()> is not
  // set. Used for the input to a PIVOT clause to ensure that each projection
  // not referenced by the PIVOT clause is treated as a grouping column.
  //
  // This is similar to ResolveQuery, but with no support for order by or limit.
  absl::Status ResolveQueryExpression(
      const ASTQueryExpression* query_expr, const NameScope* scope,
      IdString query_alias, bool force_new_columns_for_projected_outputs,
      std::unique_ptr<const ResolvedScan>* output,
      std::shared_ptr<const NameList>* output_name_list,
      const Type* inferred_type_for_query = nullptr);

  absl::Status ResolveAliasedQueryExpression(
      const ASTAliasedQueryExpression* aliased_query, const NameScope* scope,
      std::unique_ptr<const ResolvedScan>* output,
      std::shared_ptr<const NameList>* output_name_list,
      const Type* inferred_type_for_query);

  // If `with_clause` is non-NULL, this resolves all WITH entries and returns
  // them. Otherwise, just returns an empty vector.
  absl::StatusOr<std::vector<std::unique_ptr<const ResolvedWithEntry>>>
  ResolveWithClauseIfPresent(const ASTWithClause* with_clause,
                             bool is_outer_query);

  // Called immediately after resolving the main body of a query. If the query
  // contained a WITH clause, removes the WITH entries from named_subquery_map_
  // and wraps the query scan in a ResolvedWithScan node. Ownership of elements
  // in <with_entries> is transferred to the new ResolvedWithScan node, which
  // replaces <*output>.
  absl::Status FinishResolveWithClauseIfPresent(
      const ASTQuery* query,
      std::vector<std::unique_ptr<const ResolvedWithEntry>> with_entries,
      std::unique_ptr<const ResolvedScan>* output);

  // Resolve an ASTSelect.  Resolves everything within the scope of the related
  // query block, including the FROM, WHERE, GROUP BY, HAVING, and ORDER BY
  // clauses.  The ORDER BY is passed in separately because it binds outside
  // the SELECT in the parser, but since the ORDER BY can reference columns
  // from the FROM clause scope, the ORDER BY clause also resolves in
  // ResolvedSelect().
  //
  // <force_new_columns_for_projected_outputs> indicates whether or not each
  // projected output requires a new column, even if
  // <analyzer_options_.create_new_column_for_each_projected_output()> is not
  // set. Used for the input to a PIVOT clause to ensure that each projection
  // not referenced by the PIVOT clause is treated as a grouping column.
  //
  // <query_alias> is the table name used internally for the ResolvedColumns
  // produced as output of this select query block (for display only).
  absl::Status ResolveSelect(const ASTSelect* select,
                             const ASTOrderBy* order_by,
                             const ASTLimitOffset* limit_offset,
                             const NameScope* external_scope,
                             IdString query_alias,
                             bool force_new_columns_for_projected_outputs,
                             const Type* inferred_type_for_query,
                             std::unique_ptr<const ResolvedScan>* output,
                             std::shared_ptr<const NameList>* output_name_list);

  absl::Status ResolveSelectAfterFrom(
      const ASTSelect* select, const ASTOrderBy* order_by,
      const ASTLimitOffset* limit_offset, const NameScope* external_scope,
      IdString query_alias, SelectForm select_form,
      WithModifierMode with_modifier_mode,
      bool force_new_columns_for_projected_outputs,
      const Type* inferred_type_for_query,
      std::unique_ptr<const ResolvedScan>* scan,
      const std::shared_ptr<const NameList>& from_clause_name_list,
      std::shared_ptr<const NameList>* output_name_list);

  // Check that `select` has no child nodes present other than those
  // in `allowed_children`.
  // `node_context` is a name for the error message.
  absl::Status CheckForUnwantedSelectClauseChildNodes(
      const ASTSelect* select,
      absl::flat_hash_set<const ASTNode*> allowed_children,
      const char* node_context);

  // Resolves TableDataSource to a ResolvedScan for copy or clone operation.
  absl::Status ResolveDataSourceForCopyOrClone(
      const ASTTableDataSource* data_source,
      std::unique_ptr<const ResolvedScan>* output);

  // Resolves TableDataSource to a ResolvedScan for replica source.
  absl::Status ResolveReplicaSource(
      const ASTPathExpression* data_source,
      std::unique_ptr<const ResolvedScan>* output);

  // Resolve select list in TRANSFORM clause for model creation.
  absl::Status ResolveModelTransformSelectList(
      const NameScope* input_scope, const ASTSelectList* select_list,
      const std::shared_ptr<const NameList>& input_cols_name_list,
      std::vector<std::unique_ptr<const ResolvedComputedColumn>>*
          transform_list,
      std::vector<std::unique_ptr<const ResolvedOutputColumn>>*
          transform_output_column_list,
      std::vector<std::unique_ptr<const ResolvedAnalyticFunctionGroup>>*
          transform_analytic_function_group_list);

  // Resolve aliased query list for CREATE MODEL statement.
  absl::Status ResolveCreateModelAliasedQueryList(
      const ASTAliasedQueryList* aliased_query_list,
      std::vector<std::unique_ptr<const ResolvedCreateModelAliasedQuery>>*
          resolved_aliased_query_list);

  // Helper function to add grantee to grantee expression list.
  absl::Status AddGranteeToExpressionList(
      const ASTExpression* grantee,
      std::vector<std::unique_ptr<const ResolvedExpr>>* grantee_expr_list);

  // Helper function to add grantee to deprecated grantee list.
  absl::Status AddGranteeToList(const ASTExpression* grantee,
                                std::vector<std::string>* grantee_list);

  // Resolves the grantee list, which only contains string literals and
  // parameters (given the parser rules).  The <ast_grantee_list> may be
  // nullptr for ALTER ROW POLICY statements.  Only one of <grantee_list> or
  // <grantee_expr_list> will be populated, depending on whether the
  // FEATURE_PARAMETERS_IN_GRANTEE_LIST is enabled.
  // TODO: Enable this feature for all customers, and remove the
  // <grantee_list> from this function call.
  absl::Status ResolveGranteeList(
      const ASTGranteeList* ast_grantee_list,
      std::vector<std::string>* grantee_list,
      std::vector<std::unique_ptr<const ResolvedExpr>>* grantee_expr_list);

  absl::Status ResolveExecuteImmediateStatement(
      const ASTExecuteImmediateStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  absl::Status ResolveSystemVariableAssignment(
      const ASTSystemVariableAssignment* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  static absl::Status CreateSelectNamelists(
      const SelectColumnState* select_column_state,
      NameList* post_group_by_alias_name_list,
      NameList* pre_group_by_alias_name_list,
      IdStringHashMapCase<NameTarget>* error_name_targets,
      std::set<IdString, IdStringCaseLess>* select_column_aliases);

  // Assign a pre-GROUP BY ResolvedColumn to each SelectColumnState that could
  // be referenced in HAVING or ORDER BY inside an aggregate function.  For
  // example:
  //   SELECT t1.a + 1 as foo
  //   FROM t1
  //   GROUP BY 1
  //   HAVING sum(foo) > 5;
  // Resolving 'foo' in the HAVING clause requires the pre-GROUP BY version
  // of 't1.a + 1'.
  //
  // This includes SELECT columns that do not themselves have aggregation, and
  // that have non-internal aliases.  The assigned ResolvedColumn represents a
  // pre-GROUP BY version of the column/expression.  Additionally, if the
  // SelectColumnState expression needs precomputation (i.e., it is a path
  // expression), then add a new ResolvedComputedColumn for it in
  // <select_list_columns_to_compute_before_aggregation>.
  // Added ResolvedComputedColumns will be precomputed by a ProjectScan
  // before the related AggregateScan.
  absl::Status AnalyzeSelectColumnsToPrecomputeBeforeAggregation(
      QueryResolutionInfo* query_resolution_info);

  // Resolves a scalar WHERE expression and coerces it to a BOOL.
  absl::Status ResolveWhere(const ASTWhereClause* ast_where,
                            const NameScope* name_scope,
                            const char* clause_name,
                            QueryResolutionInfo* query_resolution_info,
                            std::unique_ptr<const ResolvedExpr>* resolved_expr);

  // Resolve the WHERE clause expression (which must be non-NULL) and
  // generate a ResolvedFilterScan for it.  The <current_scan> will be
  // wrapped with this new ResolvedFilterScan.
  absl::Status ResolveWhereClauseAndCreateScan(
      const ASTWhereClause* where_clause, const NameScope* from_scan_scope,
      std::unique_ptr<const ResolvedScan>* current_scan);

  // Check the ExprResolutionInfo for an expression resolved in this query,
  // checking for any features required or not allowed in this SelectForm.
  absl::Status CheckExprResolutionInfoForQuery(
      const ASTNode* ast_location, QueryResolutionInfo* query_resolution_info,
      const ExprResolutionInfo& expr_resolution_info);

  // Performs first pass analysis on the SELECT list expressions against the
  // FROM clause. This pass includes star and dot-star expansion, but defers
  // resolution of expressions that use GROUP ROWS or GROUP BY modifiers (see
  // comments for `ResolveDeferredFirstPassSelectListExprs`).
  // Populates the SelectColumnStateList in <query_resolution_info>, and also
  // records information about referenced and resolved aggregation and analytic
  // functions.
  absl::Status ResolveSelectListExprsFirstPass(
      const ASTSelectList* select_list, const NameScope* from_scan_scope,
      const std::shared_ptr<const NameList>& from_clause_name_list,
      QueryResolutionInfo* query_resolution_info, IdString query_alias,
      std::unique_ptr<NameScope>& updated_name_scope,
      std::shared_ptr<NameList>& updated_name_list,
      const Type* inferred_type_for_query = nullptr);

  // Resolve SELECT list expressions that were excluded from first pass
  // resolution. The only expressions currently excluded from first pass
  // resolution are those that use GROUP ROWS or GROUP BY modifiers outside of
  // an expression subquery. A placeholder `SelectColumnState` object is created
  // for these expressions during first pass resolution.
  absl::Status ResolveDeferredFirstPassSelectListExprs(
      const NameScope* name_scope,
      const std::shared_ptr<const NameList>& name_list,
      QueryResolutionInfo* query_resolution_info, IdString query_alias,
      const Type* inferred_type_for_query = nullptr);

  // Performs first pass analysis on a SELECT list expression. Resulting
  // `SelectColumnState` objects are written to the `SelectColumnStateList` of
  // `query_resolution_info`, starting at `select_column_state_list_write_idx`.
  // If `select_column_state_list_write_idx` points at an existing
  // `SelectColumnState`, it is overwritten. Overwrites are not permitted when
  // resolving star and dot-star expansion. Overwrites are only performed when
  // resolving SELECT list expressions that were excluded from first pass
  // resolution (i.e. `ResolveDeferredFirstPassSelectListExprs`).
  // Precondition: `select_column_state_list_write_idx` <=
  // query_resolution_info->select_column_state_list()->Size()
  absl::Status ResolveSelectColumnFirstPass(
      const ASTSelectColumn* ast_select_column,
      const NameScope* from_scan_scope,
      const std::shared_ptr<const NameList>& from_clause_name_list,
      std::unique_ptr<NameScope>& current_name_scope,
      IdString select_column_alias, int select_column_state_list_write_idx,
      QueryResolutionInfo* query_resolution_info,
      const Type* inferred_type = nullptr);

  // Finishes resolving the SelectColumnStateList after first pass
  // analysis.  For each <select_column_state_list> entry, a ResolvedColumn
  // is produced as its output.  Columns that need computing are added
  // to the appropriate list.  Must only be called if there is no grouping
  // or SELECT list aggregation or analytic function present.
  //
  // <force_new_columns_for_projected_outputs> indicates whether or not each
  // projected output requires a new column, even if
  // <analyzer_options_.create_new_column_for_each_projected_output()> is not
  // set. Used for the input to a PIVOT clause to ensure that each projection
  // not referenced by the PIVOT clause is treated as a grouping column.
  absl::Status FinalizeSelectColumnStateList(
      const ASTSelectList* ast_select_list, IdString query_alias,
      bool force_new_columns_for_projected_outputs,
      QueryResolutionInfo* query_resolution_info,
      SelectColumnStateList* select_column_state_list);

  // Performs second pass analysis on the SELECT list expressions, re-resolving
  // expressions against GROUP BY scope if necessary.  After this pass, each
  // SelectColumnState has an initialized output ResolvedColumn.
  absl::Status ResolveSelectListExprsSecondPass(
      IdString query_alias, const NameScope* group_by_scope,
      std::shared_ptr<NameList>* final_project_name_list,
      QueryResolutionInfo* query_resolution_info);

  // Performs second pass analysis on a SELECT list expression, as indicated
  // by <select_column_state>.
  absl::Status ResolveSelectColumnSecondPass(
      IdString query_alias, const NameScope* group_by_scope,
      SelectColumnState* select_column_state,
      std::shared_ptr<NameList>* final_project_name_list,
      QueryResolutionInfo* query_resolution_info);

  // Resolves a dot-star source expression in the 2nd pass.
  absl::Status ResolveDotStarSourceExpressionSecondPass(
      DotStarSourceExprInfo* dot_star_source_expr_info,
      QueryResolutionInfo* query_resolution_info,
      const NameScope* group_by_scope);

  // Runs as part of `ResolveSelectColumnSecondPass`. Resolves an expression on
  // a SELECT column when it needs re-resolution.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
  ResolveSelectExpressionSecondPass(
      const ASTExpression* ast_expr, IdString alias, IdString query_alias,
      const NameScope* group_by_scope,
      const ResolvedExpr* first_pass_resolved_expr,
      QueryResolutionInfo* query_resolution_info);

  // Resolve modifiers for StarWithModifiers or DotStarWithModifiers.
  // Stores the modifier mappings in <column_replacements>.
  // Exactly one of <name_list_for_star> or <type_for_star> must be non-NULL,
  // and is used to check that excluded names actually exist.
  // <scope> is the scope for resolving full expressions in REPLACE.
  absl::Status ResolveSelectStarModifiers(
      const ASTSelectColumn* ast_select_column,
      const ASTStarModifiers* modifiers, const NameList* name_list_for_star,
      const Type* type_for_star, const NameScope* scope,
      QueryResolutionInfo* query_resolution_info,
      ColumnReplacements* column_replacements);

  // Resolves a Star expression in the SELECT list, producing multiple
  // columns and adding them to SelectColumnStateList in
  // <query_resolution_info>.
  // <ast_select_expr> can be ASTStar or ASTStarWithModifiers.
  absl::Status ResolveSelectStar(
      const ASTSelectColumn* ast_select_column,
      const std::shared_ptr<const NameList>& from_clause_name_list,
      const NameScope* from_scan_scope,
      QueryResolutionInfo* query_resolution_info);

  // Resolves a DotStar expression in the SELECT list, producing multiple
  // columns and adding them to SelectColumnStateList in
  // <query_resolution_info>.
  // If the lhs is a range variable, adds all the columns visible from that
  // range variable.
  // If the lhs is a struct/proto, adds one column for each field.
  // If the lhs is an expression rather than a ColumnRef, a ComputedColumn will
  // be added to <precompute_columns> to materialize the struct/proto before
  // extracting its fields.
  // <ast_dotstar> can be ASTStar or ASTStarWithModifiers.
  absl::Status ResolveSelectDotStar(const ASTSelectColumn* ast_select_column,
                                    const NameScope* from_scan_scope,
                                    QueryResolutionInfo* query_resolution_info);

  // Adds all fields of the column referenced by `src_column_ref` to the SELECT
  // list on `query_resolution_info`, like we do for 'SELECT column.*'.
  // Copies `src_column_ref`, without taking ownership.
  // Updates the SelectColumnState's `expr_findings` with
  // `src_column_expr_findings`. If the column has no fields, then if
  // `column_alias_if_no_fields` is non-empty, emits the column itself, and
  // otherwise returns an error.
  absl::Status AddColumnFieldsToSelectList(
      const ASTSelectColumn* ast_select_column,
      const ResolvedColumnRef* src_column_ref,
      std::unique_ptr<const ResolvedExpr> resolved_src_expr,
      ExprFindings src_column_expr_findings,
      std::vector<SelectColumnState*> columns_referenced_laterally,
      IdString column_alias_if_no_fields,
      const IdStringSetCase* excluded_field_names,
      QueryResolutionInfo* query_resolution_info,
      ColumnReplacements* column_replacements = nullptr);

  // Add all columns in <name_list> into the SELECT list on
  // `query_resolution_info`, optionally excluding value table fields that have
  // been marked as excluded.
  absl::Status AddNameListToSelectList(
      const ASTSelectColumn* ast_select_column,
      const std::shared_ptr<const NameList>& name_list,
      const CorrelatedColumnsSetList& correlated_columns_set_list,
      bool ignore_excluded_value_table_fields,
      QueryResolutionInfo* query_resolution_info,
      ColumnReplacements* column_replacements = nullptr);

  // If <resolved_expr> is a resolved path expression (zero or more
  // RESOLVED_GET_*_FIELD expressions over a ResolvedColumnRef) then inserts
  // a new entry into 'query_resolution_info->group_by_valid_field_info_map'
  // with a source ResolvedColumn that is the <resolved_expr> source
  // ResolvedColumnRef column, the name path derived from the <resolved_expr>
  // get_*_field expressions, along with the <target_column>.
  // If <resolved_expr> is not a resolved path expression then has no
  // effect.
  absl::Status CollectResolvedPathExpressionInfoIfRelevant(
      QueryResolutionInfo* query_resolution_info,
      const ResolvedExpr* resolved_expr, ResolvedColumn target_column) const;

  // Resolve the 'SELECT DISTINCT ...' part of the query.
  // Creates a new aggregate scan in <current_scan> (that wraps the input
  // <current_scan>) having GROUP BY on the columns visible in the input scan.
  // Updates <query_resolution_info> with the mapping between pre-distinct and
  // post-distinct versions of columns.
  absl::Status ResolveSelectDistinct(
      const ASTSelect* select, SelectColumnStateList* select_column_state_list,
      const NameList* input_name_list,
      std::unique_ptr<const ResolvedScan>* current_scan,
      QueryResolutionInfo* query_resolution_info,
      std::shared_ptr<const NameList>* output_name_list);

  // Resolve the 'SELECT AS {STRUCT | TypeName}' part of a query.
  // Creates a new output_scan that wraps input_scan_in and converts it to
  // the requested type.
  absl::Status ResolveSelectAs(
      const ASTSelectAs* select_as,
      const SelectColumnStateList& select_column_state_list,
      std::unique_ptr<const ResolvedScan> input_scan_in,
      const NameList* input_name_list,
      std::unique_ptr<const ResolvedScan>* output_scan,
      std::shared_ptr<const NameList>* output_name_list);

  // Add ResolvedProjectScan(s) wrapping `current_scan` and computing
  // `computed_columns` if `computed_columns` is non-empty.
  // `current_scan` will be updated to point at the wrapper scan.
  // In general, a single ResolvedProjectScan is added to wrap `current_scan`;
  // an additional 2nd scan may be added if any columns in `computed_columns`
  // depend on other columns in `computed_columns` for their computation result.
  // If the dependency tree depth between `computed_columns` exceeds 2, an
  // internal error is returned.
  static absl::Status MaybeAddProjectForComputedColumns(
      std::vector<std::unique_ptr<const ResolvedComputedColumn>>
          computed_columns,
      std::unique_ptr<const ResolvedScan>* current_scan);

  // Adds each expression in `successive_columns_to_compute` on its own project
  // scan, in order.
  // Takes ownership of `successive_columns_to_compute`.
  static absl::Status WrapInSuccessiveProjectScans(
      std::vector<std::unique_ptr<const ResolvedComputedColumn>>
          successive_columns_to_compute,
      std::unique_ptr<const ResolvedScan>* current_scan);

  // Add a ResolvedProjectScan wrapping `current_scan` but keeping only the
  // visible columns from `name_list` in its column_list.
  // No-op if there are no columns to prune away.
  absl::Status MaybeAddProjectForColumnPruning(
      const NameList& name_list,
      std::unique_ptr<const ResolvedScan>* current_scan);

  // Add all remaining scans for this SELECT query on top of <current_scan>,
  // which already includes the FROM clause scan and WHERE clause scan (if
  // present).  The remaining scans include any necessary scans for
  // grouping/aggregation, HAVING clause filtering, analytic functions,
  // QUALIFY clause filtering, DISTINCT, ORDER BY, LIMIT/OFFSET,
  // a final ProjectScan for the SELECT list output, and HINTs.
  absl::Status AddRemainingScansForSelect(
      const ASTSelect* select, const ASTOrderBy* order_by,
      const ASTLimitOffset* limit_offset,
      const NameScope* having_and_order_by_scope,
      std::unique_ptr<const ResolvedExpr> resolved_where_expr,
      std::unique_ptr<const ResolvedExpr> resolved_having_expr,
      std::unique_ptr<const ResolvedExpr> resolved_qualify_expr,
      QueryResolutionInfo* query_resolution_info,
      std::shared_ptr<const NameList>* output_name_list,
      std::unique_ptr<const ResolvedScan>* current_scan);

  // Add a ResolvedAggregateScan wrapping <current_scan> and producing the
  // aggregate expression columns.  Must only be called if an aggregate scan
  // is necessary.  <is_for_select_distinct> indicates this AggregateScan is
  // being added for SELECT DISTINCT, so shouldn't inherit hints from the query.
  absl::Status AddAggregateScan(
      const ASTSelect* select, bool is_for_select_distinct,
      QueryResolutionInfo* query_resolution_info,
      std::unique_ptr<const ResolvedScan>* current_scan);

  // Add a ResolvedAnonymizedAggregateScan or
  // ResolvedDifferentialPrivacyAggregateScan - depending on query - wrapping
  // <current_scan> and producing the anonymization function call / expression
  // columns. Must only be called if FEATURE_ANONYMIZATION or
  // FEATURE_DIFFERENTIAL_PRIVACY is enabled and the column list contains
  // anonymization function calls and/or group by columns.
  absl::Status AddAnonymizedAggregateScan(
      const ASTSelect* select, QueryResolutionInfo* query_resolution_info,
      std::unique_ptr<const ResolvedScan>* current_scan);

  // Add a AddAggregationThresholdAggregateScan wrapping <input_scan> and
  // producing the function call / expression columns. Must only be called if
  // FEATURE_AGGREGATION_THRESHOLD is enabled and the column list contains
  // aggregate function calls and/or group by columns.
  absl::StatusOr<std::unique_ptr<const ResolvedScan>>
  AddAggregationThresholdAggregateScan(
      const ASTSelect* select, QueryResolutionInfo* query_resolution_info,
      std::unique_ptr<const ResolvedScan> input_scan);

  // Add a ResolvedAnalyticScan wrapping <current_scan> and producing the
  // analytic function columns.  A ProjectScan will be inserted between the
  // input <current_scan> and ResolvedAnalyticScan if needed.
  // <current_scan> will be updated to point at the wrapper
  // ResolvedAnalyticScan.
  absl::Status AddAnalyticScan(
      QueryResolutionInfo* query_resolution_info,
      std::unique_ptr<const ResolvedScan>* current_scan);

  // Create a new scan wrapping <input_scan_in> converting it to a struct type.
  // If <named_struct_type> is NULL, convert to a new anonymous struct type.
  // If <named_struct_type> is non-NULL, convert to that struct type.
  absl::Status ConvertScanToStruct(
      const ASTNode* ast_location,
      const StructType* named_struct_type,  // May be NULL
      std::unique_ptr<const ResolvedScan> input_scan,
      const NameList* input_name_list,
      std::unique_ptr<const ResolvedScan>* output_scan,
      std::shared_ptr<const NameList>* output_name_list);

  // Creates a STRUCT out of the columns present in <name_list> as its fields.
  absl::Status CreateStructFromNameList(
      const NameList* name_list,
      const CorrelatedColumnsSetList& correlated_column_sets,
      std::unique_ptr<ResolvedComputedColumn>* computed_column);

  class AliasOrASTPathExpression {
   public:
    enum Kind { ALIAS, AST_PATH_EXPRESSION };

    explicit AliasOrASTPathExpression(IdString alias)
        : alias_or_ast_path_expr_(alias) {}

    explicit AliasOrASTPathExpression(const ASTPathExpression* ast_path_expr)
        : alias_or_ast_path_expr_(ast_path_expr) {}

    AliasOrASTPathExpression(const AliasOrASTPathExpression&) = delete;
    AliasOrASTPathExpression& operator=(const AliasOrASTPathExpression&) =
        delete;

    Kind kind() const {
      if (std::holds_alternative<IdString>(alias_or_ast_path_expr_)) {
        return ALIAS;
      }
      return AST_PATH_EXPRESSION;
    }

    // Requires kind() == ALIAS.
    IdString alias() const {
      return std::get<IdString>(alias_or_ast_path_expr_);
    }

    // Requires kind() == AST_PATH_EXPRESSION.
    const ASTPathExpression* ast_path_expr() const {
      return std::get<const ASTPathExpression*>(alias_or_ast_path_expr_);
    }

   private:
    const std::variant<IdString, const ASTPathExpression*>
        alias_or_ast_path_expr_;
  };

  struct ResolvedBuildProtoArg {
    ResolvedBuildProtoArg(const ASTNode* ast_location_in,
                          std::unique_ptr<const ResolvedExpr> expr_in,
                          const Type* leaf_field_type_in,
                          const std::vector<const google::protobuf::FieldDescriptor*>
                              field_descriptor_path_in)
        : ast_location(ast_location_in),
          expr(std::move(expr_in)),
          leaf_field_type(leaf_field_type_in),
          field_descriptor_path(field_descriptor_path_in) {}
    const ASTNode* ast_location;
    std::unique_ptr<const ResolvedExpr> expr;
    const Type* leaf_field_type;
    std::vector<const google::protobuf::FieldDescriptor*> field_descriptor_path;
  };

  // Create a ResolvedMakeProto from a type and a vector of arguments.
  // <input_scan> is used only to look up whether some argument expressions
  // may be literals coming from ProjectScans.
  // <argument_description> and <query_description> are the words used to
  // describe those entities in error messages.
  absl::Status ResolveBuildProto(const ASTNode* ast_type_location,
                                 const ProtoType* proto_type,
                                 const ResolvedScan* input_scan,
                                 absl::string_view argument_description,
                                 absl::string_view query_description,
                                 std::vector<ResolvedBuildProtoArg>* arguments,
                                 std::unique_ptr<const ResolvedExpr>* output);

  // Returns the proto field descriptor corresponding to an
  // AliasOrASTPathExpression.
  absl::StatusOr<const google::protobuf::FieldDescriptor*> FindFieldDescriptor(
      const google::protobuf::Descriptor* descriptor,
      const AliasOrASTPathExpression& alias_or_ast_path_expr,
      const ASTNode* ast_location, int field_index,
      absl::string_view argument_description);

  // Returns the GoogleSQL type corresponding to a FieldDescriptor.
  // Validates that the type is supported for the language.
  absl::StatusOr<const Type*> FindProtoFieldType(
      const google::protobuf::FieldDescriptor* field_descriptor,
      const ASTNode* ast_location,
      absl::Span<const std::string> catalog_name_path);

  // Returns the FieldDescriptor corresponding to <ast_path_expr>. First tries
  // to look up with respect to <descriptor>, and failing that extracts a type
  // name from <ast_path_expr>, looks up the type name, and then looks for the
  // extension field name in that type.
  absl::StatusOr<const google::protobuf::FieldDescriptor*> FindExtensionFieldDescriptor(
      const ASTPathExpression* ast_path_expr,
      const google::protobuf::Descriptor* descriptor);

  // Returns the FieldDescriptor corresponding to a top level field with the
  // given <name>. The field is looked up  with respect to <descriptor>. Returns
  // nullptr if no matching field was found.
  absl::StatusOr<const google::protobuf::FieldDescriptor*> FindFieldDescriptor(
      const ASTNode* ast_name_location, const google::protobuf::Descriptor* descriptor,
      absl::string_view name);

  // Returns a vector of FieldDescriptors that correspond to each of the fields
  // in the path <path_vector>. The first FieldDescriptor in the returned
  // vector is looked up with respect to <root_type>.
  // <path_vector> must only contain nested field extractions.
  absl::Status FindFieldDescriptors(
      absl::Span<const ASTIdentifier* const> path_vector,
      const ProtoType* root_type,
      std::vector<const google::protobuf::FieldDescriptor*>* field_descriptors);

  // The output of FindFieldsFromPathExpression.
  struct FindFieldsOutput {
    struct StructFieldInfo {
      explicit StructFieldInfo(int field_index_in,
                               const StructType::StructField* field_in)
          : field_index(field_index_in), field(field_in) {}

      int field_index;
      const StructType::StructField* field;
    };
    std::vector<StructFieldInfo> struct_path;
    std::vector<const google::protobuf::FieldDescriptor*> field_descriptor_path;
  };

  void AppendFindFieldsOutput(const FindFieldsOutput& to_append,
                              FindFieldsOutput* output) {
    output->struct_path.insert(output->struct_path.end(),
                               to_append.struct_path.begin(),
                               to_append.struct_path.end());
    output->field_descriptor_path.insert(
        output->field_descriptor_path.end(),
        to_append.field_descriptor_path.begin(),
        to_append.field_descriptor_path.end());
  }

  // Parses <generalized_path>, filling in <struct_path> and/or
  // <field_descriptors> in FindFieldsOutput (returned as a value) as
  // appropriate, with the struct and proto fields that correspond to each of
  // the fields in the path. The first field is looked up with respect to
  // <root_type>. Both <struct_path> and <field_descriptors> may be populated if
  // <generalized_path> contains accesses to fields of a proto nested within a
  // struct. In this case, when parsing the output vectors, the first part of
  // <generalized_path> corresponds to <struct_path> and the last part to
  // <field_descriptors>. If <can_traverse_array_fields> is true, the
  // <generalized_path> can traverse array or repeated fields. <function_name>
  // is for generating error messages.
  absl::StatusOr<FindFieldsOutput> FindFieldsFromPathExpression(
      absl::string_view function_name,
      const ASTGeneralizedPathExpression* generalized_path,
      const Type* root_type, bool can_traverse_array_fields);

  // Fills in a vector of FindFieldsOutput::StructFieldInfo which contains
  // StructFields and their indexes corresponding to the fields in the path
  // represented by <path_vector>. The first field in the returned vector is
  // looked up with respect to <root_struct>. If a field of proto type is
  // encountered in the path, it will be inserted into <struct_path> and the
  // function will return without examining any further fields in the path.
  absl::Status FindStructFieldPrefix(
      absl::Span<const ASTIdentifier* const> path_vector,
      const StructType* root_struct,
      std::vector<FindFieldsOutput::StructFieldInfo>* struct_path);

  // Looks up a proto message type name first in <descriptor_pool> and then in
  // <catalog>. Returns NULL if the type name is not found. If
  // 'return_error_for_non_message' is false, then also returns NULL if the type
  // name is found in <catalog> but is not a proto.
  absl::StatusOr<const google::protobuf::Descriptor*> FindMessageTypeForExtension(
      const ASTPathExpression* ast_path_expr,
      absl::Span<const std::string> type_name_path,
      const google::protobuf::DescriptorPool* descriptor_pool,
      bool return_error_for_non_message);

  // Create a new scan wrapping <input_scan_in> converting it to <proto_type>.
  absl::Status ConvertScanToProto(
      const ASTNode* ast_type_location,
      const SelectColumnStateList& select_column_state_list,
      const ProtoType* proto_type,
      std::unique_ptr<const ResolvedScan> input_scan,
      const NameList* input_name_list,
      std::unique_ptr<const ResolvedScan>* output_scan,
      std::shared_ptr<const NameList>* output_name_list);

  absl::Status ResolveSetOperation(
      const ASTSetOperation* set_operation, const NameScope* scope,
      const Type* inferred_type_for_query,
      std::unique_ptr<const ResolvedScan>* output,
      std::shared_ptr<const NameList>* output_name_list);

  // Visitor to walk the resolver tree of a recursive UNION and verify that
  // recursive references appear only in a supported context.
  class ValidateRecursiveTermVisitor : public ResolvedASTVisitor {
   public:
    ValidateRecursiveTermVisitor(const Resolver* resolver,
                                 IdString recursive_query_name);

   private:
    absl::Status DefaultVisit(const ResolvedNode* node) override;

    absl::Status VisitResolvedAggregateScan(
        const ResolvedAggregateScan* node) override;

    absl::Status VisitResolvedLimitOffsetScan(
        const ResolvedLimitOffsetScan* node) override;

    absl::Status VisitResolvedAnalyticScan(
        const ResolvedAnalyticScan* node) override;

    absl::Status VisitResolvedJoinScan(const ResolvedJoinScan* node) override;

    absl::Status VisitResolvedSubqueryExpr(
        const ResolvedSubqueryExpr* node) override;

    absl::Status VisitResolvedRecursiveRefScan(
        const ResolvedRecursiveRefScan* node) override;

    absl::Status VisitResolvedRecursiveScan(
        const ResolvedRecursiveScan* node) override;

    absl::Status VisitResolvedSampleScan(
        const ResolvedSampleScan* node) override;

    absl::Status VisitResolvedSetOperationScan(
        const ResolvedSetOperationScan* node) override;

    absl::Status VisitResolvedOrderByScan(
        const ResolvedOrderByScan* node) override;

    absl::Status VisitResolvedFunctionArgument(
        const ResolvedFunctionArgument* node) override;

    absl::Status VisitResolvedWithEntry(const ResolvedWithEntry* node) override;

    // Returns either the address of right_operand_of_left_join_count_,
    // left_operand_of_right_join_count_, or full_join_operand_count_,
    // depending on the arguments, or nullptr if none of the above apply.
    //
    // Used to increment or decrement the appropriate join count field when
    // starting and finishing the processing of an operand.
    int* GetJoinCountField(ResolvedJoinScan::JoinType join_type,
                           bool left_operand);

    // Adjusts the values of the appropriate join count field by <offset>,
    // in response to entering or exiting a join operand.
    void MaybeAdjustJoinCount(ResolvedJoinScan::JoinType join_type,
                              bool left_operand, int offset);

    const Resolver* resolver_;

    // Name of the recursive table currently being resolved. Used to distinguish
    // between recursive references to that table itself vs. recursive
    // references to some outer table. The latter results in an error, as it
    // is not supported.
    IdString recursive_query_name_;

    // Number of nested WITH entries we are inside of (relative to the recursive
    // term of the recursive query being validated). It is illegal to reference
    // a recursive table through any inner WITH entry.
    int nested_with_entry_count_ = 0;

    // Number of aggregate scans we are inside of.
    int aggregate_scan_count_ = 0;

    // Number of analytic scans we are inside of.
    int analytic_scan_count_ = 0;

    // Number of limit/offset scans we are inside of.
    int limit_offset_scan_count_ = 0;

    // Number of order by scans we are inside of.
    int order_by_scan_count_ = 0;

    // Number of sample sacns we are inside of.
    int sample_scan_count_ = 0;

    // Number of subquery expressions we are inside of.
    int subquery_expr_count_ = 0;

    // Number of times we are inside the right operand of a left join.
    int right_operand_of_left_join_count_ = 0;

    // Number of times we are inside the left operand of a right join.
    int left_operand_of_right_join_count_ = 0;

    // Number of times we are inside any operand of a full join.
    int full_join_operand_count_ = 0;

    // Number of TVF arguments we are inside of.
    int tvf_argument_count_ = 0;

    // Number of EXCEPT clauses we are inside the rhs of.
    int except_clause_count_ = 0;

    // Number of times we are inside of any operand of INTERSECT/UNION/EXCEPT
    // with the DISTINCT modifier.
    int setop_distinct_count_ = 0;

    // True if we've already encountered a recursive reference to the current
    // query. Multiple recursive references to the same query are disallowed.
    bool seen_recursive_reference_ = false;
  };

  // Helper class used to implement ResolveSetOperation().
  class SetOperationResolver : public SetOperationResolverBase {
   public:
    SetOperationResolver(const ASTSetOperation* set_operation,
                         Resolver* resolver);

    SetOperationResolver(const ASTPipeSetOperation* pipe_set_operation,
                         const std::shared_ptr<const NameList>& lhs_name_list,
                         std::unique_ptr<const ResolvedScan>* lhs_scan,
                         Resolver* resolver);

    SetOperationResolver(const ASTPipeRecursiveUnion* pipe_recursive_union,
                         const std::shared_ptr<const NameList>& lhs_name_list,
                         std::unique_ptr<const ResolvedScan>* lhs_scan,
                         Resolver* resolver);

    // Resolves the ASTSetOperation passed to the constructor, returning the
    // ResolvedScan and NameList in the given output parameters.
    // <scope> represents the name scope used to resolve each of the set items.
    absl::Status Resolve(const NameScope* scope,
                         const Type* inferred_type_for_query,
                         std::unique_ptr<const ResolvedScan>* output,
                         std::shared_ptr<const NameList>* output_name_list);

    // Resolves the UNION representing a recursive query.
    // <scope>: the NameScope used to resolve the union's components.
    // <recursive_alias>: the name of the alias used in the query to
    //   refer to the recursive table reference.
    // <recursive_query_unique_name>: A unique name to associate with the
    //   recursive query in the resolved tree.
    // <output>: Receives a scan containing the result.
    // <output_name_list>: Receives a NameList containing the columns of the
    //   result.
    absl::Status ResolveRecursive(
        const NameScope* scope, const std::vector<IdString>& recursive_alias,
        const IdString& recursive_query_unique_name,
        std::unique_ptr<const ResolvedScan>* output,
        std::shared_ptr<const NameList>* output_name_list);

    // Handles recursion depth modifier for recursive scan.
    // - <ast_location>: the corresponding AST of the depth modifier;
    // - <recursive_alias>: the name of the alias used in the query to
    //   refer to the recursive table reference.
    // - <depth_modifier>: an optional recursion depth modifier;
    // - <output>: Receives a scan containing the result.
    // - <output_name_list>: Receives a NameList containing the columns of the
    //   result.
    //
    // Note that we do post-process of recursion depth modifier because
    // the intermediate resolution result will also be used to resolve the
    // recursive reference to which recursion depth column is not visible.
    absl::Status AddDepthColumnToRecursiveScan(
        const ASTNode* ast_location,
        const std::vector<IdString>& recursive_alias,
        std::unique_ptr<const ResolvedRecursionDepthModifier> depth_modifier,
        std::unique_ptr<const ResolvedScan>* output,
        std::shared_ptr<const NameList>* output_name_list);

   private:
    using InputSetOperation = std::variant<const ASTSetOperation* const,
                                           const ASTPipeSetOperation* const,
                                           const ASTPipeRecursiveUnion* const>;
    // The kind of the input set operation.
    enum class InputKind {
      // The input is a standard set operation, e.g. <query> UNION ALL <query>.
      kStandard,
      // The input is a pipe set operation, e.g. <query> |> INTERSECT DISTINCT
      // (<query>).
      kPipe,
      // The input is a pipe recursive union, e.g. <query> |> RECURSIVE UNION
      // ALL (<query>).
      kPipeRecursive,
    };

    // Represents the result of resolving one input to the set operation.
    struct ResolvedInputResult {
      std::unique_ptr<ResolvedSetOperationItem> node;
      std::shared_ptr<const NameList> name_list;
      const ASTNode* ast_location;
      // The (0-based) index of the input queries. For pipe syntax, the pipe
      // input table has index 0, and the rhs input indices start from 1.
      //
      // For pipe recursive queries, the recursive term index is always 1
      // because there is exactly one non-recursive input, i.e. the pipe input
      // table. For recursive queries using the standard syntax, the recursive
      // term index is >= 1.
      int query_idx = -1;
    };

    struct NonRecursiveTerm {
      // The resolved inputs for the non-recursive term.
      std::vector<ResolvedInputResult> resolved_inputs;

      // The final column list for the recursive set operation.
      ResolvedColumnList final_column_list;

      // The NameList template to build the final NameList for the recursive
      // query. The final NameList inherits the template's column attributes,
      // like the value-table-ness and column explictness. See
      // `BuildFinalNameList()` for more details.
      std::shared_ptr<const NameList> name_list_template;
    };

    // Converts a span of ResolvedInputResult to a vector of IndexedColumnNames.
    static std::vector<IndexedColumnNames> ToIndexedColumnNamesList(
        absl::Span<const ResolvedInputResult> resolved_inputs);

    // Resolves the non-recursive term of the recursive set operation.
    //
    // `scope`: the name scope used to resolve the non-recursive term.
    // `op_type`: the set operation type of the recursive set operation.
    absl::StatusOr<NonRecursiveTerm> ResolveNonRecursiveTerm(
        const NameScope* scope,
        ResolvedSetOperationScan::SetOperationType op_type);

    // Resolves a single input into a ResolvedSetOperationItem.
    // `scope` = name scope for resolution
    // `ast_input_index` = child index within `ast_inputs()` of the query to
    // resolve.
    absl::StatusOr<ResolvedInputResult> ResolveInputQuery(
        const NameScope* scope, int ast_input_index,
        const Type* inferred_type_for_query) const;

    // Resolves the input subpipeline of the recursive term. Should only be
    // called if the input set operation is a pipe recursive union and its
    // input is a subpipeline instead of a subquery.
    //
    // `outer_scope`: the scope from *outside* the containing query, for
    //   correlated column references. It's not the scope for the containing
    //   query.
    // `alias`: the alias used in the query to refer to the
    //   recursive table reference. Could be user-provided or a generated name.
    // `recursive_query_unique_name`: a unique name to associate with the
    //   recursive query in the resolved tree.
    // `named_subquery`: the NamedSubquery corresponding to the recursive table.
    absl::StatusOr<ResolvedInputResult> ResolveRecursiveTermSubpipeline(
        const NameScope* outer_scope, IdString alias,
        IdString recursive_query_unique_name,
        const NamedSubquery& named_subquery);

    // Creates a recursive reference scan for the subpipeline input of the
    // recursive term and a NameList for it.
    //
    // `named_subquery`: the NamedSubquery corresponding to the recursive table.
    // `alias` will be added to `recursive_ref_scan_name_list` if it is not an
    // internal alias.
    absl::Status CreateRecursiveRefScanForSubpipeline(
        IdString alias, const NamedSubquery& named_subquery,
        std::unique_ptr<const ResolvedScan>* resolved_recursive_ref_scan,
        std::shared_ptr<const NameList>* recursive_ref_scan_name_list);

    // Builds a vector specifying the type of each column for each input scan.
    // After calling:
    //   GOOGLESQL_ASSIGN_OR_RETURN(column_type_lists,
    //       BuildColumnTypeListsByPosition(...));
    //
    // column_type_lists[column_idx][scan_idx] specifies the type for the given
    // column index/input index combination.
    //
    // This function should only be called if the column_match_mode is
    // BY_POSITION.
    absl::StatusOr<std::vector<std::vector<InputArgumentType>>>
    BuildColumnTypeListsByPosition(
        absl::Span<ResolvedInputResult> resolved_inputs) const;

    // Adds a cast to the `set_operation_item` of `resolved_input`
    // if necessary to convert each column to the respective final column
    // type in `column_list`.
    absl::Status CreateWrapperScanWithCastsForSetOperationItem(
        const ResolvedColumnList& column_list,
        ResolvedInputResult& resolved_input) const;

    // Builds the final NameList for the resolution of the set operation from
    // the given `name_list_template`. The properties of each NamedColumn in
    // `name_list_template` will be preserved; only the ResolvedColumns will be
    // replaced by the ones in `final_column_list`.
    absl::StatusOr<std::shared_ptr<const NameList>> BuildFinalNameList(
        const NameList& name_list_template,
        const ResolvedColumnList& final_column_list) const;

    // Validates that there is at most one hint and is at the first set
    // operation. Returns a sql error if the validation fails.
    absl::Status ValidateHint() const;

    // Validates the corresponding clause if it presents. Specifically, it
    // validates FULL, LEFT, and STRICT, if present, are used with CORRESPONDING
    // or CORRESPONDING BY.
    absl::Status ValidateCorresponding() const;

    // Checks that the usage of CORRESPONDING or BY NAME is valid for
    // recursive queries. Specifically, it validates:
    // - Either STRICT CORRESPONDING or BY NAME is used.
    absl::Status ValidateMatchByNameForRecursive() const;

    // Validates that all the set operations are identical. Returns a sql error
    // if the validation fails.
    absl::Status ValidateIdenticalSetOperator() const;

    // This returns either "CORRESPONDING" or "BY NAME", depending which
    // syntax was used.  For use in error messages.
    std::string GetByNameString() const;

    // This returns either "CORRESPONDING BY" or "BY NAME ON", depending which
    // syntax was used.  For use in error messages.
    std::string GetByNameOnString() const;

    // This returns either "CORRESPONDING" or "BY NAME", depending which
    // syntax was used, including the LEFT/FULL/STRICT/INNER modifier
    // if one was in effect.  For use in error messages.
    std::string GetByNameStringFull() const;

    // This returns either "BY" or "ON", as appropriate for the syntax used.
    std::string GetByNameByOrOn() const;

    // Returns the `column_match_mode()` of the first set operation metadata.
    // Must be called after verifying all set operation metadata share the same
    // column match mode.
    ASTSetOperation::ColumnMatchMode ASTColumnMatchMode() const;

    // Returns the `column_propagation_mode()` of the first set operation
    // metadata. Must be called after verifying all set operation metadata share
    // the same column propagation mode.
    ASTSetOperation::ColumnPropagationMode ASTColumnPropagationMode() const;

    // Checks whether all the inputs in `resolved_inputs` have the same number
    // of columns in their namelists.
    absl::Status CheckSameColumnNumber(
        const std::vector<ResolvedInputResult>& resolved_inputs) const;

    // Validates that no elements in `resolved_inputs` have value table columns.
    absl::Status CheckNoValueTable(
        absl::Span<const ResolvedInputResult> resolved_inputs) const;

    // Returns the final column names for the set operation. The result depends
    // on the column_propagation_mode. Check the implementation for more
    // information.
    //
    // This function should only be called when column_match_mode is
    // CORRESPONDING.
    absl::StatusOr<std::vector<IdString>>
    CalculateFinalColumnNamesForCorresponding(
        absl::Span<const ResolvedInputResult> resolved_inputs) const;

    // Returns the final column names of the set operation, which are the same
    // as the identifiers in the corresponding by list.
    //
    // This function should only be called when column_match_mode is
    // CORRESPONDING_BY.
    absl::StatusOr<std::vector<IdString>>
    CalculateFinalColumnNamesForCorrespondingBy(
        const std::vector<ResolvedInputResult>& resolved_inputs) const;

    // Builds a vector specifying the type of each column for each input scan,
    // where the returned column_type_lists[column_idx][scan_idx] specifies the
    // type for the given (column index, input index) combination.
    //
    // This function should only be called if the column_match_mode is
    // CORRESPONDING.
    absl::StatusOr<std::vector<std::vector<InputArgumentType>>>
    BuildColumnTypeListsForCorresponding(
        int final_column_num,
        absl::Span<const ResolvedInputResult> resolved_inputs,
        const IndexMapper* index_mapper) const;

    // Adds type casts to convert the column types of each item in the
    // `resolvd_inputs` to the types of the corresponding columns in
    // `final_column_list`, if they differ. The column mapping is provided by
    // `index_mapper`.
    absl::Status AddTypeCastIfNeededForCorresponding(
        const ResolvedColumnList& final_column_list,
        absl::Span<ResolvedInputResult> resolved_inputs,
        const IndexMapper* index_mapper) const;

    // Builds the NameList template for the final NameList for CORRESPONDING.
    // If a column in `final_column_list` does not appear in the first input,
    // it is not an explicit column. Otherwise inherits the explicit'ness from
    // the corresponding column in the first input.
    absl::StatusOr<std::shared_ptr<const NameList>>
    BuildNameListTemplateForCorresponding(
        const ResolvedColumnList& final_column_list,
        const NameList& first_item_name_list,
        const IndexMapper* index_mapper) const;

    // Adjust the output columns of each resolved_input in `resolved_inputs` to
    // match the names and column order of `final_column_list`.
    absl::Status AdjustAndReorderColumns(
        const ResolvedColumnList& final_column_list,
        const IndexMapper* index_mapper,
        std::vector<ResolvedInputResult>& resolved_inputs) const;

    // Moves the `node` field of each ResolvedInputResults in
    // `resolved_inputs` to a separate vector.
    std::vector<std::unique_ptr<const ResolvedSetOperationItem>>
    ExtractSetOperationItems(
        absl::Span<ResolvedInputResult> resolved_inputs) const;

    // For a given `resolved_input`, calculates the input argument types of its
    // resolved columns.
    absl::StatusOr<std::vector<InputArgumentType>> BuildColumnTypeList(
        const ResolvedInputResult& resolved_input) const;

    // Returns a union of column names provided in `resolved_inputs`. It is ok
    // for an input scan to have duplicate column names.
    absl::flat_hash_set<IdString, IdStringCaseHash, IdStringCaseEqualFunc>
    GetAllColumnNames(
        const std::vector<ResolvedInputResult>& resolved_inputs) const;

    struct FinalColumnList {
      // The calculated final column list of the set operation.
      ResolvedColumnList column_list;

      // The NameList template to build the final NameList for the recursive
      // query. The final NameList inherits the template's column attributes,
      // like the value-table-ness and column explictness. See
      // `BuildFinalNameList()` for more details.
      std::shared_ptr<const NameList> name_list_template;
    };

    // Mutates the parameter `resolved_inputs` to adjust the input
    // queries so that their matching columns are in the same position and share
    // the same types. The position and type adjustment are done by adding
    // ProjectScans to the input queries and updating their corresponding
    // NameLists.
    //
    // Returns the final column list of the set operation.
    absl::StatusOr<FinalColumnList> MatchInputsAndCalculateFinalColumns(
        std::vector<ResolvedInputResult>& resolved_inputs);

    // Validates that the column types of the recursive term columns can be
    // coerced into the corresponding columns in `final_name_list`.
    //
    // If `index_mapper` is not nullptr, it is used to map the columns of the
    // recursive term to the columns in `final_name_list`.
    absl::Status ValidateRecursiveTermColumnTypes(
        const NameList& final_name_list,
        const ResolvedInputResult& resolved_recursive_input,
        const IndexMapper* index_mapper = nullptr) const;

    // Returns the metadata of the input set operation. Standard set operations
    // contain a list of metadata, and we use the first one as the effective
    // metadata. Pipe set operations only have one metadata.
    const ASTSetOperationMetadata& effective_metadata() const {
      switch (input_kind_) {
        case InputKind::kStandard:
          return *ast_set_operation()->metadata()->set_operation_metadata_list(
              0);
        case InputKind::kPipe:
          return *ast_pipe_set_operation()->metadata();
        case InputKind::kPipeRecursive:
          return *ast_pipe_recursive_union()->metadata();
      }
    }

    // Returns the ast node for the input set operation.
    const ASTNode* ast_node() const {
      switch (input_kind_) {
        case InputKind::kStandard:
          return ast_set_operation();
        case InputKind::kPipe:
          return ast_pipe_set_operation();
        case InputKind::kPipeRecursive:
          return ast_pipe_recursive_union();
      }
    }

    // Returns the AST nodes in the `inputs` field for the input standard or
    // pipe set operation.
    //
    // Note it does not include resolved input query for pipe set operations.
    // For example, in
    //
    // ```
    // FROM Table
    // |> UNION ALL (SELECT 1), (SELECT 2)
    // ```
    //
    // `ast_inputs()` returns the AST nodes corresponding to "SELECT 1" and
    // "SELECT 2", not "FROM Table" (which has been resolved before resolving
    // the pipe UNION operator).
    //
    // `ast_inputs(1)` returns the AST node corresponding to "SELECT 2".
    //
    // For pipe recursive unions, it returns the input subquery or subpipeline.
    // The return value is always a vector of size 1.
    absl::Span<const ASTNode* const> ast_inputs() const {
      switch (input_kind_) {
        case InputKind::kStandard:
          return absl::MakeSpan(reinterpret_cast<const ASTNode* const*>(
                                    ast_set_operation()->inputs().data()),
                                ast_set_operation()->inputs().size());

        case InputKind::kPipe:
          return absl::MakeSpan(reinterpret_cast<const ASTNode* const*>(
                                    ast_pipe_set_operation()->inputs().data()),
                                ast_pipe_set_operation()->inputs().size());
        case InputKind::kPipeRecursive:
          ABSL_DCHECK_EQ(pipe_recursive_rhs_input_nodes_.size(), 1);
          return absl::MakeSpan(pipe_recursive_rhs_input_nodes_);
      }
    }

    const ASTNode* ast_inputs(int ast_input_idx) const {
      return ast_inputs()[ast_input_idx];
    }

    // Returns the input standard set operation, or nullptr if the input is a
    // pipe set operation.
    const ASTSetOperation* ast_set_operation() const {
      if (input_kind_ == InputKind::kStandard) {
        return std::get<const ASTSetOperation* const>(set_operation_);
      }
      return nullptr;
    }

    // Returns the input pipe set operation, or nullptr if the input is a
    // standard set operation.
    const ASTPipeSetOperation* ast_pipe_set_operation() const {
      if (input_kind_ == InputKind::kPipe) {
        return std::get<const ASTPipeSetOperation* const>(set_operation_);
      }
      return nullptr;
    }

    const ASTPipeRecursiveUnion* ast_pipe_recursive_union() const {
      if (input_kind_ == InputKind::kPipeRecursive) {
        return std::get<const ASTPipeRecursiveUnion* const>(set_operation_);
      }
      return nullptr;
    }

    // Returns the index of the recursive term in the `ast_inputs()`. Should
    // only be called when the input set operation is recursive.
    int ast_recursive_term_idx() const {
      // Recursive term is always the last input to the set operation.
      return static_cast<int>(ast_inputs().size()) - 1;
    }

    // Returns the AST node for the recursive term in the `ast_inputs()`.
    // Should only be called when the input set operation is recursive.
    const ASTNode* ast_recursive_term() const { return ast_inputs().back(); }

    // Determines how the given query should be referenced in the error
    // messages.
    //
    // `query_idx` is 0-based. For standard set operations, the 0-th query is
    // ASTSetOperation::inputs(0). For pipe set operations, the 0-th query is
    // the lhs pipe input query.
    std::string GetQueryLabel(int query_idx,
                              bool capitalize_first_char = false) const;

    // Shorthand for GetQueryLabel(0, capitalize_first_char).
    std::string GetFirstQueryLabel(bool capitalize_first_char = false) const {
      return GetQueryLabel(/*query_idx=*/0, capitalize_first_char);
    }

    SetOperationResolver(InputSetOperation set_operation,
                         const std::shared_ptr<const NameList>& lhs_name_list,
                         std::unique_ptr<const ResolvedScan>* lhs_scan,
                         Resolver* resolver);

    // Returns the resolved input scans for the set operation. For pipe set
    // operations the first item of the resolved inputs will be
    // `resolved_pipe_input_`.
    absl::StatusOr<std::vector<ResolvedInputResult>> GetResolvedInputs(
        const NameScope* scope, const Type* inferred_type_for_query);

    // Assembles and returns a ResolvedInputResult from `lhs_name_list` and
    // `lhs_scan` corresponding to the given `ast_pipe_operator`.
    static std::optional<ResolvedInputResult> GetResolvedPipeInput(
        const ASTNode* ast_pipe_operator,
        const std::shared_ptr<const NameList>& lhs_name_list,
        std::unique_ptr<const ResolvedScan>* lhs_scan);

    inline bool IsPipeSyntax() const {
      return input_kind_ != InputKind::kStandard;
    }

    // The input set operation.
    InputSetOperation set_operation_;

    // This field is true if `set_operation_` is an ASTPipeSetOperation, and
    // false if it is an ASTSetOperation.
    //
    // We cache this field instead of checking, for example,
    // `ast_set_operation() == nullptr` to avoid performing unnecessary `get`
    // function calls.
    const InputKind input_kind_;

    // If the input is a pipe set operation, the field holds its resolved input
    // scan until GetResolvedInputs() or ResolveNonRecursiveTerm() is called,
    // after which the field is nullopt. For standard set operations, the field
    // is always nullopt.
    std::optional<ResolvedInputResult> resolved_pipe_input_;

    Resolver* const resolver_;
    const IdString op_type_str_;

    // If the input set operation is a pipe recursive union, the field holds
    // either the input subquery or subpipeline of the pipe recursive union and
    // the size is always 1.
    //
    // Otherwise the vector is always empty.
    std::vector<const ASTNode*> pipe_recursive_rhs_input_nodes_;
  };

  // Called only for the query associated with an actually-recursive WITH
  // entry. Verifies that the query is
  // - a standard UNION:
  //     <non-recursive term>
  //     UNION (ALL|DISTINCT)
  //     <recursive-term>
  // - or a pipe UNION:
  //     <non-recursive term>
  //     |> UNION (ALL|DISTINCT) <recursive-term>
  //
  // Returns a SetOperationResolver to resolve the recursive query.
  //
  // Side effects:
  // - Register a NULL entry for the named subquery in `named_subquery_map_` so
  //   that any references to it from within the non-recursive term result in an
  //   error.
  //
  // `query`: the ast node of the recursive subquery.
  // `table_alias`: the alias of the recursive table, for example:
  //    - "t" in "WITH RECURSIVE t AS (SELECT 1)".
  //    - "a.b.c" in "CREATE RECURSIVE VIEW a.b.c".
  absl::StatusOr<SetOperationResolver> GetSetOperationResolverForRecursiveQuery(
      const ASTQuery* query, const std::vector<IdString>& table_alias);

  absl::Status ResolveGroupByExprs(const ASTGroupBy* group_by,
                                   const NameScope* from_clause_scope,
                                   QueryResolutionInfo* query_resolution_info);

  // Resolves a single grouping expression to column. If the expression belongs
  // to a grouping set, then the resolved column will be appended to the
  // grouping_set_item which is stored in query_resolution_info eventually. If
  // it's not from the grouping set, grouping_set_item will have the default
  // value nullptr.
  absl::Status ResolveGroupingItemExpression(
      const ASTExpression* ast_group_by_expr, const ASTAlias* ast_alias,
      const ASTGroupingItemOrder* ast_grouping_item_order,
      const NameScope* from_clause_scope, bool from_grouping_set,
      QueryResolutionInfo* query_resolution_info,
      ResolvedComputedColumnList* column_list = nullptr);

  // Resolves a list of grouping set expressions belonging to the same grouping
  // set. The grouping set type is specified by the kind argument. After all
  // grouping set expressions are resolved to columns, it adds all columns to
  // a grouping set info and returns it.
  absl::StatusOr<GroupingSetInfo> ResolveGroupingSetExpressions(
      absl::Span<const ASTExpression* const> expressions,
      const NameScope* from_clause_scope, GroupingSetKind kind,
      QueryResolutionInfo* query_resolution_info);

  // Allocates a new ResolvedColumn for the post-GROUP BY version of the
  // column and returns it in <group_by_column>.  Resets <resolved_expr>
  // to the original SELECT column expression, and assign the original
  // pre-group-by-expression in SELECT column to <pre_group_by_expr>. Updates
  // the SelectColumnState to reflect that the corresponding SELECT list column
  // is being grouped by.
  absl::Status HandleGroupBySelectColumn(
      const SelectColumnState* group_by_column_state,
      QueryResolutionInfo* query_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr,
      const ResolvedExpr** pre_group_by_expr, ResolvedColumn* group_by_column);

  // Allocates a new ResolvedColumn for the post-GROUP BY version of the
  // column and returns it in <group_by_column>.  If the expression is
  // already on the precomputed list (in <query_resolution_info>),
  // updates <resolved_expr> to be a column reference to the precomputed
  // column, and assign the original pre-group-by-expression in SELECT column to
  // <pre_group_by_expr>.
  absl::Status HandleGroupByExpression(
      const ASTExpression* ast_group_by_expr,
      QueryResolutionInfo* query_resolution_info, IdString alias,
      std::unique_ptr<const ResolvedExpr>* resolved_expr,
      const ResolvedExpr** pre_group_by_expr, ResolvedColumn* group_by_column);

  // Add a select list column to the group by list of GROUP BY ALL.
  absl::Status AddSelectColumnToGroupByAllComputedColumn(
      const SelectColumnState* select_column_state,
      QueryResolutionInfo* query_resolution_info);

  // Resolves GROUP BY ALL to group by selected columns that are non-aggregate,
  // non-analytic, and reference at least a name from the current query's FROM
  // clause name scope. GROUP BY ALL syntax is mutually exclusive with any other
  // GROUP BY `grouping items` syntax.
  absl::Status ResolveGroupByAll(const ASTGroupBy* group_by,
                                 const NameScope* from_clause_scope,
                                 QueryResolutionInfo* query_resolution_info);

  absl::Status ResolveQualifyExpr(
      const ASTQualify* qualify, const NameScope* having_and_order_by_scope,
      const NameScope* select_list_and_from_scan_scope,
      QueryResolutionInfo* query_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_qualify_expr);

  absl::Status ResolveHavingExpr(
      const ASTHaving* having, const NameScope* having_and_order_by_scope,
      const NameScope* select_list_and_from_scan_scope,
      QueryResolutionInfo* query_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_having_expr);

  // Ensures that each undeclared parameter got assigned a type.
  absl::Status ValidateUndeclaredParameters(const ResolvedNode* node);

  // Validate and resolves ASTCollate node for collation in columns.
  absl::Status ValidateAndResolveCollate(
      const ASTCollate* ast_collate, const ASTNode* ast_location,
      const Type* column_type,
      std::unique_ptr<const ResolvedExpr>* resolved_collate);

  // Resolves ASTCollate node for default collation in tables and datasets.
  absl::Status ValidateAndResolveDefaultCollate(
      const ASTCollate* ast_collate, const ASTNode* ast_location,
      std::unique_ptr<const ResolvedExpr>* resolved_collate);

  // Validate and resolves ASTCollate node for ORDER BY COLLATE.
  absl::Status ValidateAndResolveOrderByCollate(
      const ASTCollate* ast_collate, const ASTNode* ast_order_by_item_location,
      const Type* order_by_item_column,
      std::unique_ptr<const ResolvedExpr>* resolved_collate);

  // Resolves ASTCollate node.
  absl::Status ResolveCollate(
      const ASTCollate* ast_collate,
      std::unique_ptr<const ResolvedExpr>* resolved_collate);

  // Resolves the table name and predicate expression in an ALTER ROW POLICY
  // or CREATE ROW POLICY statement.
  absl::Status ResolveTableAndPredicate(
      const ASTPathExpression* table_path, const ASTExpression* predicate,
      const char* clause_name,
      std::unique_ptr<const ResolvedTableScan>* resolved_table_scan,
      std::unique_ptr<const ResolvedExpr>* resolved_predicate,
      std::string* predicate_str);

  // Create a ResolvedColumn for each ORDER BY item in <order_by_info> that
  // is not supposed to be a reference to a SELECT column (which currently only
  // corresponds to an item that is not an integer literal, and includes
  // the alias references).
  // If the ORDER BY expression is not a column reference or is an outer
  // reference, then create a ResolvedComputedColumn and insert it into
  // <computed_columns>.
  absl::Status AddColumnsForOrderByExprs(
      IdString query_alias, std::vector<OrderByItemInfo>* order_by_info,
      std::vector<std::unique_ptr<const ResolvedComputedColumn>>*
          computed_columns);

  // Resolves the lambda with the provided list of <arg_types> and expected
  // lambda type of <body_result_type>.
  // If <body_result_type> is not nullptr, then the result of the body
  // expression will be coerced to <body_result_type> if necessary.
  // This function is passed as a callback to FunctionSignatureMatcher.
  // Annotations are not yet considered at this stage.
  absl::Status ResolveLambda(
      const ASTLambda* ast_lambda, absl::Span<const IdString> arg_names,
      absl::Span<const Type* const> arg_types, const Type* body_result_type,
      bool allow_argument_coercion, const NameScope* name_scope,
      std::unique_ptr<const ResolvedInlineLambda>* resolved_expr_out);

  // Resolves the lambda with the provided list of `arg_types` and expected
  // lambda type of `body_result_type`, including annotations.
  //
  // This function is called after signature matching, and after all coercions
  // and checks have been determined.
  //
  // It is invoked with the correct types and annotations, to propagate them as
  // if the lambda was being inlined, now that the argument types (and
  // annotations) have been pinned. This is important because:
  // 1. To detect any propagation errors inside the lambda body (e.g. collation
  //    conflict).
  // 2. To determine the annotations on the output, e.g.
  //    ARRAY_TRANSFORM(T1{annotation1}, e -> STRUCT(e, T2{annotation})}) the
  //    output annotation here is determined by a mix of the lambda args and the
  //    body itself.
  absl::Status ResolveLambdaWithAnnotations(
      const ASTLambda* ast_lambda, absl::Span<const IdString> arg_names,
      absl::Span<const AnnotatedType> arg_types, const Type* body_result_type,
      bool allow_argument_coercion, const NameScope* name_scope,
      std::unique_ptr<const ResolvedInlineLambda>* resolved_expr_out);

  // Resolves the given LIMIT or OFFSET clause <ast_expr> and stores the
  // resolved expression in <resolved_expr>.
  absl::Status ResolveLimitOrOffsetExpr(
      const ASTExpression* ast_expr, const char* clause_name,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* expr);

  // Resolves LIMIT and OFFSET clause and adds a ResolvedFilterScan onto `scan`.
  // Accepts clauses from ASTLimitOffset. Requires LIMIT to be present.
  absl::Status ResolveLimitOffsetScan(
      const ASTLimitOffset* limit_offset, const NameScope* name_scope,
      std::unique_ptr<const ResolvedScan>* scan);

  // Resolves LIMIT and OFFSET clause and adds a ResolvedFilterScan onto `scan`.
  // Accepts LIMIT and OFFSET clauses separately. Does not require LIMIT to be
  // present.
  absl::Status ResolveLimitOffsetScan(
      const ASTExpression* limit, const ASTExpression* offset,
      const NameScope* name_scope, std::unique_ptr<const ResolvedScan>* scan);

  // Translates the enum representing an IGNORE NULLS or RESPECT NULLS modifier.
  ResolvedNonScalarFunctionCallBase::NullHandlingModifier
  ResolveNullHandlingModifier(
      ASTFunctionCall::NullHandlingModifier ast_null_handling_modifier);

  // Resolves the WHERE modifier for aggregate and analytic functions.
  absl::Status ResolveWhereModifier(
      const ASTFunctionCall* ast_function_call,
      const ResolvedFunctionCall* resolved_function_call,
      const NameScope* name_scope,
      std::unique_ptr<const ResolvedExpr>* resolved_where_expr);

  // Resolves the given HAVING MAX or HAVING MIN argument, and stores the
  // result in <resolved_having>.
  absl::Status ResolveHavingMaxMinModifier(
      const ASTFunctionCall* ast_function_call,
      const ResolvedFunctionCall* resolved_function_call,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedAggregateHavingModifier>* resolved_having);

  // Resolve the pipe operators in `pipe_operator_list`.
  // `current_scan` is the initial input and is mutated to be the output.
  // `current_name_list` is the input scope and is mutated to be the output.
  //
  // If `options.allow_terminal` is true, then `*current_name_list` can be NULL
  // on return, indicating there was a terminal pipe operator and this query
  // doesn't return a table.
  absl::Status ResolvePipeOperatorList(
      absl::Span<const ASTPipeOperator* const> pipe_operator_list,
      const NameScope* outer_scope,
      std::unique_ptr<const ResolvedScan>* current_scan,
      std::shared_ptr<const NameList>* current_name_list,
      const Type* inferred_type_for_query, bool allow_terminal);

  absl::Status ResolvePipeWhere(
      const ASTPipeWhere* where, const NameScope* scope,
      std::unique_ptr<const ResolvedScan>* current_scan);

  absl::Status ResolvePipeLimitOffset(
      const ASTPipeLimitOffset* limit_offset, const NameScope* scope,
      std::unique_ptr<const ResolvedScan>* current_scan);

  absl::Status ResolvePipeTablesample(
      const ASTPipeTablesample* tablesample, const NameScope* scope,
      std::unique_ptr<const ResolvedScan>* current_scan,
      std::shared_ptr<const NameList>* current_name_list);

  absl::Status ResolvePipeSelect(
      const ASTPipeSelect* select, const NameScope* scope,
      std::unique_ptr<const ResolvedScan>* current_scan,
      std::shared_ptr<const NameList>* current_name_list,
      const Type* inferred_type_for_pipe);

  absl::Status ResolvePipeExtend(
      const ASTPipeExtend* extend, const NameScope* scope,
      std::unique_ptr<const ResolvedScan>* current_scan,
      std::shared_ptr<const NameList>* current_name_list);

  absl::Status ResolvePipeAggregate(
      const ASTPipeAggregate* aggregate, const NameScope* scope,
      std::unique_ptr<const ResolvedScan>* current_scan,
      std::shared_ptr<const NameList>* current_name_list);

  absl::Status ResolvePipeDistinct(
      const ASTPipeDistinct* distinct, const NameScope* scope,
      std::unique_ptr<const ResolvedScan>* current_scan,
      std::shared_ptr<const NameList>* current_name_list);

  using DistinctExprMap =
      absl::flat_hash_map<const ResolvedExpr*, const ResolvedComputedColumn*,
                          FieldPathHashOperator,
                          FieldPathExpressionEqualsOperator>;

  absl::StatusOr<const ResolvedComputedColumn*> AddPipeDistinctColumn(
      const ASTPipeDistinct* distinct, int column_pos, IdString name,
      const ResolvedColumn& column, DistinctExprMap* distinct_expr_map,
      std::vector<std::unique_ptr<const ResolvedComputedColumn>>*
          distinct_columns_to_compute);

  absl::Status ResolvePipeOrderBy(
      const ASTPipeOrderBy* order_by, const NameScope* scope,
      std::unique_ptr<const ResolvedScan>* current_scan,
      std::shared_ptr<const NameList>* current_name_list);

  absl::Status ResolvePipeCall(
      const ASTPipeCall* call, const NameScope* outer_scope,
      const NameScope* scope, std::unique_ptr<const ResolvedScan>* current_scan,
      std::shared_ptr<const NameList>* current_name_list);

  absl::Status ResolvePipeWindow(
      const ASTPipeWindow* window, const NameScope* scope,
      std::unique_ptr<const ResolvedScan>* current_scan,
      std::shared_ptr<const NameList>* current_name_list);

  absl::Status ResolvePipeJoin(
      const ASTPipeJoin* join, const NameScope* outer_scope,
      const NameScope* scope, std::unique_ptr<const ResolvedScan>* current_scan,
      std::shared_ptr<const NameList>* current_name_list);

  absl::Status ResolvePipeAs(
      const ASTPipeAs* pipe_as, const NameScope* scope,
      std::unique_ptr<const ResolvedScan>* current_scan,
      std::shared_ptr<const NameList>* current_name_list);

  std::string GetDescribeString(const ResolvedScan* current_scan,
                                const NameList* current_name_list,
                                bool internal_debug_string);

  absl::Status ResolvePipeDescribe(
      const ASTPipeDescribe* pipe_describe, const NameScope* scope,
      std::unique_ptr<const ResolvedScan>* current_scan,
      std::shared_ptr<const NameList>* current_name_list);

  absl::Status ResolvePipeStaticDescribe(
      const ASTPipeStaticDescribe* pipe_static_describe, const NameScope* scope,
      std::unique_ptr<const ResolvedScan>* current_scan,
      std::shared_ptr<const NameList>* current_name_list);

  absl::Status ResolvePipeAssert(
      const ASTPipeAssert* pipe_assert, const NameScope* scope,
      std::unique_ptr<const ResolvedScan>* current_scan,
      std::shared_ptr<const NameList> current_name_list);

  absl::Status ResolvePipeLog(
      const ASTPipeLog* pipe_log, const NameScope* outer_scope,
      const NameScope* scope, std::unique_ptr<const ResolvedScan>* current_scan,
      std::shared_ptr<const NameList>* current_name_list);

  absl::Status ResolvePipeDrop(
      const ASTPipeDrop* pipe_drop, const NameScope* scope,
      std::unique_ptr<const ResolvedScan>* current_scan,
      std::shared_ptr<const NameList>* current_name_list);

  absl::Status ResolvePipeRename(
      const ASTPipeRename* pipe_rename, const NameScope* scope,
      std::unique_ptr<const ResolvedScan>* current_scan,
      std::shared_ptr<const NameList>* current_name_list);

  absl::Status ResolvePipeSet(
      const ASTPipeSet* pipe_set, const NameScope* scope,
      std::unique_ptr<const ResolvedScan>* current_scan,
      std::shared_ptr<const NameList>* current_name_list);

  absl::Status ResolvePipePivot(
      const ASTPipePivot* pipe_pivot, const NameScope* scope,
      std::unique_ptr<const ResolvedScan>* current_scan,
      std::shared_ptr<const NameList>* current_name_list);

  absl::Status ResolvePipeUnpivot(
      const ASTPipeUnpivot* pipe_unpivot, const NameScope* scope,
      std::unique_ptr<const ResolvedScan>* current_scan,
      std::shared_ptr<const NameList>* current_name_list);

  absl::Status ResolvePipeMatchRecognize(
      const ASTPipeMatchRecognize* pipe_match_recognize, const NameScope* scope,
      std::unique_ptr<const ResolvedScan>* current_scan,
      std::shared_ptr<const NameList>* current_name_list);

  absl::Status ResolvePipeSetOperation(
      const ASTPipeSetOperation* set_operation, const NameScope* outer_scope,
      std::unique_ptr<const ResolvedScan>* current_scan,
      std::shared_ptr<const NameList>* current_name_list,
      const Type* inferred_type_for_pipe);

  // Read a constant Value out of an expression.
  // Give an error referencing using `clause_name` in the message if the
  // expression doesn't have a constant value available at analysis time.
  // TODO: b/277365877 - Upgrade this to support all expressions qualified as
  // ANALYSIS_CONST, per (broken link).
  absl::StatusOr<Value> GetConstantValue(const ASTExpression* ast_expr,
                                         const ResolvedExpr* resolved_expr,
                                         absl::string_view clause_name);

  absl::Status ResolvePipeIf(const ASTPipeIf* pipe_if,
                             const NameScope* outer_scope,
                             const NameScope* scope,
                             std::unique_ptr<const ResolvedScan>* current_scan,
                             std::shared_ptr<const NameList>* current_name_list,
                             bool allow_terminal);

  absl::Status ResolvePipeForkOrTeeSubpipeline(
      const ASTSubpipeline* ast_subpipeline, const NameScope* outer_scope,
      const ResolvedScan* current_scan, const NameList* current_name_list,
      bool allow_terminal,
      std::vector<std::unique_ptr<const ResolvedGeneralizedQuerySubpipeline>>*
          resolved_subpipelines);

  absl::Status ResolvePipeFork(
      const ASTPipeFork* pipe_fork, const NameScope* outer_scope,
      const NameScope* scope, std::unique_ptr<const ResolvedScan>* current_scan,
      std::shared_ptr<const NameList>* current_name_list, bool allow_terminal);

  absl::Status ResolvePipeTee(
      const ASTPipeTee* pipe_tee, const NameScope* outer_scope,
      const NameScope* scope, std::unique_ptr<const ResolvedScan>* current_scan,
      std::shared_ptr<const NameList>* current_name_list, bool allow_terminal);

  absl::Status ResolvePipeWith(
      const ASTPipeWith* pipe_with,
      std::vector<IdString>* named_subqueries_added,
      std::vector<std::unique_ptr<ResolvedWithScan>>* with_scans_to_add);

  absl::Status ResolvePipeExportData(
      const ASTPipeExportData* pipe_export_data, const NameScope* outer_scope,
      const NameScope* scope, std::unique_ptr<const ResolvedScan>* current_scan,
      std::shared_ptr<const NameList>* current_name_list, bool allow_terminal);

  absl::Status ResolvePipeCreateTable(
      const ASTPipeCreateTable* pipe_create_table, const NameScope* scope,
      std::unique_ptr<const ResolvedScan>* current_scan,
      std::shared_ptr<const NameList>* current_name_list, bool allow_terminal);

  absl::Status ResolvePipeInsert(
      const ASTPipeInsert* pipe_insert, const NameScope* outer_scope,
      const NameScope* scope, std::unique_ptr<const ResolvedScan>* current_scan,
      std::shared_ptr<const NameList>* current_name_list, bool allow_terminal);

  // Check if this terminal operator is allowed and give errors if not.
  absl::Status CheckTerminalPipeOperatorAllowed(
      const ASTNode* location, LanguageFeature required_language_feature,
      const char* operator_name, bool allow_terminal);

  absl::Status ResolvePipeRecursiveUnion(
      const ASTPipeRecursiveUnion* pipe_recursive_union,
      std::unique_ptr<const ResolvedScan>* current_scan,
      std::shared_ptr<const NameList>* current_name_list);

  absl::StatusOr<IdString> ResolvePipeRecursiveUnionAlias(
      const ASTPipeRecursiveUnion* pipe_recursive_union);

  // Creates a unique alias based on `with_alias` that is guaranteed not to
  // conflict with aliases in `unique_with_alias_names_`, and adds it to
  // `unique_with_alias_names_`.
  IdString MakeUniqueWithAlias(IdString with_alias);

  // Resolve a subpipeline.
  // `ast_subpipeline` is allowed to be null, for cases where the subpipeline
  //    is optional and defaults to ().
  // `outer_scope` is the scope from *outside* the containing query, for
  //    correlated column references. It's not the scope for the containing
  //    query.
  // `input_column_list` is the list of columns available on the
  //    ResolvedSubpipelineInputScan inside the subpipeline.
  // `input_is_ordered` indicates if the input table is ordered.
  // `subpipeline_name_list` is its initial NameList, and will be reset to
  //    the NameList describing the subpipeline's output table.
  // `allow_terminal` is true if the subpipeline can use a terminal operator.
  //    Then `*subpipeline_name_list` can be NULL on return, indicating there
  //    was a terminal pipe operator and this subpipeline doesn't return a
  //    table.
  absl::StatusOr<std::unique_ptr<const ResolvedSubpipeline>> ResolveSubpipeline(
      const ASTSubpipeline* ast_subpipeline, const NameScope* outer_scope,
      const ResolvedColumnList& input_column_list, bool input_is_ordered,
      std::shared_ptr<const NameList>* subpipeline_name_list,
      bool allow_terminal = false);

  // Add a ProjectScan if necessary to make sure that `scan` produces columns
  // with the desired types.
  // - `target_column_list` provides the expected column types.
  // - `scan_column_list` is the set of columns currently selected, matching
  //   positionally with `target_column_list`.
  // - `scan_alias` is the table name used internally for new ResolvedColumns
  //   in the ProjectScan.
  //
  // If any types don't match between `target_column_list` and
  // `scan_column_list`, `scan` and `scan_column_list` are mutated, adding a
  // ProjectScan and new columns.
  absl::Status CreateWrapperScanWithCasts(
      const ASTNode* ast_location, const ResolvedColumnList& target_column_list,
      IdString scan_alias, std::unique_ptr<const ResolvedScan>* scan,
      ResolvedColumnList* scan_column_list);

  IdString ComputeSelectColumnAlias(const ASTSelectColumn* ast_select_column,
                                    int column_idx) const;

  // Return true if the first identifier on the path is a name that exists in
  // <scope>.
  static bool IsPathExpressionStartingFromScope(const ASTPathExpression& expr,
                                                const NameScope& scope);

  // Return true if <table_ref> should be resolved as an array scan.
  // This happens if it has UNNEST, or it is a path with at least two
  // identifiers where the first comes from <scope>.
  bool ShouldResolveAsArrayScan(const ASTTablePathExpression* table_ref,
                                const NameScope* scope);

  // Return an expression that tests <expr1> and <expr2> for equality.
  absl::Status MakeEqualityComparison(
      const ASTNode* ast_location, std::unique_ptr<const ResolvedExpr> expr1,
      std::unique_ptr<const ResolvedExpr> expr2,
      std::unique_ptr<const ResolvedExpr>* output_expr);

  // Returns a resolved expression that computes NOT of expr.
  // NOTE: expr should resolve to a boolean type.
  absl::Status MakeNotExpr(const ASTNode* ast_location,
                           std::unique_ptr<const ResolvedExpr> expr,
                           ExprResolutionInfo* expr_resolution_info,
                           std::unique_ptr<const ResolvedExpr>* expr_out);

  // Returns a resolved expression computing COALESCE of <columns>.
  absl::Status MakeCoalesceExpr(
      const ASTNode* ast_location, const ResolvedColumnList& columns,
      std::unique_ptr<const ResolvedExpr>* output_expr);

  // Return an expression that combines <exprs> with AND.
  // <exprs> must be non-empty, and each element must have type BOOL.
  // If only one input expr, then returns it without creating an AND.
  absl::Status MakeAndExpr(
      const ASTNode* ast_location,
      std::vector<std::unique_ptr<const ResolvedExpr>> exprs,
      std::unique_ptr<const ResolvedExpr>* output_expr) const;

  // Validates the given measure source `table`, including:
  // - Each measure column has a valid measure expression,
  // - Row identity columns are present and valid,
  // - etc.
  //
  // - `ast_location`: The AST node to attach errors to.
  // - `table`: The table containing measure columns to be validated. This may
  //   represent a catalog table or the output schema of a TVF.
  // - `measure_source_string`: Used only when printing error messages.
  absl::Status ValidateMeasureSource(const ASTNode* ast_location,
                                     const Table* table,
                                     absl::string_view measure_source_string);

  // Copies the parse location from the AST to resolved node depending on the
  // value of the analyzer option 'parse_location_record_type()'.
  void MaybeRecordParseLocation(const ASTNode* ast_location,
                                ResolvedNode* resolved_node) const;

  // Copies the parse location to the resolved node if the supplied parse
  // location is valid and the AnalyzerOptions::parse_location_record_type
  // field is not "NONE".
  void MaybeRecordParseLocation(const ParseLocationRange& parse_location,
                                ResolvedNode* resolved_node) const;

  // Copies the parse location from the AST to resolved function call node
  // depending on the value of the analyzer option
  // 'parse_location_record_type()'.
  void MaybeRecordFunctionCallParseLocation(const ASTFunctionCall* ast_location,
                                            ResolvedNode* resolved_node) const;
  void MaybeRecordTVFCallParseLocation(const ASTTVF* ast_location,
                                       ResolvedNode* resolved_node) const;
  void MaybeRecordAnalyticFunctionCallParseLocation(
      const ASTAnalyticFunctionCall* ast_location,
      ResolvedNode* resolved_node) const;
  // Copies the parse location from the AST to expression subquery node
  // depending on the value of the analyzer option
  // 'parse_location_record_type()'.
  void MaybeRecordExpressionSubqueryParseLocation(
      const ASTExpressionSubquery* ast_expr_subquery,
      ResolvedNode* resolved_node) const;

  // Copies the parse location to resolved field access expr depending on the
  // value of the analyzer option 'parse_location_record_type()'.
  void MaybeRecordFieldAccessParseLocation(
      const ParseLocationRange& parse_location, const ASTIdentifier* ast_field,
      ResolvedExpr* resolved_field_access_expr) const;

  // Copies the locations of the argument name and type (if present) from the
  // 'function_argument' to the 'options'.
  void RecordArgumentParseLocationsIfPresent(
      const ASTFunctionParameter& function_argument,
      FunctionArgumentTypeOptions* options) const;

  // Records the parse locations of name and type of TVF schema column (if
  // present) into 'column'.
  void RecordTVFRelationColumnParseLocationsIfPresent(
      const ASTTVFSchemaColumn& tvf_schema_column, TVFRelation::Column* column);

  // Records the parse location of operator keyword to resolved node.
  void MaybeRecordResolvedNodeOperatorKeywordLocation(
      const ASTNode* ast_node, ResolvedNode* resolved_node) const;

  void MaybeRecordResolvedNodeOperatorKeywordLocation(
      const ParseLocationRange& location, ResolvedNode* resolved_node) const;

  // Generate a ResolvedScan for the FROM clause, populating the
  // <output_name_list> with the names visible in the FROM.  If there
  // is no FROM clause, then a ResolvedSingleRowScan will be produced.
  // Performs semantic checking to verify that queries without a FROM
  // clause do not have disallowed features.  For instance, ORDER BY is
  // not allowed if there is no FROM clause.
  absl::Status ResolveFromClauseAndCreateScan(
      const ASTSelect* select, const ASTOrderBy* order_by,
      const NameScope* external_scope,
      std::unique_ptr<const ResolvedScan>* output_scan,
      std::shared_ptr<const NameList>* output_name_list);

  // Resolve an element of a from clause.
  // This could be a table reference, a subquery, or a join.
  // `external_scope` is the scope with nothing from this FROM clause, to be
  // used for parts of the FROM clause that can't see local names.
  // `local_scope` includes all names visible in `external_scope` plus
  // names earlier in the same FROM clause that are visible.
  //
  // `is_leftmost`: indicates whether if this is the leftmost item in the
  // nearest containing parenthesized join, or in the current FROM clause if
  // it's not inside any parentheses. Used to catch LATERAL as it is not allowed
  // on these items.
  //
  // `on_rhs_of_right_or_full_join`: indicates whether we're on the RHS subtree
  // of a right or full join, with no parenthesization on the path to that join.
  absl::Status ResolveTableExpression(
      const ASTTableExpression* table_expr, const NameScope* external_scope,
      const NameScope* local_scope, bool is_leftmost,
      bool on_rhs_of_right_or_full_join,
      std::unique_ptr<const ResolvedScan>* output,
      std::shared_ptr<const NameList>* output_name_list);

  // Table referenced through a path expression.
  // `external_scope` is the scope with nothing from this FROM clause, to be
  //    used for parts of the FROM clause that can't see local names such as
  //    PIVOT and UNPIVOT.
  // `local_scope` includes all names visible in `external_scope` plus
  //    names earlier in the same FROM clause that are visible.
  absl::Status ResolveTablePathExpression(
      const ASTTablePathExpression* table_ref, const NameScope* external_scope,
      const NameScope* local_scope, std::unique_ptr<const ResolvedScan>* output,
      std::shared_ptr<const NameList>* output_name_list);

  // Resolve a path expression <path_expr> as a argument of table type within
  // the context of a CREATE TABLE FUNCTION statement. The <path_expr> should
  // exist as a key in the function_table_arguments_ map, and should only
  // comprise a single-part name with exactly one element. The <hint> is
  // optional and may be NULL.
  absl::Status ResolvePathExpressionAsFunctionTableArgument(
      const ASTPathExpression* path_expr, const ASTHint* hint, IdString alias,
      const ASTNode* ast_location, std::unique_ptr<const ResolvedScan>* output,
      std::shared_ptr<const NameList>* output_name_list);

  // Table referenced through a subquery.
  absl::Status ResolveTableSubquery(
      const ASTTableSubquery* table_ref, const NameScope* scope,
      std::unique_ptr<const ResolvedScan>* output,
      std::shared_ptr<const NameList>* output_name_list);

  // Resolve a identifier that is known to resolve to a named subquery
  // (e.g. WITH entry or recursive view).
  absl::Status ResolveNamedSubqueryRef(
      const ASTPathExpression* table_path, const ASTHint* hint,
      std::unique_ptr<const ResolvedScan>* output,
      std::shared_ptr<const NameList>* output_name_list);

  // Similar to the above overload, but takes in a named subquery directly
  // instead of doing a lookup in `named_subquery_map_`.
  //
  // `ast_location`: the ast location of the table reference expression.
  // `table_path`: the path of the table reference expression.
  absl::Status ResolveNamedSubqueryRef(
      const ASTNode* ast_location, const std::vector<IdString>& table_path,
      const NamedSubquery* named_subquery, const ASTHint* hint,
      std::unique_ptr<const ResolvedScan>* output,
      std::shared_ptr<const NameList>* output_name_list);

  // If <ast_join> has a join hint keyword (e.g. HASH JOIN or LOOKUP JOIN),
  // add that hint onto <resolved_scan>.  Called with JoinScan or ArrayScan.
  static absl::Status MaybeAddJoinHintKeyword(const ASTJoin* ast_join,
                                              ResolvedScan* resolved_scan);

  // Resolves the <join_condition> for a USING clause on a join.
  // <name_list_lhs> and <name_list_rhs> are the columns visible in the left and
  // right side input.
  // Adds columns that need to be computed before or after the join to the
  // appropriate computed_column vectors.
  absl::Status ResolveUsing(
      const ASTUsingClause* using_clause, const NameList& name_list_lhs,
      const NameList& name_list_rhs, ResolvedJoinScan::JoinType join_type,
      bool is_array_scan,
      std::vector<std::unique_ptr<const ResolvedComputedColumn>>*
          lhs_computed_columns,
      std::vector<std::unique_ptr<const ResolvedComputedColumn>>*
          rhs_computed_columns,
      std::vector<std::unique_ptr<const ResolvedComputedColumn>>*
          computed_columns,
      NameList* output_name_list,
      std::unique_ptr<const ResolvedExpr>* join_condition);

  // `is_leftmost`: indicates whether if this is the leftmost item in the
  // nearest containing parenthesized join, or in the current FROM clause if
  // it's not inside any parentheses. Used to catch LATERAL as it is not allowed
  // on these items.
  //
  // `on_rhs_of_right_or_full_join`: indicates whether we're on the RHS subtree
  // of a right or full join.
  absl::Status ResolveJoin(const ASTJoin* join, const NameScope* external_scope,
                           const NameScope* local_scope, bool is_leftmost,
                           bool on_rhs_of_right_or_full_join,
                           std::unique_ptr<const ResolvedScan>* output,
                           std::shared_ptr<const NameList>* output_name_list);

  absl::Status ResolveJoinRhs(
      const ASTJoin* join, const NameScope* external_scope,
      const NameScope* scope_for_lhs,
      const std::shared_ptr<const NameList>& name_list_lhs,
      std::unique_ptr<const ResolvedScan> resolved_lhs,
      bool on_rhs_of_right_or_full_join,
      std::unique_ptr<const ResolvedScan>* output,
      std::shared_ptr<const NameList>* output_name_list);

  absl::Status AddScansForJoin(
      const ASTJoin* join, std::unique_ptr<const ResolvedScan> resolved_lhs,
      std::unique_ptr<const ResolvedScan> resolved_rhs,
      ResolvedJoinScan::JoinType resolved_join_type, bool has_using,
      std::unique_ptr<const ResolvedExpr> join_condition,
      std::vector<std::unique_ptr<const ResolvedComputedColumn>>
          computed_columns,
      bool is_lateral,
      std::vector<std::unique_ptr<const ResolvedColumnRef>> parameter_list,
      std::shared_ptr<const NameList> output_name_list,
      std::unique_ptr<const ResolvedScan>* output_scan);

  absl::Status ResolveParenthesizedJoin(
      const ASTParenthesizedJoin* parenthesized_join,
      const NameScope* external_scope, const NameScope* local_scope,
      bool on_rhs_of_right_or_full_join,
      std::unique_ptr<const ResolvedScan>* output,
      std::shared_ptr<const NameList>* output_name_list);

  // Resolves a call to a table-valued function (TVF) represented by <ast_tvf>.
  // This returns a new ResolvedTVFScan which contains the name of the function
  // to call and the scalar and table-valued arguments to pass into the call.
  //
  // An initial `pipe_input_arg` of table type can optionally be passed in and
  // will be treated as the pipe input for pipe CALL, and will be matched
  // against the first table argument in the signature. This pointer only needs
  // to live for the duration of this call.
  //
  // The steps of resolving this function call proceed in the following order:
  //
  // 1. Check to see if the language option is enabled to support TVF calls in
  //    general. If not, return an error.
  //
  // 2. Get the function name from <ast_tvf> and perform a catalog lookup to see
  //    if a TVF exists with that name. If not, return an error.
  //
  // 3. Resolve each scalar argument as an expression, and resolve each
  //    table-valued argument as a query. This step can result in nested
  //    resolution of stored SQL bodies in templated TVFs or UDFs.
  //
  // 4. Check to see if the TVF's resolved arguments match its function
  //    signature. If not, return an error.
  //
  // 5. If needed, add type coercions for scalar arguments or projections to
  //    rearrange/coerce/drop columns for table-valued arguments. Note that
  //    table-valued arguments are matched on column names, not order.
  //
  // 6. Call the virtual TableValuedFunction::Resolve method to obtain the TVF
  //    output schema based on its input arguments.
  //
  // 7. Build the final ResolvedTVFScan based on the final input arguments and
  //    output schema.
  absl::Status ResolveTVF(const ASTTVF* ast_tvf,
                          const NameScope* external_scope,
                          ResolvedTVFArg* pipe_input_arg,
                          std::unique_ptr<const ResolvedScan>* output,
                          std::shared_ptr<const NameList>* output_name_list);

  absl::StatusOr<ResolvedTVFArg> ResolveTVFArg(
      const ASTTVFArgument* ast_tvf_arg, const NameScope* external_scope,
      const FunctionArgumentType* function_argument,
      const TableValuedFunction* tvf_catalog_entry, int sig_idx,
      absl::flat_hash_map<int, std::unique_ptr<const NameScope>>*
          sig_idx_to_name_scope_map);

  absl::StatusOr<InputArgumentType> GetTVFArgType(
      const ResolvedTVFArg& resolved_tvf_arg);

  // Returns true if the relation argument of <tvf_signature_arg> at <arg_idx>
  // has a required schema where the number, order, and/or types of columns do
  // not exactly match those in the provided input relation. If so, the
  // CoerceOrRearrangeTVFRelationArgColumns method can construct a projection to
  // produce the column names that the required schema expects.
  absl::StatusOr<bool> CheckIfMustCoerceOrRearrangeTVFRelationArgColumns(
      const FunctionArgumentType& tvf_signature_arg, int arg_idx,
      const SignatureMatchResult& signature_match_result,
      const ResolvedTVFArg& resolved_tvf_arg);

  // This method adds a ProjectScan on top of a relation argument for a
  // table-valued function relation argument when the function signature
  // specifies a required schema for that argument and the provided number,
  // order, and/or types of columns do not match exactly. This way the engine
  // may consume the provided input columns in the same order as the order of
  // the requested columns, since they match 1:1 after this function returns.
  //
  // This assumes that the signature matching process has already accepted the
  // function arguments and updated the signature match results to indicate
  // which coercions need to be made (if any).
  //
  // <tvf_signature_arg> is the type of the current relation argument to
  // consider.
  //
  // <arg_idx> is the index of that argument in the list of signature
  // arguments, starting at zero.
  //
  // <signature_match_result> contains information obtained from performing the
  // match of the provided TVF arguments against the function signature.
  //
  // <ast_location> is a place in the AST to use for error messages.
  //
  // <resolved_tvf_arg> is an in/out parameter that contains the resolved scan
  // and NameList for the relation argument, and this method updates it to
  // contain a projection to perform the coercions.
  absl::Status CoerceOrRearrangeTVFRelationArgColumns(
      const FunctionArgumentType& tvf_signature_arg, int arg_idx,
      const SignatureMatchResult& signature_match_result,
      const ASTNode* ast_location, ResolvedTVFArg& resolved_tvf_arg);

  // Resolve a column in the USING clause on one side of the join.
  // <side_name> is "left" or "right", for error messages.
  absl::Status ResolveColumnInUsing(
      const ASTIdentifier* ast_identifier, const NameList& name_list,
      absl::string_view side_name, IdString key_name,
      ResolvedColumn* found_column,
      std::unique_ptr<const ResolvedExpr>* compute_expr_for_found_column);

  // Resolve an array scan that is unnested in a FROM clause. It could be
  // written as any of the following shape:
  //   FROM ... JOIN table_name.array_path
  //   FROM table_name [AS alias], {{table_name|alias}}.array_path
  //   FROM table_name.array_path
  //
  // `table_ref` is a path to array value with either explicit UNNEST operator
  //     or implicit UNNEST operator.
  //  - When `table_ref` was the argument to an explicit UNNEST operator,
  //    `path_expr` does not have value.
  //  - When `table_ref` is a path that was implicitly unnested, the prefix of
  //    `table_ref` up to and including the first name in `path_expr` identifies
  //    a table or range variable.
  // `path_expr` is a suffix of the path expression in `table_ref` that is array
  //     reference. This may be a simple path, or it may contain an implicit
  //     `FLATTEN` operator when the last name does not resolve to array type.
  // `resolved_input_scan` is either NULL or the already resolved scan feeding
  //     rows into this array scan. May be mutated if we need to compute columns
  //     before the join.
  // `on_clause` is non-NULL if this is a JOIN with an ON clause.
  // `using_clause` is non-NULL if this is a JOIN with a USING clause.
  // `ast_join` is the JOIN node for this array scan, or NULL.
  // `is_outer_scan` is true if this is a LEFT JOIN.
  // `include_lhs_name_list` is false only if we are resolving any of the
  //     following cases:
  //  - a singleton table name array path: FROM table_name.array_path
  //  - a standalone explicit UNNEST expression: FROM UNNEST(...)
  // `is_single_table_array_path` is true only if we are resolving a singleton
  //     table name array path.
  //
  // ResolveArrayScan may take ownership of `resolved_lhs_scan` and clear the
  // unique_ptr.
  //
  // Preconditions:
  // - First identifier on that path resolves to a name inside scope.
  absl::Status ResolveArrayScan(
      const ASTTablePathExpression* table_ref,
      std::optional<PathExpressionSpan> path_expr, const ASTOnClause* on_clause,
      const ASTUsingClause* using_clause, const ASTJoin* ast_join,
      bool is_outer_scan, bool include_lhs_name_list,
      bool is_single_table_array_path,
      std::unique_ptr<const ResolvedScan>* resolved_input_scan,
      const std::shared_ptr<const NameList>& name_list_input,
      const NameScope* scope, std::unique_ptr<const ResolvedScan>* output,
      std::shared_ptr<const NameList>* output_name_list);

  // Resolves the ORDER BY expressions and creates columns for them.
  // Populates OrderByItemInfo in
  // <expr_resolution_info>->query_resolution_info, along with the list
  // of computed ORDER BY columns.  <is_post_distinct> indicates that the
  // ORDER BY occurs after DISTINCT, i.e., SELECT DISTINCT ... ORDER BY...
  absl::Status ResolveOrderByExprs(
      const ASTOrderBy* order_by, const NameScope* having_and_order_by_scope,
      const NameScope* select_list_and_from_scan_scope, bool is_post_distinct,
      QueryResolutionInfo* query_resolution_info);

  // Resolves a standalone ORDER BY outside the context of a SELECT.
  // A ResolvedOrderByScan will be added to `scan`.
  // This is used for ORDER BY after set operations and for pipe ORDER BY.
  absl::Status ResolveOrderBySimple(const ASTOrderBy* order_by,
                                    const NameList& name_list,
                                    const NameScope* scope,
                                    const char* clause_name,
                                    OrderBySimpleMode mode,
                                    std::unique_ptr<const ResolvedScan>* scan);

  // Resolve ASTNullOrder to the enum, checking LanguageFeatures.
  absl::StatusOr<ResolvedOrderByItemEnums::NullOrderMode> ResolveNullOrderMode(
      const ASTNullOrder* null_order);

  // Performs initial resolution of ordering expressions, and distinguishes
  // between select list ordinals and other resolved expressions.
  // The OrderByInfo in <expr_resolution_info>->query_resolution_info is
  // populated with the resolved ORDER BY expression info.
  absl::Status ResolveOrderingExprs(
      absl::Span<const ASTOrderingExpression* const> ordering_expressions,
      ExprResolutionInfo* expr_resolution_info, bool allow_ordinals,
      absl::string_view clause_name,
      std::vector<OrderByItemInfo>* order_by_info);

  // Resolves the ORDER BY expression against the post-GROUP BY scope and falls
  // back to resolving against the pre-GROUP BY scope to find an equivalent
  // GROUP BY expression.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
  ResolveOrderingExprWithGroupByExprEquivalenceFallback(
      const ASTExpression* ordering_expr,
      ExprResolutionInfo* post_group_by_expr_resolution_info);

  // A list of `const vector<OrderByItemInfo>&`.  Used because the object is
  // not copyable, for passing lists of one or more of those vectors using
  // {vector1, vector2} syntax.
  typedef const std::vector<
      std::reference_wrapper<const std::vector<OrderByItemInfo>>>
      OrderByItemInfoVectorList;

  // Resolves <order_by_info_lists> into <resolved_order_by_items>, which is
  // used for resolving both ORDER BY clauses and ORDER BY arguments in
  // aggregate functions.
  //
  // Validation is performed to ensure that the ORDER BY expression result
  // types support ordering. For resolving select ORDER BY clause, ensures
  // that the select list ordinal references are within bounds.
  // The returned ResolvedOrderByItem objects are stored in
  // <resolved_order_by_items>.
  absl::Status ResolveOrderByItems(
      const NameList& name_list,
      const OrderByItemInfoVectorList& order_by_info_lists,
      bool is_pipe_order_by,
      std::vector<std::unique_ptr<const ResolvedOrderByItem>>*
          resolved_order_by_items);

  // Make a ResolvedOrderByScan from the <order_by_info_lists>, adding onto
  // <scan>.  <name_list> is needed to resolve columns referenced by ordinal.
  // Hints are included if <order_by_hint> is non-null.
  absl::Status MakeResolvedOrderByScan(
      const ASTHint* order_by_hint, const NameList& name_list,
      const std::vector<ResolvedColumn>& output_column_list,
      const OrderByItemInfoVectorList& order_by_info_lists,
      bool is_pipe_order_by, std::unique_ptr<const ResolvedScan>* scan);

  // Make a ResolvedColumnRef for <column>.  Caller owns the returned object.
  // Has side-effect of calling RecordColumnAccess on <column>, so that
  // the access can be recorded if necessary and the ColumnRef will stay valid
  // after pruning.
  ABSL_MUST_USE_RESULT
  std::unique_ptr<ResolvedColumnRef> MakeColumnRef(
      const ResolvedColumn& column, bool is_correlated = false,
      ResolvedStatement::ObjectAccess access_flags = ResolvedStatement::READ);

  // Make a ResolvedColumnRef with correlation if <correlated_columns_sets> is
  // non-empty, or make a ResolvedColumnRef without correlation otherwise.  If
  // creating a ResolvedColumnRef with correlation, returns a
  // ResolvedColumnRef with is_correlated=true and adds <column> to each of
  // the <correlated_columns_sets>.
  // Note that even though <correlated_columns_sets> is a const reference,
  // the items in the list will be mutated.
  std::unique_ptr<ResolvedColumnRef> MakeColumnRefWithCorrelation(
      const ResolvedColumn& column,
      const CorrelatedColumnsSetList& correlated_columns_sets,
      ResolvedStatement::ObjectAccess access_flags = ResolvedStatement::READ);

  // Returns a copy of the <column_ref>.
  ABSL_MUST_USE_RESULT
  static std::unique_ptr<const ResolvedColumnRef> CopyColumnRef(
      const ResolvedColumnRef* column_ref);

  // Resolves an input ResolvedColumn in <resolved_column_ref_expr> to a
  // version of that ResolvedColumn that is available after GROUP BY.
  // Updates <resolved_column_ref_expr> with a visible version of the
  // ResolvedColumn if necessary, and returns an error if the column is
  // not visible after GROUP BY.
  absl::Status ResolveColumnRefExprToPostGroupingColumn(
      const ASTExpression* path_expr, absl::string_view clause_name,
      QueryResolutionInfo* query_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_column_ref_expr);

  // Resolves an expression specified by AST node <ast_expr>, looking up names
  // against <name_scope>, without support for aggregate or analytic functions.
  // If the expression contains aggregate or analytic functions then this method
  // returns an error message, possibly including <clause_name>.
  inline absl::Status ResolveScalarExpr(
      const ASTExpression* ast_expr, const NameScope* name_scope,
      const char* clause_name,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out,
      const Type* inferred_type = nullptr) {
    return ResolveScalarExpr(ast_expr, name_scope, clause_name,
                             /*query_resolution_info=*/nullptr,
                             resolved_expr_out, inferred_type);
  }
  // Same as above but considers QueryResolutionInfo. Useful for lateral
  // references to SELECT columns.
  absl::Status ResolveScalarExpr(
      const ASTExpression* ast_expr, const NameScope* name_scope,
      const char* clause_name, QueryResolutionInfo* query_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out,
      const Type* inferred_type = nullptr);

  // This is the recursive method that resolves expressions.
  // For scalar-only expressions, ResolveScalarExpr can be used instead.
  //
  // It receives an ExprResolutionInfo object specifying whether aggregate
  // and/or analytic functions are allowed (among other properties) and returns
  // information about the resolved expressions in that same object, including
  // whether aggregate or analytic functions are included in the resolved
  // expression.
  //
  // If aggregate and/or analytic functions are allowed, then the
  // parent_expr_resolution_info must have a non-NULL QueryResolutionInfo.
  // Otherwise, the QueryResolutionInfo can be NULL.
  //
  // Note: If the same ExprResolutionInfo is used across multiple calls, the
  // expressions will be resolved correctly, but the output fields (like
  // has_aggregation) in ExprResolutionInfo will be updated based on all
  // expressions resolved so far.
  //
  // The caller can optionally pass in the inferred_type of the expression. This
  // affects expressions that don't specify their own type. The affected
  // expressions:
  //
  // * Braced constructors
  // * Array constructors with proto elements ([...] but not ARRAY<T>[...])
  // * Struct constructors with proto field(s) (STRUCT(...) but not STRUCT<S,
  //   T>(...))
  // * The select list of one-column expression subqueries.
  //
  // The caller should treat inferred_type as a hint because it won't affect
  // expressions that have their own type e.g. ARRAY<T>. The caller should still
  // verify that the expression matches their expected type, adding casts if
  // needed.
  absl::Status ResolveExpr(
      const ASTExpression* ast_expr,
      ExprResolutionInfo* parent_expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out,
      const Type* inferred_type = nullptr);

  // Validates <json_literal> and returns the JSONValue.
  absl::StatusOr<std::unique_ptr<const ResolvedLiteral>> ResolveJsonLiteral(
      const ASTJSONLiteral* json_literal);

  // Resolve a literal expression. Requires ast_expr->node_kind() to be one of
  // AST_*_LITERAL.
  absl::Status ResolveLiteralExpr(
      const ASTExpression* ast_expr,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status MakeResolvedDateOrTimeLiteral(
      const ASTExpression* ast_expr, TypeKind type_kind,
      absl::string_view literal_string_value,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::StatusOr<std::unique_ptr<const ResolvedLiteral>> ParseRangeBoundary(
      const TypeKind& type_kind,
      std::optional<absl::string_view> boundary_value,
      const LanguageOptions& language, absl::TimeZone default_time_zone,
      TypeFactory* type_factory);

  absl::StatusOr<std::unique_ptr<const ResolvedLiteral>> ParseRange(
      const ASTRangeLiteral* range_literal, const RangeType* range_type,
      const LanguageOptions& language, absl::TimeZone default_time_zone,
      TypeFactory* type_factory);

  absl::StatusOr<std::unique_ptr<const ResolvedLiteral>> ResolveRangeLiteral(
      const ASTRangeLiteral* ast_range_literal);

  absl::Status ValidateColumnForAggregateOrAnalyticSupport(
      const ResolvedColumn& resolved_column, IdString first_name,
      const ASTIdentifier* first_identifier,
      ExprResolutionInfo* expr_resolution_info) const;

  // If there is an in-scope function or table function argument with a name
  // matching <first_name> (the first identifier part from a path expression),
  // populates <resolved_expr_out> with a reference to that argument, attach
  // <parse_location> to the node, and increments <num_parts_consumed>.
  // Otherwise, does not modify <resolved_expr_out> or <num_parts_consumed>.
  absl::Status MaybeResolvePathExpressionAsFunctionArgumentRef(
      IdString first_name, const ParseLocationRange& parse_location,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out,
      int* num_parts_consumed);

  absl::Status ResolvePathExpressionAsExpression(
      PathExpressionSpan path_expr, ExprResolutionInfo* expr_resolution_info,
      ResolvedStatement::ObjectAccess access_flags,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveWithExpr(
      const ASTWithExpression* with_expr,
      ExprResolutionInfo* parent_expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out,
      const Type* inferred_type = nullptr);

  absl::Status ResolveModel(
      const ASTPathExpression* path_expr,
      std::unique_ptr<const ResolvedModel>* resolved_model);

  absl::Status ResolveConnection(
      const ASTExpression* path_expr_or_default,
      std::unique_ptr<const ResolvedConnection>* resolved_connection,
      bool is_default_connection_allowed = false);

  absl::Status ResolveConnectionPath(
      const ASTPathExpression* path_expr,
      std::unique_ptr<const ResolvedConnection>* resolved_connection);

  absl::Status ResolveDefaultConnection(
      const ASTDefaultLiteral* default_literal,
      std::unique_ptr<const ResolvedConnection>* resolved_connection);

  absl::Status ResolveSequence(
      const ASTPathExpression* path_expr,
      std::unique_ptr<const ResolvedSequence>* resolved_sequence);

  // Performs first pass analysis on descriptor object. This
  // pass includes preserving descriptor column names in ResolvedDescriptor.
  absl::Status ResolveDescriptorFirstPass(
      const ASTDescriptorColumnList* column_list,
      std::unique_ptr<const ResolvedDescriptor>* resolved_descriptor);

  // This method is used when descriptor objects appear in a TVF call. This
  // method resolves descriptor_column_name_list within <resolved_descriptor>
  // from <name_scope>. <name_scope> provides a namescope for the related input
  // table and populates the descriptor_column_list in <resolved_descriptor>.
  // <name_scope> must never be nullptr.
  // <ast_tvf_argument> and <sig_idx> are used for error messaging.
  absl::Status FinishResolvingDescriptor(
      const ASTTVFArgument* ast_tvf_argument,
      const std::unique_ptr<const NameScope>& name_scope, int sig_idx,
      std::unique_ptr<const ResolvedDescriptor>* resolved_descriptor);

  absl::Status ResolveForSystemTimeExpr(
      const ASTForSystemTime* for_system_time,
      std::unique_ptr<const ResolvedExpr>* resolved);

  // Make the ResolvedTableScan and NameList for scanning `table`.
  //
  // If <read_as_row_type_error_kind> is non-empty and this the table would be
  // read as ROW-typed (because it has a no-default ColumnListMode), give an
  // error, using this string in the error text.
  //
  // <output_name_list> contains the scan column list of the found table and a
  // range variable with <alias> as the name.
  //
  // <output_column_name_list> is nullable; when not null, it contains the scan
  // column list of the found table, but NOT the range variable with <alias> as
  // the name.
  absl::Status MakeScanForTable(
      const ASTNode* ast_location, const Table* table, IdString alias,
      bool has_explicit_alias, const ASTNode* alias_location,
      const ASTForSystemTime* for_system_time,
      absl::string_view read_as_row_type_error_kind,
      std::unique_ptr<ResolvedTableScan>* output_table_scan,
      NameListPtr* output_name_list,
      NameListPtr* /*absl_nullable*/ output_column_name_list,
      ResolvedColumnToCatalogColumnHashMap&
          out_resolved_columns_to_catalog_columns_for_scan);

  // Resolves <path_expr> identified as <alias> as a scan from a table in
  // catalog_ (not from the <scope>). Flag <has_explicit_alias> identifies if
  // the alias was explicitly defined in the query or was computed from the
  // expression. Returns the resulting resolved table scan in <output> and
  // <output_name_list>.
  //
  // <alias> is the alias of the table name. Only when it's explicit alias will
  // it becomes the alias of the resolved table scan.
  //
  // If <read_as_row_type_error_kind> is non-empty and this the table would be
  // read as ROW-typed (because it has a no-default ColumnListMode), give an
  // error, using this string in the error text.
  //
  // When <remaining_names> is not nullptr, we allow the prefix of
  // 'path_expr->names()' as the table names by calling
  // 'FindTableWithPathPrefix()'. After attempting to find the table names with
  // the longest possible length, the remaining part of the path expression will
  // be returned via <remaining_names>.
  // When <remaining_names> is nullptr, we use 'FindTable()' to find table with
  // the whole identifier path as name.
  //
  // <output> is the table scan that corresponds to the first few names in the
  // path expression.
  //
  // <output_name_list> contains the scan column list of the found table and a
  // range variable with <alias> as the name.
  //
  // <output_column_name_list> is nullable; when not null, it contains the scan
  // column list of the found table, but NOT the range variable with <alias> as
  // the name.
  //
  // For example, if 'table_ref->path_expr->ToIdentifierVector()' is
  // {"a", "b", "c", "d"} and <remaining_names> is non-NULL, we will do
  //     catalog_->FindTableWithPathPrefix({"a", "b", "c", "d"}, ...)
  // If table can be found in the catalog and the longest matched table name
  // path is {"a", "b"}, <remaining_names> will be set to {"b", "c", "d"}.
  absl::Status ResolvePathExpressionAsTableScan(
      const ASTPathExpression* path_expr, IdString alias,
      bool has_explicit_alias, const ASTNode* alias_location,
      const ASTHint* hints, const ASTForSystemTime* for_system_time,
      const NameScope* scope, const std::string& read_as_row_type_error_kind,
      std::unique_ptr<PathExpressionSpan>* remaining_names,
      std::unique_ptr<const ResolvedTableScan>* output,
      std::shared_ptr<const NameList>* output_name_list,
      NameListPtr* /*absl_nullable*/ output_column_name_list,
      ResolvedColumnToCatalogColumnHashMap&
          out_resolved_columns_to_catalog_columns_for_scan);

  // Resolves a path expression to a Type.  If <is_single_identifier> then
  // the path expression is treated as a single (quoted) identifier. Otherwise
  // it is treated as a nested (catalog) path expression.
  absl::Status ResolvePathExpressionAsType(const ASTPathExpression* path_expr,
                                           bool is_single_identifier,
                                           const Type** resolved_type) const;

  absl::Status ResolveParameterExpr(
      const ASTParameterExpr* param_expr,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveDotIdentifier(
      const ASTDotIdentifier* dot_identifier,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  // Options to be used when attempting to resolve a proto field access.
  struct MaybeResolveProtoFieldOptions {
    // If true, an error will be returned if the field is not found. If false,
    // then instead of returning an error on field not found, returns OK with a
    // NULL <resolved_expr_out>.
    bool error_if_not_found = true;

    // If <get_has_bit_override> has a value, then the get_has_bit field of the
    // ResolvedProtoField related to <identifier> will be set to this
    // value (without determining if the <identifier> name might be ambiguous).
    // If <get_has_bit_override> does not contain a value, <identifier> will be
    // inspected to determine the field being accessed.
    std::optional<bool> get_has_bit_override;

    // If true, then any FieldFormat.Format annotations on the field to extract
    // will be ignored. Note that this can change NULL behavior, because for
    // some types (e.g., DATE_DECIMAL), the value 0 decodes to NULL when the
    // annotation is applied. If the field to extract is not a primitive type,
    // the default value of the ResolvedGetProtoField will be NULL.
    bool ignore_format_annotations = false;
  };

  // Try to resolve a proto field access with the options specified by
  // <options>. <resolved_lhs> must have Proto type. On success, <resolved_lhs>
  // will be reset.
  absl::Status MaybeResolveProtoFieldAccess(
      const ParseLocationRange& parse_location, const ASTIdentifier* identifier,
      const MaybeResolveProtoFieldOptions& options,
      std::unique_ptr<const ResolvedExpr> resolved_lhs,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  // Try to resolve struct field access.  <resolved_lhs> must have Struct type.
  // If <error_if_not_found> is false, then instead of returning an error
  // on field not found, returns OK with a NULL <resolved_expr_out>.
  // On success, <resolved_lhs> will be reset.
  absl::Status MaybeResolveStructFieldAccess(
      const ParseLocationRange& parse_location, const ASTIdentifier* identifier,
      bool error_if_not_found, std::unique_ptr<const ResolvedExpr> resolved_lhs,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  // Resolves JSON field access.  <resolved_lhs> must have JSON type.
  // On success, <resolved_lhs> will be reset.
  absl::Status ResolveJsonFieldAccess(
      const ASTIdentifier* identifier,
      std::unique_ptr<const ResolvedExpr> resolved_lhs,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  // Resolve field access on a ROW type to a ResolvedGetRowField.
  absl::Status ResolveGetRowField(
      const ASTIdentifier* identifier, FlattenState* flatten_state,
      std::unique_ptr<const ResolvedExpr> resolved_lhs,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveFieldAccess(
      std::unique_ptr<const ResolvedExpr> resolved_lhs,
      const ParseLocationRange& parse_location, const ASTIdentifier* identifier,
      FlattenState* flatten_state,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  // Resolves a PROTO_DEFAULT_IF_NULL function call to a ResolvedGetProtoField
  // returned in <resolved_expr_out>. <resolved_arguments> must contain a single
  // ResolvedGetProtoField expression representing a non-message proto field
  // access, where the accessed field is not annotated with
  // googlesql.use_defaults=false. Element in <resolved_arguments> is
  // transferred to <resolved_expr_out>.
  absl::Status ResolveProtoDefaultIfNull(
      const ASTNode* ast_location,
      std::vector<std::unique_ptr<const ResolvedExpr>> resolved_arguments,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  struct ResolveExtensionFieldOptions {
    // Indicates whether the returned ResolvedGetProtoField denotes extraction
    // of the field's value or a bool indicating whether the field has been set.
    bool get_has_bit = false;

    // If true, then any FieldFormat.Format annotations on the extension to
    // extract will be ignored. Note that this can change NULL behavior, because
    // for some types (e.g., DATE_DECIMAL), the value 0 decodes to NULL when the
    // annotation is applied. If the extension to extract is not a primitive
    // type, the default value of the ResolvedGetProtoField will be NULL.
    bool ignore_format_annotations = false;
  };
  absl::Status ResolveExtensionFieldAccess(
      std::unique_ptr<const ResolvedExpr> resolved_lhs,
      const ResolveExtensionFieldOptions& options,
      const ASTPathExpression* ast_path_expr, FlattenState* flatten_state,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveOneofCase(
      const ASTIdentifier* oneof_identifier,
      std::unique_ptr<const ResolvedExpr> resolved_lhs,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveDotGeneralizedField(
      const ASTDotGeneralizedField* dot_generalized_field,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveReplaceFieldsExpression(
      const ASTReplaceFieldsExpression* ast_replace_fields,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveSystemVariableExpression(
      const ASTSystemVariableExpr* ast_system_variable_expr,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveUnaryExpr(
      const ASTUnaryExpression* unary_expr,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveBinaryExpr(
      const ASTBinaryExpression* binary_expr,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveBitwiseShiftExpr(
      const ASTBitwiseShiftExpression* bitwise_shift_expr,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveInExpr(
      const ASTInExpression* in_expr, ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveInSubquery(
      const ASTInExpression* in_subquery_expr,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveLikeExpr(
      const ASTLikeExpression* like_expr,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveLikeExprArray(
      const ASTLikeExpression* like_expr,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveLikeExprList(
      const ASTLikeExpression* like_expr,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveLikeExprSubquery(
      const ASTLikeExpression* like_subquery_expr,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  // Resolve quantified comparisons.  These are expressions with a comparison
  // operator (=, <, etc) and a quantifier (ANY, SOME, or ALL).  For example:
  //
  //   val = ANY (SELECT ...)
  //   val > ALL (1,2,3)
  //   val != SOME UNNEST(some_array)
  //
  // See (broken link) for more details.
  absl::Status ResolveQuantifiedComparisonExpr(
      const ASTQuantifiedComparisonExpression* cmp_expr,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveQuantifiedComparisonExprArray(
      const ASTNode* op_location, absl::string_view function_name,
      const ASTExpression* lhs_expr, const ASTUnnestExpression* unnest_expr,
      absl::string_view comparison_description,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveBetweenExpr(
      const ASTBetweenExpression* between_expr,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveAndExpr(
      const ASTAndExpr* and_expr, ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveOrExpr(
      const ASTOrExpr* or_expr, ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveExprSubquery(
      const ASTExpressionSubquery* expr_subquery,
      ExprResolutionInfo* expr_resolution_info, const Type* inferred_type,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveFunctionCall(
      const ASTFunctionCall* ast_function,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> WrapInASideEffectCall(
      const ASTNode* ast_location, std::unique_ptr<const ResolvedExpr> expr,
      const ResolvedColumn& side_effect_column,
      ExprResolutionInfo& expr_resolution_info);

  absl::Status AddColumnToGroupingListSecondPass(
      const ASTFunctionCall* ast_function,
      const ResolvedAggregateFunctionCall* agg_function_call,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<ResolvedColumn>* resolved_column_out);

  absl::Status AddColumnToGroupingListFirstPass(
      const ASTFunctionCall* ast_function,
      std::unique_ptr<const ResolvedAggregateFunctionCall> agg_function_call,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<ResolvedColumn>* resolved_column_out);

  absl::Status ResolveFilterFieldsFunctionCall(
      const ASTFunctionCall* ast_function,
      const std::vector<const ASTExpression*>& function_arguments,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  // Resolve a GROUP BY modifier for a multi-level aggregate function.
  absl::Status ResolveGroupingItem(
      const ASTGroupingItem* group_by_modifier,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_group_by);

  // Resolve aggregate function for the first pass. This function also resolves
  // GROUP_ROWS clause if it is present.
  absl::Status ResolveAggregateFunctionCallFirstPass(
      const ASTFunctionCall* ast_function, const Function* function,
      ResolvedFunctionCallBase::ErrorMode error_mode,
      const std::vector<const ASTExpression*>& function_arguments,
      const std::map<int, SpecialArgumentType>& argument_option_map,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveAnalyticFunctionCall(
      const ASTAnalyticFunctionCall* analytic_function_call,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  // Populates <resolved_date_part> with a ResolvedLiteral that wraps a literal
  // Value of EnumType(functions::DateTimestampPart) corresponding to
  // <date_part_name> and <date_part_arg_name>. If <date_part> is not null, sets
  // it to the resolved date part. <date_part_arg_name> must be empty if and
  // only if 'date_part_arg_ast_location is NULL.
  absl::Status MakeDatePartEnumResolvedLiteralFromNames(
      IdString date_part_name, IdString date_part_arg_name,
      const ASTExpression* date_part_ast_location,
      const ASTExpression* date_part_arg_ast_location,
      std::unique_ptr<const ResolvedExpr>* resolved_date_part,
      functions::DateTimestampPart* date_part);

  absl::Status MakeDatePartEnumResolvedLiteral(
      functions::DateTimestampPart date_part,
      std::unique_ptr<const ResolvedExpr>* resolved_date_part);

  // We do not attempt to coerce string/bytes literals to protos in the
  // analyzer. Instead, put a ResolvedCast node in the resolved AST and let the
  // engines do it. There are at least two reasons for this:
  // 1) We might not have the appropriate proto descriptor here (particularly
  //    for extensions).
  // 2) Since semantics of proto casting are not fully specified, applying these
  //    casts at analysis time may have inconsistent behavior with applying them
  //    at run-time in the engine. (One example is with garbage values, where
  //    engines may not be prepared for garbage proto Values in the resolved
  //    AST.)
  //
  // Otherwise, we attempt folding. If an error-occurs, we ignore the results
  // and defer evaluation to runtime.
  absl::StatusOr<bool> ShouldTryCastConstantFold(
      const ResolvedExpr* resolved_argument, const Type* resolved_cast_type);

  // Checks whether explicit cast of the <resolved_argument> to the type
  // <to_type> is possible. CheckExplicitCast can return a status that is
  // different from Ok if it gets such error status from a Catalog's
  // FindConversion method or if a Catalog returns a conversion that breaks some
  // of Coercer invariants. If this happens Resolver should abort a resolution
  // request by returning the error status. If cast involves extended types the
  // function for such extended conversion is returned in
  // <extended_type_conversion> argument.
  absl::StatusOr<bool> CheckExplicitCast(
      const ResolvedExpr* resolved_argument, const Type* to_type,
      ExtendedCompositeCastEvaluator* extended_conversion_evaluator);

  absl::Status ResolveExplicitCast(
      const ASTCastExpression* cast, ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  // Resolves the format clause used in a CAST expression. On success,
  // <resolved_format> contains the resolved format expression, and
  // <resolved_time_zone> contains the resolved time zone expression.
  // If <resolve_cast_to_null> is true, it means that the CAST expression should
  // be resolved to a null expression. For example, this happens when the cast
  // is a safe cast, and the format string is invalid.
  absl::Status ResolveFormatClause(
      const ASTCastExpression* cast, ExprResolutionInfo* expr_resolution_info,
      const std::unique_ptr<const ResolvedExpr>& resolved_argument,
      const Type* resolved_cast_type,
      std::unique_ptr<const ResolvedExpr>* resolved_format,
      std::unique_ptr<const ResolvedExpr>* resolved_time_zone,
      bool* resolve_cast_to_null);

  // Resolves the format or the time zone expression in the format clause in a
  // CAST expression. On success, <resolved_expr> contains the resolved
  // expression. The type of the expression is checked, and if it is not a
  // string, returns an error. <clause_name> is used for formatting error
  // messages.
  absl::Status ResolveFormatOrTimeZoneExpr(
      const ASTExpression* expr, ExprResolutionInfo* expr_resolution_info,
      const char* clause_name,
      std::unique_ptr<const ResolvedExpr>* resolved_expr);

  // Resolves a cast from <resolved_argument> to <to_type>.  If the
  // argument is a NULL literal, then converts it to the target type and
  // updates <resolved_argument> with a NULL ResolvedLiteral of the target
  // type.  Otherwise, wraps <resolved_argument> with a new ResolvedCast whose
  // <type_annotation_map> is nullptr. <return_null_on_error> indicates
  // whether the cast should return a NULL value of the <target_type> in case of
  // failures.
  absl::Status ResolveCastWithResolvedArgument(
      const ASTNode* ast_location, const Type* to_type,
      bool return_null_on_error,
      std::unique_ptr<const ResolvedExpr>* resolved_argument);

  // Same as the previous method, but includes <format>, <time_zone> and
  // <type_modifiers>. <to_annotated_type> is used to contain both type and its
  // annotation information. If <format> is specified, it is used as the format
  // string for the cast.
  absl::Status ResolveCastWithResolvedArgument(
      const ASTNode* ast_location, AnnotatedType to_annotated_type,
      std::unique_ptr<const ResolvedExpr> format,
      std::unique_ptr<const ResolvedExpr> time_zone,
      TypeModifiers type_modifiers, bool return_null_on_error,
      std::unique_ptr<const ResolvedExpr>* resolved_argument);

  absl::Status ResolveArrayElement(
      const ASTArrayElement* array_element,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);
  // Function names returned by ResolveArrayElement().
  static const char kArrayAtOffset[];
  static const char kArrayAtOrdinal[];
  static const char kProtoMapAtKey[];
  static const char kSafeArrayAtOffset[];
  static const char kSafeArrayAtOrdinal[];
  static const char kSafeProtoMapAtKey[];

  // Function names returned by ResolveNonArrayElement().
  static const char kSubscript[];
  static const char kSubscriptWithOffset[];
  static const char kSafeSubscriptWithOffset[];
  static const char kSubscriptWithOrdinal[];
  static const char kSafeSubscriptWithOrdinal[];
  static const char kSubscriptWithKey[];
  static const char kSafeSubscriptWithKey[];

  // Resolves subscript([]) operator for non-array type.
  // Depending on the argument the subscript operator can be resolved to one of
  // the following functions:
  //
  // $subscript_with_key: when wrapper keyword is KEY or SAFE_KEY,
  // $subscript_with_offset: when wrapper keyword is OFFSET or SAFE_OFFSET,
  // $subscript_with_ordinal: when wrapper keyword is ORDINAL or SAFE_ORDINAL
  // $subscript: no wrapper keywords as mentioned in the above cases.
  //
  // With SAFE_* prefix the resolved function_name_path will contain
  // "SAFE" as first element indicating the function runs under SAFE_ERROR_MODE
  // rather than DEFAULT_ERROR_MODE.
  //
  // To be consist with the pointer vs. reference usage in this file, but this
  // function requires all the pointers to be not nullptr.
  absl::Status ResolveNonArraySubscriptElementAccess(
      const ResolvedExpr* resolved_lhs, const ASTExpression* ast_position,
      ExprResolutionInfo* expr_resolution_info,
      std::vector<std::string>* function_name_path,
      const ASTExpression** unwrapped_ast_position_expr,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out,
      std::string* original_wrapper_name);

  // Requires that <resolved_array> is an array and verifies that
  // <ast_position> is an appropriate array element function call (e.g., to
  // OFFSET) and populates <function_name> and <unwrapped_ast_position_expr>
  // accordingly. Also resolves <unwrapped_ast_position_expr> into
  // <resolved_expr_out> and coerces it to the correct type if necessary. For
  // most arrays, this will be an INT64, but for proto maps, it will be the key
  // type of the map. <original_wrapper_name> is populated to corresponding
  // wrapper value of subscript operator.
  absl::Status ResolveArrayElementAccess(
      const ResolvedExpr* resolved_array, const ASTExpression* ast_position,
      ExprResolutionInfo* expr_resolution_info,
      absl::string_view* function_name,
      const ASTExpression** unwrapped_ast_position_expr,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out,
      std::string* original_wrapper_name);

  // Requires that <resolved_struct> is a struct and verifies that
  // <field_position> is an appropriate literal integer (wrapped in a call to
  // OFFSET for example). Resolves <ast_position> into the field_idx of the
  // ResolvedGetStructField populated into <resolved_expr_out>.
  absl::Status ResolveStructSubscriptElementAccess(
      std::unique_ptr<const ResolvedExpr> resolved_struct,
      const ASTExpression* field_position,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveCaseNoValueExpression(
      const ASTCaseNoValueExpression* case_no_value,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveCaseValueExpression(
      const ASTCaseValueExpression* case_value,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveAssertRowsModified(
      const ASTAssertRowsModified* ast_node,
      std::unique_ptr<const ResolvedAssertRowsModified>* output);

  absl::Status ResolveReturningClause(
      const ASTReturningClause* ast_node, IdString target_alias,
      const std::shared_ptr<const NameList>& from_clause_name_list,
      const NameScope* from_scan_scope,
      std::unique_ptr<const ResolvedReturningClause>* output);

  absl::Status ResolveOnConflictClause(
      const ASTPathExpression* insert_target_path,
      const ASTOnConflictClause* ast_node,
      const ResolvedTableScan* target_table_scan,
      const std::shared_ptr<const NameList>& target_name_list,
      std::unique_ptr<const ResolvedOnConflictClause>* output);

  // Populates generate column topological order related informations for DML.
  // This is later used to evaluate the updated generated columns in order.
  absl::Status ResolveGeneratedColumnsForDml(
      const Table* target_table,
      const ResolvedColumnToCatalogColumnHashMap&
          resolved_columns_to_catalog_columns_for_target_scan,
      std::vector<int>* out_topologically_sorted_generated_column_ids,
      std::vector<std::unique_ptr<const ResolvedExpr>>*
          out_generated_column_expr_list);

  // Resolves the HAVING modifier of an aggregate function. The HAVING
  // modifier is distinct from the HAVING {MAX|MIN} modifier. A HAVING
  // modifier requires a GROUP BY modifier be present and is resolved to a
  // general `ResolvedExpr`.
  absl::Status ResolveAggregateFunctionHavingModifier(
      const ASTFunctionCall* ast_function_call,
      const ResolvedFunctionCall* resolved_function_call,
      ExprResolutionInfo* expr_resolution_info,
      QueryResolutionInfo& multi_level_aggregate_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveAggregateFunctionOrderByModifiers(
      const ASTFunctionCall* ast_function_call,
      std::unique_ptr<ResolvedFunctionCall>* resolved_function_call,
      ExprResolutionInfo* expr_resolution_info,
      QueryResolutionInfo& multi_level_aggregate_info,
      std::vector<std::unique_ptr<const ResolvedComputedColumn>>*
          computed_columns_for_horizontal_aggregation,
      std::vector<std::unique_ptr<const ResolvedOrderByItem>>*
          resolved_order_by_items);

  absl::Status ResolveAggregateFunctionLimitModifier(
      const ASTFunctionCall* ast_function_call, const Function* function,
      std::unique_ptr<const ResolvedExpr>* limit_expr);

  absl::Status FinishResolvingAggregateFunction(
      const ASTFunctionCall* ast_function_call,
      std::unique_ptr<ResolvedFunctionCall>* resolved_function_call,
      ExprResolutionInfo* expr_resolution_info,
      // `multi_level_aggregate_info` is needed to resolve ORDER BY modifiers
      // for multi-level aggregate functions.
      std::unique_ptr<QueryResolutionInfo> multi_level_aggregate_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out,
      std::optional<ResolvedColumn>& out_unconsumed_side_effect_column);

  absl::Status ResolveExtractExpression(
      const ASTExtractExpression* extract_expression,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveNewConstructor(
      const ASTNewConstructor* ast_new_constructor,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  // A container that has state about the 'lhs' part of a braced constructor
  // field.
  struct BracedConstructorField {
    ASTBracedConstructorLhs::Operation operation;
    const ASTNode* location;
    FindFieldsOutput field_info;
  };

  // Resolves the 'lhs' of a braced constructor field and returns a container
  // with state.
  absl::StatusOr<BracedConstructorField> ResolveBracedConstructorLhs(
      const ASTBracedConstructorLhs* ast_braced_constructor_lhs,
      const ProtoType* parent_type);

  // Resolves a braced constructor field and returns a container that has state
  // to build the protocol buffer field.
  absl::StatusOr<ResolvedBuildProtoArg> ResolveBracedConstructorField(
      const ASTBracedConstructorField* ast_braced_constructor_field,
      const ProtoType* parent_type, int field_index, bool allow_field_paths,
      const ResolvedExpr* update_constructor_expr_to_modify,
      ExprResolutionInfo* expr_resolution_info);

  // Resolves a generalized path access from an already resolved base protocol
  // buffer expression.
  // `resolved_base`: The base protocol buffer from which we try to resolve a
  // path access.
  // `parse_location`: The parse location to use for error reporting.
  // `generalized_path`: The path access that needs to be resolved.
  // `flatten_state`: The path access that needs to be resolved.
  //
  // Returns the resolved proto expression as output.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> ResolveProtoFieldAccess(
      const ResolvedExpr& resolved_base,
      const ParseLocationRange& parse_location,
      const ASTGeneralizedPathExpression& generalized_path,
      FlattenState* flatten_state);

  absl::Status ResolveBracedConstructor(
      const ASTBracedConstructor* ast_braced_constructor,
      const Type* inferred_type, ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveBracedNewConstructor(
      const ASTBracedNewConstructor* ast_braced_new_constructor,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveStructBracedConstructor(
      const ASTStructBracedConstructor* ast_struct_braced_constructor,
      const Type* inferred_type, ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveBracedConstructorForStruct(
      const ASTBracedConstructor* ast_braced_constructor, bool is_bare_struct,
      const ASTNode* expression_location_node,
      const ASTStructType* ast_struct_type, const Type* inferred_type,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveBracedConstructorForProto(
      const ASTBracedConstructor* ast_braced_constructor,
      const Type* inferred_type, ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  // Resolves an ASTBracedConstructor that should be interpreted as an UPDATE
  // constructor. This includes the top-level ASTBracedConstructor under an
  // ASTUpdateConstructor, as well as braced constructors that are values of the
  // immediate fields being updated by another UPDATE constructor.
  //
  // For example:
  //   UPDATE (x) {
  //     a: 3
  //     b { c: 4 }
  //   }
  // Is interpreted as:
  //   UPDATE (x) {
  //     a: 3
  //     b: UPDATE(x.b) { c: 4 }
  //   }
  // Where both braced constructors are resolved with this function.
  //
  // However, given an input like this:
  //   UPDATE (x) {
  //     a: [{c: 4}]
  //   }
  // The outer braced constructor is resolved with this function, however the
  // inner braced constructor is not. Details: http://shortn/_EXoCKUiBhJ.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
  ResolveBracedConstructorInUpdateContext(
      const ASTNode& location,
      std::unique_ptr<const ResolvedExpr> expr_to_modify, std::string alias,
      const ASTBracedConstructor& ast_braced_constructor,
      ExprResolutionInfo* expr_resolution_info);

  absl::Status ResolveUpdateConstructor(
      const ASTUpdateConstructor& ast_update_constructor,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveArrayConstructor(
      const ASTArrayConstructor* ast_array_constructor,
      const Type* inferred_type, ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveStructConstructorWithParens(
      const ASTStructConstructorWithParens* ast_struct_constructor,
      const Type* inferred_type, ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveStructConstructorWithKeyword(
      const ASTStructConstructorWithKeyword* ast_struct_constructor,
      const Type* inferred_type, ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  // When resolving a STRUCT constructor expression, we generally try
  // to resolve it to a STRUCT literal where possible.  If all of the
  // fields are themselves literals, then we resolve this expression
  // to a STRUCT literal unless the STRUCT was not explicitly typed
  // (<ast_struct_type> is nullptr) and either 1) there is an untyped
  // NULL field, or 2) some fields have explicit types and others do
  // not.
  // The resulting STRUCT literal will be marked with has_explicit_type
  // if <ast_struct_type> is non-null or all of its fields were
  // has_explicit_type.
  //
  // Examples of expressions that resolve to STRUCT literals:
  // 1) CAST(NULL AS STRUCT<INT32>)              - has_explicit_type = true
  // 2) CAST((1, 2) AS STRUCT<INT32, INT64>)     - has_explicit_type = true
  // 3) STRUCT<INT64>(4)                         - has_explicit_type = true
  // 4) (1, 2, 3)                                - has_explicit_type = false
  // 5) (cast(1 as int64), cast (2 as int32))    - has_explicit_type = true
  // 6) (cast(null as int64), cast (2 as int32)) - has_explicit_type = true
  //
  // Examples of expressions that do not resolve to STRUCT literals:
  // 1) (1, NULL)             - one field is untyped null
  // 2) (1, CAST(3 as INT64)) - fields have different has_explicit_type
  absl::Status ResolveStructConstructorImpl(
      const ASTNode* ast_location, const ASTStructType* ast_struct_type,
      absl::Span<const ASTExpression* const> ast_field_expressions,
      absl::Span<const ASTIdentifier* const> ast_field_identifiers,
      const Type* inferred_type, bool require_name_match,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  // If <date_part> is not null, sets it to the resolved date part.
  absl::Status ResolveDatePartArgument(
      const ASTExpression* date_part_ast_location,
      std::unique_ptr<const ResolvedExpr>* resolved_date_part,
      functions::DateTimestampPart* date_part = nullptr);

  // Defines the accessors that can be used in the EXTRACT function with proto
  // input (e.g. EXTRACT(FIELD(x) from y) where y is a message that defines a
  // field x)
  enum class ProtoExtractionType {
    // HAS determines if a particular field is set in its containing message.
    kHas,

    // FIELD extracts the value of a field from its containing message.
    kField,

    // RAW extracts the value of a field from its containing message without
    // taking any type annotations into consideration. If
    // the field is missing then the field's default value is returned. For
    // message fields, the default value is NULL. If the containing message is
    // NULL, NULL is returned.
    kRaw,

    // ONEOF_CASE returns a string value indicating which field of a given
    // OneOf is set. If none of the fields are set, an empty string is returned.
    kOneofCase,
  };

  // Parses <extraction_type_name> and returns the corresponding
  // ProtoExtractionType. An error is returned when the input does not parse to
  // a valid ProtoExtractionType.
  static absl::StatusOr<Resolver::ProtoExtractionType>
  ProtoExtractionTypeFromName(absl::string_view extraction_type_name);

  // Returns the string name of the ProtoExtractionType corresponding to
  // <extraction_type>.
  static std::string ProtoExtractionTypeName(
      ProtoExtractionType extraction_type);

  // Resolves an EXTRACT(ACCESSOR(field) FROM proto) call.
  // <field_extraction_type_ast_location> is the ASTNode denoting the
  // ACCESSOR(field) expression. <resolved_proto_input> is the resolved proto
  // to be extracted from. The resultant resolved AST is returned in
  // <resolved_expr_out>.
  absl::Status ResolveProtoExtractExpression(
      const ASTExpression* field_extraction_type_ast_location,
      std::unique_ptr<const ResolvedExpr> resolved_proto_input,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveProtoExtractWithExtractTypeAndField(
      ProtoExtractionType field_extraction_type,
      const ASTPathExpression* field_path,
      std::unique_ptr<const ResolvedExpr> resolved_proto_input,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  // Resolves the normalize mode represented by <arg> and sets it to the
  // <resolved_expr_out>.
  absl::Status ResolveNormalizeModeArgument(
      const ASTExpression* arg,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveIntervalArgument(
      const ASTExpression* arg, ExprResolutionInfo* expr_resolution_info,
      std::vector<std::unique_ptr<const ResolvedExpr>>* resolved_arguments_out,
      std::vector<const ASTNode*>* ast_arguments_out);

  // Resolves interval expressions:
  // Literal:     INTERVAL '<literal>' <date_part> [ TO <date_part2>]
  // Constructor: INTERVAL <int64_expr> <date_part>
  absl::Status ResolveIntervalExpr(
      const ASTIntervalExpr* interval_expr,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  // Resolves AST identifier as DateTimestampPart
  absl::StatusOr<functions::DateTimestampPart> ResolveDateTimestampPart(
      const ASTIdentifier* date_part_identifier);

  absl::Status ResolveInsertValuesRow(
      const ASTInsertValuesRow* ast_insert_values_row, const NameScope* scope,
      const ResolvedColumnList& insert_columns,
      std::unique_ptr<const ResolvedInsertRow>* output);

  // Resolves the insert row by referencing all columns of <value_columns>.
  absl::Status ResolveInsertValuesRow(
      const ASTNode* ast_location, const ResolvedColumnList& value_columns,
      const ResolvedColumnList& insert_columns,
      std::unique_ptr<const ResolvedInsertRow>* output);

  // <nested_scope> is NULL for a non-nested INSERT. For a nested INSERT,
  // populates <parameter_list> with any columns in <nested_scope> (whose
  // topmost scope is always the empty scope) that are referenced by <output>.
  // For non-pipe INSERT, `query` is present.
  // For pipe INSERT, `query` is NULL, and `pipe_input_name_list` and
  // `pipe_input_scan` provide the pipe input table.
  absl::Status ResolveInsertQuery(
      const ASTNode* ast_location, const ASTQuery* query,
      const NameScope* nested_scope, const ResolvedColumnList& insert_columns,
      const NameList* pipe_input_name_list,
      std::unique_ptr<const ResolvedScan> pipe_input_scan,
      std::unique_ptr<const ResolvedScan>* output,
      ResolvedColumnList* output_column_list,
      std::vector<std::unique_ptr<const ResolvedColumnRef>>* parameter_list);

  // Resolve an expression for a DML INSERT or UPDATE targeted at a column
  // with <annotated_target_type>.  Adds a cast if necessary and possible.  If a
  // cast is impossible, this call returns OK without adding a cast, and relies
  // on the caller to check if the expression type Equals the column type. (The
  // caller can give better error messages with more context.)
  absl::Status ResolveDMLValue(const ASTExpression* ast_value,
                               AnnotatedType annotated_target_type,
                               const NameScope* scope, const char* clause_name,
                               CoercionErrorMessageFunction coercion_err_msg,
                               std::unique_ptr<const ResolvedDMLValue>* output);

  // Similar to above ResolveDMLValue(), but is used by INSERT clause of MERGE,
  // when the value list is omitted by using INSERT ROW. The <referenced_column>
  // is the resolved column from source.
  absl::Status ResolveDMLValue(const ASTNode* ast_location,
                               const ResolvedColumn& referenced_column,
                               AnnotatedType annotated_target_type,
                               CoercionErrorMessageFunction coercion_err_msg,
                               std::unique_ptr<const ResolvedDMLValue>* output);

  // Resolves the given update items corresponding to an UPDATE statement. The
  // function uses two name scopes: <target_scope> is used to resolve names that
  // appear as targets in the SET clause and come from the target table;
  // <update_scope> includes all names that can appear inside expressions in the
  // UPDATE statement, including in the WHERE clause and the right hand side of
  // assignments.
  absl::Status ResolveUpdateItemList(
      const ASTUpdateItemList* ast_update_item_list, bool is_nested,
      const NameScope* target_scope, const NameScope* update_scope,
      std::vector<std::unique_ptr<const ResolvedUpdateItem>>* update_item_list);

  // Stores information about one of the highest-level ResolvedUpdateItem
  // nodes corresponding to an ASTUpdateItemList.
  struct UpdateItemAndLocation {
    std::unique_ptr<ResolvedUpdateItem> resolved_update_item;

    // The target path of one of the ASTUpdateItems corresponding to
    // <resolved_update_item>. (All of those target paths are all the same
    // unless <resolved_update_item> modifies an array element.) Not owned.
    const ASTGeneralizedPathExpression* one_target_path = nullptr;
  };

  // Merges <ast_update_item> with an existing ResolvedUpdateItem in
  // <update_items> if possible. Otherwise, appends a new ResolvedUpdateItem to
  // <update_items>.
  absl::Status ResolveUpdateItem(
      const ASTUpdateItem* ast_update_item, bool is_nested,
      const NameScope* target_scope, const NameScope* update_scope,
      std::vector<UpdateItemAndLocation>* update_items);

  // Target information for one of the (to be created) ResolvedUpdateItem nodes
  // in a path of ResolvedUpdateItem->ResolvedUpdateItemElement->
  // ResolvedUpdateItem->ResolvedUpdateItemElement->...->ResolvedUpdateItem path
  // corresponding to a particular ASTUpdateItem.
  struct UpdateTargetInfo {
    std::unique_ptr<const ResolvedExpr> target;

    // The following fields are only non-NULL if the ResolvedUpdateItem
    // corresponds to an array element modification (i.e., it is not the last
    // ResolvedUpdateItem on the path).

    // A new column for the element that is being modified. This is the
    // element given by evaluating subscript_expr.
    std::unique_ptr<const ResolvedColumn> subscripted_element;

    // Represents the subscript expression that accesses the item being
    // modified.
    std::unique_ptr<const ResolvedExpr> subscript_expr;

    // The element presence constraint for the subscript expression.
    ResolvedUpdateItemElementEnums::UpdateItemMode element_presence_constraint =
        ResolvedUpdateItemElementEnums::ELEMENT_PRESENCE_UNSPECIFIED;

    // A ResolvedColumnRef referencing the ResolvedColumn stored in
    // `subscripted_element`, to be used in a child UpdateTargetInfo as the
    // `target` field
    ResolvedColumnRef* subscripted_element_ref = nullptr;  // Not owned.
  };

  // Check if the new `update_target_infos` overlaps with any element of the
  // existing `resolved_update_item`. This only determines if the subscripts
  // overlap if `subscript_expr` are literals.
  absl::StatusOr<bool> AreOverlappingUpdateItemsWithSubscript(
      absl::Span<const UpdateTargetInfo> update_target_infos,
      const ResolvedUpdateItem* resolved_update_item, bool is_subscript_update);

  // Returns whether the update target type supports element updates, given the
  // provided language options.
  absl::Status VerifyIfElementUpdateIsAllowed(
      const Type* update_target_type,
      googlesql::LanguageOptions language_options,
      const ASTArrayElement* array_element);

  // Populates <update_target_infos> according to the ResolvedUpdateItem nodes
  // to create for the 'path' portion of <ast_update_item>. The elements of
  // <update_target_infos> are sorted in root-to-leaf order of the corresponding
  // ResolvedUpdateItem nodes. For example, for
  // a.b[<expr1>].c[<expr2>].d.e.f[<expr3>].g, we end up with 4
  // UpdateTargetInfos, corresponding to
  // - a.b[<expr1>] with <array_element_column> = x1,
  // - x1.c[<expr2>] with <array_element_column> = x2,
  // - x2.d.e.f[<expr3>] with <array_element_column> = x3
  // - x3.g
  //
  // If the item ends in an array element, the last UpdateTargetInfo will
  // contain the `subscripted_element_ref`. For example
  // a.b[<expr1>].c[<expr2>]
  // - a.b[<expr1>] with <array_element_column> = x1,
  // - x1.c[<expr2>] with <array_element_column> = x2,
  // - x2
  absl::Status PopulateUpdateTargetInfos(
      const ASTUpdateItem* ast_update_item, bool is_nested,
      const ASTGeneralizedPathExpression* path,
      ExprResolutionInfo* expr_resolution_info,
      std::vector<UpdateTargetInfo>* update_target_infos);

  // Verifies that `target` (which must correspond to the first UpdateTargetInfo
  // returned by PopulateUpdateTargetInfos() for a non-nested ASTUpdateItem) is
  // writable using Column::IsWritableColumn(). If the column is not writable
  // and `value` is the DEFAULT token, then `target` is also checked using
  // Column::CanUpdateUnwritableToDefault(). If the column can be updated to
  // default then `target` is considered writable.
  absl::Status VerifyUpdateTargetIsWritable(
      const ASTNode* ast_location, const ResolvedExpr* target,
      const ASTExpression* value = nullptr);

  // Returns whether the column is writable.
  absl::StatusOr<bool> IsColumnWritable(const ResolvedColumn& column);

  // Verifies that `column` is writable by looking into
  // `resolved_columns_from_table_scans_` for the corresponding catalog::Column
  // and checking VerifyUpdateTargetIsWritable().
  absl::Status VerifyTableScanColumnIsWritable(
      const ASTNode* ast_location, const ResolvedColumn& column,
      const char* statement_type, const ASTExpression* value = nullptr);

  // Determines if <ast_update_item> should share the same ResolvedUpdateItem as
  // <update_item>.  Sets <merge> to true if they have the same target. Sets
  // <merge> to false if they have different, non-overlapping targets. Returns
  // an error if they have overlapping or conflicting targets, or if
  // <ast_update_item> violates the nested dml ordering
  // rules. <update_target_infos> is the output of PopulateUpdateTargetInfos()
  // corresponding to <ast_update_item>.
  absl::Status ShouldMergeWithUpdateItem(
      const ASTUpdateItem* ast_update_item,
      absl::Span<const UpdateTargetInfo> update_target_infos,
      const UpdateItemAndLocation& update_item, bool* merge);

  // Creates a `ResolvedUpdateItemElement` node
  // structure at <update_item_element> representing the subscripted path given
  // by <update_target_infos>. Modifies <update_target_infos> and
  // <deepest_new_resolved_update_item> in place.
  //
  // - <update_target_infos> is a vector of UpdateTargetInfos representing the
  //   subscripted path. It is traversed and modified such that the target and
  //   subscript_expr fields of each `UpdateTargetInfo` element are moved into
  //   the created `ResolvedUpdateItem`, except for the 0th `UpdateTargetInfo`
  //   element's `target` field, which should be moved by the caller.
  // - <deepest_new_resolved_update_item> is updated with the innermost
  //   `ResolvedUpdateItem`, or left unchanged if no new `ResolvedUpdateItem`s
  //   are created.
  absl::StatusOr<std::unique_ptr<ResolvedUpdateItemElement>>
  ResolveUpdateItemElementFromUpdateTargetInfoList(
      std::vector<UpdateTargetInfo>& update_target_infos,
      ResolvedUpdateItem*& deepest_new_resolved_update_item);

  // Merges <ast_input_update_item> into <merged_update_item> (which might be
  // uninitialized). <input_update_target_infos> is the output of
  // PopulateUpdateTargetInfos() corresponding to <ast_update_item>.
  absl::Status MergeWithUpdateItem(
      const NameScope* update_scope, const ASTUpdateItem* ast_input_update_item,
      std::vector<UpdateTargetInfo>* input_update_target_infos,
      UpdateItemAndLocation* merged_update_item);

  // Resolves privileges, validating that the privileges are non-empty. If
  // name_scope is not nullptr, validates that any referenced paths exist in the
  // name scope (e.g. validate that column- and field-level privileges
  // correspond to columns and fields that exist in the name scope of a table).
  // If enable_nested_field_privileges is false, returns an error if any of the
  // given privileges are on nested fields.
  absl::Status ResolvePrivileges(
      const ASTPrivileges* ast_privileges, const NameScope* name_scope,
      bool enable_nested_field_privileges, absl::string_view statement_type,
      std::vector<std::unique_ptr<const ResolvedPrivilege>>* privilege_list);

  // Resolves a sample scan. Adds the name of the weight column to
  // <current_name_list> if WITH WEIGHT is present.
  absl::Status ResolveTablesampleClause(
      const ASTSampleClause* sample_clause,
      std::shared_ptr<const NameList>* current_name_list,
      std::unique_ptr<const ResolvedScan>* current_scan);

  // Common implementation for resolving a single argument of all expressions.
  // Pushes the related ResolvedExpr onto <resolved_arguments>.
  absl::Status ResolveExpressionArgument(
      const ASTExpression* arg, ExprResolutionInfo* expr_resolution_info,
      std::vector<std::unique_ptr<const ResolvedExpr>>* resolved_arguments);

  // Common implementation for resolving the children of all expressions.
  // Resolves input <arguments> and returns both <resolved_arguments_out>
  // and parallel vector <ast_arguments_out> (both having the same length).
  // The <argument_option_map> identifies arguments (by index) that require
  // special treatment during resolution (i.e., for INTERVAL and DATEPART).
  // Some AST arguments will expand into more than one resolved argument
  // (e.g., ASTIntervalExpr arguments expand into two resolved arguments).
  //
  // When called while resolving arguments to an ASTFunctionCall,
  // `inside_ast_function_call` is used for special-case error messages.
  absl::Status ResolveExpressionArguments(
      ExprResolutionInfo* expr_resolution_info,
      absl::Span<const ASTExpression* const> arguments,
      const std::map<int, SpecialArgumentType>& argument_option_map,
      std::vector<std::unique_ptr<const ResolvedExpr>>* resolved_arguments_out,
      std::vector<const ASTNode*>* ast_arguments_out,
      const ASTFunctionCall* inside_ast_function_call = nullptr);

  // Common implementation for resolving all functions given resolved input
  // `arguments` and `expected_result_type` (if any, usually needed while
  // resolving cast functions).
  // `match_internal_signatures` indicates whether attempts to match an internal
  // signature will be made.
  // If `function` is an aggregate function, `ast_location` must be an
  // ASTFunctionCall, and additional validation work is done for aggregate
  // function properties in the ASTFunctionCall, such as distinct and order_by.
  // After resolving the function call, will add a deprecation warning if either
  // the function itself is deprecated or a deprecated function signature is
  // used.
  absl::Status ResolveFunctionCallWithResolvedArguments(
      const ASTNode* ast_location,
      const std::vector<const ASTNode*>& arg_locations,
      bool match_internal_signatures, const Function* function,
      ResolvedFunctionCallBase::ErrorMode error_mode,
      std::vector<std::unique_ptr<const ResolvedExpr>> resolved_arguments,
      std::vector<NamedArgumentInfo> named_arguments,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<QueryResolutionInfo> multi_level_aggregate_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  // Common implementation for resolving arguments in the USING clause of
  // EXECUTE IMMEDIATE statements.
  absl::Status ResolveExecuteImmediateArgument(
      const ASTExecuteUsingArgument* argument, ExprResolutionInfo* expr_info,
      std::unique_ptr<const ResolvedExecuteImmediateArgument>* output);

  // Resolves a generic CREATE <entity_type> statement.
  absl::Status ResolveCreateEntityStatement(
      const ASTCreateEntityStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  // Resolves a generic ALTER <entity_type> statement.
  absl::Status ResolveAlterEntityStatement(
      const ASTAlterEntityStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  // Resolves a generic DROP <entity_type> statement.
  absl::Status ResolveDropEntityStatement(
      const ASTDropEntityStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  // Resolves a create property graph statement.
  absl::Status ResolveCreatePropertyGraphStatement(
      const ASTCreatePropertyGraphStatement* ast_stmt,
      std::unique_ptr<ResolvedStatement>* output);

  // Resolves a GQL query expression.
  absl::StatusOr<std::unique_ptr<const ResolvedScan>> ResolveGqlQuery(
      const ASTGqlQuery* ast_gql_query, const NameScope* scope,
      std::shared_ptr<const NameList>* output_name_list);

  // Resolves a GRAPH_TABLE query.
  absl::Status ResolveGraphTableQuery(
      const ASTGraphTableQuery* ast_graph_table_query,
      const NameScope* external_scope,
      std::unique_ptr<const ResolvedScan>* output,
      std::shared_ptr<const NameList>* output_name_list);

  // Resolves GraphElement property access. Type of <resolved_lhs> must be
  // GraphElementType.
  absl::Status ResolveGraphElementPropertyAccess(
      const ASTIdentifier* property_name_identifier,
      std::unique_ptr<const ResolvedExpr> resolved_lhs,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  // Resolves argument of special argument type PROPERTY_NAME.
  // Requires:
  //  1. Last resolved argument in `resolved_arguments_out` is a graph element.
  //  2. `property_name_identifier` is an identifier to a property exposed in
  //  the graph referenced by the graph element.
  absl::Status ResolvePropertyNameArgument(
      const ASTExpression* property_name_identifier,
      std::vector<std::unique_ptr<const ResolvedExpr>>& resolved_arguments_out,
      std::vector<const ASTNode*>& ast_arguments_out);

  // Resolves GraphIsLabeledPredicate.
  // Requires:
  //  1. LHS of `predicate` is a graph element type
  //  2. RHS of `predicate` is a valid label expression
  // An error is returned if the label expression refers to a label
  // inappropriate for the graph element's element type (node or edge).
  absl::Status ResolveGraphIsLabeledPredicate(
      const ASTGraphIsLabeledPredicate* predicate,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  absl::Status ResolveLockMode(
      const ASTLockMode* ast_lock_mode,
      std::unique_ptr<const ResolvedLockMode>* resolved);

 public:
  // This overload takes a single-part `function_name` and calls the
  // version that takes a `function_path`.
  // This is public because rewriters/privacy/privacy_utility.cc calls it.
  absl::Status ResolveFunctionCallWithResolvedArguments(
      const ASTNode* ast_location,
      const std::vector<const ASTNode*>& arg_locations,
      bool match_internal_signatures, absl::string_view function_name,
      std::vector<std::unique_ptr<const ResolvedExpr>> resolved_arguments,
      std::vector<NamedArgumentInfo> named_arguments,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

 private:
  // Defines the handle mode When a function is kNotFound in the catalog lookup.
  enum class FunctionNotFoundHandleMode { kReturnNotFound, kReturnError };

  // Looks up a function in the catalog, returning error status if not found.
  // If the Catalog lookup returns kNotFound then this method will either
  // return a not found or invalid argument error depending on the value of
  // <handle_mode>. Also returns the <error_mode> based on whether or
  // not the function had a "SAFE." prefix.
  absl::Status LookupFunctionFromCatalog(
      const ASTNode* ast_location,
      absl::Span<const std::string> function_name_path,
      FunctionNotFoundHandleMode handle_mode, const Function** function,
      ResolvedFunctionCallBase::ErrorMode* error_mode) const;

  // If `orig_status` is an error, and `ast_function_call` is a chained call
  // on an ASTDotIdentifier that looks like an unparenthesized multi-part
  // function name, try adding a suggestion to the error.
  // e.g. For `(expr).safe.sqrt()`, suggeest `(expr).(safe.sqrt)()`.
  //
  // Note: This returns OK if it didn't modify the error message.
  // The caller must still handle the original error.
  absl::Status TryMakeErrorSuggestionForChainedCall(
      const absl::Status& orig_status,
      const ASTFunctionCall* ast_function_call);

  // Implementation of the method above, for a candidate alterate name.
  // Note: This returns OK if it didn't modify the error message.
  // The caller must still handle the original error.
  absl::Status TryMakeErrorSuggestionForChainedCallImpl(
      const absl::Status& orig_status, const ASTFunctionCall* ast_function_call,
      absl::Span<const std::string> alt_function_name,
      absl::Span<const std::string> alt_prefix_path);

  // This wraps LookupFunctionFromCatalog with additional behavior to give
  // better errors for chained function calls.
  //
  // If we have a chained function call and we're going to give a
  // function-not-found error, if the input expression is also bad, we prefer
  // giving the error for the input expression (or another function call earlier
  // in the chain), because those errors were earlier in the input query.
  //
  // For example, in
  //   SELECT (bad_expression)
  //             .call1()
  //             .bad_call2()
  //             .call3()
  //             .bad_function4();
  // We'd rather give the error for the `bad_expression` or `bad_call2`,
  // before we give the error for `bad_function4`.
  //
  // If there's an error looking up the function, this attempts to resolve the
  // base expression using the same `expr_resolution_info`.  Non-error results
  // from this secondary resolve are always thrown away.
  absl::Status LookupFunctionFromCatalogWithChainedCallErrors(
      const ASTFunctionCall* ast_function_call,
      absl::Span<const std::string> function_name_path,
      FunctionNotFoundHandleMode handle_mode,
      ExprResolutionInfo* expr_resolution_info, const Function** function,
      ResolvedFunctionCallBase::ErrorMode* error_mode);

  // Common implementation for resolving operator expressions and non-standard
  // functions such as NOT, EXTRACT and CASE.  Looks up the
  // <function_name> from the catalog.  This is a wrapper function around
  // ResolveFunctionCallImpl().
  // NOTE: If the input is ASTFunctionCall, consider calling ResolveFunctionCall
  // instead, which also verifies the aggregate properties.
  absl::Status ResolveFunctionCallByNameWithoutAggregatePropertyCheck(
      const ASTNode* ast_location, absl::string_view function_name,
      absl::Span<const ASTExpression* const> arguments,
      const std::map<int, SpecialArgumentType>& argument_option_map,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  // Similar to the previous method. First calls
  // ResolveFunctionCallByNameWithoutAggregatePropertyCheck(), but if it fails
  // with INVALID_ARGUMENT, updates the literals to be explicitly typed
  // (using AddCastOrConvertLiteral) and tries again by calling
  // ResolveFunctionCallWithResolvedArguments().
  //
  // This is only used while resolving BETWEEN.  It avoids an error in
  //   `int32val BETWEEN 1 AND 5000000000`
  // where otherwise literal coercion of 5000000000 to int32 fails.
  // This does a retry with literal coercion blocked, so the casts get added.
  absl::Status ResolveFunctionCallWithLiteralRetry(
      const ASTNode* ast_location, absl::string_view function_name,
      absl::Span<const ASTExpression* const> arguments,
      const std::map<int, SpecialArgumentType>& argument_option_map,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  // Helper function used by ResolveFunctionCallWithLiteralRetry().
  // Loops through <resolved_expr_list> adding an explicit CAST() on every
  // ResolvedLiteral.
  // The ResolvedExpr* in <resolved_expr_list> may be replaced with new ones.
  absl::Status UpdateLiteralsToExplicit(
      absl::Span<const ASTExpression* const> ast_arguments,
      std::vector<std::unique_ptr<const ResolvedExpr>>* resolved_expr_list);

  absl::Status ResolveFunctionCallImpl(
      const ASTNode* ast_location, const Function* function,
      ResolvedFunctionCallBase::ErrorMode error_mode,
      absl::Span<const ASTExpression* const> arguments,
      const std::map<int, SpecialArgumentType>& argument_option_map,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  // Helper function used by ResolvedFunctionCallImpl that resolves the
  // arguments of a multi-level aggregate function call.
  absl::Status ResolvedMultiLevelAggregateArguments(
      const ASTFunctionCall* ast_function_call, const Function* function,
      absl::Span<const ASTExpression* const> arguments,
      const std::map<int, SpecialArgumentType>& argument_option_map,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<QueryResolutionInfo>& multi_level_aggregate_info,
      std::unique_ptr<ExprResolutionInfo>& post_grouping_expr_resolution_info,
      std::unique_ptr<NameScope>& post_grouping_name_scope,
      std::vector<std::unique_ptr<const ResolvedExpr>>& resolved_arguments,
      std::vector<const ASTNode*>& ast_arguments);

  // Check if `function_call` is a chained call and if it is valid.
  absl::Status CheckChainedFunctionCall(const ASTFunctionCall* function_call);

  // Returns the function name, arguments and options. It handles the special
  // cases for COUNT(*) and DATE functions.
  // If the function is an anonymized aggregate function, then updates
  // <query_resolution_info> to indicate the presence of anonymization.
  absl::Status GetFunctionNameAndArguments(
      const ASTFunctionCall* function_call,
      std::vector<std::string>* function_name_path,
      std::vector<const ASTExpression*>* function_arguments,
      std::map<int, SpecialArgumentType>* argument_option_map,
      QueryResolutionInfo* query_resolution_info);

  // Returns the function name, arguments and options. It handles the special
  // cases for ANON functions. If FEATURE_ANONYMIZATION is disabled the function
  // does nothing and returns OkStatus. It updates <query_resolution_info> to
  // indicate the presence of anonymization.
  absl::Status GetFunctionNameAndArgumentsForAnonFunctions(
      const ASTFunctionCall* function_call, bool is_binary_anon_function,
      std::vector<std::string>* function_name_path,
      std::vector<const ASTExpression*>* function_arguments,
      QueryResolutionInfo* query_resolution_info);

  // Returns the function name, arguments and options. It handles the special
  // cases for Differential Privacy functions. If FEATURE_DIFFERENTIAL_PRIVACY
  // is disabled the function does nothing and returns OkStatus.
  absl::Status GetFunctionNameAndArgumentsForDPFunctions(
      const ASTFunctionCall* function_call,
      std::vector<std::string>* function_name_path,
      std::vector<const ASTExpression*>* function_arguments,
      QueryResolutionInfo* query_resolution_info);

  // Adds the alias of the select column to the scope for lateral references.
  absl::Status AddSelectColumnAliasToScopeForLateralReferences(
      SelectForm select_form,
      const std::shared_ptr<const NameList>& from_clause_name_list,
      const NameScope* from_scan_scope, IdString query_alias,
      SelectColumnState* select_column_state,
      std::shared_ptr<NameList>& updated_name_list,
      std::unique_ptr<NameScope>& updated_name_scope);

  // Resolve the value part of a hint or option key/value pair.
  // This includes checking against <allowed> to ensure the options are
  // valid (typically used with AnalyzerOptions::allowed_hints_and_options).
  // The value must be an identifier, literal or query parameter.
  // <is_hint> indicates if this is a hint or an option.
  // <ast_qualifier> must be NULL if !is_hint.
  // <from_name_scope> From name scope, it is used to resolve options
  // specified as identifiers that have OptionProto::ResolvingKind ==
  // FROM_NAME_SCOPE_IDENTIFIER. It cannot be null when the option resolved has
  // AllowedOptionProperties::resolving_kind == FROM_NAME_SCOPE_IDENTIFIER in
  // AllowedHintsAndOptions. Otherwise it can be null.
  // <allow_alter_array_operators> indicates whether using the "+=" and "-="
  // operators on array typed options is allowed by the statement. For example,
  // using those operators on an option make sense when in the ALTER statements,
  // but not in the CREATE statement.
  absl::Status ResolveHintOrOptionAndAppend(
      const ASTExpression* ast_value, const ASTIdentifier* ast_qualifier,
      const ASTIdentifier* ast_name, HintOrOptionType hint_or_option_type,
      const AllowedHintsAndOptions& allowed, const NameScope* from_name_scope,
      ASTOptionsEntry::AssignmentOp option_assignment_op,
      bool allow_alter_array_operators,
      std::vector<std::unique_ptr<const ResolvedOption>>* option_list);

  // Resolve <ast_hint> and add entries into <hints>.
  absl::Status ResolveHintAndAppend(
      const ASTHint* ast_hint,
      std::vector<std::unique_ptr<const ResolvedOption>>* hints);

  // Resolve <ast_hint> and add resolved hints onto <resolved_node>.
  // Works for ResolvedScan or ResolvedStatement (or any node with a hint_list).
  template <class NODE_TYPE>
  absl::Status ResolveHintsForNode(const ASTHint* ast_hints,
                                   NODE_TYPE* resolved_node);

  // Resolve <options_list> and add the options onto <resolved_options>
  // as ResolvedHints.
  // <allow_alter_array_operators> indicates whether the += and -= operators
  // should be allowed.
  absl::Status ResolveOptionsList(
      const ASTOptionsList* options_list, bool allow_alter_array_operators,
      std::vector<std::unique_ptr<const ResolvedOption>>* resolved_options);

  // Resolve <table_and_column_info> and add the entry into
  // <resolved_table_and_column_info_list>.
  absl::Status ResolveTableAndColumnInfoAndAppend(
      const ASTTableAndColumnInfo* table_and_column_info,
      std::vector<std::unique_ptr<const ResolvedTableAndColumnInfo>>*
          resolved_table_and_column_info_list);

  // Resolve <table_and_column_info_list> and add the ResolveTableAndColumnInfo
  // entries into <resolved_table_and_column_info_list> as
  // ResolvedTableAndColumnInfoList.
  absl::Status ResolveTableAndColumnInfoList(
      const ASTTableAndColumnInfoList* table_and_column_info_list,
      std::vector<std::unique_ptr<const ResolvedTableAndColumnInfo>>*
          resolved_table_and_column_info_list);

  // Resolves a TABLE clause such as TABLE name into a ResolvedScan.
  //
  // `name` can resolve to a WITH clause alias, a table in catalog, or if
  // resolving inside a function, a table-valued argument to that function.
  //
  // For name collision between a table-valued argument and a catalog table,
  // resolution order is controlled by `resolve_argument_first` along with
  // language feature FEATURE_TABLE_SYNTAX_RESOLVE_ARGUMENT_LAST. If
  // `resolve_argument_first` is true, table-valued arguments are looked up
  // before catalog tables. If false, catalog tables are looked up first, and
  // table-valued arguments are only considered if no catalog table is found.
  absl::Status ResolveTableClause(
      const ASTTableClause* ast_table_clause, const NameScope* external_scope,
      bool resolve_argument_first,
      const std::string& read_as_row_type_error_kind,
      std::unique_ptr<const ResolvedScan>* output_scan,
      std::shared_ptr<const NameList>* output_name_list);

  // Resolve <options_list> and add the options onto <resolved_options>.
  // Requires valid anonymization or differential_privacy option names and types
  // specified in AllowedHintsAndOptions. Validates option expression types and
  // coerces them to target types if necessary.
  // <query_resolution_info> is used to determine option type and construct from
  // name scope used if any of options has OptionProto::resolving_kind ==
  // FROM_NAME_SCOPE_IDENTIFIER.
  absl::Status ResolveAnonymizationOptionsList(
      const ASTOptionsList* options_list,
      const QueryResolutionInfo& query_resolution_info,
      std::vector<std::unique_ptr<const ResolvedOption>>* resolved_options);

  // Resolve <options_list> and add the options onto <resolved_options>.
  // Requires valid aggregation threshold option names and types specified in
  // AllowedHintsAndOptions. The option names are matched in a case insensitive
  // way. Validates option expression types and coerces them to target types if
  // necessary. <query_resolution_info> is used to determine option type and
  // construct from name scope used if any of options has
  // OptionProto::resolving_kind == FROM_NAME_SCOPE_IDENTIFIER.
  absl::Status ResolveAggregationThresholdOptionsList(
      const ASTOptionsList* options_list,
      const QueryResolutionInfo& query_resolution_info,
      std::vector<std::unique_ptr<const ResolvedOption>>* resolved_options);

  // This function is just for resolving the report options list for ANON
  // functions.
  // Resolve <options_list> and add the option onto <resolved_options>.
  // Requires valid report option
  //     name  - FORMAT
  //     value - JSON and PROTO
  // Validates option expression types and coerces them to target types if
  // necessary.
  // Only one option entry is allowed.
  // <format> is an output parameter, which is result of the format we resolved,
  // when it is not specified (the options list is empty), its value will be
  // <default_format>.
  absl::Status ResolveAnonWithReportOptionsList(
      const ASTOptionsList* options_list, absl::string_view default_format,
      std::vector<std::unique_ptr<const ResolvedOption>>* resolved_options,
      std::string* format);

  // Verify that the expression is an integer parameter or literal, returning
  // error status if not.
  absl::Status ValidateIntegerParameterOrLiteral(
      const char* clause_name, const ASTNode* ast_location,
      const ResolvedExpr& expr) const;

  // Validates the argument to LIMIT, OFFSET, ASSERT_ROWS_MODIFIED, or the
  // table sample clause.  The argument must be an integer parameter or
  // literal (possibly wrapped in an int64 cast).  If the expr type is not
  // int64 then <expr> is updated to be cast to int64.
  absl::Status ValidateParameterOrLiteralAndCoerceToInt64IfNeeded(
      const char* clause_name, const ASTNode* ast_location,
      std::unique_ptr<const ResolvedExpr>* expr) const;

  // <referencing_table> can be NULL.  If the table does not exist in the
  // Catalog, we  try to resolve the ALTER statement anyway.
  absl::Status ResolveAddConstraintAction(
      const Table* referencing_table, bool is_if_exists,
      const ASTAddConstraintAction* alter_action,
      std::unique_ptr<const ResolvedAddConstraintAction>*
          resolved_alter_action);

  // <referencing_table> can be NULL. If the table does not exist in the catalog
  // and the alter statement uses IF EXISTS, we try to resolve the foriegn key
  // anyway.
  absl::Status ResolveAddForeignKey(
      const Table* referencing_table, bool is_if_exists,
      const ASTAddConstraintAction* alter_action,
      std::unique_ptr<const ResolvedAddConstraintAction>*
          resolved_alter_action);

  // <target_table> can be NULL. If the table does not exist in the catalog and
  // the alter statement uses IF EXISTS, we try to resolve the primary key
  // anyway.
  absl::Status ResolveAddPrimaryKey(
      const Table* target_table, bool is_if_exists,
      const ASTAddConstraintAction* alter_action,
      std::unique_ptr<const ResolvedAddConstraintAction>*
          resolved_alter_action);

  // Struct to indicate whether each type modifier is allowed in resolving and
  // the context where the modifiers are resolved.
  struct ResolveTypeModifiersOptions {
    bool allow_type_parameters = false;
    bool allow_collation = false;
    // Used in error message when a type modifier is not allowed but exists in
    // the ASTType to be resolved. Must be specified when any type modifier is
    // disallowed in resolving.
    std::optional<absl::string_view> context = {};
  };

  // Resolve the ASTType <type> as a Type <resolved_type>. Each boolean field
  // inside <resolve_type_modifier_options>, e.g. allow_type_parameters,
  // indicates whether the corresponding type modifier, e.g. TypeParameters, is
  // allowed in resolving.
  //
  // If a certain type modifier is allowed in resolving, we will resolve it and
  // output in the corresponding field of <resolved_type_modifiers>. If a
  // certain type modifier is not allowed but it exists inside <type>, we will
  // error out with <resolve_type_modifier_options.context> in the error
  // message.

  // <resolved_type_modifiers> cannot be null when any type modifier is allowed
  // in resolving. When all type modifers are disallowed and
  // <resolved_type_modifiers> is not null, an empty TypeModifiers object
  // would be returned.
  absl::Status ResolveType(
      const ASTType* type,
      const ResolveTypeModifiersOptions& resolve_type_modifier_options,
      const Type** resolved_type, TypeModifiers* resolved_type_modifiers);

  absl::Status ResolveSimpleType(
      const ASTSimpleType* type,
      const ResolveTypeModifiersOptions& resolve_type_modifier_options,
      const Type** resolved_type, TypeModifiers* resolved_type_modifiers);

  absl::Status ResolveArrayType(
      const ASTArrayType* array_type,
      const ResolveTypeModifiersOptions& resolve_type_modifier_options,
      const ArrayType** resolved_type, TypeModifiers* resolved_type_modifiers);

  absl::Status ResolveStructType(
      const ASTStructType* struct_type,
      const ResolveTypeModifiersOptions& resolve_type_modifier_options,
      const StructType** resolved_type, TypeModifiers* resolved_type_modifiers);

  absl::Status ResolveRangeType(
      const ASTRangeType* range_type,
      const ResolveTypeModifiersOptions& resolve_type_modifier_options,
      const RangeType** resolved_type, TypeModifiers* resolved_type_modifiers);

  absl::Status ResolveMapType(
      const ASTMapType* map_type,
      const ResolveTypeModifiersOptions& resolve_type_modifier_options,
      const Type** resolved_type, TypeModifiers* resolved_type_modifiers);

  // Resolve type parameters to the resolved TypeParameters class, which stores
  // type parameters as a TypeParametersProto. If there are no type parameters,
  // then an empty TypeParameters class is returned. The type parameters can be
  // found in <type_parameters>. <resolved_type> must corresponds to the Type
  // returned when the ASTType parent of <type_parameters> is resolved.
  //
  // If the type is a STRUCT or ARRAY type, <child_parameter_list> should hold
  // the type parameters of the STRUCT fields or ARRAY elements. Otherwise,
  // <child_parameter_list> is empty.
  absl::StatusOr<TypeParameters> ResolveTypeParameters(
      const ASTTypeParameterList* type_parameters, const Type& resolved_type,
      const std::vector<TypeParameters>& child_parameter_list);

  // Resolve the simple type literals for each input type parameter. Valid
  // literal types are BOOL, BYTES, FLOAT, INT, STRING, and MAX.
  absl::StatusOr<std::vector<TypeParameterValue>> ResolveParameterLiterals(
      const ASTTypeParameterList& type_parameters);

  // Resolve <collate> to the resolved Collation class based on <resolved_type>.
  // <resolved_type> must correspond to the Type returned when the ASTType
  // parent of <collate> is resolved. If the <resolved_type> is a STRUCT or
  // ARRAY type, <child_collation_list> should hold the collations of the STRUCT
  // fields or ARRAY elements.
  absl::StatusOr<Collation> ResolveTypeCollation(
      const ASTCollate* collate, const Type& resolved_type,
      std::vector<Collation> child_collation_list);

  // Resolves operation collation for a function call from its argument list.
  // The collation will be stored in <function_call>.collation_list.
  // <error_location> is used for error messages.
  absl::Status MaybeResolveCollationForFunctionCallBase(
      const ASTNode* error_location, ResolvedFunctionCallBase* function_call);

  // Resolves operation collation for a subquery expression. Operation collation
  // is calculated only when the subquery type is IN; otherwise, this call
  // returns OK.
  // The collation will be stored in <subquery_expr>.in_collation.
  // <error_location> is used for error messages.
  absl::Status MaybeResolveCollationForSubqueryExpr(
      const ASTNode* error_location, ResolvedSubqueryExpr* subquery_expr);

  // Helper to resolve [NOT] LIKE ANY|SOME|ALL expressions when used with IN
  // list or UNNEST array.
  absl::Status ResolveLikeAnyAllExpressionHelper(
      const ASTLikeExpression* like_expr,
      absl::Span<const ASTExpression* const> arguments,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<const ResolvedExpr>* resolved_expr_out);

  void FetchCorrelatedSubqueryParameters(
      const CorrelatedColumnsSet& correlated_columns_set,
      std::vector<std::unique_ptr<const ResolvedColumnRef>>* parameters);

  absl::TimeZone default_time_zone() const {
    return analyzer_options_.default_time_zone();
  }

  absl::string_view default_anon_function_report_format() const {
    return analyzer_options_.default_anon_function_report_format();
  }

  bool in_strict_mode() const {
    return language().name_resolution_mode() == NAME_RESOLUTION_STRICT;
  }

  ProductMode product_mode() const { return language().product_mode(); }

  // Check our assumptions about value tables.
  // These errors shouldn't show up to users. They only happen if an engine
  // gives us a bad Table in the Catalog.
  absl::Status CheckValidValueTable(const ASTNode* ast_location,
                                    const Table& table) const;
  absl::Status CheckValidValueTableFromTVF(const ASTTVF* path_expr,
                                           absl::string_view full_tvf_name,
                                           const TVFRelation& schema) const;

  // Collapse the expression trees (present inside <node_ptr>) into literals if
  // possible, thus mutating the <node_ptr> subsequently.
  // This will not change any semantics of the tree and is mostly done to allow
  // typed struct literals as hints.
  void TryCollapsingExpressionsAsLiterals(
      const ASTNode* ast_location,
      std::unique_ptr<const ResolvedNode>* node_ptr);

  // Given a ResolvedUpdateStmt or ResolvedMergeStmt statement, this will call
  // RecordColumnAccess with READ access for scenarios where the AST does not
  // directly indicate a READ, but for which a READ is implied by the operation.
  // For example, all nested DML on arrays imply a READ because they allow
  // the caller to count the number of rows on the array. For example, the
  // following SQL will give an error if any rows exist, which should require
  // READ.
  //   UPDATE Table SET
  //   (DELETE ArrayCol WHERE CAST(ERROR("Rows found!") AS BOOL));
  // Similarly, access to fields of a proto/struct requires the engine to read
  // the old proto value before modifying it and writing it back. We can
  // consider relaxing this if needed in the future.
  // Array offsets also are implied READS even when used in the LHS because
  // the lack of a runtime exception tells the caller the array is at least the
  // size of the offset.
  absl::Status RecordImpliedAccess(const ResolvedStatement* statement);

  // Records access to a column (or vector of columns). Access is bitwise OR'd
  // with any existing access. If analyzer_options_.prune_unused_columns is
  // true, columns without any recorded access will be removed from the
  // table_scan().
  void RecordColumnAccess(
      const ResolvedColumn& column,
      ResolvedStatement::ObjectAccess access_flags = ResolvedStatement::READ);
  void RecordColumnAccess(
      absl::Span<const ResolvedColumn> columns,
      ResolvedStatement::ObjectAccess access_flags = ResolvedStatement::READ);

  void RecordPropertyGraphRef(const PropertyGraph* property_graph);

  // For all ResolvedScan nodes under <node>, prune the column_lists to remove
  // any columns not included in referenced_columns_.  This removes any columns
  // from the Resolved AST that were never referenced in the query.
  // NOTE: This mutates the column_list on Scan nodes in <tree>.
  // Must be called before SetColumnAccessList.
  absl::Status PruneColumnLists(const ResolvedNode* node) const;

  // Fills in <column_access_list> on <statement> to indicate, for each
  // ResolvedColumn in statement's <table_scan> whether it was read and/or
  // written. Only applies on ResolvedUpdateStmt and ResolvedMergeStmt.
  // Must be called after PruneColumnList.
  absl::Status SetColumnAccessList(ResolvedStatement* statement);

  // If the given expression is an untyped parameter, replaces it with an
  // equivalent parameter with type <type>. The return value indicates whether
  // the expression was replaced.
  absl::StatusOr<bool> MaybeAssignTypeToUndeclaredParameter(
      std::unique_ptr<const ResolvedExpr>* expr, const Type* type);

  // Checks that the type of a previously encountered parameter referenced at
  // <location> agrees with <type> and records it in undeclared_parameters_.
  // Erases the corresponding entry in untyped_undeclared_parameters_.
  absl::Status AssignTypeToUndeclaredParameter(
      const ParseLocationPoint& location, const Type* type);

  // Attempts to find a table in the catalog. Sets <table> to nullptr if not
  // found.
  absl::Status FindTable(const ASTPathExpression* name, const Table** table);

  // Attempts to find a column in <table> by <name>. Sets <index> to -1 if not
  // found; otherwise, sets it to the first column found, starting at index 0.
  // Sets <duplicate> to true if two or more were found.
  static void FindColumnIndex(const Table* table, absl::string_view name,
                              int* index, bool* duplicate);

  // Returns true if two values of the given types can be tested for equality
  // either directly or by coercing the values to a common supertype.
  absl::StatusOr<bool> SupportsEquality(const Type* type1, const Type* type2);

  // Returns the column alias from <expr_resolution_info> if <ast_expr> matches
  // the top level expression in <expr_resolution_info>. Returns an empty
  // IdString if the <expr_resolution_info> has no top level expression,
  // <ast_expr> does not match, or the column alias is an internal alias.
  static IdString GetColumnAliasForTopLevelExpression(
      ExprResolutionInfo* expr_resolution_info, const ASTExpression* ast_expr);

  // Returns an error for an unrecognized identifier. Errors take the form
  // "Unrecognized name: foo", with a "Did you mean <bar>?" suggestion added
  // if the path expression is sufficiently close to a symbol in <name_scope>
  // or <catalog_>. <path_location_point> is used to construct the error
  // message. If the <identifiers> come from a system variable,
  // <path_location_point> corresponds to the position of the '@@'. Otherwise,
  // <path_location_point> is the location of the unrecognized identifier.
  absl::Status GetUnrecognizedNameError(
      const ParseLocationPoint& path_location_point,
      absl::Span<const std::string> identifiers, const NameScope* name_scope,
      bool is_system_variable);

  // Returns an internal catalog used just for looking up system variables.
  // The results of this function are cached in system_variables_catalog_, so
  // only the first call actually populates the catalog.
  Catalog* GetSystemVariablesCatalog();

  // Return a string to append to error messages that suggests using INPUT
  // TABLE, if the feature is enabled.  Otherwise, return empty string.
  std::string MaybeSuggestUsingInputTable();

  // Finds the matching TVF signature corresponding to a TVF call. Resolution
  // of the TVF call currently only succeeds if there is a single matching
  // signature. If there are multiple matching signatures, the rules to find the
  // closest matching signature are not yet implemented, so the resolution will
  // fail with an internal error. Until this support is added, the catalog must
  // be constructed such that it's impossible for multiple signatures to match.
  // `pipe_input_arg` if present is the pipe input argument in pipe CALL.
  //
  // The following arguments are function outputs and are populated if exactly
  // one TVF signature matches the input arguments - `result_signature`,
  // `arg_locations`, `resolved_tvf_args`, `named_arguments` and
  // `signature_match_result`.
  // The `arg_locations` and `resolved_tvf_args` reflect and match 1:1 to the
  // concrete function call arguments in the `result_signature`.
  //
  // Returned integer is the index of the matching signature.
  // TODO: b/409730404 - Add support to score and select a signature when
  // multiple signatures match.
  absl::StatusOr<int> FindMatchingTVFSignature(
      const ASTTVF* /*absl_nonnull*/ ast_tvf,
      const TableValuedFunction* /*absl_nonnull*/ tvf_catalog_entry,
      const NameScope* /*absl_nonnull*/ external_scope,
      const FunctionResolver& function_resolver,
      ResolvedTVFArg* /*absl_nullable*/ pipe_input_arg,
      std::unique_ptr<FunctionSignature>& result_signature,
      std::vector<const ASTNode*>& arg_locations,
      std::vector<ResolvedTVFArg>& resolved_tvf_args,
      std::vector<NamedArgumentInfo>& named_arguments,
      SignatureMatchResult& signature_match_result);

  // Checks if a TVF signature matches the input arguments and returns an
  // OK-status in case of a successful match. Resolves the input arguments
  // based on the signature and checks if the argument count and types matches
  // the signature.
  //
  // `pipe_input_arg` if present is the pipe input argument in pipe CALL.
  //
  // The following arguments are function outputs and are populated if the
  // signature corresponding to `signature_idx` matches with the input
  // arguments: `result_signature`, `arg_locations`, `resolved_tvf_args`,
  // `named_arguments` and `signature_match_result`.
  // The `arg_locations` and `resolved_tvf_args` reflect and match 1:1 to the
  // concrete function call arguments in the `result_signature`.
  absl::Status MatchTVFSignature(
      const ASTTVF* /*absl_nonnull*/ ast_tvf,
      const TableValuedFunction* /*absl_nonnull*/ tvf_catalog_entry,
      int signature_idx, const NameScope* /*absl_nonnull*/ external_scope,
      const FunctionResolver& function_resolver,
      ResolvedTVFArg* /*absl_nullable*/ pipe_input_arg,
      std::unique_ptr<FunctionSignature>& result_signature,
      std::vector<const ASTNode*>& arg_locations,
      std::vector<ResolvedTVFArg>& resolved_tvf_args,
      std::vector<NamedArgumentInfo>& named_arguments,
      SignatureMatchResult& signature_match_result);

  // Prepares a list of TVF input arguments and a result signature. This
  // includes adding necessary casts and coercions, and wrapping the resolved
  // input arguments with TVFInputArgumentType as appropriate.
  // `pipe_input_arg` if present is the pipe input argument in pipe CALL.
  // `result_signature`, `resolved_tvf_args` and `tvf_input_arguments` are
  // output parameters.
  absl::Status PrepareTVFInputArguments(
      absl::string_view tvf_name_string, const ASTTVF* ast_tvf,
      const TableValuedFunction* tvf_catalog_entry,
      const NameScope* external_scope, ResolvedTVFArg* pipe_input_arg,
      std::unique_ptr<FunctionSignature>& result_signature,
      std::vector<ResolvedTVFArg>& resolved_tvf_args,
      std::vector<TVFInputArgumentType>& tvf_input_arguments);

  // Generates an error status about a TVF call not matching a signature.
  // It is made to avoid redundant code in MatchTVFSignature.
  absl::Status GenerateTVFNotMatchError(
      const ASTTVF* ast_tvf, const std::vector<const ASTNode*>& arg_locations,
      const SignatureMatchResult& signature_match_result,
      const TableValuedFunction& tvf_catalog_entry, absl::string_view tvf_name,
      absl::Span<const InputArgumentType> input_arg_types, int signature_idx);

  // Struct to control the features to be resolved by
  // ResolveCreateTableStmtBaseProperties.
  struct ResolveCreateTableStmtBasePropertiesArgs {
    const bool table_element_list_enabled;
  };

  // Struct to store the properties of ASTCreateTableStmtBase.
  struct ResolveCreateTableStatementBaseProperties {
    std::vector<std::string> table_name;
    const Table* like_table = nullptr;
    std::unique_ptr<const ResolvedScan> clone_from;
    std::unique_ptr<const ResolvedScan> copy_from;
    ResolvedCreateStatement::CreateScope create_scope;
    ResolvedCreateStatement::CreateMode create_mode;
    std::vector<std::unique_ptr<const ResolvedOption>> resolved_options;
    std::vector<std::unique_ptr<const ResolvedColumnDefinition>>
        column_definition_list;
    std::vector<ResolvedColumn> pseudo_column_list;
    std::unique_ptr<ResolvedPrimaryKey> primary_key;
    std::vector<std::unique_ptr<const ResolvedForeignKey>> foreign_key_list;
    std::vector<std::unique_ptr<const ResolvedCheckConstraint>>
        check_constraint_list;
    std::unique_ptr<const ResolvedExpr> collation;
    std::unique_ptr<const ResolvedAuxLoadDataPartitionFilter> partition_filter;
    std::vector<std::unique_ptr<const ResolvedExpr>> partition_by_list;
    std::vector<std::unique_ptr<const ResolvedExpr>> cluster_by_list;
    std::unique_ptr<const ResolvedWithPartitionColumns> with_partition_columns;
    std::unique_ptr<const ResolvedConnection> connection;
    bool is_value_table;
    std::unique_ptr<const ResolvedScan> query_scan;
    std::vector<std::unique_ptr<const ResolvedOutputColumn>> output_column_list;

    // Input columns that are visible if the statement has no explicit column
    // definitions.
    ResolvedColumnList default_visible_columns;

    // Gets either the explicit columns if present, or the
    // default_visible_columns.
    absl::Status GetVisibleColumnNames(NameList* column_names) const;

    // Add names from the "WITH PARTITION COLUMNS" clause to 'column_names'
    // if they aren't already there.
    absl::Status WithPartitionColumnNames(NameList* column_names) const;
  };

  // Resolves the shared properties of the statements inheriting from
  // ASTCreateTableStmtBase (ASTCreateTableStatement,
  // ASTCreateExternalTableStatement). The optional features are resolved on the
  // basis of flag values in resolved_properties_control_args.
  absl::Status ResolveCreateTableStmtBaseProperties(
      const ASTCreateTableStmtBase* ast_statement,
      absl::string_view statement_type,
      const ASTPathExpression* like_table_name,
      const ASTQuery* query,                 // Present if there's an AS SELECT.
      const NameList* pipe_input_name_list,  // Present for pipe CREATE TABLE.
      const ASTCollate* collate, const ASTPartitionBy* partition_by,
      const ASTClusterBy* cluster_by,
      const ASTWithPartitionColumnsClause* with_partition_columns_clause,
      const ASTWithConnectionClause* with_connection_clause,
      const ASTAuxLoadDataPartitionsClause* partitions_clause,
      const ResolveCreateTableStmtBasePropertiesArgs&
          resolved_properties_control_args,
      ResolveCreateTableStatementBaseProperties* statement_base_properties);

  // Resolve WithPartitionColumnsClause and also update column_indexes with all
  // the resolved columns from WithPartitionColumnsClause.
  absl::Status ResolveWithPartitionColumns(
      const ASTWithPartitionColumnsClause* with_partition_columns_clause,
      IdString table_name_id_string, absl::string_view statement_type,
      ColumnIndexMap* column_indexes,
      std::unique_ptr<const ResolvedWithPartitionColumns>*
          resolved_with_partition_columns);

  // Computes the pivot-value portion of the name of a pivot column without an
  // explicit alias. <pivot_value> represents the literal IN-clause value used
  // to generate the name. If successful, the generated column name is appended
  // to <*column_name>.
  //
  // Pivot column names are not guaranteed uniqueness; if the same pivot value
  // is used multiple times, the same column name will result.
  //
  // Returns a failed status if we do not support a default name for
  // <pivot_value>. <ast_location> determines the parse location to use for
  // error messages. It is not used unless there's an error.
  absl::Status AppendPivotColumnName(const Value& pivot_value,
                                     const ASTNode* ast_location,
                                     std::string* column_name);

  // Implements AppendPivotColumnName() for the specific case where
  // <pivot_value> is a non-null value of one of the following types:
  //   BOOL/INT32/INT64/UINT32/UINT64/NUMERIC/BIGNUMERIC.
  //
  // Generates the column name by casting the value to STRING (which, for the
  //   above types is guaranteed to succeed), then performing
  //   the following transformations to ensure that the result is a valid
  //   GoogleSQL identifier.
  //  - If the first character is a digit *and* <column_name> is currently
  //    empty (e.g. if no pivot expression alias exists), prepends the generated
  //    name with "_".
  //  - Replaces "-" with "minus_".
  //  - Replaces "." with "_point_".
  absl::Status AppendPivotColumnNameViaStringCast(const Value& pivot_value,
                                                  std::string* column_name);

  // Obtains the value of a pivot column `node` used for an IN-clause inside a
  // PIVOT clause. `error_location` is the parsed location for SQL error.
  // Returns the constant value, if the value is known at resolution time, or an
  // error otherwise.
  //
  // Currently, this function supports only literal values and struct
  // constructors where all arguments are either literals or other struct
  // constructors.
  absl::StatusOr<Value> GetPivotValue(const ResolvedExpr* node,
                                      const ASTPivotValue* error_location);
  // Similar to `GetPivotValue`. Obtains the value of a pivot column when it's a
  // STRUCT constructor.
  absl::StatusOr<Value> GetStructPivotValue(
      const ResolvedMakeStruct* node, const ASTPivotValue* error_location);

  absl::StatusOr<ResolvedColumn> CreatePivotColumn(
      const ASTPivotExpression* ast_pivot_expr,
      const ResolvedExpr* resolved_pivot_expr, bool is_only_pivot_expr,
      const ASTPivotValue* ast_pivot_value,
      const ResolvedExpr* resolved_pivot_value);

  absl::Status ResolvePivotExpressions(
      const ASTPivotExpressionList* ast_pivot_expr_list, const NameScope* scope,
      std::vector<std::unique_ptr<const ResolvedExpr>>* pivot_expr_columns,
      QueryResolutionInfo& query_resolution_info);

  absl::Status ResolveForExprInPivotClause(
      const ASTExpression* for_expr, const NameScope* scope,
      std::unique_ptr<const ResolvedExpr>* resolved_for_expr);

  absl::Status ResolveInClauseInPivotClause(
      const ASTPivotValueList* pivot_values, const NameScope* scope,
      const Type* for_expr_type,
      std::vector<std::unique_ptr<const ResolvedExpr>>* resolved_in_exprs);

  // Resolves a PIVOT clause denoted by <ast_pivot_clause>.
  //
  // Expressions inside the pivot clause are resolved using <input_name_list>
  // with <previous_scope> as a fallback scope for names not in the list.
  //
  // On success, sets <output> and <output_name_list> to a scan and NameList
  // describing the PIVOT output.
  absl::Status ResolvePivotClause(
      std::unique_ptr<const ResolvedScan> input_scan,
      std::shared_ptr<const NameList> input_name_list,
      const NameScope* previous_scope, bool input_is_subquery,
      const ASTPivotClause* ast_pivot_clause,
      std::unique_ptr<const ResolvedScan>* output,
      std::shared_ptr<const NameList>* output_name_list);

  absl::Status ResolveUnpivotOutputValueColumns(
      const ASTPathExpressionList* ast_unpivot_expr_list,
      std::vector<ResolvedColumn>* unpivot_value_columns,
      const std::vector<const Type*>& value_column_types,
      const NameScope* scope);

  absl::Status ResolveUnpivotInClause(
      const ASTUnpivotInItemList* ast_unpivot_expr_list,
      std::vector<std::unique_ptr<const ResolvedUnpivotArg>>* resolved_in_items,
      const std::vector<ResolvedColumn>* input_scan_columns,
      absl::flat_hash_set<ResolvedColumn>* in_clause_input_columns,
      std::vector<const Type*>* value_column_types, const Type** label_type,
      std::vector<std::unique_ptr<const ResolvedLiteral>>* resolved_label_list,
      const ASTUnpivotClause* ast_unpivot_clause, const NameScope* scope);

  // Gets either the explicitly provided label or otherwise auto-generated
  // string label by concatenating columns names from column groups in the IN
  // clause of UNPIVOT.
  absl::StatusOr<Value> GetLabelForUnpivotInColumnList(
      const ASTUnpivotInItem* in_column_list);

  // Resolves an UNPIVOT clause.
  //  - <input_scan> represents the input to unpivot. On success, ownership is
  //      transferred to 'output'.
  //  - <input_name_list> represents a NameList for columns in the input scan.
  //      This defines the list of valid columns when resolving unpivot
  //      expressions and FOR expressions.
  //  - <ast_unpivot_clause> represents the parse tree of the entire unpivot
  //  clause.
  //  - On output, '*output' contains a scan representing the result of the
  //      UNPIVOT clause and '*output_name_list' contains a NameList which can
  //      be used by external clauses to refer to columns in the UNPIVOT output.
  absl::Status ResolveUnpivotClause(
      std::unique_ptr<const ResolvedScan> input_scan,
      std::shared_ptr<const NameList> input_name_list,
      const NameScope* previous_scope,
      const ASTUnpivotClause* ast_unpivot_clause,
      std::unique_ptr<const ResolvedScan>* output,
      std::shared_ptr<const NameList>* output_name_list);

  // Resolves a MATCH_RECOGNIZE clause.
  //  See (broken link).
  //  - <input_scan> represents the input to MATCH_RECOGNIZE. On success,
  //      ownership is transferred to 'output'.
  //  - <input_name_list> represents a NameList for columns in the input scan.
  //      This defines the list of valid columns when resolving match recognize
  //      expressions.
  //  - <ast_match_recognize_clause> represents the parse tree of the
  //      entire MATCH_RECOGNIZE clause.
  //  - On output, '*output' contains a scan representing the result of the
  //      MATCH_RECOGNIZE clause and '*output_name_list' contains a NameList
  //      which can be used by external clauses to refer to columns in the
  //      MATCH_RECOGNIZE output.
  absl::Status ResolveMatchRecognizeClause(
      std::unique_ptr<const ResolvedScan> input_scan,
      std::shared_ptr<const NameList> input_name_list,
      const NameScope* previous_scope,
      const ASTMatchRecognizeClause* ast_match_recognize_clause,
      std::unique_ptr<const ResolvedScan>* output,
      std::shared_ptr<const NameList>* output_name_list);

  // Resolves the OPTIONS() list for a MATCH_RECOGNIZE clause.
  absl::StatusOr<std::vector<std::unique_ptr<const ResolvedOption>>>
  ResolveMatchRecognizeOptionsList(const ASTOptionsList* options_list);

  // Resolves the pattern expression in a MATCH_RECOGNIZE clause.
  absl::Status ResolveMatchRecognizePatternExpr(
      const ASTRowPatternExpression* node,
      ExprResolutionInfo* expr_resolution_info,
      const IdStringHashMapCase<const ASTIdentifier*>&
          pattern_variables_defined,
      IdStringHashMapCase<const ASTIdentifier*>& undeclared_pattern_variables,
      IdStringHashSetCase& referenced_pattern_variables,
      std::unique_ptr<const ResolvedMatchRecognizePatternExpr>& output);

  // Resolves the quantifier on a MATCH_RECOGNIZE quantified pattern where the
  // operand has already been resolved. This is because there's no ResolvedNode
  // just for the quantifier. Rather, the bounds & reluctance are directly on
  // the ResolvedMatchRecognizePatternQuantification node.
  absl::StatusOr<std::unique_ptr<const ResolvedMatchRecognizePatternExpr>>
  ResolveMatchRecognizePatternQuantifier(
      std::unique_ptr<const ResolvedMatchRecognizePatternExpr> operand,
      const ASTQuantifier* quantifier,
      ExprResolutionInfo* expr_resolution_info);

  // Resolves the variable definitions in the DEFINE clause.
  absl::StatusOr<std::vector<
      std::unique_ptr<const ResolvedMatchRecognizeVariableDefinition>>>
  ResolveMatchRecognizeVariableDefinitions(
      const ASTSelectList* definition_list,
      IdStringHashSetCase referenced_pattern_variables, NameScope* local_scope,
      QueryResolutionInfo* query_resolution_info,
      const IdStringHashMapCase<NameTarget>& disallowed_access_targets);

  // Resolves expressions in the MEASURES clause.
  absl::Status ResolveMatchRecognizeMeasures(
      const ASTSelectList* ast_measures, const NameList* input_name_list,
      const NameScope* input_scope, const NameScope* local_scope,
      absl::Span<const ResolvedColumn> partitioning_columns,
      const IdStringHashMapCase<const ASTIdentifier*>&
          pattern_variables_defined,
      const IdStringHashMapCase<NameTarget>& pattern_variable_targets,
      const IdStringHashMapCase<NameTarget>& disallowed_access_targets,
      std::unique_ptr<const ResolvedScan>& input_scan, NameList& out_name_list,
      std::vector<std::unique_ptr<const ResolvedMeasureGroup>>&
          out_measure_groups,
      std::vector<std::unique_ptr<const ResolvedComputedColumn>>&
          out_resolved_measures);

  // Resolves an expression that is used as a bound on a quantifier.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> ResolveQuantifierBound(
      const ASTExpression* ast_quantifier_bound,
      ExprResolutionInfo* expr_resolution_info);

  absl::Status ResolveAuxLoadDataStatement(
      const ASTAuxLoadDataStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  // Recursively translate the CollationAnnotation and
  // TimestampPrecisionAnnotation in <type_annotation_map> into a
  // ResolvedColumnAnnotations object. The provided <options_list> is applied to
  // only the top-level ResolvedColumnAnnotation.
  absl::StatusOr<std::unique_ptr<ResolvedColumnAnnotations>>
  MakeResolvedColumnAnnotationsFromAnnotationMap(
      const Type* target_type, const AnnotationMap* type_annotation_map,
      const ASTOptionsList* options_list);

  // Creates a name scope with all column names with access errors. When default
  // value expression references a column in the name scope, it throws error.
  // <allow_duplicates> prevents returning an internal error if
  // <all_column_names> contains a duplicate column (e.g. in the case of ADD
  // COLUMN IF NOT EXISTS referencing an existing column); by default it is set
  // to false.
  absl::StatusOr<std::unique_ptr<NameScope>>
  CreateNameScopeWithAccessErrorForDefaultExpr(
      IdString table_name_id_string, std::vector<IdString>& all_column_names,
      bool allow_duplicates = false);

  // Throws an error if the input <resolved_expr> has collation.
  // <error_template> is used to produce error message and the debug string of
  // collation annotations of <resolved_expr> will replace '$0'.
  absl::Status ThrowErrorIfExprHasCollation(const ASTNode* error_node,
                                            absl::string_view error_template,
                                            const ResolvedExpr* resolved_expr);

  // Validates the UNNEST expression `unnest_expr`:
  // - Contains at most one expression.
  // - Does not specify `array_zip_mode`.
  // - The expression inside `unnest_expr` does not have alias specified.
  absl::Status ValidateUnnestSingleExpression(
      const ASTUnnestExpression* unnest_expr,
      absl::string_view expression_type) const;

  // Information about an array element column's alias name and parse location.
  struct UnnestArrayColumnAlias {
    IdString alias;
    const ASTNode* alias_location;
  };

  // Enforce correct application of table and column aliases of the given
  // explicit UNNEST expression `table_ref`.
  absl::Status ValidateUnnestAliases(const ASTTablePathExpression* table_ref);

  // Resolve the `mode` argument for the given UNNEST expression and coerce to
  // ARRAY_ZIP_MODE enum type if necessary.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>> ResolveArrayZipMode(
      const ASTUnnestExpression* unnest, ExprResolutionInfo* info);

  // Obtain alias name and location for a given array argument in multiway
  // UNNEST, whose number of arguments should be greater than 1.
  // Returns empty alias if no explicit alias is provided or it's impossible to
  // infer alias. The returned alias_location should always be valid.
  UnnestArrayColumnAlias GetArrayElementColumnAlias(
      const ASTExpressionWithOptAlias* argument);

  // Resolve a single array argument for explicit UNNEST.
  // `num_arguments` indicates if this is a multi-argument UNNEST.
  absl::Status ResolveArrayArgumentForExplicitUnnest(
      const ASTExpressionWithOptAlias* argument,
      UnnestArrayColumnAlias& arg_alias, int num_unnest_arguments,
      ExprResolutionInfo* info,
      std::vector<UnnestArrayColumnAlias>& output_alias_list,
      ResolvedColumnList& output_column_list,
      std::shared_ptr<NameList>& output_name_list,
      std::vector<std::unique_ptr<const ResolvedExpr>>&
          resolved_array_expr_list,
      std::vector<ResolvedColumn>& resolved_element_column_list);

  // Resolve N-ary UNNEST operator with N >= 1.
  absl::Status ResolveUnnest(
      const ASTTablePathExpression* table_ref, ExprResolutionInfo* info,
      std::vector<UnnestArrayColumnAlias>& output_alias_list,
      ResolvedColumnList& output_column_list,
      std::shared_ptr<NameList>& output_name_list,
      std::vector<std::unique_ptr<const ResolvedExpr>>&
          resolved_array_expr_list,
      std::vector<ResolvedColumn>& resolved_element_column_list);

  // Resolves the proto expression portion of the UPDATE constructor i.e.
  // "UPDATE(foo.bar AS baz)".
  // The resolved proto expression is returned in `expr_to_modify` and if an
  // alias is present it is filled in `alias`.
  absl::Status ResolveUpdateConstructorProtoExpression(
      const ASTUpdateConstructor& ast_update_constructor,
      ExprResolutionInfo* expr_resolution_info, std::string& alias,
      std::unique_ptr<const ResolvedExpr>& expr_to_modify);

  // Fills in `last_field_type` with the type of a the last element found in
  // FindFieldsOutput.
  static absl::Status GetLastSeenFieldType(
      const FindFieldsOutput& output,
      absl::Span<const std::string> catalog_name_path,
      TypeFactory* type_factory, const Type** last_field_type);

  // Resolves the given function call (NEXT() or PREV()) as a LEAD() or LAG()
  // analytic function call. Used only for MATCH_RECOGNIZE navigation functions.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
  ResolveAsMatchRecognizePhysicalNavigationFunction(
      const ASTNode* ast_location, const Function* function,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<ResolvedFunctionCall> resolved_function_call);

  // Validates that the result type of a REGEXP_EXTRACT_GROUPS() function call
  // can be cast to the actual struct type with field types based on type
  // suffixes of the capturing groups.
  absl::Status ValidateRegexpExtractGroupsResultCast(
      const ResolvedFunctionCall* resolved_function_call,
      const Type* result_type, const Type* cast_result_type);

  // Resolves the REGEXP_EXTRACT_GROUPS() function call, by wrapping it in an
  // explicit CAST to a STRUCT with field types derived from the names of the
  // capturing groups.
  absl::StatusOr<std::unique_ptr<const ResolvedExpr>>
  ResolveAsRegexpExtractGroupsFunction(
      const ASTNode* ast_location, const Function* function,
      ExprResolutionInfo* expr_resolution_info,
      std::unique_ptr<ResolvedFunctionCall> resolved_function_call);

  absl::Status ResolveAlterSequenceStatement(
      const ASTAlterSequenceStatement* ast_statement,
      std::unique_ptr<ResolvedStatement>* output);

  // Builds the string representation of a field path using `struct_path_prefix`
  // and `proto_field_path_suffix` and attempts to add it to `field_path_trie`.
  // If non-empty, the field path is expanded starting with the fields in
  // `struct_path_prefix`. Returns an error if this path string overlaps with a
  // path that is already present in `field_path_trie`. For example,
  // message.nested and message.nested.field are overlapping field paths, but
  // message.nested.field1 and message.nested.field2 are not overlapping. Two
  // paths that modify the same OneOf field are considered overlapping only when
  // the language feature FEATURE_REPLACE_FIELDS_ALLOW_MULTI_ONEOF is not
  // enabled. `oneof_path_to_full_path` maps from paths of OneOf fields that
  // have already been modified to the corresponding path expression that
  // accessed the OneOf path. If `proto_field_path_suffix` modifies a OneOf
  // field that has not already been modified, it will be added to
  // `oneof_path_to_full_path`.
  static absl::Status AddToFieldPathTrie(
      const LanguageOptions& language_options, const ASTNode* path_location,
      absl::Span<const FindFieldsOutput::StructFieldInfo> struct_path_prefix,
      const std::vector<const google::protobuf::FieldDescriptor*>&
          proto_field_path_suffix,
      absl::flat_hash_map<std::string, std::string>* oneof_path_to_full_path,
      googlesql_base::GeneralTrie<const ASTNode*, nullptr>* field_path_trie);

  friend class AnalyticFunctionResolver;
  friend class FunctionResolver;
  friend class FunctionResolverTest;
  friend class GraphStmtResolver;
  friend class GraphTableQueryResolver;
  friend class ResolverTest;
  FRIEND_TEST(ResolverTest, TestGetFunctionNameAndArguments);
  FRIEND_TEST(ResolverTest, TestPipeDistinct);
};

// Encapsulates metadata about function arguments when resolving a
// `CREATE ... FUNCTION` statement or when resolving the body of an invoked
// function template.
class FunctionArgumentInfo {
 public:
  // Details about a specific argument.
  struct ArgumentDetails {
    IdString name;
    FunctionArgumentType arg_type;
    // <arg_kind> is used only for scalar arguments.
    std::optional<ResolvedArgumentDef::ArgumentKind> arg_kind;
    // Annotations are used only for scalar arguments.
    const AnnotationMap* annotation_map = nullptr;
  };

  // Returns true if there is an arg <name>.
  bool HasArg(const IdString& name) const;

  // Returns a pointer to the argument details if a relational argument <name>
  // is found in this metadata. Otherwise, returns nullptr.
  const ArgumentDetails* FindTableArg(IdString name) const;

  // Returns a pointer to the argument details if a scalar argument <name> is
  // found in this metadata. Otherwise, returns nullptr.
  const ArgumentDetails* FindScalarArg(IdString name) const;

  // Returns a pointer to the argument details if an argument <name> is found in
  // this metadata. Otherwise, returns nullptr.
  const ArgumentDetails* FindArg(IdString name) const;

  // Returns true if any of the arguments contained is a template argument.
  bool contains_templated_arguments() const {
    return contains_templated_arguments_;
  }

  // Returns a list of argument names as strings in the order they were added.
  std::vector<std::string> ArgumentNames() const;

  // Returns a list of argument types in the order they were added. This is used
  // when constructing a FunctionSignature.
  FunctionArgumentTypeList SignatureArguments() const;

  // Return a list of argument details.
  std::vector<const ArgumentDetails*> GetArgumentDetails() const;

  // Add details of a scalar argument.
  absl::Status AddScalarArg(IdString name,
                            ResolvedArgumentDef::ArgumentKind arg_kind,
                            FunctionArgumentType arg_type,
                            const AnnotationMap* annotation_map);

  // Add details for a relation argument.
  absl::Status AddRelationArg(IdString name, FunctionArgumentType arg_type);

 private:
  // std::unique_ptr is used to ensure stability of any pointers to details
  // returned even when more arguments are added to the details_ list.
  // details_ is stored in argument order to enable constructing function
  // signature and NameLists.
  std::vector<std::unique_ptr<ArgumentDetails>> details_;
  // This map functions as an index of details to make lookup-by-name cheap and
  // idiomatic.
  IdStringHashMapCase<int64_t> details_index_by_name_;
  bool contains_templated_arguments_ = false;

  absl::Status AddArgCommon(ArgumentDetails details);
};

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_RESOLVER_H_
