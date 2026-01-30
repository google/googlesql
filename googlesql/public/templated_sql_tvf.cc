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

#include "googlesql/public/templated_sql_tvf.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/analyzer/name_scope.h"
#include "googlesql/analyzer/resolver.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/status_payload_utils.h"
#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser.h"
#include "googlesql/proto/function.pb.h"
#include "googlesql/proto/internal_error_location.pb.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/cycle_detector.h"
#include "googlesql/public/deprecation_warning.pb.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/error_location.pb.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/input_argument_type.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/simple_table.pb.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_deserializer.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_enums.pb.h"
#include "googlesql/resolved_ast/resolved_column.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

absl::Status TemplatedSQLTVF::Serialize(
    FileDescriptorSetMap* file_descriptor_set_map,
    TableValuedFunctionProto* proto) const {
  GOOGLESQL_RETURN_IF_ERROR(
      TableValuedFunction::Serialize(file_descriptor_set_map, proto));
  proto->set_type(FunctionEnums::TEMPLATED_SQL_TVF);
  for (const std::string& arg_name : GetArgumentNames()) {
    proto->add_argument_name(arg_name);
  }
  parse_resume_location_.Serialize(proto->mutable_parse_resume_location());
  return absl::OkStatus();
}

// static
absl::Status TemplatedSQLTVF::Deserialize(
    const TableValuedFunctionProto& proto,
    const std::vector<const google::protobuf::DescriptorPool*>& pools,
    TypeFactory* factory, std::unique_ptr<TableValuedFunction>* result) {
  std::vector<std::string> path;
  for (const std::string& name : proto.name_path()) {
    path.push_back(name);
  }
  std::unique_ptr<FunctionSignature> signature;
  GOOGLESQL_ASSIGN_OR_RETURN(signature,
                   FunctionSignature::Deserialize(
                       proto.signature(), TypeDeserializer(factory, pools)));

  std::vector<std::string> arg_name_list;
  arg_name_list.reserve(proto.argument_name_size());
  for (const std::string& arg_name : proto.argument_name()) {
    arg_name_list.push_back(arg_name);
  }
  GOOGLESQL_RET_CHECK(proto.has_parse_resume_location()) << proto.DebugString();
  const ParseResumeLocation parse_resume_location =
      ParseResumeLocation::FromProto(proto.parse_resume_location());

  std::unique_ptr<TableValuedFunctionOptions> options;
  GOOGLESQL_RETURN_IF_ERROR(
      TableValuedFunctionOptions::Deserialize(proto.options(), &options));

  *result = std::make_unique<TemplatedSQLTVF>(path, *signature, arg_name_list,
                                              parse_resume_location, *options);

  if (proto.has_anonymization_info()) {
    GOOGLESQL_RET_CHECK(!proto.anonymization_info().userid_column_name().empty());
    const std::vector<std::string> userid_column_name_path = {
        proto.anonymization_info().userid_column_name().begin(),
        proto.anonymization_info().userid_column_name().end()};
    GOOGLESQL_RETURN_IF_ERROR(
        (*result)->GetAs<TemplatedSQLTVF>()->SetUserIdColumnNamePath(
            userid_column_name_path));
  }
  return absl::OkStatus();
}

absl::Status TemplatedSQLTVF::Resolve(
    const AnalyzerOptions* analyzer_options,
    const std::vector<TVFInputArgumentType>& input_arguments,
    const FunctionSignature& concrete_signature, Catalog* catalog,
    TypeFactory* type_factory,
    std::shared_ptr<TVFSignature>* tvf_signature) const {
  // TODO: Attach proper error locations to the returned Status.
  GOOGLESQL_RETURN_IF_ERROR(CheckIsValid());

  // Check if this function calls itself. If so, return an error. Otherwise, add
  // a pointer to this class to the cycle detector in the analyzer options.
  CycleDetector::ObjectInfo object(
      FullName(), this, analyzer_options->find_options().cycle_detector());
  // TODO: Attach proper error locations to the returned Status.
  GOOGLESQL_RETURN_IF_ERROR(object.DetectCycle("table function"));

  // Build maps for scalar and table-valued function arguments.
  IdStringHashMapCase<std::unique_ptr<ResolvedArgumentRef>> function_arguments;
  IdStringHashMapCase<TVFRelation> function_table_arguments;
  // TODO: Attach proper error locations to the returned Status.
  GOOGLESQL_RET_CHECK_EQ(GetArgumentNames().size(), input_arguments.size())
      << DebugString();
  for (int i = 0; i < input_arguments.size(); ++i) {
    const IdString tvf_arg_name =
        analyzer_options->id_string_pool()->Make(GetArgumentNames()[i]);
    const TVFInputArgumentType& tvf_arg_type = input_arguments[i];
    if (tvf_arg_type.is_relation()) {
      // TODO: Attach proper error locations to the returned Status.
      GOOGLESQL_RET_CHECK(googlesql_base::InsertIfNotPresent(&function_table_arguments, tvf_arg_name,
                                        tvf_arg_type.relation()));
    } else {
      // TODO: Attach proper error locations to the returned Status.
      GOOGLESQL_ASSIGN_OR_RETURN(const InputArgumentType& arg_type,
                       tvf_arg_type.GetScalarArgType());
      if (function_arguments.contains(tvf_arg_name)) {
        // TODO: Attach proper error locations to the returned Status.
        return MakeTVFQueryAnalysisError(
            absl::StrCat("Duplicate argument name ", tvf_arg_name.ToString()));
      }
      function_arguments[tvf_arg_name] =
          MakeResolvedArgumentRef(arg_type.type(), tvf_arg_name.ToString(),
                                  ResolvedArgumentDefEnums::SCALAR);
    }
  }

  // Create a separate new parser and parse the templated TVFs SQL query body.
  // Use the same ID string pool from the original parser.
  ParserOptions parser_options(analyzer_options->id_string_pool(),
                               analyzer_options->arena(),
                               analyzer_options->language());
  std::unique_ptr<ParserOutput> parser_output;
  bool at_end_of_input = false;
  ParseResumeLocation this_parse_resume_location(parse_resume_location_);
  GOOGLESQL_RETURN_IF_ERROR(ForwardNestedResolutionAnalysisError(
      ParseNextStatement(&this_parse_resume_location, parser_options,
                         &parser_output, &at_end_of_input),
      analyzer_options->error_message_options()));
  if (parser_output->statement()->node_kind() != AST_QUERY_STATEMENT) {
    // TODO: Attach proper error locations to the returned Status.
    return MakeTVFQueryAnalysisError("SQL body is not a query");
  }

  if (resolution_catalog_ != nullptr) {
    catalog = resolution_catalog_;
  }

  // Create a separate new resolver and resolve the TVF's SQL query body, using
  // the specified function arguments. Note that if this resolver uses the
  // catalog passed into the class constructor, then the catalog may include
  // names that were not available when the function was initially declared.
  Resolver resolver(catalog, type_factory, analyzer_options);
  std::optional<TVFRelation> specified_output_schema;
  if (signatures_[0].result_type().options().has_relation_input_schema()) {
    specified_output_schema =
        signatures_[0].result_type().options().relation_input_schema();
  }
  std::unique_ptr<const ResolvedStatement> resolved_sql_body;
  std::shared_ptr<const NameList> tvf_body_name_list;
  GOOGLESQL_RETURN_IF_ERROR(ForwardNestedResolutionAnalysisError(
      resolver.ResolveQueryStatementWithFunctionArguments(
          parse_resume_location_.input(),
          static_cast<const ASTQueryStatement*>(parser_output->statement()),
          specified_output_schema, allow_query_parameters_, &function_arguments,
          &function_table_arguments, &resolved_sql_body, &tvf_body_name_list),
      analyzer_options->error_message_options()));
  // TODO: Attach proper error locations to the returned Status.
  GOOGLESQL_RET_CHECK_EQ(RESOLVED_QUERY_STMT, resolved_sql_body->node_kind());

  // Construct the output schema for the TemplatedSQLTVFSignature return object.
  TVFRelation return_tvf_relation({});
  if (specified_output_schema) {
    return_tvf_relation = *specified_output_schema;
  } else if (tvf_body_name_list->is_value_table()) {
    // TODO: Attach proper error locations to the returned Status.
    GOOGLESQL_RET_CHECK_EQ(1, tvf_body_name_list->num_columns());
    return_tvf_relation = TVFRelation::ValueTable(
        tvf_body_name_list->column(0).column().annotated_type());
  } else {
    std::vector<TVFRelation::Column> output_schema_columns;
    output_schema_columns.reserve(tvf_body_name_list->num_columns());
    for (const NamedColumn& tvf_body_name_list_column :
         tvf_body_name_list->columns()) {
      // Ccheck if any of the output column names are internally-generated. If
      // so, return an error since the enclosing query will never be able to
      // reference them. This behavior matches that of non-templated TVF calls.
      // TODO: Ideally make this work for a backquoted explicit column
      // that happens to return true for IsInternalAlias (e.g. `$col`).
      if (IsInternalAlias(tvf_body_name_list_column.name())) {
        // TODO: Attach proper error locations to the returned Status.
        return MakeTVFQueryAnalysisError(
            "Function body is missing one or more explicit output column "
            "names");
      }
      output_schema_columns.emplace_back(
          tvf_body_name_list_column.name().ToString(),
          tvf_body_name_list_column.column().annotated_type());
    }
    return_tvf_relation = TVFRelation(output_schema_columns);
  }

  TVFSignatureOptions tvf_signature_options;
  tvf_signature_options.additional_deprecation_warnings =
      concrete_signature.AdditionalDeprecationWarnings();

  // Return the final TVFSignature and resolved templated query.
  std::unique_ptr<const ResolvedQueryStmt> resolved_templated_query(
      static_cast<const ResolvedQueryStmt*>(resolved_sql_body.release()));
  tvf_signature->reset(new TemplatedSQLTVFSignature(
      input_arguments, return_tvf_relation, tvf_signature_options,
      std::move(resolved_templated_query), GetArgumentNames()));
  if (anonymization_info_ != nullptr) {
    auto anonymization_info =
        std::make_unique<AnonymizationInfo>(*anonymization_info_);
    tvf_signature->get()->SetAnonymizationInfo(std::move(anonymization_info));
  }
  return absl::OkStatus();
}

absl::Status TemplatedSQLTVF::CheckIsValid() const {
  for (const FunctionSignature& signature : signatures_) {
    GOOGLESQL_RET_CHECK(std::all_of(
        signature.arguments().begin(), signature.arguments().end(),
        [](const FunctionArgumentType& arg) {
          return arg.required() || (arg.optional() && arg.HasDefault());
        }))
        << "Table-valued function declarations with argument(s) of templated "
        << "type do not support repeated arguments or non-default optional "
           "arguments when a SQL body is also present";
  }
  return absl::OkStatus();
}

absl::Status TemplatedSQLTVF::ForwardNestedResolutionAnalysisError(
    const absl::Status& status, ErrorMessageOptions options) const {
  absl::Status new_status;
  if (status.ok()) {
    return absl::OkStatus();
  } else if (HasErrorLocation(status)) {
    new_status = MakeTVFQueryAnalysisError();
    googlesql::internal::AttachPayload(
        &new_status, SetErrorSourcesFromStatus(
                         googlesql::internal::GetPayload<ErrorLocation>(status),
                         status, options.mode, parse_resume_location_.input()));
  } else {
    new_status = StatusWithInternalErrorLocation(
        MakeTVFQueryAnalysisError(),
        ParseLocationPoint::FromByteOffset(
            parse_resume_location_.filename(),
            parse_resume_location_.byte_position()));
    googlesql::internal::AttachPayload(
        &new_status,
        SetErrorSourcesFromStatus(
            googlesql::internal::GetPayload<InternalErrorLocation>(new_status),
            status, options.mode, parse_resume_location_.input()));
  }
  // Update the <new_status> based on <mode>.
  return MaybeUpdateErrorFromPayload(
      options, parse_resume_location_.input(),
      ConvertInternalErrorPayloadsToExternal(new_status,
                                             parse_resume_location_.input()));
}

absl::Status TemplatedSQLTVF::MakeTVFQueryAnalysisError(
    absl::string_view message) const {
  std::string result =
      absl::StrCat("Analysis of table-valued function ", FullName(), " failed");
  if (!message.empty()) {
    absl::StrAppend(&result, ":\n", message);
  }
  return MakeSqlError() << result;
}

std::shared_ptr<TemplatedSQLTVFSignature>
TemplatedSQLTVFSignature::CopyWithoutResolvedTemplatedQuery() const {
  std::shared_ptr<TemplatedSQLTVFSignature> copy =
      std::make_shared<TemplatedSQLTVFSignature>(
          input_arguments(), result_schema(), options(),
          /*resolved_templated_query=*/nullptr, arg_name_list_);
  std::optional<const AnonymizationInfo> anonymization_info =
      GetAnonymizationInfo();
  if (anonymization_info.has_value()) {
    copy->SetAnonymizationInfo(
        std::make_unique<AnonymizationInfo>(*anonymization_info));
  }
  return copy;
}

namespace {
static bool module_initialization_complete = []() {
  TableValuedFunction::RegisterDeserializer(
      FunctionEnums::TEMPLATED_SQL_TVF,
      TemplatedSQLTVF::Deserialize);
  return true;
} ();
}  // namespace

}  // namespace googlesql
