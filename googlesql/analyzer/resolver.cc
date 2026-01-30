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

#include "googlesql/analyzer/resolver.h"

#include <stddef.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/base/atomic_sequence_num.h"
#include "googlesql/base/logging.h"
#include "googlesql/analyzer/annotation_propagator.h"
#include "googlesql/analyzer/expr_resolver_helper.h"
#include "googlesql/analyzer/function_resolver.h"
#include "googlesql/analyzer/name_scope.h"
#include "googlesql/public/aggregation_threshold_utils.h"
// This includes common macro definitions to define in the resolver cc files.
#include "googlesql/analyzer/query_resolver_helper.h"
#include "googlesql/analyzer/resolver_common_inl.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/status_payload_utils.h"
#include "googlesql/common/warning_sink.h"
#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parse_tree_errors.h"
#include "googlesql/parser/parser.h"
#include "googlesql/proto/internal_error_location.pb.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/annotation/collation.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/coercer.h"
#include "googlesql/public/deprecation_warning.pb.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/error_location.pb.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/functions/convert_string.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/input_argument_type.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/collation.h"
#include "googlesql/public/types/simple_value.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/type_parameters.h"
#include "googlesql/public/value.h"
#include "googlesql/public/with_modifier_mode.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/make_node_vector.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_collation.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/base/case.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

const std::map<int, Resolver::SpecialArgumentType>* const
    Resolver::kEmptyArgumentOptionMap =
        new std::map<int, Resolver::SpecialArgumentType>();

Resolver::Resolver(Catalog* catalog, TypeFactory* type_factory,
                   const AnalyzerOptions* analyzer_options)
    : catalog_(catalog),
      type_factory_(type_factory),
      analyzer_options_(*analyzer_options),
      coercer_(type_factory, &analyzer_options_.language(), catalog),
      empty_name_list_(new NameList),
      empty_name_scope_(new NameScope(*empty_name_list_)),
      id_string_pool_(analyzer_options_.id_string_pool().get()),
      warning_sink_(/*consider_location=*/false) {
  function_resolver_ =
      std::make_unique<FunctionResolver>(catalog, type_factory, this);
  annotation_propagator_ =
      std::make_unique<AnnotationPropagator>(*analyzer_options, *type_factory);
  ABSL_DCHECK(analyzer_options_.AllArenasAreInitialized());
}

Resolver::~Resolver() = default;

void Resolver::Reset(absl::string_view sql) {
  sql_ = sql;
  named_subquery_map_.clear();
  unique_with_alias_names_.clear();
  next_subquery_id_ = 1;
  next_unnest_id_ = 1;
  analyzing_expression_ = false;
  analyzing_partition_by_clause_name_ = nullptr;
  disallowing_query_parameters_with_error_ = {};
  // generated_column_cycle_detector_ contains a pointer to a local object, so
  // there is no need to deallocate.
  generated_column_cycle_detector_ = nullptr;
  analyzing_nonvolatile_stored_expression_columns_ = false;
  analyzing_check_constraint_expression_ = false;
  default_expr_access_error_name_scope_.reset();
  warning_sink_.Reset();
  function_argument_info_ = nullptr;
  resolved_columns_from_table_scans_.clear();
  side_effect_scope_depth_ = 0;
  graph_context_stack_ = {};
  referenced_property_graphs_ = {};
  lock_mode_stack_ = {};

  if (analyzer_options_.column_id_sequence_number() != nullptr) {
    column_factory_ = std::make_unique<ColumnFactory>(
        /*max_col_id=*/0, *id_string_pool_,
        *analyzer_options_.column_id_sequence_number());
  } else {
    column_factory_ = std::make_unique<ColumnFactory>(
        /*max_col_id=*/0, *id_string_pool_,
        std::make_unique<googlesql_base::SequenceNumber>());
  }
}

ResolvedColumn Resolver::MakeGroupingOutputColumn(
    const ExprResolutionInfo* expr_resolution_info, const IdString grouping_id,
    AnnotatedType annotated_type) {
  const uint64_t size =
      expr_resolution_info->query_resolution_info->grouping_columns_list()
          .size();
  IdString alias = MakeIdString(absl::StrCat("$grouping_call", size + 1));
  ResolvedColumn grouping_argument_column(AllocateColumnId(), grouping_id,
                                          alias, annotated_type);
  return grouping_argument_column;
}

int Resolver::AllocateColumnId() { return column_factory_->AllocateColumnId(); }

IdString Resolver::AllocateSubqueryName() {
  return MakeIdString(absl::StrCat("$subquery", next_subquery_id_++));
}

IdString Resolver::AllocateUnnestName() {
  return MakeIdString(absl::StrCat("$unnest", next_unnest_id_++));
}

IdString Resolver::MakeIdString(absl::string_view str) const {
  return id_string_pool_->Make(str);
}

std::unique_ptr<const ResolvedLiteral> Resolver::MakeResolvedLiteral(
    const ASTNode* ast_location, const Value& value,
    bool set_has_explicit_type) const {
  auto resolved_literal = googlesql::MakeResolvedLiteral(value.type(), value,
                                                         set_has_explicit_type);
  MaybeRecordParseLocation(ast_location, resolved_literal.get());
  return resolved_literal;
}

std::unique_ptr<const ResolvedLiteral> Resolver::MakeResolvedLiteral(
    const ASTNode* ast_location, AnnotatedType annotated_type,
    const Value& value, bool has_explicit_type) const {
  auto status_or_resolved_literal =
      ResolvedLiteralBuilder()
          .set_value(value)
          .set_type(annotated_type.type)
          .set_type_annotation_map(annotated_type.annotation_map)
          .set_has_explicit_type(has_explicit_type)
          .Build();
  GOOGLESQL_DCHECK_OK(status_or_resolved_literal.status());
  MaybeRecordParseLocation(
      ast_location,
      const_cast<ResolvedLiteral*>(status_or_resolved_literal.value().get()));
  return std::move(status_or_resolved_literal.value());
}

std::unique_ptr<const ResolvedLiteral> Resolver::MakeResolvedFloatLiteral(
    const ASTNode* ast_location, const Type* type, const Value& value,
    bool has_explicit_type, absl::string_view image) {
  if (!language().LanguageFeatureEnabled(FEATURE_NUMERIC_TYPE) &&
      !language().LanguageFeatureEnabled(FEATURE_BIGNUMERIC_TYPE)) {
    return MakeResolvedLiteral(ast_location, {type, /*annotation_map=*/nullptr},
                               value, has_explicit_type);
  }
  const int float_literal_id = next_float_literal_image_id_++;
  auto resolved_literal = googlesql::MakeResolvedLiteral(
      type, value, has_explicit_type, float_literal_id);
  float_literal_images_[float_literal_id] = std::string(image);
  MaybeRecordParseLocation(ast_location, resolved_literal.get());
  return resolved_literal;
}

// static
std::unique_ptr<const ResolvedLiteral>
Resolver::MakeResolvedLiteralWithoutLocation(const Value& value) {
  return googlesql::MakeResolvedLiteral(value);
}

absl::Status Resolver::AddAdditionalDeprecationWarningsForCalledFunction(
    const ASTNode* ast_location, const FunctionSignature& signature,
    absl::string_view function_name, bool is_tvf) {
  absl::flat_hash_set<DeprecationWarning_Kind> warning_kinds_seen;
  for (const FreestandingDeprecationWarning& warning :
       signature.AdditionalDeprecationWarnings()) {
    const DeprecationWarning_Kind warning_kind =
        warning.deprecation_warning().kind();
    // To facilitate log analysis, we record a warning for every deprecation
    // warning kind present in the body. But to avoid creating too many
    // warnings, we only record the first warning for each kind.
    if (!warning_kinds_seen.emplace(warning_kind).second) {
      continue;
    }
    // For non-TVFs, 'function_name' starts with "Function ".
    std::string warning_message =
        absl::StrCat(is_tvf ? "Table-valued function " : "", function_name,
                     " triggers a deprecation warning with kind ",
                     DeprecationWarning_Kind_Name(warning_kind));
    absl::Status warning_status = MakeSqlErrorAt(ast_location)
                                  << warning_message;
    GOOGLESQL_RET_CHECK(
        internal::HasPayloadWithType<InternalErrorLocation>(warning_status));
    auto internal_error_location =
        internal::GetPayload<InternalErrorLocation>(warning_status);
    // Add the error sources from `warning` to `internal_error_location`.
    google::protobuf::RepeatedPtrField<ErrorSource>* error_sources =
        internal_error_location.mutable_error_source();
    GOOGLESQL_RET_CHECK(error_sources->empty());
    error_sources->CopyFrom(warning.error_location().error_source());

    // Add a new error source corresponding to `warning`.
    ErrorSource* new_error_source = error_sources->Add();
    new_error_source->set_error_message(warning.message());
    new_error_source->set_error_message_caret_string(warning.caret_string());
    *new_error_source->mutable_error_location() = warning.error_location();

    // Override existing error location
    internal::AttachPayload(&warning_status, internal_error_location);
    GOOGLESQL_RETURN_IF_ERROR(warning_sink_.AddWarning(warning_kind, warning_status));
  }

  return absl::OkStatus();
}

absl::Status Resolver::AddDeprecationWarning(const ASTNode* ast_location,
                                             DeprecationWarning::Kind kind,
                                             absl::string_view message) {
  return warning_sink_.AddWarning(kind, MakeSqlErrorAt(ast_location)
                                            << message);
}

absl::Status Resolver::ResolveStandaloneExpr(
    absl::string_view sql, const ASTExpression* ast_expr,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  Reset(sql);
  analyzing_expression_ = true;

  // target_column_types is only allowed on statements
  if (!analyzer_options().get_target_column_types().empty()) {
    return MakeSqlError() << "AnalyzerOptions contain target column types, "
                          << "which are not currently supported when resolving "
                          << "standalone expressions";
  }

  if (analyzer_options_.allow_aggregate_standalone_expression()) {
    std::unique_ptr<QueryResolutionInfo> query_resolution_info =
        std::make_unique<QueryResolutionInfo>(this);
    auto expr_resolution_info = std::make_unique<ExprResolutionInfo>(
        query_resolution_info.get(), empty_name_scope_.get(),
        ExprResolutionInfoOptions{.allows_aggregation = true,
                                  .clause_name = "standalone expression"});
    GOOGLESQL_RETURN_IF_ERROR(
        ResolveExpr(ast_expr, expr_resolution_info.get(), resolved_expr_out));
    // Columns in the `ORDER BY` clause of an aggregate function call are
    // pushed into a ProjectScan, and are thus not available in the standalone
    // expression resolution.
    if (!query_resolution_info
             ->select_list_columns_to_compute_before_aggregation()
             ->empty()) {
      return MakeSqlErrorAt(ast_expr)
             << "Standalone expression resolution does not support aggregate "
                "function calls with the ORDER BY clause";
    }
  } else {
    GOOGLESQL_RETURN_IF_ERROR(ResolveScalarExpr(ast_expr, empty_name_scope_.get(),
                                      "standalone expression",
                                      resolved_expr_out));
  }
  GOOGLESQL_RETURN_IF_ERROR(ValidateUndeclaredParameters(resolved_expr_out->get()));
  GOOGLESQL_RETURN_IF_ERROR(PruneColumnLists(resolved_expr_out->get()));
  return absl::OkStatus();
}

absl::Status Resolver::ResolveStandaloneExprAndAssignToType(
    absl::string_view sql, const ASTExpression* ast_expr,
    AnnotatedType target_type,
    std::unique_ptr<const ResolvedExpr>* resolved_expr_out) {
  GOOGLESQL_RETURN_IF_ERROR(ResolveStandaloneExpr(sql, ast_expr, resolved_expr_out));
  return CoerceExprToType(ast_expr, target_type, Resolver::kImplicitAssignment,
                          resolved_expr_out);
}

absl::Status Resolver::ResolveExprWithFunctionArguments(
    absl::string_view sql, const ASTExpression* ast_expr,
    const IdStringHashMapCase<std::unique_ptr<ResolvedArgumentRef>>&
        function_arguments,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* output) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  Reset(sql);
  auto arg_info = std::make_unique<FunctionArgumentInfo>();
  for (auto& [arg_name, resolved_arg] : function_arguments) {
    GOOGLESQL_RETURN_IF_ERROR(
        arg_info->AddScalarArg(arg_name, resolved_arg->argument_kind(),
                               FunctionArgumentType(resolved_arg->type()),
                               resolved_arg->type_annotation_map()));
  }
  auto scoped_reset = SetArgumentInfo(arg_info.get());
  disallowing_query_parameters_with_error_ =
      "Query parameters cannot be used inside SQL function bodies";
  std::unique_ptr<const ResolvedExpr> resolved_expr;
  GOOGLESQL_RETURN_IF_ERROR(ConvertInternalErrorPayloadsToExternal(
      ResolveExpr(ast_expr, expr_resolution_info, &resolved_expr), sql));
  *output = std::move(resolved_expr);
  return absl::OkStatus();
}

absl::Status Resolver::ResolveQueryStatementWithFunctionArguments(
    absl::string_view sql, const ASTQueryStatement* query_stmt,
    const std::optional<TVFRelation>& specified_output_schema,
    bool allow_query_parameters,
    IdStringHashMapCase<std::unique_ptr<ResolvedArgumentRef>>*
        function_arguments,
    IdStringHashMapCase<TVFRelation>* function_table_arguments,
    std::unique_ptr<const ResolvedStatement>* output_stmt,
    std::shared_ptr<const NameList>* output_name_list) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  Reset(sql);
  auto arg_info = std::make_unique<FunctionArgumentInfo>();
  for (auto& [arg_name, resolved_arg] : *function_arguments) {
    GOOGLESQL_RETURN_IF_ERROR(
        arg_info->AddScalarArg(arg_name, resolved_arg->argument_kind(),
                               FunctionArgumentType(resolved_arg->type()),
                               resolved_arg->type_annotation_map()));
  }
  for (auto& [arg_name, tvf_relation] : *function_table_arguments) {
    // The 'extra_relation_input_columns_allowed' argument value does not matter
    // in this case. It is used when matching TVF function signatures while this
    // code is invoked only after a signature is matched.
    FunctionArgumentTypeOptions argument_type_options(
        tvf_relation, /*extra_relation_input_columns_allowed=*/true);
    FunctionArgumentType arg_type(ARG_TYPE_RELATION, argument_type_options);
    GOOGLESQL_RETURN_IF_ERROR(arg_info->AddRelationArg(arg_name, arg_type));
  }
  auto scoped_reset = SetArgumentInfo(arg_info.get());
  if (!allow_query_parameters) {
    disallowing_query_parameters_with_error_ =
        "Query parameters cannot be used inside SQL function bodies";
  }
  std::unique_ptr<ResolvedStatement> resolved_statement;
  GOOGLESQL_RETURN_IF_ERROR(ConvertInternalErrorPayloadsToExternal(
      ResolveQueryStatement(query_stmt, &resolved_statement, output_name_list),
      sql));

  // If the function signature specified a required output schema that the TVF
  // must return, this method compares it against <return_tvf_relation> and
  // returns an error if the expected and provided column types are incompatible
  // or otherwise adds coercions for column types that are not Equals().
  if (specified_output_schema) {
    GOOGLESQL_RET_CHECK_EQ(RESOLVED_QUERY_STMT, resolved_statement->node_kind());
    auto* resolved_query =
        static_cast<ResolvedQueryStmt*>(resolved_statement.get());
    std::vector<std::unique_ptr<const ResolvedOutputColumn>>
        output_column_list = resolved_query->release_output_column_list();
    std::unique_ptr<const ResolvedScan> output_query =
        resolved_query->release_query();

    // We don't have a pointer to the whole CREATE TABLE FUNCTION statement,
    // so we pass the location of its body.
    GOOGLESQL_RETURN_IF_ERROR(ConvertInternalErrorPayloadsToExternal(
        CheckSQLBodyReturnTypesAndCoerceIfNeeded(
            query_stmt, *specified_output_schema, output_name_list->get(),
            &output_query, &output_column_list),
        sql));
    resolved_statement = MakeResolvedQueryStmt(
        std::move(output_column_list),
        specified_output_schema->is_value_table(), std::move(output_query));
  }

  GOOGLESQL_RETURN_IF_ERROR(PruneColumnLists(resolved_statement.get()));
  *output_stmt = std::move(resolved_statement);
  return absl::OkStatus();
}

absl::Status Resolver::ResolveTypeName(absl::string_view type_name,
                                       const Type** type) {
  TypeModifiers type_modifiers;
  return ResolveTypeName(type_name, type, &type_modifiers);
}

absl::Status Resolver::ResolveTypeNameInternal(absl::string_view type_name,
                                               const Type** type,
                                               TypeModifiers* type_modifiers) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_RETURN_IF_ERROR(ParseType(type_name, analyzer_options_.GetParserOptions(),
                            &parser_output));
  GOOGLESQL_RETURN_IF_ERROR(
      ResolveType(parser_output->type(),
                  {.allow_type_parameters = true, .allow_collation = true},
                  type, type_modifiers));
  return absl::OkStatus();
}

absl::Status Resolver::ResolveTypeName(absl::string_view type_name,
                                       const Type** type,
                                       TypeModifiers* type_modifiers) {
  // Reset state because ResolveTypeName is a public entry-point.
  Reset(type_name);
  return ResolveTypeNameInternal(type_name, type, type_modifiers);
}

ResolvedColumnList Resolver::ConcatColumnLists(
    const ResolvedColumnList& left, const ResolvedColumnList& right) {
  ResolvedColumnList out = left;
  for (const ResolvedColumn& column : right) {
    out.emplace_back(column);
  }
  return out;
}

ResolvedColumnList Resolver::ConcatColumnListWithComputedColumnsAndSort(
    const ResolvedColumnList& column_list,
    absl::Span<const std::unique_ptr<const ResolvedComputedColumn>>
        computed_columns) {
  ResolvedColumnList out = column_list;
  for (const std::unique_ptr<const ResolvedComputedColumn>& computed_column :
       computed_columns) {
    out.push_back(computed_column->column());
  }
  std::sort(out.begin(), out.end());
  return out;
}

absl::StatusOr<bool> Resolver::MaybeAssignTypeToUndeclaredParameter(
    std::unique_ptr<const ResolvedExpr>* expr, const Type* type) {
  if (expr->get()->node_kind() != RESOLVED_PARAMETER) {
    return false;
  }
  const ResolvedParameter* parameter = expr->get()->GetAs<ResolvedParameter>();
  if (!parameter->is_untyped()) {
    return false;
  }
  const ParseLocationRange* location = parameter->GetParseLocationRangeOrNULL();
  GOOGLESQL_RET_CHECK(location != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(AssignTypeToUndeclaredParameter(location->start(), type));
  auto coerced_parameter = MakeResolvedParameter(type, parameter->name(),
                                                 parameter->position(), false);
  if (parameter->GetParseLocationRangeOrNULL() != nullptr) {
    coerced_parameter->SetParseLocationRange(
        *parameter->GetParseLocationRangeOrNULL());
  }
  *expr = std::move(coerced_parameter);
  return true;
}

absl::StatusOr<QueryParametersMap> Resolver::AssignTypesToUndeclaredParameters()
    const {
  QueryParametersMap resolved_undeclared_parameters;
  for (const auto& [name, locations_and_types] : undeclared_parameters_) {
    for (const auto& [location, type] : locations_and_types) {
      auto [it, _] = resolved_undeclared_parameters.insert({name, type});
      if (!it->second->Equals(type)) {
        // Currently, we require the types to agree exactly, even for different
        // version of protos. This can be relaxed in the future, incl. using
        // common supertypes.
        const Type* previous_type = it->second;
        if (previous_type->Equivalent(type)) {
          return MakeSqlErrorAtPoint(location)
                 << "Undeclared parameter '" << name
                 << "' is used assuming different versions of the same type ("
                 << type->ShortTypeName(product_mode()) << ")";
        } else {
          return MakeSqlErrorAtPoint(location)
                 << "Undeclared parameter '" << name
                 << "' is used assuming different types ("
                 << previous_type->ShortTypeName(product_mode()) << " vs "
                 << type->ShortTypeName(product_mode()) << ")";
        }
      }
    }
  }

  return resolved_undeclared_parameters;
}

absl::Status Resolver::AssignTypeToUndeclaredParameter(
    const ParseLocationPoint& location, const Type* type) {
  const auto it = untyped_undeclared_parameters_.find(location);
  GOOGLESQL_RET_CHECK(it != untyped_undeclared_parameters_.end());
  const std::variant<std::string, int> name_or_position = it->second;
  untyped_undeclared_parameters_.erase(it);

  if (std::holds_alternative<std::string>(name_or_position)) {
    const std::string& name = std::get<std::string>(name_or_position);
    undeclared_parameters_[name].emplace_back(location, type);
  } else {
    const int position = std::get<int>(name_or_position);
    if (position - 1 >= undeclared_positional_parameters_.size()) {
      // The resolver has not visited this undeclared positional parameter
      // before. The resolver may visit an undeclared parameter multiple times
      // and assign a different type, such as when coercing parameters to a
      // common supertype in a BETWEEN expression.
      undeclared_positional_parameters_.resize(position);
    }
    undeclared_positional_parameters_[position - 1] = type;
  }

  return absl::OkStatus();
}

absl::Status Resolver::ValidateUndeclaredParameters(const ResolvedNode* node) {
  if (!analyzer_options_.allow_undeclared_parameters()) {
    return absl::OkStatus();
  }

  // Copying avoids modifying the collection while iterating over it.
  const auto copy = untyped_undeclared_parameters_;
  for (const auto& location_and_name : copy) {
    GOOGLESQL_RETURN_IF_ERROR(AssignTypeToUndeclaredParameter(location_and_name.first,
                                                    types::Int64Type()));
  }
  return absl::OkStatus();
}

absl::Status Resolver::MakeEqualityComparison(
    const ASTNode* ast_location, std::unique_ptr<const ResolvedExpr> expr1,
    std::unique_ptr<const ResolvedExpr> expr2,
    std::unique_ptr<const ResolvedExpr>* output_expr) {
  std::unique_ptr<ResolvedFunctionCall> resolved_function_call;
  GOOGLESQL_RETURN_IF_ERROR(function_resolver_->ResolveGeneralFunctionCall(
      ast_location, {ast_location, ast_location},
      /*match_internal_signatures=*/false, "$equal", /*is_analytic=*/false,
      MakeNodeVector(std::move(expr1), std::move(expr2)),
      /*named_arguments=*/{}, /*expected_result_type=*/nullptr,
      &resolved_function_call));

  *output_expr = std::move(resolved_function_call);
  return absl::OkStatus();
}

absl::Status Resolver::MakeNotExpr(
    const ASTNode* ast_location, std::unique_ptr<const ResolvedExpr> expr,
    ExprResolutionInfo* expr_resolution_info,
    std::unique_ptr<const ResolvedExpr>* expr_out) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  GOOGLESQL_RET_CHECK(expr->type()->IsBool())
      << "MakeNotExpr can only be called on bool: "
      << expr->type()->ShortTypeName(product_mode());
  std::vector<std::unique_ptr<const ResolvedExpr>> arguments;
  arguments.push_back(std::move(expr));
  return ResolveFunctionCallWithResolvedArguments(
      ast_location, {ast_location}, /*match_internal_signatures=*/false, "$not",
      std::move(arguments), /*named_arguments=*/{}, expr_resolution_info,
      expr_out);
}

absl::Status Resolver::MakeCoalesceExpr(
    const ASTNode* ast_location, const ResolvedColumnList& columns,
    std::unique_ptr<const ResolvedExpr>* output_expr) {
  GOOGLESQL_RET_CHECK_GE(columns.size(), 1);

  std::vector<std::unique_ptr<const ResolvedExpr>> exprs;
  for (const ResolvedColumn& column : columns) {
    exprs.push_back(MakeColumnRef(column));
  }

  std::unique_ptr<ResolvedFunctionCall> resolved_function_call;
  // Coerces the arguments to a common supertype, if necessary.
  size_t exprs_size = exprs.size();
  GOOGLESQL_RETURN_IF_ERROR(function_resolver_->ResolveGeneralFunctionCall(
      ast_location, std::vector<const ASTNode*>(exprs_size, ast_location),
      /*match_internal_signatures=*/false, "coalesce", /*is_analytic=*/false,
      std::move(exprs), /*named_arguments=*/{},
      /*expected_result_type=*/nullptr, &resolved_function_call));

  *output_expr = std::move(resolved_function_call);
  return absl::OkStatus();
}

absl::Status Resolver::MakeAndExpr(
    const ASTNode* ast_location,
    std::vector<std::unique_ptr<const ResolvedExpr>> exprs,
    std::unique_ptr<const ResolvedExpr>* output_expr) const {
  GOOGLESQL_RET_CHECK_GE(exprs.size(), 1);
  for (const std::unique_ptr<const ResolvedExpr>& expr : exprs) {
    GOOGLESQL_RET_CHECK(expr->type()->IsBool()) << expr->DebugString();
  }

  if (exprs.size() == 1) {
    *output_expr = std::move(exprs[0]);
  } else {
    int expr_count = exprs.size();
    // Construct the AND expression and resolve a concrete signature.
    std::unique_ptr<ResolvedFunctionCall> resolved_function_call;
    GOOGLESQL_RETURN_IF_ERROR(function_resolver_->ResolveGeneralFunctionCall(
        ast_location, std::vector<const ASTNode*>(expr_count, ast_location),
        /*match_internal_signatures=*/false, "$and", /*is_analytic=*/false,
        std::move(exprs), /*named_arguments=*/{},
        /*expected_result_type=*/nullptr, &resolved_function_call));

    GOOGLESQL_RET_CHECK_EQ(resolved_function_call->function()->mode(), Function::SCALAR);

    *output_expr = std::move(resolved_function_call);
  }
  return absl::OkStatus();
}

std::unique_ptr<ResolvedColumnRef> Resolver::MakeColumnRef(
    const ResolvedColumn& column, bool is_correlated,
    ResolvedStatement::ObjectAccess access_flags) {
  ABSL_DCHECK(column.IsInitialized());
  RecordColumnAccess(column, access_flags);
  std::unique_ptr<ResolvedColumnRef> resolved_node =
      MakeResolvedColumnRef(column.type(), column, is_correlated);
  // TODO: Replace ABSL_DCHECK below with GOOGLESQL_RETURN_IF_ERROR and update all
  // the references of this function.
  absl::Status status =
      CheckAndPropagateAnnotations(/*error_node=*/nullptr, resolved_node.get());
  GOOGLESQL_DCHECK_OK(status);
  return resolved_node;
}

std::unique_ptr<ResolvedColumnRef> Resolver::MakeColumnRefWithCorrelation(
    const ResolvedColumn& column,
    const CorrelatedColumnsSetList& correlated_columns_sets,
    ResolvedStatement::ObjectAccess access_flags) {
  bool is_correlated = false;
  if (!correlated_columns_sets.empty()) {
    is_correlated = true;
    for (CorrelatedColumnsSet* column_set : correlated_columns_sets) {
      // If we are referencing a variable correlated through more than one
      // level of subquery, the sets are ordered so that the set for
      // the outermost query is last.
      const bool is_already_correlated =
          (column_set != correlated_columns_sets.back());
      if (!googlesql_base::InsertIfNotPresent(column_set, column, is_already_correlated)) {
        // is_already_correlated should always be computed consistently.
        ABSL_DCHECK_EQ((*column_set)[column], is_already_correlated)
            << column.DebugString();
      }
    }
  }
  return MakeColumnRef(column, is_correlated, access_flags);
}

// static
std::unique_ptr<const ResolvedColumnRef> Resolver::CopyColumnRef(
    const ResolvedColumnRef* column_ref) {
  auto resolved_column_ref = MakeResolvedColumnRef(
      column_ref->type(), column_ref->column(), column_ref->is_correlated());
  resolved_column_ref->set_type_annotation_map(
      column_ref->type_annotation_map());
  return resolved_column_ref;
}

absl::Status Resolver::ResolvePathExpressionAsType(
    const ASTPathExpression* path_expr, bool is_single_identifier,
    const Type** resolved_type) const {
  const std::vector<std::string> identifier_path =
      path_expr->ToIdentifierVector();

  // Fast-path check for builtin SimpleTypes. If we do not find the name here,
  // then we will try to look up the type name in <catalog_>.
  if (identifier_path.size() == 1) {
    TypeKind type_kind = Type::ResolveBuiltinTypeNameToKindIfSimple(
        identifier_path[0], language());
    if (type_kind != TYPE_UNKNOWN) {
      *resolved_type = type_factory_->MakeSimpleType(type_kind);
      ABSL_DCHECK((*resolved_type)->IsSupportedType(language()))
          << identifier_path[0];
      return absl::OkStatus();
    }
  }

  std::string single_name;
  if (is_single_identifier) {
    single_name = absl::StrJoin(path_expr->ToIdentifierVector(), ".");
  }

  const absl::Status status = catalog_->FindType(
      (is_single_identifier ? std::vector<std::string>{single_name}
                            : identifier_path),
      resolved_type, analyzer_options_.find_options());
  if (status.code() == absl::StatusCode::kNotFound ||
      // TODO: Ideally, Catalogs should not include unsupported types.
      // As such, we should remove the IsSupportedType() check. But we need to
      // verify with engines to ensure they do not include unsupported types in
      // their Catalogs before removing this check.
      (status.ok() && !(*resolved_type)->IsSupportedType(language()))) {
    return MakeSqlErrorAt(path_expr)
           << "Type not found: "
           << (is_single_identifier ? ToIdentifierLiteral(single_name)
                                    : path_expr->ToIdentifierPathString());
  }

  return status;
}

// Get name of a hint or option formatted appropriately for error messages.
static std::string HintName(absl::string_view qualifier,
                            absl::string_view name) {
  return absl::StrCat((qualifier.empty() ? "" : ToIdentifierLiteral(qualifier)),
                      (qualifier.empty() ? "" : "."),
                      ToIdentifierLiteral(name));
}

static constexpr const char* GetHintOrOptionName(
    Resolver::HintOrOptionType hint_or_option_type) {
  switch (hint_or_option_type) {
    case Resolver::HintOrOptionType::Hint:
      return "hint";
    case Resolver::HintOrOptionType::AnonymizationOption:
      return "anonymization option";
    case Resolver::HintOrOptionType::Option:
      return "option";
    case Resolver::HintOrOptionType::DifferentialPrivacyOption:
      return "differential privacy option";
    case Resolver::HintOrOptionType::AggregationThresholdOption:
      return "aggregation threshold option";
  }
}

static absl::StatusOr<AllowedOptionProperties> GetHintOrOptionProperties(
    const AllowedHintsAndOptions& allowed, absl::string_view qualifier,
    const ASTIdentifier* ast_name, absl::string_view name,
    Resolver::HintOrOptionType hint_or_option_type) {
  auto process_select_with_options =
      [&](const auto& option_map) -> absl::StatusOr<AllowedOptionProperties> {
    GOOGLESQL_RET_CHECK(qualifier.empty());
    auto iter = option_map.find(absl::AsciiStrToLower(name));
    if (iter == option_map.end()) {
      return MakeSqlErrorAt(ast_name)
             << "Unknown " << GetHintOrOptionName(hint_or_option_type) << ": "
             << HintName(qualifier, name);
    }
    return iter->second;
  };
  switch (hint_or_option_type) {
    case Resolver::HintOrOptionType::Hint: {
      auto iter = allowed.hints_lower.find(std::make_pair(
          absl::AsciiStrToLower(qualifier), absl::AsciiStrToLower(name)));
      if (iter != allowed.hints_lower.end()) {
        return AllowedOptionProperties{.type = iter->second};
      }
      if (googlesql_base::ContainsKey(allowed.disallow_unknown_hints_with_qualifiers,
                           std::string(qualifier))) {
        return MakeSqlErrorAt(ast_name)
               << "Unknown hint: " << HintName(qualifier, name);
      }
      return AllowedOptionProperties{.type = nullptr};
    } break;
    case Resolver::HintOrOptionType::Option: {
      GOOGLESQL_RET_CHECK(qualifier.empty());
      auto iter = allowed.options_lower.find(absl::AsciiStrToLower(name));
      if (iter != allowed.options_lower.end()) {
        return iter->second;
      }
      if (allowed.disallow_unknown_options) {
        return MakeSqlErrorAt(ast_name)
               << "Unknown option: " << HintName(qualifier, name);
      }
      return AllowedOptionProperties{.type = nullptr};
    } break;
    case Resolver::HintOrOptionType::AnonymizationOption:
      return process_select_with_options(allowed.anonymization_options_lower);
    case Resolver::HintOrOptionType::DifferentialPrivacyOption:
      return process_select_with_options(
          allowed.differential_privacy_options_lower);
    case Resolver::HintOrOptionType::AggregationThresholdOption:
      return process_select_with_options(
          GetAllowedAggregationThresholdOptions());
  }
}

absl::Status Resolver::ResolveHintOrOptionAndAppend(
    const ASTExpression* ast_value, const ASTIdentifier* ast_qualifier,
    const ASTIdentifier* ast_name, HintOrOptionType hint_or_option_type,
    const AllowedHintsAndOptions& allowed, const NameScope* from_name_scope,
    ASTOptionsEntry::AssignmentOp option_assignment_op,
    bool allow_alter_array_operators,
    std::vector<std::unique_ptr<const ResolvedOption>>* option_list) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  GOOGLESQL_RET_CHECK(ast_name != nullptr);

  const std::string qualifier =
      ast_qualifier == nullptr ? "" : ast_qualifier->GetAsString();
  const std::string name = ast_name->GetAsString();

  std::unique_ptr<const ResolvedExpr> resolved_expr;

  const char* hint_or_option_name = GetHintOrOptionName(hint_or_option_type);
  const std::string first_char_upper_case_option_name =
      [&hint_or_option_name]() {
        std::string option_name = hint_or_option_name;
        if (!option_name.empty()) {
          option_name[0] = absl::ascii_toupper(option_name[0]);
        }
        return option_name;
      }();

  // expected_type points at the Type* of the hint or option we found.
  // If it points at a NULL, any type is allowed.
  GOOGLESQL_ASSIGN_OR_RETURN(auto option_properties,
                   GetHintOrOptionProperties(allowed, qualifier, ast_name, name,
                                             hint_or_option_type));
  auto [expected_type, resolving_kind, option_allow_alter_array] =
      std::move(option_properties);
  switch (resolving_kind) {
    case AllowedHintsAndOptionsProto::OptionProto::
        CONSTANT_OR_EMPTY_NAME_SCOPE_IDENTIFIER: {
      // Single identifiers are accepted as hint values, and are stored as
      // string values.  These show up in the AST as path expressions with one
      // element.
      if (ast_value->node_kind() == AST_PATH_EXPRESSION) {
        const ASTPathExpression* path_expr =
            static_cast<const ASTPathExpression*>(ast_value);
        if (path_expr->num_names() == 1 && !path_expr->parenthesized()) {
          // For backward compatibility, standalone identifier names need to be
          // treated as a literal string.  But, if the name happens to resolve
          // as an expression, emit an error, since it's not clear whether the
          // user is referring to a literal string "foo" or a constant symbol
          // named "foo".  The user can resolve the error by either adding
          // parentheses or enclosing the name in quotation marks.
          if (ResolveScalarExpr(ast_value, empty_name_scope_.get(),
                                hint_or_option_name, &resolved_expr)
                  .ok()) {
            return MakeSqlErrorAt(ast_value)
                   << "Unable to determine if "
                   << path_expr->name(0)->GetAsIdString().ToStringView()
                   << " is a string or expression.  If a string is intended, "
                   << "please enclose it with quotation marks.  If an "
                      "expression "
                   << "is intended, please enclose it with parentheses.";
          }

          resolved_expr = MakeResolvedLiteral(
              ast_value, Value::String(path_expr->first_name()->GetAsString()));
        }
      }

      // Otherwise, we parse the value as a constant expression,
      // with no names visible in scope.
      if (resolved_expr == nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(ResolveScalarExpr(ast_value, empty_name_scope_.get(),
                                          hint_or_option_name, &resolved_expr));
        // We try collapsing the resolved_expr early as we want to be sure that
        // a literal/parameter is passed as a hint value when possible.
        TryCollapsingExpressionsAsLiterals(
            ast_value, reinterpret_cast<std::unique_ptr<const ResolvedNode>*>(
                           &resolved_expr));
      }
    } break;

    case AllowedHintsAndOptionsProto::OptionProto::FROM_NAME_SCOPE_IDENTIFIER: {
      const bool is_dp_syntax =
          hint_or_option_type == HintOrOptionType::DifferentialPrivacyOption &&
          language().LanguageFeatureEnabled(FEATURE_DIFFERENTIAL_PRIVACY);
      const bool is_aggregation_threshold_syntax =
          hint_or_option_type == HintOrOptionType::AggregationThresholdOption &&
          language().LanguageFeatureEnabled(FEATURE_AGGREGATION_THRESHOLD);
      GOOGLESQL_RET_CHECK(is_dp_syntax || is_aggregation_threshold_syntax);
      GOOGLESQL_RET_CHECK_NE(from_name_scope, nullptr);
      // TODO: Add support for multi column privacy unit column as
      // defined in proposal: (broken link).
      if (ast_value->node_kind() != AST_PATH_EXPRESSION) {
        return MakeSqlErrorAt(ast_value) << first_char_upper_case_option_name
                                         << " must be a path expression";
      }
      GOOGLESQL_RETURN_IF_ERROR(ResolveScalarExpr(ast_value, from_name_scope,
                                        hint_or_option_name, &resolved_expr));
    } break;
  }

  // If we found the option, try to coerce the value to the appropriate type.
  if (expected_type != nullptr) {
    auto make_error_msg = [&first_char_upper_case_option_name, &qualifier,
                           &name](absl::string_view target_t,
                                  absl::string_view arg_t) {
      return absl::Substitute(
          "$2 $3 value has type $0 which cannot be coerced to expected type $1",
          arg_t, target_t, first_char_upper_case_option_name,
          HintName(qualifier, name));
    };
    GOOGLESQL_RETURN_IF_ERROR(CoerceExprToType(ast_value, expected_type,
                                     kImplicitCoercion, make_error_msg,
                                     &resolved_expr));
  }

  if (hint_or_option_type == HintOrOptionType::Option &&
      option_assignment_op != ASTOptionsEntry::AssignmentOp::ASSIGN) {
    if (!allow_alter_array_operators || !option_allow_alter_array) {
      return MakeSqlErrorAt(ast_name)
             << "Operators '+=' and '-=' are not allowed for option " << name;
    }
    if (expected_type != nullptr && !expected_type->IsArray()) {
      return MakeSqlErrorAt(ast_name)
             << "Invalid operator "
             << (option_assignment_op == ASTOptionsEntry::ADD_ASSIGN ? "+="
                                                                     : "-=")
             << " for option " << name << " with expected type "
             << expected_type->DebugString();
    }
  }

  ResolvedOption::AssignmentOp resolved_assignment_op;

  switch (option_assignment_op) {
    case ASTOptionsEntry::ADD_ASSIGN:
      resolved_assignment_op = ResolvedOption::ADD_ASSIGN;
      break;
    case ASTOptionsEntry::SUB_ASSIGN:
      resolved_assignment_op = ResolvedOption::SUB_ASSIGN;
      break;
    default:
      resolved_assignment_op = ResolvedOption::DEFAULT_ASSIGN;
  }

  auto resolved_option =
      MakeResolvedOption(qualifier, name, std::move(resolved_expr));
  resolved_option->set_assignment_op(resolved_assignment_op);
  MaybeRecordParseLocation(ast_name->parent(), resolved_option.get());
  option_list->push_back(std::move(resolved_option));
  return absl::OkStatus();
}

absl::Status Resolver::ResolveHintAndAppend(
    const ASTHint* ast_hint,
    std::vector<std::unique_ptr<const ResolvedOption>>* hints) {
  // Resolve the @<num_shards> hint if we have one, and turn it into the
  // long-form hint @{ num_shards: <integer> }.
  if (ast_hint->num_shards_hint() != nullptr) {
    const ASTIntLiteral* int_literal = ast_hint->num_shards_hint();
    int64_t num_shards;
    if (!functions::StringToNumeric(int_literal->image(), &num_shards,
                                    nullptr)) {
      return MakeSqlErrorAt(int_literal)
             << "Invalid INT64 literal in @num_shards hint: "
             << int_literal->image();
    }
    // @<num_shards> hint cannot be rewritten using a parameter; don't record
    // parse location.
    hints->push_back(MakeResolvedOption(
        "" /* qualifier */, "num_shards",
        MakeResolvedLiteralWithoutLocation(Value::Int64(num_shards))));
  }

  for (const ASTHintEntry* ast_hint_entry : ast_hint->hint_entries()) {
    std::unique_ptr<const ResolvedExpr> resolved_expr;
    GOOGLESQL_RETURN_IF_ERROR(ResolveHintOrOptionAndAppend(
        ast_hint_entry->value(), ast_hint_entry->qualifier(),
        ast_hint_entry->name(), HintOrOptionType::Hint,
        analyzer_options_.allowed_hints_and_options(),
        /*from_name_scope=*/nullptr, ASTOptionsEntry::ASSIGN,
        /*allow_alter_array_operators=*/false, hints));
  }

  return absl::OkStatus();
}

// The input `table` must be a value table.
static bool IsValueColumn(const Column& column, const Table& table) {
  return table.GetColumn(0)->Name() == column.Name();
}

// Returns whether the given `column_name` is a non-pseudo field of the value
// column of the given `table`. `table` must be a value table.
static bool IsValueColumnField(const Table& table,
                               const std::string& column_name) {
  Type::HasFieldResult has_field_result =
      table.GetColumn(0)->GetType()->HasField(column_name);
  return has_field_result == Type::HAS_FIELD ||
         has_field_result == Type::HAS_AMBIGUOUS_FIELD;
}

absl::Status Resolver::ResolveTableAndColumnInfoAndAppend(
    const ASTTableAndColumnInfo* table_and_column_info,
    std::vector<std::unique_ptr<const ResolvedTableAndColumnInfo>>*
        resolved_table_and_column_info_list) {
  const Table* table;
  GOOGLESQL_RETURN_IF_ERROR(FindTable(table_and_column_info->table_name(), &table));
  GOOGLESQL_RET_CHECK_NE(table, nullptr);
  for (int i = 0; i < resolved_table_and_column_info_list->size(); i++) {
    if (resolved_table_and_column_info_list->at(i)->table() == table) {
      return MakeSqlErrorAt(table_and_column_info->table_name())
             << "The ANALYZE statement allows each table to be specified only "
                "once, but found duplicate table "
             << table_and_column_info->table_name()->ToIdentifierPathString();
    }
  }
  if (table_and_column_info->column_list() == nullptr) {
    auto table_info = MakeResolvedTableAndColumnInfo(table);
    resolved_table_and_column_info_list->push_back(std::move(table_info));
    return absl::OkStatus();
  }
  absl::flat_hash_set<const Column*> column_set;
  std::set<std::string, googlesql_base::CaseLess> column_names;
  for (const ASTIdentifier* column_identifier :
       table_and_column_info->column_list()->identifiers()) {
    const IdString column_name = column_identifier->GetAsIdString();
    const Column* column = table->FindColumnByName(column_name.ToString());
    if (table->IsValueTable() &&
        (column == nullptr || IsValueColumn(*column, *table))) {
      if (IsValueColumnField(*table, column_name.ToString())) {
        // Give a more descriptive error message for the common mistake of
        // listing fields of the value column.
        return MakeSqlErrorAt(table_and_column_info)
               << ToIdentifierLiteral(column_name)
               << " is a field in the row type of value table "
               << ToIdentifierLiteral(table->Name())
               << ". ANALYZE statement only supports listing columns; "
               << "expressions, including field accesses, are not supported in "
               << "the ANALYZE statement";
      } else {
        return MakeSqlErrorAt(table_and_column_info)
               << "Cannot find a column with name "
               << ToIdentifierLiteral(column_name) << " in the value table "
               << ToIdentifierLiteral(table->Name());
      }
    }
    if (column == nullptr) {
      return MakeSqlErrorAt(table_and_column_info)
             << "Column not found: " << column_name;
    }
    if (!column_names.insert(column_name.ToString()).second) {
      return MakeSqlErrorAt(column_identifier)
             << "The table column list of an ANALYZE statement can only "
                "contain each column once, but found duplicate column "
             << column_name;
    }
    column_set.insert(column);
  }

  std::vector<int> column_index_list;
  column_index_list.reserve(column_set.size());
  for (int i = 0; i < table->NumColumns(); i++) {
    if (column_set.contains(table->GetColumn(i))) {
      column_index_list.push_back(i);
    }
  }
  auto table_and_column_index = MakeResolvedTableAndColumnInfo(table);
  table_and_column_index->set_column_index_list(column_index_list);
  resolved_table_and_column_info_list->push_back(
      std::move(table_and_column_index));
  return absl::OkStatus();
}

absl::Status Resolver::ResolveTableAndColumnInfoList(
    const ASTTableAndColumnInfoList* table_and_column_info_list,
    std::vector<std::unique_ptr<const ResolvedTableAndColumnInfo>>*
        resolved_table_and_column_info_list) {
  if (table_and_column_info_list != nullptr) {
    for (const ASTTableAndColumnInfo* table_and_column_info :
         table_and_column_info_list->table_and_column_info_entries()) {
      GOOGLESQL_RETURN_IF_ERROR(ResolveTableAndColumnInfoAndAppend(
          table_and_column_info, resolved_table_and_column_info_list));
    }
  }
  return absl::OkStatus();
}

absl::Status Resolver::ResolveOptionsList(
    const ASTOptionsList* options_list, bool allow_alter_array_operators,
    std::vector<std::unique_ptr<const ResolvedOption>>* resolved_options) {
  // Function arguments are never resolved inside options. Sanity check to make
  // sure none are accidentally in scope.
  GOOGLESQL_RET_CHECK_EQ(function_argument_info_, nullptr);
  if (options_list != nullptr) {
    absl::flat_hash_set<std::string> specified_options;
    for (const ASTOptionsEntry* options_entry :
         options_list->options_entries()) {
      if (!language().LanguageFeatureEnabled(
              FEATURE_ENABLE_ALTER_ARRAY_OPTIONS)) {
        switch (options_entry->assignment_op()) {
          case ASTOptionsEntry::ADD_ASSIGN:
            return MakeSqlErrorAt(options_entry)
                   << "Operator `+=` is not supported";
          case ASTOptionsEntry::SUB_ASSIGN:
            return MakeSqlErrorAt(options_entry)
                   << "Operator `-=` is not supported";
          default:
            break;
        }
      }
      const std::string option_name =
          absl::AsciiStrToLower(options_entry->name()->GetAsString());
      if (analyzer_options_.allowed_hints_and_options()
              .disallow_duplicate_option_names &&
          !googlesql_base::InsertIfNotPresent(&specified_options, option_name)) {
        return MakeSqlErrorAt(options_entry->name())
               << "Duplicate " << GetHintOrOptionName(HintOrOptionType::Option)
               << " specified for '" << options_entry->name()->GetAsString()
               << "'";
      }
      GOOGLESQL_RETURN_IF_ERROR(ResolveHintOrOptionAndAppend(
          options_entry->value(), /*ast_qualifier=*/nullptr,
          options_entry->name(), HintOrOptionType::Option,
          analyzer_options_.allowed_hints_and_options(),
          /*from_name_scope=*/nullptr, options_entry->assignment_op(),
          allow_alter_array_operators, resolved_options));
    }
  }
  return absl::OkStatus();
}

absl::Status Resolver::ResolveAnonymizationOptionsList(
    const ASTOptionsList* options_list,
    const QueryResolutionInfo& query_resolution_info,
    std::vector<std::unique_ptr<const ResolvedOption>>* resolved_options) {
  const HintOrOptionType option_type =
      query_resolution_info.with_modifier_mode() ==
              WithModifierMode::DIFFERENTIAL_PRIVACY
          ? HintOrOptionType::DifferentialPrivacyOption
          : HintOrOptionType::AnonymizationOption;
  const char* option_type_name =
      option_type == HintOrOptionType::AnonymizationOption
          ? "anonymization"
          : "differential privacy";
  if (options_list != nullptr) {
    // GoogleSQL defines an allowlist of valid option names for anonymization
    // options.
    std::set<std::string> specified_options;
    NameScope from_name_scope(*query_resolution_info.from_clause_name_list());
    for (const ASTOptionsEntry* options_entry :
         options_list->options_entries()) {
      const std::string option_name =
          (language().LanguageFeatureEnabled(
               FEATURE_ANONYMIZATION_CASE_INSENSITIVE_OPTIONS) ||
           option_type == HintOrOptionType::DifferentialPrivacyOption)
              ? absl::AsciiStrToLower(options_entry->name()->GetAsString())
              : options_entry->name()->GetAsString();
      if (!googlesql_base::InsertIfNotPresent(&specified_options, option_name)) {
        return MakeSqlErrorAt(options_entry->name())
               << "Duplicate " << option_type_name << " option specified for '"
               << options_entry->name()->GetAsString() << "'";
      }
      GOOGLESQL_RETURN_IF_ERROR(ResolveHintOrOptionAndAppend(
          options_entry->value(), /*ast_qualifier=*/nullptr,
          options_entry->name(), option_type,
          analyzer_options_.allowed_hints_and_options(), &from_name_scope,
          options_entry->assignment_op(),
          /*allow_alter_array_operators=*/false, resolved_options));
    }

    // Validate that if epsilon is specified, then only at most one of delta or
    // k_threshold are present in the user input.  The engine will compute the
    // third option value from the two that are specified, i.e.,
    // (epsilon, delta) -> k_threshold or (epsilon, k_threshold) -> delta.
    if (googlesql_base::ContainsKey(specified_options, "epsilon")) {
      // If epsilon is specified, then only one of delta or k_threshold can
      // be specified (but it is also valid for neither to be specified).
      if (googlesql_base::ContainsKey(specified_options, "delta") &&
          googlesql_base::ContainsKey(specified_options, "k_threshold")) {
        return MakeSqlErrorAt(options_list)
               << "The anonymization options specify all of (epsilon, delta, "
               << "and k_threshold), but must only specify (epsilon, delta) or "
               << "(epsilon, k_threshold)";
      }
    }

    // Validate that at most one of the options kappa, max_groups_contributed,
    // and max_rows_contributed are specified. The options kappa and
    // max_groups_contributed are synonyms and are mutually exclusive with the
    // option max_rows_contributed.
    static const auto* const kBoundingOptions = new std::set<absl::string_view>{
        "kappa", "max_groups_contributed", "max_rows_contributed"};
    std::set<std::string> specified_bounding_options;
    std::set_intersection(specified_options.begin(), specified_options.end(),
                          kBoundingOptions->begin(), kBoundingOptions->end(),
                          std::inserter(specified_bounding_options,
                                        specified_bounding_options.begin()));
    if (option_type != HintOrOptionType::DifferentialPrivacyOption &&
        googlesql_base::ContainsKey(specified_bounding_options, "kappa") &&
        googlesql_base::ContainsKey(specified_bounding_options,
                         "max_groups_contributed")) {
      return MakeSqlErrorAt(options_list)
             << "The anonymization options specify mutually exclusive options "
                "kappa and max_groups_contributed";
    } else if (specified_bounding_options.size() > 1) {
      const char* allowed_options_string =
          option_type == HintOrOptionType::DifferentialPrivacyOption
              ? "max_groups_contributed or max_rows_contributed may be "
                "specified in differential privacy options"
              : "kappa, max_groups_contributed, and "
                "max_rows_contributed may be specified in anonymization "
                "options";
      return MakeSqlErrorAt(options_list)
             << "At most one of the options " << allowed_options_string
             << ", instead got: "
             << absl::StrJoin(specified_bounding_options.begin(),
                              specified_bounding_options.end(), ", ");
    } else if (!language().LanguageFeatureEnabled(
                   FEATURE_DIFFERENTIAL_PRIVACY_MAX_ROWS_CONTRIBUTED) &&
               googlesql_base::ContainsKey(specified_bounding_options,
                                "max_rows_contributed")) {
      return MakeSqlErrorAt(options_list)
             << "max_rows_contributed is not supported";
    }

    // TODO Not all SQL engines have the language feature
    // FEATURE_ANONYMIZATION_CASE_INSENSITIVE_OPTIONS enabled (e.g. F1 currently
    // does not). When that feature flag is enabled, we can simplify this code,
    // because we just need to check whether specified_bounding_options contains
    // kappa.
    const bool caseInsensitiveKappaSpecified =
        std::find_if(specified_options.begin(), specified_options.end(),
                     [](const absl::string_view option) {
                       return absl::AsciiStrToLower(option) == "kappa";
                     }) != specified_options.end();
    if (caseInsensitiveKappaSpecified) {
      GOOGLESQL_RETURN_IF_ERROR(AddDeprecationWarning(
          options_list,
          DeprecationWarning::DEPRECATED_ANONYMIZATION_OPTION_KAPPA,
          "Anonymization option kappa is deprecated, instead use "
          "max_groups_contributed"));
    }
    // Only allow the `min_privacy_units_per_group` option, if the corresponding
    // language feature is enabled.
    if (!language().LanguageFeatureEnabled(
            FEATURE_DIFFERENTIAL_PRIVACY_MIN_PRIVACY_UNITS_PER_GROUP) &&
        googlesql_base::ContainsKey(specified_options, "min_privacy_units_per_group")) {
      return MakeSqlErrorAt(options_list)
             << "min_privacy_units_per_group is not supported";
    }
    // The options `min_privacy_units_per_group` and `k_threshold` don't make
    // sense together. Thus we forbit it.
    if (googlesql_base::ContainsKey(specified_options, "min_privacy_units_per_group") &&
        googlesql_base::ContainsKey(specified_options, "k_threshold")) {
      return MakeSqlErrorAt(options_list)
             << "The options min_privacy_units_per_group and k_threshold "
                "cannot be specified simultaneously.";
    }
  }
  return absl::OkStatus();
}

absl::Status Resolver::ResolveAggregationThresholdOptionsList(
    const ASTOptionsList* options_list,
    const QueryResolutionInfo& query_resolution_info,
    std::vector<std::unique_ptr<const ResolvedOption>>* resolved_options) {
  if (options_list == nullptr) {
    return absl::OkStatus();
  }
  absl::flat_hash_set<std::string> specified_options;
  NameScope from_name_scope(*query_resolution_info.from_clause_name_list());
  for (const ASTOptionsEntry* options_entry : options_list->options_entries()) {
    const std::string option_name =
        absl::AsciiStrToLower(options_entry->name()->GetAsString());
    if (!googlesql_base::InsertIfNotPresent(&specified_options, option_name)) {
      return MakeSqlErrorAt(options_entry->name())
             << "Duplicate "
             << GetHintOrOptionName(
                    HintOrOptionType::AggregationThresholdOption)
             << " specified for '" << options_entry->name()->GetAsString()
             << "'";
    }
    GOOGLESQL_RETURN_IF_ERROR(ResolveHintOrOptionAndAppend(
        options_entry->value(), /*ast_qualifier=*/nullptr,
        options_entry->name(), HintOrOptionType::AggregationThresholdOption,
        analyzer_options_.allowed_hints_and_options(), &from_name_scope,
        options_entry->assignment_op(),
        /*allow_alter_array_operators=*/false, resolved_options));
  }

  // Validate that at most one of the options max_groups_contributed,
  // and max_rows_contributed are specified.
  if (specified_options.contains("max_rows_contributed") &&
      specified_options.contains("max_groups_contributed")) {
    return MakeSqlErrorAt(options_list)
           << "The "
           << GetHintOrOptionName(HintOrOptionType::AggregationThresholdOption)
           << "s specify mutually exclusive options max_rows_contributed and "
              "max_groups_contributed";
  }
  return absl::OkStatus();
}

absl::Status Resolver::ResolveAnonWithReportOptionsList(
    const ASTOptionsList* options_list, absl::string_view default_format,
    std::vector<std::unique_ptr<const ResolvedOption>>* resolved_options,
    std::string* format) {
  *format = default_format;
  if (options_list == nullptr) {
    return absl::OkStatus();
  }
  // The options_list can currently only contain one
  // option, which is the format of the return value.
  if (options_list != nullptr && options_list->options_entries().size() != 1) {
    return MakeSqlErrorAt(options_list)
           << "WITH REPORT expects exactly 1 option but found "
           << options_list->options_entries().size() << " options";
  }

  static constexpr char kReportFormat[] = "format";
  static const auto* supported_report_formats =
      new std::unordered_set<std::string>{"json", "proto"};

  // GoogleSQL defines an allowlist of valid option names for anonymization
  // report format.
  AllowedHintsAndOptions allowed_report_options(/*qualifier=*/"");
  allowed_report_options.AddOption(kReportFormat, types::StringType());
  const ASTOptionsEntry* options_entry = options_list->options_entries(0);
  GOOGLESQL_RETURN_IF_ERROR(ResolveHintOrOptionAndAppend(
      options_entry->value(), /*ast_qualifier=*/nullptr, options_entry->name(),
      HintOrOptionType::Option, allowed_report_options,
      /*from_name_scope=*/nullptr, options_entry->assignment_op(),
      /*allow_alter_array_operators=*/false, resolved_options));

  GOOGLESQL_RET_CHECK_EQ(resolved_options->size(), 1);
  GOOGLESQL_RET_CHECK(
      googlesql_base::CaseEqual(resolved_options->at(0)->name(), kReportFormat));
  const ResolvedExpr* value = resolved_options->at(0)->value();
  if (!value->Is<ResolvedLiteral>() ||
      !value->GetAs<ResolvedLiteral>()->value().type()->IsString()) {
    return MakeSqlErrorAt(options_list)
           << "WITH REPORT expects a string literal as the format";
  }
  absl::string_view format_string_view =
      value->GetAs<ResolvedLiteral>()->value().string_value();
  std::string format_in_lower_case = absl::AsciiStrToLower(format_string_view);
  if (supported_report_formats->find(format_in_lower_case) ==
      supported_report_formats->end()) {
    return MakeSqlErrorAt(options_list)
           << "The given REPORT format " << format_string_view
           << " is not supported";
  }
  *format = format_in_lower_case;

  return absl::OkStatus();
}

absl::Status Resolver::ResolveType(
    const ASTType* type,
    const ResolveTypeModifiersOptions& resolve_type_modifier_options,
    const Type** resolved_type, TypeModifiers* resolved_type_modifiers) {
  if (!resolve_type_modifier_options.allow_type_parameters ||
      !resolve_type_modifier_options.allow_collation) {
    GOOGLESQL_RET_CHECK(resolve_type_modifier_options.context.has_value());
  }

  switch (type->node_kind()) {
    case AST_SIMPLE_TYPE: {
      return ResolveSimpleType(type->GetAsOrDie<ASTSimpleType>(),
                               resolve_type_modifier_options, resolved_type,
                               resolved_type_modifiers);
    }

    case AST_ARRAY_TYPE: {
      const ArrayType* array_type;
      GOOGLESQL_RETURN_IF_ERROR(ResolveArrayType(type->GetAsOrDie<ASTArrayType>(),
                                       resolve_type_modifier_options,
                                       &array_type, resolved_type_modifiers));
      *resolved_type = array_type;
      return absl::OkStatus();
    }

    case AST_STRUCT_TYPE: {
      const StructType* struct_type;
      GOOGLESQL_RETURN_IF_ERROR(ResolveStructType(type->GetAsOrDie<ASTStructType>(),
                                        resolve_type_modifier_options,
                                        &struct_type, resolved_type_modifiers));
      *resolved_type = struct_type;
      return absl::OkStatus();
    }

    case AST_RANGE_TYPE: {
      const RangeType* range_type;
      GOOGLESQL_RETURN_IF_ERROR(ResolveRangeType(type->GetAsOrDie<ASTRangeType>(),
                                       resolve_type_modifier_options,
                                       &range_type, resolved_type_modifiers));
      *resolved_type = range_type;
      return absl::OkStatus();
    }

    case AST_FUNCTION_TYPE: {
      return MakeSqlErrorAt(type) << "FUNCTION type not supported";
    }

    case AST_MAP_TYPE: {
      return ResolveMapType(type->GetAsOrDie<ASTMapType>(),
                            resolve_type_modifier_options, resolved_type,
                            resolved_type_modifiers);
    }

    default:
      break;
  }

  GOOGLESQL_RET_CHECK_FAIL() << type->DebugString();
  return absl::OkStatus();
}

absl::Status Resolver::ResolveSimpleType(
    const ASTSimpleType* type,
    const ResolveTypeModifiersOptions& resolve_type_modifier_options,
    const Type** resolved_type, TypeModifiers* resolved_type_modifiers) {
  if (!resolve_type_modifier_options.allow_type_parameters ||
      !resolve_type_modifier_options.allow_collation) {
    GOOGLESQL_RET_CHECK(resolve_type_modifier_options.context.has_value());
  }

  TypeParameters resolved_type_params;
  Collation resolved_collation;

  GOOGLESQL_RETURN_IF_ERROR(ResolvePathExpressionAsType(type->type_name(),
                                              /*is_single_identifier=*/false,
                                              resolved_type));

  // Resolve type parameters if type parameters are allowed.
  if (resolve_type_modifier_options.allow_type_parameters) {
    std::vector<TypeParameters> child_parameter_list;
    GOOGLESQL_ASSIGN_OR_RETURN(
        resolved_type_params,
        ResolveTypeParameters(type->type_parameters(), **resolved_type,
                              child_parameter_list));
  } else {
    if (type->type_parameters() != nullptr) {
      return MakeSqlErrorAt(type->type_parameters())
             << "Parameterized types are not supported in "
             << resolve_type_modifier_options.context.value();
    }
  }

  // Resolve type collation if type collation is allowed.
  if (resolve_type_modifier_options.allow_collation) {
    GOOGLESQL_ASSIGN_OR_RETURN(resolved_collation,
                     ResolveTypeCollation(type->collate(), **resolved_type,
                                          /*child_collation_list=*/{}));
  } else {
    if (type->collate() != nullptr) {
      return MakeSqlErrorAt(type->collate())
             << "Type with collation name is not supported in "
             << resolve_type_modifier_options.context.value();
    }
  }

  if (resolve_type_modifier_options.allow_type_parameters ||
      resolve_type_modifier_options.allow_collation) {
    GOOGLESQL_RET_CHECK(resolved_type_modifiers != nullptr);
    *resolved_type_modifiers = TypeModifiers::MakeTypeModifiers(
        std::move(resolved_type_params), std::move(resolved_collation));
  }

  return absl::OkStatus();
}

absl::Status Resolver::ResolveArrayType(
    const ASTArrayType* array_type,
    const ResolveTypeModifiersOptions& resolve_type_modifier_options,
    const ArrayType** resolved_type, TypeModifiers* resolved_type_modifiers) {
  if (!resolve_type_modifier_options.allow_type_parameters ||
      !resolve_type_modifier_options.allow_collation) {
    GOOGLESQL_RET_CHECK(resolve_type_modifier_options.context.has_value());
  }

  const Type* resolved_element_type;
  TypeModifiers resolved_element_type_modifiers;
  // Use the same <resolve_type_modifier_options> to resolve the type modifiers
  // for the element type.
  GOOGLESQL_RETURN_IF_ERROR(
      ResolveType(array_type->element_type(), resolve_type_modifier_options,
                  &resolved_element_type, &resolved_element_type_modifiers));

  if (resolved_element_type->IsArray()) {
    return MakeSqlErrorAt(array_type) << "Arrays of arrays are not supported";
  }

  GOOGLESQL_RETURN_IF_ERROR(
      type_factory_->MakeArrayType(resolved_element_type, resolved_type));

  TypeParameters resolved_element_type_params =
      resolved_element_type_modifiers.release_type_parameters();
  Collation resolved_element_collation =
      resolved_element_type_modifiers.release_collation();

  TypeParameters resolved_type_params;
  Collation resolved_collation;
  // Resolve type parameters if type parameters are allowed.
  if (resolve_type_modifier_options.allow_type_parameters) {
    // For an array, determine if the elements in the array have type
    // parameters. If they do, then child_parameter_list[0] will have the
    // element type parameters stored in a TypeParameters class.
    std::vector<TypeParameters> child_parameter_list;
    if (!resolved_element_type_params.IsEmpty()) {
      child_parameter_list.push_back(std::move(resolved_element_type_params));
    }
    GOOGLESQL_ASSIGN_OR_RETURN(
        resolved_type_params,
        ResolveTypeParameters(array_type->type_parameters(), **resolved_type,
                              child_parameter_list));
  } else {
    if (array_type->type_parameters() != nullptr) {
      return MakeSqlErrorAt(array_type->type_parameters())
             << "Parameterized types are not supported in "
             << resolve_type_modifier_options.context.value();
    }
  }

  // Resolve type collation if type collation is allowed.
  if (resolve_type_modifier_options.allow_collation) {
    std::vector<Collation> child_collation_list;
    child_collation_list.push_back(std::move(resolved_element_collation));
    GOOGLESQL_ASSIGN_OR_RETURN(
        resolved_collation,
        ResolveTypeCollation(array_type->collate(), **resolved_type,
                             std::move(child_collation_list)));
  } else {
    if (array_type->collate() != nullptr) {
      return MakeSqlErrorAt(array_type->collate())
             << "Type with collation name is not supported in "
             << resolve_type_modifier_options.context.value();
    }
  }

  if (resolve_type_modifier_options.allow_type_parameters ||
      resolve_type_modifier_options.allow_collation) {
    GOOGLESQL_RET_CHECK(resolved_type_modifiers != nullptr);
    *resolved_type_modifiers = TypeModifiers::MakeTypeModifiers(
        std::move(resolved_type_params), std::move(resolved_collation));
  }

  return absl::OkStatus();
}

absl::Status Resolver::ResolveStructType(
    const ASTStructType* struct_type,
    const ResolveTypeModifiersOptions& resolve_type_modifier_options,
    const StructType** resolved_type, TypeModifiers* resolved_type_modifiers) {
  if (!resolve_type_modifier_options.allow_type_parameters ||
      !resolve_type_modifier_options.allow_collation) {
    GOOGLESQL_RET_CHECK(resolve_type_modifier_options.context.has_value());
  }

  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();
  std::vector<StructType::StructField> struct_fields;
  bool has_parameter_children = false;
  std::vector<TypeParameters> child_parameter_list;
  std::vector<Collation> child_collation_list;
  child_parameter_list.reserve(struct_type->struct_fields().size());

  TypeModifiers resolved_field_type_modifiers;

  for (auto struct_field : struct_type->struct_fields()) {
    const Type* field_type;
    // Use the same <resolve_type_modifier_options> to resolve the type
    // modifiers for the field types.
    absl::Status status =
        ResolveType(struct_field->type(), resolve_type_modifier_options,
                    &field_type, &resolved_field_type_modifiers);

    if (!status.ok()) {
      // Try to give a better error message for the common case where the user
      // may have meant to use a struct field name as the struct field type.
      // E.g. `struct<int64 x>`
      if (struct_field->name() != nullptr) {
        std::string maybe_type = struct_field->name()->GetAsString();
        if (!maybe_type.empty()) {
          TypeKind type_kind = Type::ResolveBuiltinTypeNameToKindIfSimple(
              maybe_type, language());
          if (type_kind != TYPE_UNKNOWN) {
            absl::StatusOr<absl::string_view> maybe_name =
                GetSQLForASTNode(struct_field->type());
            if (maybe_name.ok()) {
              return googlesql_base::StatusBuilder(status)
                     << "Did you mean to define the STRUCT field as `"
                     << maybe_name << " " << maybe_type << "` instead? ";
            }
          }
        }
      }
      return status;
    }

    struct_fields.emplace_back(StructType::StructField(
        struct_field->name() != nullptr ? struct_field->name()->GetAsString()
                                        : "",
        field_type));

    TypeParameters resolved_field_type_params =
        resolved_field_type_modifiers.release_type_parameters();
    Collation resolved_field_collation =
        resolved_field_type_modifiers.release_collation();

    // For each field in a struct, determine whether the field has type
    // parameters. If the i-th field has a type parameter, then
    // child_parameter_list[i] will have the TypeParameters stored.
    if (resolve_type_modifier_options.allow_type_parameters) {
      if (!resolved_field_type_params.IsEmpty()) {
        has_parameter_children = true;
      }
      child_parameter_list.push_back(std::move(resolved_field_type_params));
    }

    // For each field in a struct, determine whether the field has collation. If
    // the i-th field has a collation, then child_collation_list[i] will have
    // the Collation stored.
    if (resolve_type_modifier_options.allow_collation) {
      child_collation_list.push_back(std::move(resolved_field_collation));
    }
  }

  GOOGLESQL_RETURN_IF_ERROR(type_factory_->MakeStructType(struct_fields, resolved_type));

  TypeParameters resolved_type_params;
  Collation resolved_collation;

  // Resolve type parameters if type parameters are allowed.
  if (resolve_type_modifier_options.allow_type_parameters) {
    if (!has_parameter_children) {
      child_parameter_list = {};
    }
    GOOGLESQL_ASSIGN_OR_RETURN(
        resolved_type_params,
        ResolveTypeParameters(struct_type->type_parameters(), **resolved_type,
                              child_parameter_list));
  } else {
    if (struct_type->type_parameters() != nullptr) {
      return MakeSqlErrorAt(struct_type->type_parameters())
             << "Parameterized types are not supported in "
             << resolve_type_modifier_options.context.value();
    }
  }

  // Resolve type collation if type collation is allowed.
  if (resolve_type_modifier_options.allow_collation) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        resolved_collation,
        ResolveTypeCollation(struct_type->collate(), **resolved_type,
                             std::move(child_collation_list)));
  } else {
    if (struct_type->collate() != nullptr) {
      return MakeSqlErrorAt(struct_type->collate())
             << "Type with collation name is not supported in "
             << resolve_type_modifier_options.context.value();
    }
  }

  if (resolve_type_modifier_options.allow_type_parameters ||
      resolve_type_modifier_options.allow_collation) {
    GOOGLESQL_RET_CHECK(resolved_type_modifiers != nullptr);
    *resolved_type_modifiers = TypeModifiers::MakeTypeModifiers(
        std::move(resolved_type_params), std::move(resolved_collation));
  }

  return absl::OkStatus();
}

absl::Status Resolver::ResolveRangeType(
    const ASTRangeType* range_type,
    const ResolveTypeModifiersOptions& resolve_type_modifier_options,
    const RangeType** resolved_type, TypeModifiers* resolved_type_modifiers) {
  if (!resolve_type_modifier_options.allow_type_parameters ||
      !resolve_type_modifier_options.allow_collation) {
    GOOGLESQL_RET_CHECK(resolve_type_modifier_options.context.has_value());
  }
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  const Type* resolved_element_type;
  TypeModifiers resolved_element_type_modifiers;
  GOOGLESQL_RETURN_IF_ERROR(
      ResolveType(range_type->element_type(), resolve_type_modifier_options,
                  &resolved_element_type, &resolved_element_type_modifiers));

  absl::Status make_range_type_status =
      type_factory_->MakeRangeType(resolved_element_type, resolved_type);
  if (!make_range_type_status.ok()) {
    return MakeSqlErrorAt(range_type->element_type())
           << make_range_type_status.message();
  }

  TypeParameters resolved_element_type_params =
      resolved_element_type_modifiers.release_type_parameters();
  Collation resolved_element_collation =
      resolved_element_type_modifiers.release_collation();
  TypeParameters resolved_type_params;
  Collation resolved_collation;

  // Resolve type parameters if type parameters are allowed.
  if (resolve_type_modifier_options.allow_type_parameters) {
    // For a range, determine if the elements in the range have type
    // parameters. If they do, then child_parameter_list[0] will have the
    // element type parameters stored in a TypeParameters class.
    std::vector<TypeParameters> child_parameter_list;
    if (!resolved_element_type_params.IsEmpty()) {
      child_parameter_list.push_back(std::move(resolved_element_type_params));
    }
    GOOGLESQL_ASSIGN_OR_RETURN(
        resolved_type_params,
        ResolveTypeParameters(range_type->type_parameters(), **resolved_type,
                              child_parameter_list));
  } else {
    if (range_type->type_parameters() != nullptr) {
      return MakeSqlErrorAt(range_type->type_parameters())
             << "Parameterized types are not supported in "
             << resolve_type_modifier_options.context.value();
    }
  }

  // Resolve type collation if type collation is allowed.
  if (resolve_type_modifier_options.allow_collation) {
    // The collatale types such as STRING, ARRAY<STRING> or
    // STRUCT<x STRING, y INT64> cannot be the element type of RANGE types so
    // far.
    GOOGLESQL_RET_CHECK(resolved_element_collation.Empty());

    if (range_type->collate() != nullptr) {
      return MakeSqlErrorAt(range_type->collate())
             << "Range type does not support collation name";
    }
  } else {
    if (range_type->collate() != nullptr) {
      return MakeSqlErrorAt(range_type->collate())
             << "Type with collation name is not supported in "
             << resolve_type_modifier_options.context.value();
    }
  }

  if (resolve_type_modifier_options.allow_type_parameters ||
      resolve_type_modifier_options.allow_collation) {
    GOOGLESQL_RET_CHECK(resolved_type_modifiers != nullptr);
    *resolved_type_modifiers = TypeModifiers::MakeTypeModifiers(
        std::move(resolved_type_params), std::move(resolved_collation));
  }

  return absl::OkStatus();
}

absl::StatusOr<std::vector<TypeParameterValue>>
Resolver::ResolveParameterLiterals(
    const ASTTypeParameterList& type_parameters) {
  std::vector<TypeParameterValue> resolved_literals;
  for (const ASTLeaf* type_parameter : type_parameters.parameters()) {
    std::unique_ptr<const ResolvedExpr> resolved_literal_out;
    if (type_parameter->node_kind() == AST_MAX_LITERAL) {
      resolved_literals.push_back(
          TypeParameterValue(TypeParameterValue::kMaxLiteral));
      continue;
    }
    GOOGLESQL_RETURN_IF_ERROR(ResolveLiteralExpr(type_parameter, &resolved_literal_out));
    GOOGLESQL_RET_CHECK_EQ(resolved_literal_out->node_kind(), RESOLVED_LITERAL);
    const Value& resolved_value =
        resolved_literal_out->GetAs<ResolvedLiteral>()->value();

    switch (type_parameter->node_kind()) {
      case AST_INT_LITERAL: {
        if (resolved_value.type_kind() == TYPE_UINT64) {
          // If someone uses a truly huge parameter, it will 'flip over'
          // to being parsed as a uint64.
          return MakeSqlErrorAt(type_parameter)
                 << "Integer type parameters must fall in the domain of INT64. "
                 << "Supplied value '" << resolved_value.uint64_value()
                 << "' is outside that range. Specific types typically have "
                 << "tighter bounds specific to that type.";
        }
        GOOGLESQL_RET_CHECK_EQ(resolved_value.type_kind(), TYPE_INT64);
        resolved_literals.push_back(TypeParameterValue(
            SimpleValue::Int64(resolved_value.int64_value())));
        break;
      }
      case AST_STRING_LITERAL: {
        GOOGLESQL_RET_CHECK_EQ(resolved_value.type_kind(), TYPE_STRING);
        resolved_literals.push_back(TypeParameterValue(
            SimpleValue::String(resolved_value.string_value())));
        break;
      }
      case AST_FLOAT_LITERAL: {
        GOOGLESQL_RET_CHECK_EQ(resolved_value.type_kind(), TYPE_DOUBLE);
        resolved_literals.push_back(TypeParameterValue(
            SimpleValue::Double(resolved_value.double_value())));
        break;
      }
      case AST_BOOLEAN_LITERAL: {
        GOOGLESQL_RET_CHECK_EQ(resolved_value.type_kind(), TYPE_BOOL);
        resolved_literals.push_back(
            TypeParameterValue(SimpleValue::Bool(resolved_value.bool_value())));
        break;
      }
      case AST_BYTES_LITERAL: {
        GOOGLESQL_RET_CHECK_EQ(resolved_value.type_kind(), TYPE_BYTES);
        resolved_literals.push_back(TypeParameterValue(
            SimpleValue::Bytes(resolved_value.bytes_value())));
        break;
      }
      default:
        // This code should be unreachable since the parser will only accept the
        // above AST nodes.
        GOOGLESQL_RET_CHECK_FAIL() << "Unexpected Literal: Did not expect parser to "
                            "allow literal as a valid type parameter input. ";
    }
  }
  return resolved_literals;
}

constexpr absl::string_view kMapCollationError =
    "Collation is not supported on MAP or its key and value types";

absl::Status Resolver::ResolveMapType(
    const ASTMapType* map_type,
    const ResolveTypeModifiersOptions& resolve_type_modifier_options,
    const Type** resolved_type, TypeModifiers* resolved_type_modifiers) {
  RETURN_ERROR_IF_OUT_OF_STACK_SPACE();

  if (map_type->collate() != nullptr) {
    return MakeSqlErrorAt(map_type->collate()) << kMapCollationError;
  }

  const Type* resolved_key_type;
  TypeModifiers resolved_key_type_modifiers;
  GOOGLESQL_RETURN_IF_ERROR(ResolveType(map_type->key_type(),
                              resolve_type_modifier_options, &resolved_key_type,
                              &resolved_key_type_modifiers));

  const Type* resolved_value_type;
  TypeModifiers resolved_value_type_modifiers;
  GOOGLESQL_RETURN_IF_ERROR(
      ResolveType(map_type->value_type(), resolve_type_modifier_options,
                  &resolved_value_type, &resolved_value_type_modifiers));

  if (!resolved_key_type_modifiers.collation().Empty()) {
    return MakeSqlErrorAt(map_type->key_type()) << kMapCollationError;
  }
  if (!resolved_value_type_modifiers.collation().Empty()) {
    return MakeSqlErrorAt(map_type->value_type()) << kMapCollationError;
  }

  TypeParameters resolved_key_type_params =
      resolved_key_type_modifiers.release_type_parameters();
  TypeParameters resolved_value_type_params =
      resolved_value_type_modifiers.release_type_parameters();

  GOOGLESQL_ASSIGN_OR_RETURN(*resolved_type,
                   type_factory_->MakeMapType(resolved_key_type,
                                              resolved_value_type, language()),
                   _.With(LocationOverride(map_type)));

  if (!(*resolved_type)->IsSupportedType(language())) {
    return MakeSqlErrorAt(map_type)
           << (*resolved_type)->ShortTypeName(language().product_mode())
           << " is not supported";
  }

  std::vector<TypeParameters> child_parameter_list = {};

  if (!resolved_key_type_params.IsEmpty() ||
      !resolved_value_type_params.IsEmpty()) {
    child_parameter_list = {std::move(resolved_key_type_params),
                            std::move(resolved_value_type_params)};
  }

  TypeParameters resolved_type_params;
  if (resolve_type_modifier_options.allow_type_parameters) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        resolved_type_params,
        ResolveTypeParameters(map_type->type_parameters(), **resolved_type,
                              std::move(child_parameter_list)));
  } else {
    GOOGLESQL_RET_CHECK(map_type->type_parameters() == nullptr)
        << "Type parameters are not allowed directly on MAP";
  }

  if (resolve_type_modifier_options.allow_type_parameters ||
      resolve_type_modifier_options.allow_collation) {
    GOOGLESQL_RET_CHECK(resolved_type_modifiers != nullptr);
    *resolved_type_modifiers = TypeModifiers::MakeTypeModifiers(
        std::move(resolved_type_params), Collation{});
  }

  return absl::OkStatus();
}

absl::Status Resolver::MaybeResolveCollationForFunctionCallBase(
    const ASTNode* error_location, ResolvedFunctionCallBase* function_call) {
  GOOGLESQL_RET_CHECK_NE(function_call, nullptr);
  if (!language().LanguageFeatureEnabled(FEATURE_COLLATION_SUPPORT)) {
    return absl::OkStatus();
  }
  // Aggregate and analytic function calls with is_distinct should resolve the
  // collation for the 'distinct' operation. When the input argument has
  // non-string type, the collation resolution still produces empty collation
  // list correctly.
  const bool is_aggregate_function_with_distinct =
      function_call->node_kind() == RESOLVED_AGGREGATE_FUNCTION_CALL &&
      function_call->GetAs<ResolvedAggregateFunctionCall>()->distinct();

  const bool is_analytic_function_with_distinct =
      function_call->node_kind() == RESOLVED_ANALYTIC_FUNCTION_CALL &&
      function_call->GetAs<ResolvedAnalyticFunctionCall>()->distinct();

  if (function_call->signature().options().uses_operation_collation() ||
      is_aggregate_function_with_distinct ||
      is_analytic_function_with_distinct) {
    // TODO: Refactor the CollationAnnotation class to avoid
    // creating an instance of CollationAnnotation to invoke a static logic.
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<AnnotationMap> annotation_map,
        CollationAnnotation().GetCollationFromFunctionArguments(
            error_location, *function_call, FunctionEnums::AFFECTS_OPERATION));
    if (annotation_map != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(const AnnotationMap* operation_collation,
                       type_factory_->TakeOwnership(std::move(annotation_map)));
      GOOGLESQL_ASSIGN_OR_RETURN(
          ResolvedCollation resolved_collation,
          ResolvedCollation::MakeResolvedCollation(*operation_collation));
      function_call->add_collation_list(std::move(resolved_collation));
    }
  }
  return absl::OkStatus();
}

absl::Status Resolver::MaybeResolveCollationForSubqueryExpr(
    const ASTNode* error_location, ResolvedSubqueryExpr* subquery_expr) {
  GOOGLESQL_RET_CHECK_NE(subquery_expr, nullptr);
  if (!language().LanguageFeatureEnabled(FEATURE_COLLATION_SUPPORT) ||
      subquery_expr->subquery_type() != ResolvedSubqueryExpr::IN) {
    return absl::OkStatus();
  }
  const ResolvedScan* subquery_scan = subquery_expr->subquery();
  GOOGLESQL_RET_CHECK_NE(subquery_scan, nullptr);
  GOOGLESQL_RET_CHECK_EQ(subquery_scan->column_list_size(), 1);
  GOOGLESQL_RET_CHECK_NE(subquery_expr->in_expr(), nullptr);
  const AnnotationMap* subquery_annotation_map =
      subquery_scan->column_list(0).type_annotation_map();
  const AnnotationMap* in_expr_annotation_map =
      subquery_expr->in_expr()->type_annotation_map();
  const Type* in_expr_type = subquery_expr->in_expr()->type();
  absl::StatusOr<std::unique_ptr<AnnotationMap>> status_or_result_collation =
      CollationAnnotation().GetCollationFromAnnotationMaps(
          in_expr_type, {subquery_annotation_map, in_expr_annotation_map});

  if (!status_or_result_collation.ok()) {
    absl::Status status = status_or_result_collation.status();
    // TODO: Use a better mechanism to detect the collation
    // mismatch error from the result of MergeAnnotations function.
    if (status.code() == absl::StatusCode::kInternal) {
      return status;
    }
    return MakeSqlErrorAt(error_location) << absl::Substitute(
               "$0. Collation for IN operator is different on input expr ($1) "
               "and subquery column ($2)",
               status.message(),
               in_expr_annotation_map->DebugString(
                   CollationAnnotation::GetId()),
               subquery_annotation_map->DebugString(
                   CollationAnnotation::GetId()));
  }
  // The status is OK. Create Collation if the returned AnnotationMap is not
  // nullptr.
  std::unique_ptr<AnnotationMap> result_annotation_map =
      std::move(status_or_result_collation.value());
  if (result_annotation_map != nullptr) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        ResolvedCollation resolved_collation,
        ResolvedCollation::MakeResolvedCollation(*result_annotation_map));
    subquery_expr->set_in_collation(resolved_collation);
  }
  return absl::OkStatus();
}

void Resolver::RecordColumnAccess(
    const ResolvedColumn& column,
    ResolvedStatement::ObjectAccess access_flags) {
  ResolvedStatement::ObjectAccess& access = referenced_column_access_[column];
  access = static_cast<ResolvedStatement::ObjectAccess>(access_flags | access);
}

void Resolver::RecordColumnAccess(
    absl::Span<const ResolvedColumn> columns,
    ResolvedStatement::ObjectAccess access_flags) {
  for (const ResolvedColumn& column : columns) {
    RecordColumnAccess(column, access_flags);
  }
}

void Resolver::RecordPropertyGraphRef(const PropertyGraph* property_graph) {
  referenced_property_graphs_.insert(property_graph);
}

absl::flat_hash_set<const PropertyGraph*>
Resolver::release_referenced_property_graphs() {
  absl::flat_hash_set<const PropertyGraph*> tmp;
  referenced_property_graphs_.swap(tmp);
  return tmp;
}

absl::Status Resolver::RecordImpliedAccess(const ResolvedStatement* statement) {
  // ResolvedUpdateStmt and ResolvedMergeStmt are both supported and we return
  // immediately if statement is not one of these two. This saves us from
  // wasteful parsing of the AST and potentially unintended actions on
  // statements where this was not designed to run.
  if (statement->node_kind() != googlesql::RESOLVED_UPDATE_STMT &&
      statement->node_kind() != googlesql::RESOLVED_MERGE_STMT) {
    return absl::OkStatus();
  }

  std::vector<const ResolvedNode*> update_items;
  // Call GetDescendantsWithKinds to find all ResolvedUpdateItems to analyze.
  // For ResolvedUpdateStmt, these include the set of ResolvedUpdateItems
  // specified directly after UPDATE.
  // For ResolvedMergeStmt, it will return the set of ResolvedUpdateItems
  // under the WHEN clause if the MERGE includes an UPDATE statement.
  statement->GetDescendantsWithKinds({googlesql::RESOLVED_UPDATE_ITEM},
                                     &update_items);
  for (auto node : update_items) {
    const ResolvedUpdateItem* updateItem = node->GetAs<ResolvedUpdateItem>();
    // For every encountered update item, determine whether we need to
    // mark the associated column as an 'implied' READ. These are accesses that
    // imply READ even if the AST does not directly have a read operation.
    // If any of delete_list_size(), update_list_size(), or insert_list_size()
    // are greater than zero, it indicates a nested DML statement and we should
    // mark it as a READ because the user could use a UDF or other indirect
    // means and infer information about the array that should only be
    // accessible via READ.
    // If update_item_element_list_size() is greater than zero, we also mark it
    // as a READ because the lack of an exception gives information that the
    // access into the collection was valid. Finally, if the target is not
    // directly a ResolvedColumnRef, then it must be a field access (ex:
    // GetProtoField, GetStructField) and should be treated as a READ because
    // the accessed object will need to be read before it is modified and
    // written back.
    bool is_implied_read =
        updateItem->update_item_element_list_size() > 0 ||
        updateItem->insert_list_size() > 0 ||
        updateItem->update_list_size() > 0 ||
        updateItem->delete_list_size() > 0 ||
        updateItem->target()->node_kind() != googlesql::RESOLVED_COLUMN_REF;

    if (is_implied_read) {
      std::vector<const ResolvedNode*> target_column;
      // The target of an update must be a column, possibly wrapped with
      // Get{Proto,Struct}Field operators.  We can use this to extract that
      // column rather than writing a traversal method that supports only those
      // specific operators.
      updateItem->target()->GetDescendantsWithKinds(
          {googlesql::RESOLVED_COLUMN_REF}, &target_column);
      GOOGLESQL_RET_CHECK_EQ(target_column.size(), 1);
      RecordColumnAccess(
          target_column.front()->GetAs<ResolvedColumnRef>()->column(),
          ResolvedStatement::READ);
    }
  }
  return absl::OkStatus();
}

absl::Status Resolver::SetColumnAccessList(ResolvedStatement* statement) {
  const ResolvedTableScan* scan = nullptr;

  // Currently, we are only setting column access info on nodes that support it,
  // including Update, Merge, Insert, and Delete.
  std::vector<ResolvedStatement::ObjectAccess>* mutable_access_list = nullptr;
  switch (statement->node_kind()) {
    case RESOLVED_UPDATE_STMT: {
      auto update_stmt = static_cast<ResolvedUpdateStmt*>(statement);
      scan = update_stmt->table_scan();
      mutable_access_list = update_stmt->mutable_column_access_list();
      break;
    }
    case RESOLVED_MERGE_STMT: {
      auto merge_stmt = static_cast<ResolvedMergeStmt*>(statement);
      scan = merge_stmt->table_scan();
      mutable_access_list = merge_stmt->mutable_column_access_list();
      break;
    }
    case RESOLVED_INSERT_STMT: {
      auto insert_stmt = static_cast<ResolvedInsertStmt*>(statement);
      scan = insert_stmt->table_scan();
      mutable_access_list = insert_stmt->mutable_column_access_list();
      break;
    }
    case RESOLVED_DELETE_STMT: {
      auto delete_stmt = static_cast<ResolvedDeleteStmt*>(statement);
      scan = delete_stmt->table_scan();
      mutable_access_list = delete_stmt->mutable_column_access_list();
      break;
    }
    default: {
      return absl::OkStatus();
    }
  }

  GOOGLESQL_RET_CHECK(scan != nullptr);
  GOOGLESQL_RET_CHECK(mutable_access_list != nullptr);
  GOOGLESQL_RETURN_IF_ERROR(RecordImpliedAccess(statement));

  std::vector<ResolvedStatement::ObjectAccess> column_access_list;
  for (int i = 0; i < scan->column_list().size(); ++i) {
    const ResolvedColumn& column = scan->column_list(i);
    if (googlesql_base::ContainsKey(referenced_column_access_, column)) {
      column_access_list.push_back(referenced_column_access_.at(column));
    } else {
      // This should only happen if this method is used for an unpruned
      // statement.
      column_access_list.push_back(ResolvedStatement::NONE);
    }
  }
  *mutable_access_list = column_access_list;
  return absl::OkStatus();
}

absl::Status Resolver::PruneColumnLists(const ResolvedNode* node) const {
  if (!analyzer_options_.prune_unused_columns()) {
    return absl::OkStatus();
  }

  // Validate that SetColumnAccessList was not called before PruneColumnLists,
  // on the current outermost node.  This does not recheck inner statements,
  // which can occur on the statements inside terminal pipe operators like
  // ResolvedPipeInsertScan, where SetAccessList would already have run
  // during the inner call to FinishResolveStatement.
  GOOGLESQL_RET_CHECK(node->node_kind() != googlesql::RESOLVED_UPDATE_STMT ||
            node->GetAs<ResolvedUpdateStmt>()->column_access_list_size() == 0)
      << "SetColumnAccessList was called before PruneColumnList";

  GOOGLESQL_RET_CHECK(node->node_kind() != googlesql::RESOLVED_MERGE_STMT ||
            node->GetAs<ResolvedMergeStmt>()->column_access_list_size() == 0)
      << "SetColumnAccessList was called before PruneColumnList";

  GOOGLESQL_RET_CHECK(node->node_kind() != googlesql::RESOLVED_INSERT_STMT ||
            node->GetAs<ResolvedInsertStmt>()->column_access_list_size() == 0)
      << "SetColumnAccessList was called before PruneColumnList";

  GOOGLESQL_RET_CHECK(node->node_kind() != googlesql::RESOLVED_PIPE_INSERT_SCAN ||
            node->GetAs<ResolvedPipeInsertScan>()
                    ->insert_stmt()
                    ->column_access_list_size() == 0)
      << "SetColumnAccessList was called before PruneColumnList";

  GOOGLESQL_RET_CHECK(node->node_kind() != googlesql::RESOLVED_DELETE_STMT ||
            node->GetAs<ResolvedDeleteStmt>()->column_access_list_size() == 0)
      << "SetColumnAccessList was called before PruneColumnList";

  std::vector<const ResolvedNode*> scan_nodes;
  node->GetDescendantsSatisfying(&ResolvedNode::IsScan, &scan_nodes);

  std::vector<ResolvedColumn> pruned_column_list;
  std::vector<int> pruned_column_index_list;
  for (const ResolvedNode* scan_node : scan_nodes) {
    const ResolvedScan* scan = scan_node->GetAs<ResolvedScan>();

    const std::vector<int>* column_index_list = nullptr;
    if (scan_node->node_kind() == RESOLVED_TABLE_SCAN) {
      const auto* table_scan = scan->GetAs<ResolvedTableScan>();
      if (!table_scan->read_as_row_type()) {
        column_index_list = &table_scan->column_index_list();
      }
    } else if (scan_node->node_kind() == RESOLVED_TVFSCAN) {
      column_index_list = &scan->GetAs<ResolvedTVFScan>()->column_index_list();
    }

    pruned_column_list.clear();
    pruned_column_index_list.clear();
    for (int i = 0; i < scan->column_list().size(); ++i) {
      const ResolvedColumn& column = scan->column_list(i);
      if (googlesql_base::ContainsKey(referenced_column_access_, column)) {
        pruned_column_list.push_back(column);
        if (column_index_list != nullptr) {
          const int column_index = (*column_index_list)[i];
          pruned_column_index_list.push_back(column_index);
        }
      }
    }

    if (pruned_column_list.size() < scan->column_list_size()) {
      if (scan->node_kind() == RESOLVED_PIVOT_SCAN) {
        // If any pivot columns have been pruned, remove the column from the
        // pivot output column list also.
        ResolvedPivotScan* mutable_pivot_scan =
            const_cast<ResolvedPivotScan*>(scan->GetAs<ResolvedPivotScan>());

        std::vector<std::unique_ptr<const ResolvedPivotColumn>>
            orig_output_column_list =
                mutable_pivot_scan->release_pivot_column_list();

        std::vector<std::unique_ptr<const ResolvedPivotColumn>>
            pruned_output_column_list;
        for (int i = 0; i < orig_output_column_list.size(); ++i) {
          if (googlesql_base::ContainsKey(referenced_column_access_,
                               orig_output_column_list[i]->column())) {
            pruned_output_column_list.push_back(
                std::move(orig_output_column_list[i]));
          }
        }
        mutable_pivot_scan->set_pivot_column_list(
            std::move(pruned_output_column_list));
      }

      // We use const_cast to mutate the column_list vector on Scan nodes.
      // This is only called right at the end, after we've done all resolving,
      // and before we transfer ownership to the caller.
      ResolvedScan* mutable_scan = const_cast<ResolvedScan*>(scan);
      mutable_scan->set_column_list(pruned_column_list);
      if (column_index_list != nullptr) {
        if (scan_node->node_kind() == RESOLVED_TABLE_SCAN) {
          mutable_scan->GetAs<ResolvedTableScan>()->set_column_index_list(
              pruned_column_index_list);
        } else if (scan_node->node_kind() == RESOLVED_TVFSCAN) {
          mutable_scan->GetAs<ResolvedTVFScan>()->set_column_index_list(
              pruned_column_index_list);
        }
      }
    }
  }
  return absl::OkStatus();
}

absl::Status Resolver::FindTable(const ASTPathExpression* name,
                                 const Table** table) {
  GOOGLESQL_RET_CHECK(name != nullptr);
  GOOGLESQL_RET_CHECK(table != nullptr);

  absl::Status status = catalog_->FindTable(name->ToIdentifierVector(), table,
                                            analyzer_options_.find_options());
  if (status.code() == absl::StatusCode::kNotFound) {
    std::string message;
    absl::StrAppend(&message,
                    "Table not found: ", name->ToIdentifierPathString());
    std::string suggestion(catalog_->SuggestTable(name->ToIdentifierVector()));
    if (!suggestion.empty()) {
      absl::StrAppend(&message, "; did you mean: ", suggestion, "?");
    }
    status = MakeSqlErrorAt(name) << message;
  }
  return status;
}

void Resolver::FindColumnIndex(const Table* table, absl::string_view name,
                               int* index, bool* duplicate) {
  ABSL_DCHECK(table != nullptr);
  ABSL_DCHECK(index != nullptr);
  ABSL_DCHECK(duplicate != nullptr);

  *index = -1;
  *duplicate = false;
  for (int i = 0; i < table->NumColumns(); i++) {
    if (googlesql_base::CaseEqual(table->GetColumn(i)->Name(), name)) {
      if (*index == -1) {
        *index = i;
      } else {
        *duplicate = true;
      }
    }
  }
}

absl::StatusOr<bool> Resolver::SupportsEquality(const Type* type1,
                                                const Type* type2) {
  GOOGLESQL_RET_CHECK_NE(type1, nullptr);
  GOOGLESQL_RET_CHECK_NE(type2, nullptr);

  // Quick check for a common case.
  if (type1->Equals(type2)) {
    return type1->SupportsEquality(analyzer_options_.language());
  }

  // INT64 and UINT64 support equality but cannot be coerced to a common type.
  // INT32 also implicitly coerces to INT64 and supports equality with UINT64.
  // Although not all numerical types are coercible to all other numerical
  // types, we nonetheless support equality between all numerical types.
  if (type1->IsNumerical() && type2->IsNumerical()) {
    return type1->SupportsEquality(analyzer_options_.language()) &&
           type2->SupportsEquality(analyzer_options_.language());
  }

  // Check if values of these types can be coerced to a common supertype that
  // support equality.
  InputArgumentType arg1(type1);
  InputArgumentType arg2(type2);
  InputArgumentTypeSet arg_set;
  arg_set.Insert(arg1);
  arg_set.Insert(arg2);
  const Type* supertype = nullptr;
  GOOGLESQL_RETURN_IF_ERROR(coercer_.GetCommonSuperType(arg_set, &supertype));
  return supertype != nullptr &&
         supertype->SupportsEquality(analyzer_options_.language());
}

absl::Status Resolver::ValidateUnnestSingleExpression(
    const ASTUnnestExpression* unnest_expr,
    absl::string_view expression_type) const {
  GOOGLESQL_RET_CHECK(unnest_expr != nullptr);
  GOOGLESQL_RET_CHECK(!unnest_expr->expressions().empty());
  if (unnest_expr->expressions().size() > 1) {
    return MakeSqlErrorAt(unnest_expr->expressions()[1])
           << "UNNEST expression used with " << expression_type
           << " does not allow multiple arguments";
  }
  if (unnest_expr->array_zip_mode() != nullptr) {
    return MakeSqlErrorAt(unnest_expr->array_zip_mode())
           << "UNNEST expression used with " << expression_type
           << " does not allow named arguments";
  }
  const ASTExpressionWithOptAlias* expr = unnest_expr->expressions()[0];
  if (expr->optional_alias() != nullptr) {
    return MakeSqlErrorAt(expr->optional_alias())
           << "UNNEST expression used with " << expression_type
           << " does not allow argument aliases inside UNNEST";
  }
  return absl::OkStatus();
}

absl::Status Resolver::CheckAndPropagateAnnotations(
    const ASTNode* error_node, ResolvedNode* resolved_node) {
  return annotation_propagator_->CheckAndPropagateAnnotations(error_node,
                                                              resolved_node);
}

absl::Status Resolver::ThrowErrorIfExprHasCollation(
    const ASTNode* error_node, absl::string_view error_template,
    const ResolvedExpr* resolved_expr) {
  if (resolved_expr == nullptr ||
      resolved_expr->type_annotation_map() == nullptr) {
    return absl::OkStatus();
  }
  if (CollationAnnotation::ExistsIn(resolved_expr->type_annotation_map())) {
    std::string error_msg = absl::Substitute(
        error_template, resolved_expr->type_annotation_map()->DebugString(
                            CollationAnnotation::GetId()));
    if (error_node != nullptr) {
      return MakeSqlErrorAt(error_node) << error_msg;
    } else {
      return MakeSqlError() << error_msg;
    }
  }
  return absl::OkStatus();
}

Resolver::AutoUnsetArgumentInfo Resolver::SetArgumentInfo(
    const FunctionArgumentInfo* arg_info) {
  function_argument_info_ = arg_info;
  return AutoUnsetArgumentInfo(
      [this]() { this->function_argument_info_ = nullptr; });
}

absl::Status Resolver::ResolveCollate(
    const ASTCollate* ast_collate,
    std::unique_ptr<const ResolvedExpr>* resolved_collate) {
  GOOGLESQL_RET_CHECK_NE(nullptr, ast_collate);

  GOOGLESQL_RETURN_IF_ERROR(ResolveScalarExpr(ast_collate->collation_name(),
                                    empty_name_scope_.get(), "COLLATE",
                                    resolved_collate));

  // parameter after COLLATE is only allowed in ORDER BY COLLATE
  ResolvedNodeKind kind = resolved_collate->get()->node_kind();
  bool is_order_by_collate =
      ast_collate->parent()->node_kind() == AST_ORDERING_EXPRESSION;
  if (kind == RESOLVED_LITERAL && resolved_collate->get()->type()->IsString()) {
    return absl::OkStatus();
  }
  if (kind == RESOLVED_PARAMETER &&
      resolved_collate->get()->type()->IsString() && is_order_by_collate) {
    return absl::OkStatus();
  }
  if (is_order_by_collate) {
    return MakeSqlErrorAt(ast_collate->collation_name())
           << "COLLATE must be followed by a string literal or a string "
              "parameter";
  }
  return MakeSqlErrorAt(ast_collate->collation_name())
         << "COLLATE must be followed by a string literal";
}

absl::Status Resolver::ValidateAndResolveDefaultCollate(
    const ASTCollate* ast_collate, const ASTNode* ast_location,
    std::unique_ptr<const ResolvedExpr>* resolved_collate) {
  GOOGLESQL_RET_CHECK_NE(nullptr, ast_collate);
  GOOGLESQL_RET_CHECK(language().LanguageFeatureEnabled(FEATURE_COLLATION_SUPPORT));
  return ResolveCollate(ast_collate, resolved_collate);
}

absl::Status Resolver::ValidateAndResolveCollate(
    const ASTCollate* ast_collate, const ASTNode* ast_location,
    const Type* column_type,
    std::unique_ptr<const ResolvedExpr>* resolved_collate) {
  GOOGLESQL_RET_CHECK_NE(nullptr, ast_collate);
  GOOGLESQL_RET_CHECK(language().LanguageFeatureEnabled(FEATURE_COLLATION_SUPPORT));
  if (!column_type->IsString()) {
    return MakeSqlErrorAt(ast_location)
           << "COLLATE can only be applied to columns or expressions of type "
              "STRING, but was applied to "
           << column_type->ShortTypeName(product_mode());
  }
  return ResolveCollate(ast_collate, resolved_collate);
}

absl::StatusOr<absl::string_view> Resolver::GetSQLForASTNode(
    const ASTNode* node) {
  GOOGLESQL_RET_CHECK(node != nullptr);
  const ParseLocationRange& range = node->location();
  GOOGLESQL_RET_CHECK(range.start().IsValid());
  GOOGLESQL_RET_CHECK(range.end().IsValid());
  GOOGLESQL_RET_CHECK_GE(sql_.length(), range.end().GetByteOffset()) << sql_;
  return absl::ClippedSubstr(
      sql_, range.start().GetByteOffset(),
      range.end().GetByteOffset() - range.start().GetByteOffset());
}

std::vector<std::string> FunctionArgumentInfo::ArgumentNames() const {
  std::vector<std::string> ret;
  ret.reserve(details_.size());
  for (const auto& details : details_) {
    ret.push_back(details->name.ToString());
  }
  return ret;
}

FunctionArgumentTypeList FunctionArgumentInfo::SignatureArguments() const {
  FunctionArgumentTypeList ret;
  ret.reserve(details_.size());
  for (const auto& details : details_) {
    ret.push_back(details->arg_type);
  }
  return ret;
}

std::vector<const FunctionArgumentInfo::ArgumentDetails*>
FunctionArgumentInfo::GetArgumentDetails() const {
  std::vector<const FunctionArgumentInfo::ArgumentDetails*> arg_details;
  arg_details.reserve(details_.size());
  for (const auto& details : details_) {
    arg_details.push_back(details.get());
  }
  return arg_details;
}

bool FunctionArgumentInfo::HasArg(const IdString& name) const {
  return details_index_by_name_.contains(name);
}

const FunctionArgumentInfo::ArgumentDetails* FunctionArgumentInfo::FindTableArg(
    IdString name) const {
  if (const ArgumentDetails* details = FindArg(name);
      details != nullptr && details->arg_type.IsRelation()) {
    return details;
  }
  return nullptr;
}

const FunctionArgumentInfo::ArgumentDetails*
FunctionArgumentInfo::FindScalarArg(IdString name) const {
  if (const ArgumentDetails* details = FindArg(name);
      details != nullptr && !details->arg_type.IsRelation()) {
    return details;
  }
  return nullptr;
}

absl::Status FunctionArgumentInfo::AddScalarArg(
    IdString name, ResolvedArgumentDef::ArgumentKind arg_kind,
    FunctionArgumentType arg_type, const AnnotationMap* annotation_map) {
  GOOGLESQL_RET_CHECK(!arg_type.IsRelation());
  return AddArgCommon({.name = name,
                       .arg_type = arg_type,
                       .arg_kind = arg_kind,
                       .annotation_map = annotation_map});
}

absl::Status FunctionArgumentInfo::AddRelationArg(
    IdString name, FunctionArgumentType arg_type) {
  GOOGLESQL_RET_CHECK(arg_type.IsRelation());
  return AddArgCommon({.name = name, .arg_type = arg_type});
}

absl::Status FunctionArgumentInfo::AddArgCommon(ArgumentDetails details) {
  GOOGLESQL_RET_CHECK(googlesql_base::InsertIfNotPresent(&details_index_by_name_, details.name,
                                    details_.size()));
  if (details.arg_type.IsTemplated()) {
    contains_templated_arguments_ = true;
  }
  details_.emplace_back(std::make_unique<ArgumentDetails>(details));
  return absl::OkStatus();
}

const FunctionArgumentInfo::ArgumentDetails* FunctionArgumentInfo::FindArg(
    IdString name) const {
  int64_t index = googlesql_base::FindWithDefault(details_index_by_name_, name, -1);
  ABSL_DCHECK_LT(index, static_cast<int64_t>(details_.size()));
  if (index < 0 || index >= details_.size()) {
    return nullptr;
  }
  return details_.at(index).get();
}

void Resolver::MaybeRecordResolvedNodeOperatorKeywordLocation(
    const ParseLocationRange& location, ResolvedNode* resolved_node) const {
  if (analyzer_options_.parse_location_record_type() ==
      PARSE_LOCATION_RECORD_FULL_NODE_SCOPE) {
    resolved_node->SetOperatorKeywordLocationRange(location);
  }
}

void Resolver::MaybeRecordResolvedNodeOperatorKeywordLocation(
    const ASTNode* ast_node, ResolvedNode* resolved_node) const {
  if (ast_node != nullptr) {
    MaybeRecordResolvedNodeOperatorKeywordLocation(
        ast_node->operator_keyword_location(), resolved_node);
  }
}

}  // namespace googlesql
