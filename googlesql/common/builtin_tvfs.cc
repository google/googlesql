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

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "googlesql/common/builtin_function_internal.h"
#include "googlesql/common/builtins_output_properties.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

static constexpr std::array<absl::string_view, 3> kAllowedDistanceTypes = {
    "COSINE", "DOT_PRODUCT", "EUCLIDEAN"};

// This constant represents the index of the `options` argument of
// batch VECTOR_SEARCH TVF. Only the `options` argument allows a
// Type to be defined through BuiltinFunctionOptions.
static constexpr int kBatchVectorSearchTVFOptionsArgIdx = 4;

// Checks if a column with the given `column_name` exists in the `relation_arg`
// and returns an error if it does not.
absl::StatusOr<TVFSchemaColumn> FindColumnInRelation(
    const TVFInputArgumentType& relation_arg, absl::string_view column_name) {
  for (const auto& column : relation_arg.relation().columns()) {
    if (googlesql_base::CaseEqual(column.name, column_name)) {
      return column;
    }
  }
  return absl::InvalidArgumentError(
      absl::Substitute("Unrecognized name: $0 in table $1", column_name,
                       relation_arg.DebugString()));
}

bool IsScalarNullLiteral(const TVFInputArgumentType& arg) {
  if (!arg.is_scalar()) return false;
  const ResolvedExpr* expr = arg.scalar_expr();
  if (expr == nullptr || !expr->Is<ResolvedLiteral>()) return false;
  return expr->GetAs<ResolvedLiteral>()->value().is_null();
}

// Validates that the given `column_name_arg` is a constant string that refers
// to a column in `relation_arg` of type ARRAY<DOUBLE>, ARRAY<FLOAT>, or STRING.
// Returns the appropriate error if not.
// It is expected that the argument passed is not a NULL literal. If it is, then
// this function will return an internal error.
absl::StatusOr<TVFSchemaColumn> ValidateColumnToSearchArgument(
    const TVFInputArgumentType& column_name_arg,
    const TVFInputArgumentType& relation_arg, std::string_view argument_name) {
  if (!column_name_arg.is_scalar() ||
      !column_name_arg.GetScalarArgType()->type()->IsString()) {
    return absl::InvalidArgumentError(absl::Substitute(
        "$0 argument of vector_search TVF must be a scalar of type STRING",
        ToAlwaysQuotedIdentifierLiteral(argument_name)));
  }
  const ResolvedExpr* scalar_expr = column_name_arg.scalar_expr();
  if (scalar_expr == nullptr || !scalar_expr->Is<ResolvedLiteral>()) {
    return absl::InvalidArgumentError(absl::Substitute(
        "$0 argument of vector_search TVF must be a constant STRING literal",
        ToAlwaysQuotedIdentifierLiteral(argument_name)));
  }

  const ResolvedLiteral* literal = scalar_expr->GetAs<ResolvedLiteral>();
  if (literal->value().is_null()) {
    return absl::InvalidArgumentError(
        absl::Substitute("$0 argument of vector_search TVF cannot be NULL",
                         ToAlwaysQuotedIdentifierLiteral(argument_name)));
  }
  std::string column_to_search_name = literal->value().string_value();

  GOOGLESQL_ASSIGN_OR_RETURN(TVFSchemaColumn column_to_search,
                   FindColumnInRelation(relation_arg, column_to_search_name));
  bool is_valid_column_type = false;
  if (column_to_search.type->IsArray() &&
      (column_to_search.type->AsArray()->element_type()->IsDouble() ||
       column_to_search.type->AsArray()->element_type()->IsFloat())) {
    is_valid_column_type = true;
  } else if (column_to_search.type->IsString()) {
    is_valid_column_type = true;
  }
  if (!is_valid_column_type) {
    return absl::InvalidArgumentError(
        absl::Substitute("The column specified by the $0 "
                         "argument of vector_search TVF must be of type "
                         "ARRAY<DOUBLE> or ARRAY<FLOAT> or STRING",
                         ToAlwaysQuotedIdentifierLiteral(argument_name)));
  }
  return column_to_search;
}

bool IsValidDistanceType(absl::string_view distance_type) {
  return absl::c_linear_search(kAllowedDistanceTypes, distance_type);
}

absl::Status CheckBatchVectorSearchPostResolutionArguments(
    const FunctionSignature& signature,
    absl::Span<const TVFInputArgumentType> arguments,
    const LanguageOptions& language_options) {
  GOOGLESQL_RET_CHECK_EQ(arguments.size(), 8);
  // The first argument is the base table, which must be a relation.
  const TVFInputArgumentType& base_table_arg = arguments[0];
  // The second argument is the column to search, which must be a constant
  // string literal that refers to a column in the base table of type
  // ARRAY<DOUBLE>/ARRAY<FLOAT>/STRING.
  const TVFInputArgumentType& column_to_search_arg = arguments[1];
  // The third argument is the query_table, which must be a relation.
  const TVFInputArgumentType& query_table_arg = arguments[2];
  // The fourth argument is the query column to search, which must be a constant
  // string literal that refers to a column in the query table of type
  // ARRAY<DOUBLE>/ARRAY<FLOAT>/STRING.
  const TVFInputArgumentType& query_column_to_search_arg = arguments[3];
  // The distance_type argument type STRING.
  const googlesql::TVFInputArgumentType& distance_type_argument = arguments[6];

  const googlesql::FunctionArgumentTypeOptions& column_to_search_options =
      signature.arguments()[1].options();
  const googlesql::FunctionArgumentTypeOptions& query_column_to_search_options =
      signature.arguments()[3].options();
  const googlesql::FunctionArgumentTypeOptions& distance_type_options =
      signature.arguments()[6].options();

  GOOGLESQL_ASSIGN_OR_RETURN(
      TVFSchemaColumn column_to_search,
      ValidateColumnToSearchArgument(column_to_search_arg, base_table_arg,
                                     column_to_search_options.argument_name()));
  // If query_column_to_search is not provided, the value to be used
  // would be found as follows:
  // 1. If column_to_search is present in the query data then that's used.
  // 2. Otherwise the most appropriate column is selected. Refer to
  // (broken link) for more details on this.
  std::optional<TVFSchemaColumn> query_column_to_search;
  if (!IsScalarNullLiteral(query_column_to_search_arg)) {
    GOOGLESQL_ASSIGN_OR_RETURN(query_column_to_search,
                     ValidateColumnToSearchArgument(
                         query_column_to_search_arg, query_table_arg,
                         query_column_to_search_options.argument_name()));
  }

  // Check if the types of the column to search in the base and query tables
  // are the same.
  if (query_column_to_search.has_value()) {
    if (!column_to_search.type->Equals(query_column_to_search.value().type)) {
      return absl::InvalidArgumentError(absl::Substitute(
          "The column types of argument $0 in the base table and argument $1 "
          "in the query table must be the same",
          ToAlwaysQuotedIdentifierLiteral(
              column_to_search_options.argument_name()),
          ToAlwaysQuotedIdentifierLiteral(
              query_column_to_search_options.argument_name())));
    }
  } else {
    // Check if column_to_search is present in the query table.
    absl::StatusOr<TVFSchemaColumn> query_table_column_to_search =
        FindColumnInRelation(query_table_arg, column_to_search.name);
    if (query_table_column_to_search.ok()) {
      if (!column_to_search.type->Equals(
              query_table_column_to_search.value().type)) {
        return absl::InvalidArgumentError(absl::Substitute(
            "The column types of argument $0 in the base table and "
            "the query table must be the same",
            ToAlwaysQuotedIdentifierLiteral(
                column_to_search_options.argument_name())));
      }
    } else {
      // Check that query_table contains only 1 column and its type matches
      // the column_to_search type in the base table.
      if (query_table_arg.relation().columns().size() != 1) {
        return absl::InvalidArgumentError(
            "The `query_table` argument to the vector_search TVF must contain "
            "only 1 column if the `query_column_to_search` argument is not "
            "explicitly provided");
      }
      if (!column_to_search.type->Equals(
              query_table_arg.relation().columns()[0].type)) {
        return absl::InvalidArgumentError(absl::Substitute(
            "The column type of argument $0 in the base table and the only "
            "column in the query table must be the same when $1 is "
            "neither explicitly provided nor does it contain the $0 column",
            ToAlwaysQuotedIdentifierLiteral(
                column_to_search_options.argument_name()),
            ToAlwaysQuotedIdentifierLiteral(
                query_column_to_search_options.argument_name())));
      }
    }
  }

  if (!distance_type_argument.is_scalar() ||
      !distance_type_argument.GetScalarArgType()->type()->IsString()) {
    return absl::InvalidArgumentError(absl::Substitute(
        "$0 argument of vector_search TVF must be a scalar of type STRING",
        ToAlwaysQuotedIdentifierLiteral(
            distance_type_options.argument_name())));
  }

  std::string distance_type =
      absl::AsciiStrToUpper(distance_type_argument.scalar_expr()
                                ->GetAs<ResolvedLiteral>()
                                ->value()
                                .string_value());

  // Check if the distance_type argument is one of the allowed distance types.
  if (!IsValidDistanceType(distance_type)) {
    return absl::InvalidArgumentError(absl::Substitute(
        "`$0` argument of vector_search TVF must be set to one of $1",
        signature.arguments()[6].options().argument_name(),
        absl::StrJoin(kAllowedDistanceTypes, " or ")));
  }

  return absl::OkStatus();
}

absl::StatusOr<googlesql::TVFRelation::Column> BuildStructColumn(
    googlesql::TypeFactory* type_factory,
    const googlesql::TVFRelation& relation, std::string_view output_name) {
  std::vector<googlesql::StructField> struct_fields;
  for (const auto& column : relation.columns()) {
    struct_fields.push_back({column.name, column.type});
  }
  const googlesql::Type* struct_type;
  GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeStructType(struct_fields, &struct_type));
  return googlesql::TVFRelation::Column(output_name, struct_type);
}

absl::StatusOr<std::shared_ptr<TVFSignature>>
ComputeResultTypeForVectorSearchTVF(
    Catalog* catalog, TypeFactory* type_factory,
    const FunctionSignature& signature,
    const std::vector<TVFInputArgumentType>& input_arguments,
    const AnalyzerOptions& analyzer_options) {
  GOOGLESQL_ASSIGN_OR_RETURN(
      auto base_struct_column,
      BuildStructColumn(type_factory, input_arguments[0].relation(), "base"));

  GOOGLESQL_ASSIGN_OR_RETURN(
      auto query_struct_column,
      BuildStructColumn(type_factory, input_arguments[2].relation(), "query"));

  std::vector<TVFRelation::Column> output_columns = {
      query_struct_column, base_struct_column,
      googlesql::TVFRelation::Column("distance", type_factory->get_double())};

  googlesql::TVFRelation output_schema(output_columns);
  return TVFSignature::Create(input_arguments, output_schema);
}

std::vector<googlesql::FunctionArgumentType> CommonVectorSearchArguments() {
  std::vector<googlesql::FunctionArgumentType> arguments = {
      // Base table.
      {googlesql::FunctionArgumentType::AnyRelation()},
      // Column to search.
      {googlesql::FunctionArgumentType(
          googlesql::types::StringType(),
          googlesql::FunctionArgumentTypeOptions()
              .set_must_be_constant()
              .set_must_be_non_null()
              .set_argument_name("column_to_search", kPositionalOnly))},
      // top_k
      {googlesql::FunctionArgumentType(
          googlesql::types::Int64Type(),
          googlesql::FunctionArgumentTypeOptions()
              .set_cardinality(googlesql::FunctionArgumentType::OPTIONAL)
              .set_argument_name("top_k", googlesql::kNamedOnly)
              .set_must_be_constant()
              .set_min_value(1)
              .set_default(Value::Int64(10)))},
      // distance_type
      {googlesql::FunctionArgumentType(
          googlesql::types::StringType(),
          googlesql::FunctionArgumentTypeOptions()
              .set_argument_name("distance_type", googlesql::kNamedOnly)
              .set_default(Value::String("EUCLIDEAN"))
              .set_must_be_constant()
              .set_cardinality(googlesql::FunctionArgumentType::OPTIONAL))},
      // max_distance
      {googlesql::FunctionArgumentType(
          googlesql::types::DoubleType(),
          googlesql::FunctionArgumentTypeOptions()
              .set_argument_name("max_distance", googlesql::kNamedOnly)
              .set_default(Value::Null(googlesql::types::DoubleType()))
              .set_must_be_constant()
              .set_cardinality(googlesql::FunctionArgumentType::OPTIONAL))}};
  return arguments;
}

absl::Status MaybeAddVectorSearchTVFProtoOptionsArgument(
    const BuiltinFunctionOptions& options, absl::string_view fn_name,
    FunctionSignatureId id, BuiltinsOutputProperties& output_properties,
    FunctionArgumentTypeList& arguments, const int arg_idx) {
  // Mark this signature as one that supports a supplied Type.
  output_properties.MarkSupportsSuppliedArgumentType(id, arg_idx);
  // Check if a Type was actually supplied in `options`.
  if (auto it = options.argument_types.find({id, arg_idx});
      it != options.argument_types.end()) {
    const Type* proto_type = it->second;
    if (!proto_type->IsProto()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Supplied argument type for the `options` argument of function ",
          fn_name, " must be a proto"));
    }

    FunctionArgumentType proto_options_arg = FunctionArgumentType(
        proto_type, FunctionArgumentTypeOptions(FunctionArgumentType::OPTIONAL)
                        .set_argument_name("options", kNamedOnly)
                        .set_must_be_immutable_constant());
    arguments.push_back(std::move(proto_options_arg));
  }
  return absl::OkStatus();
}

absl::Status InsertBatchVectorSearchTVFSignatures(
    TypeFactory* type_factory, const GoogleSQLBuiltinFunctionOptions& options,
    NameToTableValuedFunctionMap* table_valued_functions,
    BuiltinsOutputProperties& output_properties) {
  std::vector<googlesql::FunctionArgumentType> common_vector_search_arguments =
      CommonVectorSearchArguments();
  GOOGLESQL_RET_CHECK_EQ(common_vector_search_arguments.size(), 5);
  FunctionArgumentTypeList batch_vector_search_arguments = {
      // Base table.
      common_vector_search_arguments[0],
      // Column to search.
      common_vector_search_arguments[1],
      // Query data.
      {googlesql::FunctionArgumentType::AnyRelation()},
      // Query column to search.
      {googlesql::FunctionArgumentType(
          googlesql::types::StringType(),
          googlesql::FunctionArgumentTypeOptions()
              .set_must_be_constant()
              .set_cardinality(googlesql::FunctionArgumentType::OPTIONAL)
              .set_argument_name("query_column_to_search",
                                 googlesql::kPositionalOrNamed))}};
  GOOGLESQL_RETURN_IF_ERROR(MaybeAddVectorSearchTVFProtoOptionsArgument(
      options, "vector_search", FN_BATCH_VECTOR_SEARCH_TVF_WITH_PROTO_OPTIONS,
      output_properties, batch_vector_search_arguments,
      kBatchVectorSearchTVFOptionsArgIdx));
  // top_k
  batch_vector_search_arguments.push_back(common_vector_search_arguments[2]);
  // distance_type
  batch_vector_search_arguments.push_back(common_vector_search_arguments[3]);
  // max_distance
  batch_vector_search_arguments.push_back(common_vector_search_arguments[4]);

  GOOGLESQL_RETURN_IF_ERROR(InsertSimpleTableValuedFunction(
      table_valued_functions, options, "vector_search",
      {{FunctionSignatureOnHeap(
          /*result_type=*/googlesql::FunctionArgumentType::AnyRelation(),
          /*arguments=*/batch_vector_search_arguments,
          /*context_id=*/FN_BATCH_VECTOR_SEARCH_TVF_WITH_PROTO_OPTIONS)}},
      TableValuedFunctionOptions()
          .AddRequiredLanguageFeature(FEATURE_VECTOR_SEARCH_TVF)
          .set_post_resolution_argument_constraint(
              &CheckBatchVectorSearchPostResolutionArguments)
          .set_compute_result_type_callback(
              &ComputeResultTypeForVectorSearchTVF)));
  return absl::OkStatus();
}

}  // namespace

absl::Status GetVectorSearchTableValuedFunctions(
    TypeFactory* type_factory, const GoogleSQLBuiltinFunctionOptions& options,
    NameToTableValuedFunctionMap* table_valued_functions,
    BuiltinsOutputProperties& output_properties) {
  GOOGLESQL_RETURN_IF_ERROR(InsertBatchVectorSearchTVFSignatures(
      type_factory, options, table_valued_functions, output_properties));
  return absl::OkStatus();
}

}  // namespace googlesql
