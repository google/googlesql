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

#include "googlesql/public/function_signature.h"

#include <array>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "google/protobuf/util/message_differencer.h"
#include "googlesql/common/errors.h"
#include "googlesql/proto/function.pb.h"
#include "googlesql/public/constness_level.pb.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function.pb.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_deserializer.h"
#include "googlesql/resolved_ast/serialization.pb.h"
#include "googlesql/base/case.h"
#include "absl/algorithm/container.h"
#include "absl/base/no_destructor.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "googlesql/base/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {

// Helper function that returns true if an argument of <kind_> can have a
// default value.
// Currently, returns true for normal expression typed kinds, and false for
// others (model, relation, descriptor, connection, void, etc).
bool CanHaveDefaultValue(SignatureArgumentKind kind) {
  switch (kind) {
    case ARG_TYPE_FIXED:
    case ARG_TYPE_ANY_1:
    case ARG_TYPE_ANY_2:
    case ARG_TYPE_ANY_3:
    case ARG_TYPE_ANY_4:
    case ARG_TYPE_ANY_5:
    case ARG_ARRAY_TYPE_ANY_1:
    case ARG_ARRAY_TYPE_ANY_2:
    case ARG_ARRAY_TYPE_ANY_3:
    case ARG_ARRAY_TYPE_ANY_4:
    case ARG_ARRAY_TYPE_ANY_5:
    case ARG_PROTO_MAP_ANY:
    case ARG_PROTO_MAP_KEY_ANY:
    case ARG_PROTO_MAP_VALUE_ANY:
    case ARG_PROTO_ANY:
    case ARG_STRUCT_ANY:
    case ARG_ENUM_ANY:
    case ARG_TYPE_ARBITRARY:
    case ARG_RANGE_TYPE_ANY_1:
    case ARG_MAP_TYPE_ANY_1_2:
      return true;
    case ARG_TYPE_RELATION:
    case ARG_TYPE_VOID:
    case ARG_TYPE_MODEL:
    case ARG_TYPE_CONNECTION:
    case ARG_TYPE_DESCRIPTOR:
    case ARG_TYPE_GRAPH_NODE:
    case ARG_TYPE_GRAPH_EDGE:
    case ARG_TYPE_GRAPH_ELEMENT:
    case ARG_TYPE_GRAPH_PATH:
    case ARG_TYPE_SEQUENCE:
    case ARG_TYPE_GRAPH:
    case ARG_MEASURE_TYPE_ANY_1:
      return false;
    default:
      ABSL_DCHECK(false) << "Invalid signature argument kind: " << kind;
      return false;
  }
}

}  // namespace

FunctionArgumentTypeOptions::FunctionArgumentTypeOptions(
    const TVFRelation& relation_input_schema,
    bool extra_relation_input_columns_allowed)
    : data_(new Data{.relation_input_schema =
                         std::make_shared<TVFRelation>(relation_input_schema),
                     .extra_relation_input_columns_allowed =
                         extra_relation_input_columns_allowed}) {}

absl::StatusOr<std::string>
FunctionSignatureOptions::CheckFunctionSignatureConstraints(
    const FunctionSignature& concrete_signature,
    const std::vector<InputArgumentType>& arguments) const {
  if (constraints_ == nullptr) {
    return "";
  }
  GOOGLESQL_RET_CHECK(concrete_signature.HasConcreteArguments())
      << "FunctionSignatureArgumentConstraintsCallback must be called with "
         "concrete arguments. "
      << concrete_signature.DebugString();
  return constraints_(concrete_signature, arguments);
}

absl::Status FunctionSignatureOptions::Deserialize(
    const FunctionSignatureOptionsProto& proto,
    std::unique_ptr<FunctionSignatureOptions>* result) {
  *result = std::make_unique<FunctionSignatureOptions>();
  (*result)->set_is_deprecated(proto.is_deprecated());
  (*result)->set_additional_deprecation_warnings(
      proto.additional_deprecation_warning());
  for (const int each : proto.required_language_feature()) {
    (*result)->AddRequiredLanguageFeature(LanguageFeature(each));
  }
  (*result)->set_is_aliased_signature(proto.is_aliased_signature());
  (*result)->set_propagates_collation(proto.propagates_collation());
  (*result)->set_uses_operation_collation(proto.uses_operation_collation());
  (*result)->set_rejects_collation(proto.rejects_collation());
  if (proto.has_rewrite_options()) {
    FunctionSignatureRewriteOptions rewrite_options;
    GOOGLESQL_RETURN_IF_ERROR(FunctionSignatureRewriteOptions::Deserialize(
        proto.rewrite_options(), rewrite_options));
    (*result)->set_rewrite_options(rewrite_options);
  }

  return absl::OkStatus();
}

void FunctionSignatureOptions::Serialize(
    FunctionSignatureOptionsProto* proto) const {
  proto->set_is_deprecated(is_deprecated());
  for (const FreestandingDeprecationWarning& warning :
       additional_deprecation_warnings()) {
    *proto->add_additional_deprecation_warning() = warning;
  }
  for (const LanguageFeature each : required_language_features_) {
    proto->add_required_language_feature(each);
  }
  if (is_aliased_signature()) {
    proto->set_is_aliased_signature(true);
  }
  if (!propagates_collation()) {
    proto->set_propagates_collation(false);
  }
  if (uses_operation_collation()) {
    proto->set_uses_operation_collation(true);
  }
  if (rejects_collation()) {
    proto->set_rejects_collation(true);
  }
  if (rewrite_options().has_value()) {
    rewrite_options()->Serialize(proto->mutable_rewrite_options());
  }
}

// static
absl::Status FunctionSignatureRewriteOptions::Deserialize(
    const FunctionSignatureRewriteOptionsProto& proto,
    FunctionSignatureRewriteOptions& result) {
  result.set_enabled(proto.enabled())
      .set_rewriter(proto.rewriter())
      .set_sql(proto.sql())
      .set_allow_table_references(proto.allow_table_references())
      .set_allowed_function_groups(
          std::vector(proto.allowed_function_groups().begin(),
                      proto.allowed_function_groups().end()));
  return absl::OkStatus();
}

void FunctionSignatureRewriteOptions::Serialize(
    FunctionSignatureRewriteOptionsProto* proto) const {
  ABSL_DCHECK(proto != nullptr);
  proto->set_enabled(enabled());
  proto->set_rewriter(rewriter());
  proto->set_sql(sql_);
  if (allow_table_references()) {
    proto->set_allow_table_references(true);
  }
  for (const std::string& group : allowed_function_groups()) {
    proto->add_allowed_function_groups(group);
  }
}

const FunctionEnums::ArgumentCardinality FunctionArgumentType::REQUIRED;
const FunctionEnums::ArgumentCardinality FunctionArgumentType::REPEATED;
const FunctionEnums::ArgumentCardinality FunctionArgumentType::OPTIONAL;

// static
absl::Status FunctionArgumentTypeOptions::Deserialize(
    const FunctionArgumentTypeOptionsProto& options_proto,
    const TypeDeserializer& type_deserializer, SignatureArgumentKind arg_kind,
    const Type* arg_type, FunctionArgumentTypeOptions* options) {
  options->set_cardinality(options_proto.cardinality());
  options->data_->constness_level = options_proto.constness_level();

  // Migration code between constness_level and the deprecated constness flags.
  // TODO: b/434986689 - Remove this after some soak time.
  if (options_proto.constness_level() ==
      ConstnessLevelProto::CONSTNESS_UNSPECIFIED) {
    if (options_proto.must_be_constant()) {
      options->data_->constness_level =
          ConstnessLevelProto::LEGACY_LITERAL_OR_PARAMETER;
    }
    if (options_proto.must_be_constant_expression()) {
      ABSL_DCHECK(options->data_->constness_level ==
             ConstnessLevelProto::CONSTNESS_UNSPECIFIED);
      options->data_->constness_level =
          ConstnessLevelProto::LEGACY_CONSTANT_EXPRESSION;
    }
  }
  options->set_must_be_non_null(options_proto.must_be_non_null());
  options->set_is_not_aggregate(options_proto.is_not_aggregate());
  options->set_must_support_equality(options_proto.must_support_equality());
  options->set_must_support_ordering(options_proto.must_support_ordering());
  options->set_must_support_grouping(options_proto.must_support_grouping());
  options->set_array_element_must_support_ordering(
      options_proto.array_element_must_support_ordering());
  options->set_array_element_must_support_equality(
      options_proto.array_element_must_support_equality());
  options->set_array_element_must_support_grouping(
      options_proto.array_element_must_support_grouping());
  if (options_proto.has_procedure_argument_mode()) {
    options->set_procedure_argument_mode(
        options_proto.procedure_argument_mode());
  }
  if (options_proto.has_min_value()) {
    options->set_min_value(options_proto.min_value());
  }
  if (options_proto.has_max_value()) {
    options->set_max_value(options_proto.max_value());
  }
  if (options_proto.has_extra_relation_input_columns_allowed()) {
    options->set_extra_relation_input_columns_allowed(
        options_proto.extra_relation_input_columns_allowed());
  }
  if (options_proto.has_relation_input_schema()) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        TVFRelation relation,
        TVFRelation::Deserialize(options_proto.relation_input_schema(),
                                 type_deserializer));
    *options = FunctionArgumentTypeOptions(
        relation, options->extra_relation_input_columns_allowed());
  }
  if (options_proto.has_argument_name()) {
    NamedArgumentKind named_argument_kind = kPositionalOrNamed;
    if (options_proto.has_named_argument_kind() &&
        options_proto.named_argument_kind() !=
            FunctionEnums::NAMED_ARGUMENT_KIND_UNSPECIFIED) {
      named_argument_kind = options_proto.named_argument_kind();
    } else if (options_proto.has_argument_name_is_mandatory() &&
               options_proto.argument_name_is_mandatory()) {
      named_argument_kind = kNamedOnly;
    }
    options->set_argument_name(options_proto.argument_name(),
                               named_argument_kind);
  }
  ParseLocationRange location;
  if (options_proto.has_argument_name_parse_location()) {
    GOOGLESQL_ASSIGN_OR_RETURN(location,
                     ParseLocationRange::Create(
                         options_proto.argument_name_parse_location()));
    options->set_argument_name_parse_location(location);
  }
  if (options_proto.has_argument_type_parse_location()) {
    GOOGLESQL_ASSIGN_OR_RETURN(location,
                     ParseLocationRange::Create(
                         options_proto.argument_type_parse_location()));
    options->set_argument_type_parse_location(location);
  }
  if (options_proto.has_descriptor_resolution_table_offset()) {
    options->set_resolve_descriptor_names_table_offset(
        options_proto.descriptor_resolution_table_offset());
  }
  if (options_proto.has_default_value()) {
    if (!CanHaveDefaultValue(arg_kind)) {
      return googlesql_base::InvalidArgumentErrorBuilder()
             << FunctionArgumentType::SignatureArgumentKindToString(arg_kind)
             << " argument cannot have a default value";
    }
    const Type* default_value_type = arg_type;
    // For templated arguments, we use
    // `FunctionArgumentTypeOptionsProto.default_value_type` to help
    // deserializing the default value, while for fixed type arguments, we use
    // directly `type` which is from `FunctionArgumentTypeProto.type`. Only one
    // of the two types will be set.
    if (options_proto.has_default_value_type()) {
      GOOGLESQL_RET_CHECK_EQ(arg_type, nullptr);
      GOOGLESQL_ASSIGN_OR_RETURN(
          default_value_type,
          type_deserializer.Deserialize(options_proto.default_value_type()));
    }
    GOOGLESQL_RET_CHECK_NE(default_value_type, nullptr);
    GOOGLESQL_ASSIGN_OR_RETURN(
        Value value,
        Value::Deserialize(options_proto.default_value(), default_value_type));
    options->set_default(std::move(value));
  }
  if (options_proto.has_argument_collation_mode()) {
    options->set_argument_collation_mode(
        options_proto.argument_collation_mode());
  }
  if (options_proto.has_uses_array_element_for_collation()) {
    options->set_uses_array_element_for_collation(
        options_proto.uses_array_element_for_collation());
  }
  // The default for `argument_alias_kind` is set to NON_ALIASED, so we don't
  // need to the check `options_proto.has_argument_alias_kind()`.
  options->set_argument_alias_kind(options_proto.argument_alias_kind());
  return absl::OkStatus();
}

// static
absl::StatusOr<std::unique_ptr<FunctionArgumentType>>
FunctionArgumentType::Deserialize(const FunctionArgumentTypeProto& proto,
                                  const TypeDeserializer& type_deserializer) {
  const Type* type = nullptr;
  if (proto.kind() == ARG_TYPE_FIXED) {
    GOOGLESQL_ASSIGN_OR_RETURN(type, type_deserializer.Deserialize(proto.type()));
  }

  FunctionArgumentTypeOptions options;
  GOOGLESQL_RETURN_IF_ERROR(FunctionArgumentTypeOptions::Deserialize(
      proto.options(), type_deserializer, proto.kind(), type, &options));

  if (type != nullptr) {
    // <type> can not be nullptr when proto.kind() == ARG_TYPE_FIXED
    return std::make_unique<FunctionArgumentType>(type, options,
                                                  proto.num_occurrences());
  }

  if (proto.kind() == ARG_TYPE_LAMBDA) {
    auto result = std::make_unique<FunctionArgumentType>(ARG_TYPE_LAMBDA);
    std::vector<FunctionArgumentType> lambda_argument_types;
    for (const FunctionArgumentTypeProto& arg_proto :
         proto.lambda().argument()) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          std::unique_ptr<FunctionArgumentType> arg_type,
          FunctionArgumentType::Deserialize(arg_proto, type_deserializer));
      lambda_argument_types.push_back(*arg_type);
    }
    GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<FunctionArgumentType> lambda_body_type,
                     FunctionArgumentType::Deserialize(proto.lambda().body(),
                                                       type_deserializer));
    (*result) =
        FunctionArgumentType::Lambda(std::move(lambda_argument_types),
                                     std::move(*lambda_body_type), options);
    return result;
  }

  return std::make_unique<FunctionArgumentType>(proto.kind(), options,
                                                proto.num_occurrences());
}

absl::Status FunctionArgumentTypeOptions::Serialize(
    const Type* arg_type, FunctionArgumentTypeOptionsProto* options_proto,
    FileDescriptorSetMap* file_descriptor_set_map) const {
  options_proto->set_cardinality(cardinality());
  if (procedure_argument_mode() != FunctionEnums::NOT_SET) {
    options_proto->set_procedure_argument_mode(procedure_argument_mode());
  }
  if (data_->constness_level != ConstnessLevelProto::CONSTNESS_UNSPECIFIED) {
    options_proto->set_constness_level(data_->constness_level);
  }
  // TODO: b/434986689 These fields are deprecated. They should be removed and
  // only ConstnessLevel should be used.
  if (must_be_constant()) {
    options_proto->set_must_be_constant(must_be_constant());
  }
  if (must_be_constant_expression()) {
    options_proto->set_must_be_constant_expression(
        must_be_constant_expression());
  }
  if (must_be_non_null()) {
    options_proto->set_must_be_non_null(must_be_non_null());
  }
  if (is_not_aggregate()) {
    options_proto->set_is_not_aggregate(is_not_aggregate());
  }
  if (must_support_equality()) {
    options_proto->set_must_support_equality(must_support_equality());
  }
  if (must_support_ordering()) {
    options_proto->set_must_support_ordering(must_support_ordering());
  }
  if (must_support_grouping()) {
    options_proto->set_must_support_grouping(must_support_grouping());
  }
  if (array_element_must_support_ordering()) {
    options_proto->set_array_element_must_support_ordering(
        array_element_must_support_ordering());
  }
  if (array_element_must_support_equality()) {
    options_proto->set_array_element_must_support_equality(
        array_element_must_support_equality());
  }
  if (array_element_must_support_grouping()) {
    options_proto->set_array_element_must_support_grouping(
        array_element_must_support_grouping());
  }
  if (has_min_value()) {
    options_proto->set_min_value(min_value());
  }
  if (has_max_value()) {
    options_proto->set_max_value(max_value());
  }
  if (get_resolve_descriptor_names_table_offset().has_value()) {
    options_proto->set_descriptor_resolution_table_offset(
        get_resolve_descriptor_names_table_offset().value());
  }
  if (get_default().has_value()) {
    const Value& default_value = get_default().value();
    GOOGLESQL_RETURN_IF_ERROR(
        default_value.Serialize(options_proto->mutable_default_value()));
    if (arg_type == nullptr) {
      GOOGLESQL_RETURN_IF_ERROR(
          default_value.type()->SerializeToProtoAndDistinctFileDescriptors(
              options_proto->mutable_default_value_type(),
              file_descriptor_set_map));
    }
  }
  options_proto->set_extra_relation_input_columns_allowed(
      extra_relation_input_columns_allowed());
  if (has_relation_input_schema()) {
    GOOGLESQL_RETURN_IF_ERROR(relation_input_schema().Serialize(
        file_descriptor_set_map,
        options_proto->mutable_relation_input_schema()));
  }
  if (has_argument_name()) {
    options_proto->set_argument_name(argument_name());
    options_proto->set_named_argument_kind(named_argument_kind());
    if (named_argument_kind() == kNamedOnly) {
      options_proto->set_argument_name_is_mandatory(true);
    }
  }
  std::optional<ParseLocationRange> parse_location_range =
      argument_name_parse_location();
  if (parse_location_range.has_value()) {
    GOOGLESQL_ASSIGN_OR_RETURN(*options_proto->mutable_argument_name_parse_location(),
                     parse_location_range.value().ToProto());
  }
  parse_location_range = argument_type_parse_location();
  if (parse_location_range.has_value()) {
    GOOGLESQL_ASSIGN_OR_RETURN(*options_proto->mutable_argument_type_parse_location(),
                     parse_location_range.value().ToProto());
  }
  if (argument_collation_mode() !=
      FunctionEnums::AFFECTS_OPERATION_AND_PROPAGATION) {
    options_proto->set_argument_collation_mode(argument_collation_mode());
  }
  if (uses_array_element_for_collation()) {
    options_proto->set_uses_array_element_for_collation(true);
  }
  if (argument_alias_kind() != FunctionEnums::ARGUMENT_NON_ALIASED) {
    options_proto->set_argument_alias_kind(argument_alias_kind());
  }
  return absl::OkStatus();
}

absl::Status FunctionArgumentType::Serialize(
    FileDescriptorSetMap* file_descriptor_set_map,
    FunctionArgumentTypeProto* proto) const {
  proto->set_kind(kind());
  proto->set_num_occurrences(num_occurrences());

  if (type() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(type()->SerializeToProtoAndDistinctFileDescriptors(
        proto->mutable_type(), file_descriptor_set_map));
  }

  GOOGLESQL_RETURN_IF_ERROR(options().Serialize(type(), proto->mutable_options(),
                                      file_descriptor_set_map));

  if (IsLambda()) {
    for (const FunctionArgumentType& arg_type : lambda().argument_types()) {
      GOOGLESQL_RETURN_IF_ERROR(arg_type.Serialize(
          file_descriptor_set_map, proto->mutable_lambda()->add_argument()));
    }
    GOOGLESQL_RETURN_IF_ERROR(lambda().body_type().Serialize(
        file_descriptor_set_map, proto->mutable_lambda()->mutable_body()));
  }

  return absl::OkStatus();
}

FunctionArgumentType FunctionArgumentType::Lambda(
    std::vector<FunctionArgumentType> lambda_argument_types,
    FunctionArgumentType lambda_body_type,
    FunctionArgumentTypeOptions options) {
  // For now, we don't have the use cases of non REQUIRED values.
  FunctionArgumentType arg_type =
      FunctionArgumentType(ARG_TYPE_LAMBDA, options);
  arg_type.lambda_ = std::make_shared<ArgumentTypeLambda>(
      std::move(lambda_argument_types), std::move(lambda_body_type));
  arg_type.num_occurrences_ = 1;
  arg_type.type_ = nullptr;
  return arg_type;
}

FunctionArgumentType FunctionArgumentType::Lambda(
    std::vector<FunctionArgumentType> lambda_argument_types,
    FunctionArgumentType lambda_body_type) {
  return Lambda(std::move(lambda_argument_types), std::move(lambda_body_type),
                FunctionArgumentTypeOptions());
}

// static
std::string FunctionArgumentTypeOptions::OptionsDebugString() const {
  // Print the options in a format matching proto ShortDebugString.
  // In java, we just print the proto itself.
  std::vector<std::string> options;
  if (data_->constness_level ==
      ConstnessLevelProto::LEGACY_LITERAL_OR_PARAMETER) {
    options.push_back("must_be_constant: true");
  }
  if (data_->constness_level == ConstnessLevelProto::ANALYSIS_CONST) {
    options.push_back("must_be_analysis_constant: true");
  }
  if (data_->constness_level == ConstnessLevelProto::IMMUTABLE_CONST) {
    options.push_back("must_be_immutable_constant: true");
  }
  if (data_->constness_level ==
      ConstnessLevelProto::LEGACY_CONSTANT_EXPRESSION) {
    options.push_back("must_be_constant_expression: true");
  }
  if (data_->must_be_non_null) options.push_back("must_be_non_null: true");
  if (data_->default_value.has_value()) {
    options.push_back(absl::StrCat("default_value: ",
                                   data_->default_value->ShortDebugString()));
  }
  if (data_->is_not_aggregate) options.push_back("is_not_aggregate: true");
  if (data_->procedure_argument_mode != FunctionEnums::NOT_SET) {
    options.push_back(absl::StrCat("procedure_argument_mode: ",
                                   FunctionEnums::ProcedureArgumentMode_Name(
                                       data_->procedure_argument_mode)));
  }
  // No need to print the default ARGUMENT_NON_ALIASED.
  if (data_->argument_alias_kind == FunctionEnums::ARGUMENT_ALIASED) {
    options.push_back(absl::StrCat(
        "argument_alias_kind: ",
        FunctionEnums::ArgumentAliasKind_Name(data_->argument_alias_kind)));
  }
  if (options.empty()) {
    return "";
  } else {
    return absl::StrCat(" {", absl::StrJoin(options, ", "), "}");
  }
}

std::string FunctionArgumentTypeOptions::GetSQLDeclaration(
    ProductMode product_mode) const {
  return GetSQLDeclaration(product_mode, /*use_external_float32=*/false);
}

std::string FunctionArgumentTypeOptions::GetSQLDeclaration(
    ProductMode product_mode, bool use_external_float32) const {
  std::vector<std::string> options;
  // Some of these don't currently have any SQL syntax.
  // We emit a comment for those cases.
  if (data_->constness_level ==
      ConstnessLevelProto::LEGACY_LITERAL_OR_PARAMETER)
    options.push_back("/*must_be_constant*/");
  if (data_->constness_level == ConstnessLevelProto::ANALYSIS_CONST)
    options.push_back("/*must_be_analysis_constant*/");
  if (data_->constness_level == ConstnessLevelProto::IMMUTABLE_CONST)
    options.push_back("/*must_be_immutable_constant*/");
  if (data_->constness_level == ConstnessLevelProto::LEGACY_CONSTANT_EXPRESSION)
    options.push_back("/*must_be_constant_expression*/");

  if (data_->must_be_non_null) options.push_back("/*must_be_non_null*/");
  if (data_->default_value.has_value()) {
    options.push_back("DEFAULT");
    options.push_back(data_->default_value->GetSQLLiteral(
        product_mode, use_external_float32));
  }
  if (data_->is_not_aggregate) options.push_back("NOT AGGREGATE");
  if (options.empty()) {
    return "";
  } else {
    return absl::StrCat(" ", absl::StrJoin(options, " "));
  }
}

std::string FunctionArgumentType::SignatureArgumentKindToString(
    SignatureArgumentKind kind) {
  switch (kind) {
    case ARG_TYPE_FIXED:
      return "FIXED";
    case ARG_TYPE_ANY_1:
      return "<T1>";
    case ARG_TYPE_ANY_2:
      return "<T2>";
    case ARG_TYPE_ANY_3:
      return "<T3>";
    case ARG_TYPE_ANY_4:
      return "<T4>";
    case ARG_TYPE_ANY_5:
      return "<T5>";
    case ARG_ARRAY_TYPE_ANY_1:
      return "<array<T1>>";
    case ARG_ARRAY_TYPE_ANY_2:
      return "<array<T2>>";
    case ARG_ARRAY_TYPE_ANY_3:
      return "<array<T3>>";
    case ARG_ARRAY_TYPE_ANY_4:
      return "<array<T4>>";
    case ARG_ARRAY_TYPE_ANY_5:
      return "<array<T5>>";
    case ARG_PROTO_MAP_ANY:
      return "<proto_map<proto_K, proto_V>>";
    case ARG_PROTO_MAP_KEY_ANY:
      return "<proto_K>";
    case ARG_PROTO_MAP_VALUE_ANY:
      return "<proto_V>";
    case ARG_PROTO_ANY:
      return "<proto>";
    case ARG_STRUCT_ANY:
      return "<struct>";
    case ARG_ENUM_ANY:
      return "<enum>";
    case ARG_TYPE_RELATION:
      return "ANY TABLE";
    case ARG_TYPE_MODEL:
      return "ANY MODEL";
    case ARG_TYPE_CONNECTION:
      return "ANY CONNECTION";
    case ARG_TYPE_DESCRIPTOR:
      return "ANY DESCRIPTOR";
    case ARG_TYPE_ARBITRARY:
      return "<arbitrary>";
    case ARG_TYPE_VOID:
      return "<void>";
    case ARG_TYPE_LAMBDA:
      return "<function<T->T>>";
    case ARG_RANGE_TYPE_ANY_1:
      return "<range<T>>";
    case ARG_TYPE_GRAPH_NODE:
      return "<graph_node>";
    case ARG_TYPE_GRAPH_EDGE:
      return "<graph_edge>";
    case ARG_TYPE_GRAPH_ELEMENT:
      return "<graph_element>";
    case ARG_TYPE_GRAPH_PATH:
      return "<graph_path>";
    case ARG_TYPE_GRAPH:
      return "ANY GRAPH";
    case ARG_TYPE_SEQUENCE:
      return "ANY SEQUENCE";
    case ARG_MEASURE_TYPE_ANY_1:
      return "<measure<T1>>";
    case ARG_MAP_TYPE_ANY_1_2:
      return "<map<T1, T2>>";
    case __SignatureArgumentKind__switch_must_have_a_default__:
      break;  // Handling this case is only allowed internally.
  }
  return "UNKNOWN_ARG_KIND";
}

std::shared_ptr<const FunctionArgumentTypeOptions>
FunctionArgumentType::SimpleOptions(ArgumentCardinality cardinality) {
  static auto* options =
      new std::array<std::shared_ptr<const FunctionArgumentTypeOptions>, 3>{
          std::shared_ptr<const FunctionArgumentTypeOptions>(
              new FunctionArgumentTypeOptions(FunctionEnums::REQUIRED)),
          std::shared_ptr<const FunctionArgumentTypeOptions>(
              new FunctionArgumentTypeOptions(FunctionEnums::OPTIONAL)),
          std::shared_ptr<const FunctionArgumentTypeOptions>(
              new FunctionArgumentTypeOptions(FunctionEnums::REPEATED))};
  switch (cardinality) {
    case FunctionEnums::REQUIRED:
      return (*options)[0];
    case FunctionEnums::OPTIONAL:
      return (*options)[1];
    case FunctionEnums::REPEATED:
      return (*options)[2];
  }
}

FunctionArgumentType::FunctionArgumentType(
    SignatureArgumentKind kind, const Type* type,
    std::shared_ptr<const FunctionArgumentTypeOptions> options,
    int num_occurrences)
    : kind_(kind),
      num_occurrences_(num_occurrences),
      type_(type),
      options_(std::move(options)) {
  ABSL_DCHECK_EQ(kind == ARG_TYPE_FIXED, type != nullptr);
}

FunctionArgumentType::FunctionArgumentType(SignatureArgumentKind kind,
                                           ArgumentCardinality cardinality,
                                           int num_occurrences)
    : FunctionArgumentType(kind, /*type=*/nullptr, SimpleOptions(cardinality),
                           num_occurrences) {}

FunctionArgumentType::FunctionArgumentType(SignatureArgumentKind kind,
                                           FunctionArgumentTypeOptions options,
                                           int num_occurrences)
    : FunctionArgumentType(
          kind, /*type=*/nullptr,
          std::make_shared<FunctionArgumentTypeOptions>(std::move(options)),
          num_occurrences) {}

FunctionArgumentType::FunctionArgumentType(SignatureArgumentKind kind,
                                           int num_occurrences)
    : FunctionArgumentType(kind, /*type=*/nullptr, SimpleOptions(),
                           num_occurrences) {}

FunctionArgumentType::FunctionArgumentType(const Type* type,
                                           ArgumentCardinality cardinality,
                                           int num_occurrences)
    : FunctionArgumentType(ARG_TYPE_FIXED, type, SimpleOptions(cardinality),
                           num_occurrences) {}

FunctionArgumentType::FunctionArgumentType(const Type* type,
                                           FunctionArgumentTypeOptions options,
                                           int num_occurrences)
    : FunctionArgumentType(
          ARG_TYPE_FIXED, type,
          std::make_shared<FunctionArgumentTypeOptions>(std::move(options)),
          num_occurrences) {}

FunctionArgumentType::FunctionArgumentType(const Type* type,
                                           int num_occurrences)
    : FunctionArgumentType(ARG_TYPE_FIXED, type, SimpleOptions(),
                           num_occurrences) {}

FunctionArgumentType::FunctionArgumentType(const Type* type,
                                           int num_occurrences,
                                           SignatureArgumentKind original_kind)
    : FunctionArgumentType(ARG_TYPE_FIXED, type, SimpleOptions(),
                           num_occurrences) {
  ABSL_DCHECK(IsConcrete());
  original_kind_ = original_kind;
}

bool FunctionArgumentType::IsConcrete() const {
  if (kind_ != ARG_TYPE_FIXED && kind_ != ARG_TYPE_RELATION &&
      kind_ != ARG_TYPE_MODEL && kind_ != ARG_TYPE_CONNECTION &&
      kind_ != ARG_TYPE_LAMBDA && kind_ != ARG_TYPE_SEQUENCE &&
      kind_ != ARG_TYPE_GRAPH && kind_ != ARG_TYPE_VOID) {
    return false;
  }
  if (num_occurrences_ < 0) {
    return false;
  }

  // Lambda is concrete if all args and body are concrete.
  if (kind_ == ARG_TYPE_LAMBDA) {
    for (const auto& arg : lambda().argument_types()) {
      if (!arg.IsConcrete()) {
        return false;
      }
    }
    return lambda().body_type().IsConcrete();
  }
  return true;
}

bool FunctionArgumentType::IsTemplated() const {
  // It is templated if it is not a fixed scalar, it is not a fixed relation,
  // and it is not a void argument. It is also templated if it is a lambda that
  // has a templated argument or body.
  if (kind_ == ARG_TYPE_LAMBDA) {
    for (const FunctionArgumentType& arg_type : lambda().argument_types()) {
      if (arg_type.IsTemplated()) {
        return true;
      }
    }
    return lambda().body_type().IsTemplated();
  }
  return kind_ != ARG_TYPE_FIXED && !IsFixedRelation() && !IsVoid();
}

bool FunctionArgumentType::IsScalar() const {
  return kind_ == ARG_TYPE_FIXED || kind_ == ARG_TYPE_ANY_1 ||
         kind_ == ARG_TYPE_ANY_2 || kind_ == ARG_TYPE_ANY_3 ||
         kind_ == ARG_TYPE_ANY_4 || kind_ == ARG_TYPE_ANY_5 ||
         kind_ == ARG_ARRAY_TYPE_ANY_1 || kind_ == ARG_ARRAY_TYPE_ANY_2 ||
         kind_ == ARG_ARRAY_TYPE_ANY_3 || kind_ == ARG_ARRAY_TYPE_ANY_4 ||
         kind_ == ARG_ARRAY_TYPE_ANY_5 || kind_ == ARG_PROTO_MAP_ANY ||
         kind_ == ARG_PROTO_MAP_KEY_ANY || kind_ == ARG_PROTO_MAP_VALUE_ANY ||
         kind_ == ARG_PROTO_ANY || kind_ == ARG_STRUCT_ANY ||
         kind_ == ARG_ENUM_ANY || kind_ == ARG_TYPE_ARBITRARY ||
         kind_ == ARG_TYPE_GRAPH_NODE || kind_ == ARG_TYPE_GRAPH_EDGE ||
         kind_ == ARG_TYPE_GRAPH_ELEMENT || kind_ == ARG_TYPE_GRAPH_PATH ||
         kind_ == ARG_RANGE_TYPE_ANY_1 || kind_ == ARG_MAP_TYPE_ANY_1_2;
}

// Intentionally restrictive for known functional programming functions. If this
// is to be expanded in the future, make sure type inference part of signature
// matching works as intended.
static bool IsLambdaAllowedArgKind(const SignatureArgumentKind kind) {
  return kind == ARG_TYPE_FIXED || kind == ARG_TYPE_ANY_1 ||
         kind == ARG_TYPE_ANY_2 || kind == ARG_TYPE_ANY_3 ||
         kind == ARG_TYPE_ANY_4 || kind == ARG_TYPE_ANY_5;
}

absl::Status FunctionArgumentType::CheckLambdaArgType(
    const FunctionArgumentType& arg_type) {
  if (!IsLambdaAllowedArgKind(arg_type.kind())) {
    return ::googlesql_base::UnimplementedErrorBuilder()
           << "Argument kind not supported by function-type argument: "
           << SignatureArgumentKindToString(arg_type.kind());
  }

  // Make sure the argument type options are just simple REQUIRED options.
  googlesql::Type::FileDescriptorSetMap arg_fdset_map;
  FunctionArgumentTypeOptionsProto arg_options_proto;
  GOOGLESQL_RETURN_IF_ERROR(arg_type.options().Serialize(
      /*arg_type=*/nullptr, &arg_options_proto, &arg_fdset_map));
  GOOGLESQL_RET_CHECK(arg_fdset_map.empty());

  FunctionArgumentTypeOptionsProto simple_options_proto;
  googlesql::Type::FileDescriptorSetMap simple_arg_fdset_map;
  GOOGLESQL_RETURN_IF_ERROR(SimpleOptions(REQUIRED)->Serialize(
      nullptr, &simple_options_proto, &simple_arg_fdset_map));
  GOOGLESQL_RET_CHECK(simple_arg_fdset_map.empty());

  GOOGLESQL_RET_CHECK(google::protobuf::util::MessageDifferencer::Equals(arg_options_proto,
                                                     simple_options_proto))
      << "Only REQUIRED simple options are supported by function-type "
         "arguments";
  return absl::OkStatus();
}

absl::Status FunctionArgumentType::IsValid(ProductMode product_mode) const {
  switch (cardinality()) {
    case REPEATED:
      if (IsConcrete()) {
        if (num_occurrences_ < 0) {
          return MakeSqlError()
                 << "REPEATED concrete argument has " << num_occurrences_
                 << " occurrences but must have at least 0: " << DebugString();
        }
      }
      if (HasDefault()) {
        return MakeSqlError()
               << "Default value cannot be applied to a REPEATED argument: "
               << DebugString();
      }
      break;
    case OPTIONAL:
      if (IsConcrete()) {
        if (num_occurrences_ < 0 || num_occurrences_ > 1) {
          return MakeSqlError()
                 << "OPTIONAL concrete argument has " << num_occurrences_
                 << " occurrences but must have 0 or 1: " << DebugString();
        }
      }
      if (HasDefault()) {
        if (!CanHaveDefaultValue(kind())) {
          // Relation/Model/Connection/Descriptor arguments cannot have
          // default values.
          return MakeSqlError()
                 << SignatureArgumentKindToString(kind())
                 << " argument cannot have a default value: " << DebugString();
        }
        if (!GetDefault().value().is_valid()) {
          return MakeSqlError()
                 << "Default value must be valid: " << DebugString();
        }
        // Verify type match for fixed-typed arguments.
        if (type() != nullptr && !GetDefault().value().type()->Equals(type())) {
          return MakeSqlError()
                 << "Default value type does not match the argument type: "
                 << type()->ShortTypeName(product_mode) << " vs "
                 << GetDefault().value().type()->ShortTypeName(product_mode)
                 << "; " << DebugString();
        }
      }
      break;
    case REQUIRED:
      if (IsConcrete()) {
        if (num_occurrences_ != 1) {
          return MakeSqlError()
                 << "REQUIRED concrete argument has " << num_occurrences_
                 << " occurrences but must have exactly 1: " << DebugString();
        }
      }
      if (HasDefault()) {
        return MakeSqlError()
               << "Default value cannot be applied to a REQUIRED argument: "
               << DebugString();
      }
      break;
  }

  if (IsLambda()) {
    if (lambda_ == nullptr) {
      return absl::InternalError(
          "FunctionArgumentType with ARG_TYPE_LAMBDA constructed directly is "
          "not allowed. Use FunctionArgumentType::Lambda instead.");
    }
    GOOGLESQL_RET_CHECK_EQ(cardinality(), REQUIRED);
    for (const auto& arg_type : lambda().argument_types()) {
      GOOGLESQL_RETURN_IF_ERROR(CheckLambdaArgType(arg_type));
    }
    GOOGLESQL_RETURN_IF_ERROR(CheckLambdaArgType(lambda().body_type()));
  }
  return absl::OkStatus();
}

std::string FunctionArgumentType::UserFacingName(
    ProductMode product_mode, bool print_template_details) const {
  if (IsLambda()) {
    // If we only return "FUNCTION", for signature not found error, the user
    // would get a list of two identical signature strings.
    std::string args = absl::StrJoin(
        lambda().argument_types(), ", ",
        [product_mode, print_template_details](
            std::string* out, const FunctionArgumentType& arg) {
          out->append(arg.UserFacingName(product_mode, print_template_details));
        });
    if (lambda().argument_types().size() == 1) {
      return absl::Substitute("FUNCTION<$0->$1>", args,
                              lambda().body_type().UserFacingName(
                                  product_mode, print_template_details));
    }
    return absl::Substitute("FUNCTION<($0)->$1>", args,
                            lambda().body_type().UserFacingName(
                                product_mode, print_template_details));
  }
  if (type() == nullptr) {
    switch (kind()) {
      case ARG_ARRAY_TYPE_ANY_1:
        return print_template_details ? "ARRAY<T1>" : "ARRAY";
      case ARG_ARRAY_TYPE_ANY_2:
        return print_template_details ? "ARRAY<T2>" : "ARRAY";
      case ARG_ARRAY_TYPE_ANY_3:
        return print_template_details ? "ARRAY<T3>" : "ARRAY";
      case ARG_ARRAY_TYPE_ANY_4:
        return print_template_details ? "ARRAY<T4>" : "ARRAY";
      case ARG_ARRAY_TYPE_ANY_5:
        return print_template_details ? "ARRAY<T5>" : "ARRAY";
      case ARG_PROTO_ANY:
        return "PROTO";
      case ARG_STRUCT_ANY:
        return "STRUCT";
      case ARG_ENUM_ANY:
        return "ENUM";
      case ARG_PROTO_MAP_ANY:
        return "PROTO_MAP";
      case ARG_PROTO_MAP_KEY_ANY:
        return "PROTO_MAP_KEY";
      case ARG_PROTO_MAP_VALUE_ANY:
        return "PROTO_MAP_VALUE";
      case ARG_TYPE_ANY_1:
        return print_template_details ? "T1" : "ANY";
      case ARG_TYPE_ANY_2:
        return print_template_details ? "T2" : "ANY";
      case ARG_TYPE_ANY_3:
        return print_template_details ? "T3" : "ANY";
      case ARG_TYPE_ANY_4:
        return print_template_details ? "T4" : "ANY";
      case ARG_TYPE_ANY_5:
        return print_template_details ? "T5" : "ANY";
      case ARG_TYPE_ARBITRARY:
        return "ANY";
      case ARG_TYPE_RELATION:
        return "TABLE";
      case ARG_TYPE_MODEL:
        return "MODEL";
      case ARG_TYPE_CONNECTION:
        return "CONNECTION";
      case ARG_TYPE_DESCRIPTOR:
        return "DESCRIPTOR";
      case ARG_TYPE_VOID:
        return "VOID";
      case ARG_TYPE_LAMBDA:
        return "FUNCTION";
      case ARG_RANGE_TYPE_ANY_1:
        return "RANGE";
      case ARG_TYPE_GRAPH_NODE:
        return "GRAPH_NODE";
      case ARG_TYPE_GRAPH_EDGE:
        return "GRAPH_EDGE";
      case ARG_TYPE_GRAPH_ELEMENT:
        return "GRAPH_ELEMENT";
      case ARG_TYPE_GRAPH_PATH:
        return "GRAPH_PATH";
      case ARG_TYPE_GRAPH:
        return "GRAPH";
      case ARG_TYPE_SEQUENCE:
        return "SEQUENCE";
      case ARG_MAP_TYPE_ANY_1_2:
        return print_template_details ? "MAP<T1, T2>" : "MAP";
      case ARG_MEASURE_TYPE_ANY_1:
        return print_template_details ? "MEASURE<T1>" : "MEASURE";
      case ARG_TYPE_FIXED:
      default:
        // We really should have had type() != nullptr in this case.
        ABSL_DCHECK(type() != nullptr) << DebugString();
        return "?";
    }
  } else {
    return type()->ShortTypeName(product_mode);
  }
}

std::string FunctionArgumentType::UserFacingNameWithCardinality(
    ProductMode product_mode, NamePrintingStyle print_style,
    bool print_template_details) const {
  std::string arg_type_string =
      UserFacingName(product_mode, print_template_details);
  const auto named_argument_kind = options().named_argument_kind();
  if (options().has_argument_name() &&
      ((named_argument_kind == kNamedOnly &&
        print_style == NamePrintingStyle::kIfNamedOnly) ||
       (named_argument_kind != kPositionalOnly &&
        print_style == NamePrintingStyle::kIfNotPositionalOnly))) {
    if (named_argument_kind == FunctionEnums::POSITIONAL_OR_NAMED) {
      arg_type_string =
          absl::StrCat("[", argument_name(), "=>]", arg_type_string);
    } else {
      arg_type_string = absl::StrCat(argument_name(), " => ", arg_type_string);
    }
  }
  if (optional()) {
    return absl::StrCat("[", arg_type_string, "]");
  } else if (repeated()) {
    return absl::StrCat("[", arg_type_string, ", ...]");
  } else {
    return arg_type_string;
  }
}

std::string FunctionArgumentType::DebugString(bool verbose) const {
  // Note, an argument cannot be both repeated and optional.
  std::string cardinality(repeated()   ? "repeated"
                          : optional() ? "optional"
                                       : "");
  std::string occurrences(IsConcrete() && !required()
                              ? absl::StrCat("(", num_occurrences_, ")")
                              : "");
  std::string result =
      absl::StrCat(cardinality, occurrences, required() ? "" : " ");
  if (IsLambda()) {
    std::string args = absl::StrJoin(
        lambda().argument_types(), ", ",
        [verbose](std::string* out, const FunctionArgumentType& arg) {
          out->append(arg.DebugString(verbose));
        });
    if (lambda().argument_types().size() == 1) {
      absl::SubstituteAndAppend(&result, "FUNCTION<$0->$1>", args,
                                lambda().body_type().DebugString());
    } else {
      absl::SubstituteAndAppend(&result, "FUNCTION<($0)->$1>", args,
                                lambda().body_type().DebugString());
    }
  } else if (type_ != nullptr) {
    absl::StrAppend(&result, type_->DebugString());
  } else if (IsRelation() && options_->has_relation_input_schema()) {
    result = options_->relation_input_schema().DebugString();
  } else if (kind_ == ARG_TYPE_ARBITRARY) {
    absl::StrAppend(&result, "ANY TYPE");
  } else {
    absl::StrAppend(&result, SignatureArgumentKindToString(kind_));
  }
  if (verbose) {
    absl::StrAppend(&result, options_->OptionsDebugString());
  }
  if (options_->has_argument_name()) {
    absl::StrAppend(&result, " ", options_->argument_name());
  }
  return result;
}
std::string FunctionArgumentType::GetSQLDeclaration(
    ProductMode product_mode) const {
  return GetSQLDeclaration(product_mode, /*use_external_float32=*/false);
}

std::string FunctionArgumentType::GetSQLDeclaration(
    ProductMode product_mode, bool use_external_float32) const {
  // We emit comments for the things that don't have a SQL syntax currently.
  std::string cardinality(repeated()   ? "/*repeated*/"
                          : optional() ? "/*optional*/"
                                       : "");
  std::string result = absl::StrCat(cardinality, required() ? "" : " ");
  if (IsLambda()) {
    std::string args = absl::StrJoin(
        lambda().argument_types(), ", ",
        [product_mode](std::string* out, const FunctionArgumentType& arg) {
          out->append(arg.GetSQLDeclaration(product_mode));
        });
    if (lambda().argument_types().size() == 1) {
      return absl::Substitute("FUNCTION<$0->$1>", args,
                              lambda().body_type().GetSQLDeclaration(
                                  product_mode, use_external_float32));
    }
    return absl::Substitute("FUNCTION<($0)->$1>", args,
                            lambda().body_type().GetSQLDeclaration(
                                product_mode, use_external_float32));
  }
  // TODO: Consider using UserFacingName() here.
  if (type_ != nullptr) {
    absl::StrAppend(&result,
                    type_->TypeName(product_mode, use_external_float32));
  } else if (options_->has_relation_input_schema()) {
    absl::StrAppend(
        &result,
        options_->relation_input_schema().GetSQLDeclaration(product_mode));
  } else if (kind_ == ARG_TYPE_ARBITRARY) {
    absl::StrAppend(&result, "ANY TYPE");
  } else {
    absl::StrAppend(&result, SignatureArgumentKindToString(kind_));
  }
  absl::StrAppend(
      &result, options_->GetSQLDeclaration(product_mode, use_external_float32));
  return result;
}

FunctionSignature::FunctionSignature(FunctionArgumentType result_type,
                                     FunctionArgumentTypeList arguments,
                                     void* context_ptr)
    : arguments_(std::move(arguments)),
      result_type_(std::move(result_type)),
      num_repeated_arguments_(ComputeNumRepeatedArguments()),
      num_optional_arguments_(ComputeNumOptionalArguments()),
      context_ptr_(context_ptr) {
  Init();
}

FunctionSignature::FunctionSignature(FunctionArgumentType result_type,
                                     FunctionArgumentTypeList arguments,
                                     int64_t context_id)
    : FunctionSignature(std::move(result_type), std::move(arguments),
                        context_id, FunctionSignatureOptions()) {}

FunctionSignature::FunctionSignature(const FunctionArgumentType& result_type,
                                     FunctionArgumentTypeList arguments,
                                     int64_t context_id,
                                     FunctionSignatureOptions options)
    : arguments_(std::move(arguments)),
      result_type_(std::move(result_type)),
      num_repeated_arguments_(ComputeNumRepeatedArguments()),
      num_optional_arguments_(ComputeNumOptionalArguments()),
      context_id_(context_id),
      options_(std::move(options)) {
  Init();
}

void FunctionSignature::Init() {
  init_status_ = InitInternal();
  // Check failure is only expected if the object was constructed with invalid
  // args.
  // Most function signature code is static and should be OK.
  // In case of dynamically defined functions with CREATE FUNCTION, engines
  // should have done rigorous validation on the input arguments. That being
  // said, engines should still check init_status() after creating a
  // FunctionSignature.
  GOOGLESQL_DCHECK_OK(init_status_);
}

absl::Status FunctionSignature::InitInternal() {
  GOOGLESQL_RETURN_IF_ERROR(CreateNamedArgumentToOptionsMap());
  GOOGLESQL_RETURN_IF_ERROR(IsValid(ProductMode::PRODUCT_EXTERNAL));
  ComputeConcreteArgumentTypes();
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<FunctionSignature>>
FunctionSignature::Deserialize(const FunctionSignatureProto& proto,
                               const TypeDeserializer& type_deserializer) {
  FunctionArgumentTypeList arguments;
  for (const FunctionArgumentTypeProto& argument_proto : proto.argument()) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<FunctionArgumentType> argument,
        FunctionArgumentType::Deserialize(argument_proto, type_deserializer));
    arguments.push_back(*argument);
  }

  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<FunctionArgumentType> result_type,
                   FunctionArgumentType::Deserialize(proto.return_type(),
                                                     type_deserializer));

  std::unique_ptr<FunctionSignatureOptions> options;
  GOOGLESQL_RETURN_IF_ERROR(
      FunctionSignatureOptions::Deserialize(proto.options(), &options));

  return std::make_unique<FunctionSignature>(*result_type, arguments,
                                             proto.context_id(), *options);
}

absl::Status FunctionSignature::Serialize(
    FileDescriptorSetMap* file_descriptor_set_map,
    FunctionSignatureProto* proto) const {
  options_.Serialize(proto->mutable_options());

  GOOGLESQL_RETURN_IF_ERROR(result_type().Serialize(file_descriptor_set_map,
                                          proto->mutable_return_type()));

  for (const FunctionArgumentType& argument : arguments()) {
    GOOGLESQL_RETURN_IF_ERROR(
        argument.Serialize(file_descriptor_set_map, proto->add_argument()));
  }

  proto->set_context_id(context_id());
  return absl::OkStatus();
}

bool FunctionSignature::HasUnsupportedType(
    const LanguageOptions& language_options) const {
  // The 'result_type()->type()' can be nullptr for templated
  // arguments.
  if (result_type().type() != nullptr &&
      !result_type().type()->IsSupportedType(language_options)) {
    return true;
  }
  for (const FunctionArgumentType& argument_type : arguments()) {
    // The 'argument_type.type()' can be nullptr for templated arguments.
    if (argument_type.type() != nullptr &&
        !argument_type.type()->IsSupportedType(language_options)) {
      return true;
    }
  }
  return false;
}

void FunctionSignature::ComputeConcreteArgumentTypes() {
  // TODO: Do we really care if the result signature is concrete?
  is_concrete_ = ComputeIsConcrete();
  if (!HasConcreteArguments()) return;

  // Count number of concrete args, and find the range of repeateds.
  int first_repeated_idx = -1;
  int last_repeated_idx = -1;
  int num_concrete_args = 0;

  for (int idx = 0; idx < arguments_.size(); ++idx) {
    const FunctionArgumentType& arg = arguments_[idx];
    if (arg.repeated()) {
      if (first_repeated_idx == -1) first_repeated_idx = idx;
      last_repeated_idx = idx;
    }
    if (arg.num_occurrences() > 0) {
      num_concrete_args += arg.num_occurrences();
    }
  }

  concrete_arguments_.reserve(num_concrete_args);

  if (first_repeated_idx == -1) {
    // If we have no repeateds, just loop through and copy present args.
    for (int idx = 0; idx < arguments_.size(); ++idx) {
      const FunctionArgumentType& arg = arguments_[idx];
      if (arg.num_occurrences() == 1) {
        concrete_arguments_.push_back(arg);
      }
    }
  } else {
    // Add arguments that come before repeated arguments.
    for (int idx = 0; idx < first_repeated_idx; ++idx) {
      const FunctionArgumentType& arg = arguments_[idx];
      if (arg.num_occurrences() == 1) {
        concrete_arguments_.push_back(arg);
      }
    }

    // Add concrete repetitions of all repeated arguments.
    const int num_repeated_occurrences =
        arguments_[first_repeated_idx].num_occurrences();
    for (int c = 0; c < num_repeated_occurrences; ++c) {
      for (int idx = first_repeated_idx; idx <= last_repeated_idx; ++idx) {
        concrete_arguments_.push_back(arguments_[idx]);
      }
    }

    // Add any arguments that come after the repeated arguments.
    for (int idx = last_repeated_idx + 1; idx < arguments_.size(); ++idx) {
      const FunctionArgumentType& arg = arguments_[idx];
      if (arg.num_occurrences() == 1) {
        concrete_arguments_.push_back(arg);
      }
    }
  }
}

bool FunctionSignature::HasConcreteArguments() const {
  if (is_concrete_) {
    return true;
  }
  for (const FunctionArgumentType& argument : arguments_) {
    // num_occurrences cannot be -1 for a concrete signature, because templated
    // types must have been set already. However, we need to clean up existing
    // callers that may not have set it up.

    // Missing templated arguments may have unknown types in a concrete
    // signature if they are omitted in a function call.
    if (argument.num_occurrences() > 0 && !argument.IsConcrete()) {
      return false;
    }
  }
  return true;
}

absl::Status FunctionSignature::CreateNamedArgumentToOptionsMap() {
  for (int i = 0; i < arguments().size(); ++i) {
    const FunctionArgumentType& arg_type = arguments()[i];

    if (arg_type.options().has_argument_name() &&
        (arg_type.options().named_argument_kind() ==
             FunctionEnums::NAMED_ONLY ||
         arg_type.options().named_argument_kind() ==
             FunctionEnums::POSITIONAL_OR_NAMED)) {
      const auto it = argument_name_to_options_.try_emplace(
          arg_type.options().argument_name(), &arg_type.options());
      GOOGLESQL_RET_CHECK(it.second) << "Duplicate named argument "
                           << arg_type.options().argument_name()
                           << " found in signature";
      last_named_arg_index_ = i;
    }
    if (arg_type.GetDefault().has_value()) {
      last_arg_index_with_default_ = i;
    }
  }
  return absl::OkStatus();
}

bool FunctionSignature::ComputeIsConcrete() const {
  if (!HasConcreteArguments()) {
    return false;
  }
  if (result_type().IsRelation()) {
    // This signature is for a TVF, so the return type is always a relation.
    // The signature is concrete if and only if all the arguments are concrete.
    // TODO: A relation argument or result_type indicates that any
    // relation can be used, and therefore it is not concrete.  Fix this.
    return true;
  } else {
    return result_type_.IsConcrete();
  }
}

absl::StatusOr<std::string> FunctionSignature::CheckArgumentConstraints(
    const std::vector<InputArgumentType>& arguments) const {
  return options_.CheckFunctionSignatureConstraints(*this, arguments);
}

std::string FunctionSignature::DebugString(absl::string_view function_name,
                                           bool verbose) const {
  std::string result = absl::StrCat(function_name, "(");
  int first = true;
  for (const FunctionArgumentType& argument : arguments_) {
    absl::StrAppend(&result, (first ? "" : ", "),
                    argument.DebugString(verbose));
    first = false;
  }
  absl::StrAppend(&result, ") -> ", result_type_.DebugString(verbose));
  if (verbose) {
    const std::string deprecation_warnings_debug_string =
        DeprecationWarningsToDebugString(AdditionalDeprecationWarnings());
    if (!deprecation_warnings_debug_string.empty()) {
      absl::StrAppend(&result, " ", deprecation_warnings_debug_string);
    }
    if (options_.rejects_collation()) {
      absl::StrAppend(&result, " rejects_collation=TRUE");
    }
  }
  return result;
}

std::string FunctionSignature::SignaturesToString(
    absl::Span<const FunctionSignature> signatures, bool verbose,
    absl::string_view prefix, absl::string_view separator) {
  std::string out;
  for (const FunctionSignature& signature : signatures) {
    absl::StrAppend(&out, (out.empty() ? "" : separator), prefix,
                    signature.DebugString(/*function_name=*/"", verbose));
  }
  return out;
}

const ComputeResultAnnotationsCallback&
FunctionSignature::GetComputeResultAnnotationsCallback() const {
  return options_.compute_result_annotations_callback();
}

namespace {
absl::StatusOr<bool> HasColumnWithCollation(const TVFRelation& relation) {
  for (const TVFSchemaColumn& column : relation.columns()) {
    if (column.annotation_map != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(Collation collation,
                       Collation::MakeCollation(*column.annotation_map));
      if (!collation.Empty()) {
        return true;
      }
    }
  }
  return false;
}

// Decides if a FunctionSignature should have "RETURNS" clause in its SQL
// declaration based on its <result_type> field.
absl::StatusOr<bool> ShouldHaveReturnsClauseInSQLDeclaration(
    const FunctionArgumentType& result_type) {
  if (result_type.IsVoid() || result_type.kind() == ARG_TYPE_ARBITRARY) {
    return false;
  }

  if (result_type.IsRelation()) {
    if (!result_type.options().has_relation_input_schema()) {
      return false;
    }

    // When TVF query has collated output columns, if an explicit result schema
    // is present, the analyzer will throw an error. To avoid failing the
    // reparsing test, we do not generate "RETURNS" clause for this situation.
    GOOGLESQL_ASSIGN_OR_RETURN(
        bool has_column_with_collation,
        HasColumnWithCollation(result_type.options().relation_input_schema()));
    if (has_column_with_collation) {
      return false;
    }
  }

  return true;
}
}  // namespace

std::string FunctionSignature::GetSQLDeclaration(
    absl::Span<const std::string> argument_names,
    ProductMode product_mode) const {
  return GetSQLDeclaration(argument_names, product_mode,
                           /*use_external_float32=*/false);
}

std::string FunctionSignature::GetSQLDeclaration(
    absl::Span<const std::string> argument_names, ProductMode product_mode,
    bool use_external_float32) const {
  std::string out = "(";
  for (int i = 0; i < arguments_.size(); ++i) {
    if (i > 0) out += ", ";
    if (arguments_[i].options().procedure_argument_mode() !=
        FunctionEnums::NOT_SET) {
      absl::StrAppend(&out,
                      FunctionEnums::ProcedureArgumentMode_Name(
                          arguments_[i].options().procedure_argument_mode()),
                      " ");
    }
    if (argument_names.size() > i) {
      absl::StrAppend(&out, ToIdentifierLiteral(argument_names[i]), " ");
    }
    absl::StrAppend(&out, arguments_[i].GetSQLDeclaration(
                              product_mode, use_external_float32));
  }
  absl::StrAppend(&out, ")");
  absl::StatusOr<bool> status_or_should_have_returns_clause =
      ShouldHaveReturnsClauseInSQLDeclaration(result_type());
  if (!status_or_should_have_returns_clause.ok()) {
    absl::StrAppend(&out, " [Error in generating RETURNS clause: ",
                    status_or_should_have_returns_clause.status().message(),
                    "] ");
  }
  if (status_or_should_have_returns_clause.value()) {
    absl::StrAppend(
        &out, " RETURNS ",
        result_type_.GetSQLDeclaration(product_mode, use_external_float32));
  }
  return out;
}

namespace {
inline bool IsRelatedToAny1(SignatureArgumentKind kind) {
  return kind == ARG_TYPE_ANY_1 || kind == ARG_ARRAY_TYPE_ANY_1 ||
         kind == ARG_MAP_TYPE_ANY_1_2 || kind == ARG_RANGE_TYPE_ANY_1 ||
         kind == ARG_MEASURE_TYPE_ANY_1;
}

inline bool IsRelatedToAny2(SignatureArgumentKind kind) {
  return kind == ARG_TYPE_ANY_2 || kind == ARG_ARRAY_TYPE_ANY_2 ||
         kind == ARG_MAP_TYPE_ANY_1_2;
}

// Returns true if `kind_1` is an ARRAY templated type of `kind_2`
static inline bool TemplatedKindRelatedArrayType(
    const SignatureArgumentKind kind_1, const SignatureArgumentKind kind_2) {
  return (kind_1 == ARG_ARRAY_TYPE_ANY_1 && IsRelatedToAny1(kind_2)) ||
         (kind_1 == ARG_ARRAY_TYPE_ANY_2 && IsRelatedToAny2(kind_2)) ||
         (kind_1 == ARG_ARRAY_TYPE_ANY_3 && kind_2 == ARG_TYPE_ANY_3) ||
         (kind_1 == ARG_ARRAY_TYPE_ANY_4 && kind_2 == ARG_TYPE_ANY_4) ||
         (kind_1 == ARG_ARRAY_TYPE_ANY_5 && kind_2 == ARG_TYPE_ANY_5);
}

// Returns true if `kind_1` is a PROTO_MAP templated type of `kind_2`
static inline bool TemplatedKindRelatedProtoMapType(
    const SignatureArgumentKind kind_1, const SignatureArgumentKind kind_2) {
  return (kind_1 == ARG_PROTO_MAP_ANY && kind_2 == ARG_PROTO_MAP_KEY_ANY) ||
         (kind_1 == ARG_PROTO_MAP_ANY && kind_2 == ARG_PROTO_MAP_VALUE_ANY);
}

// Returns true if `kind_1` is a RANGE templated type of `kind_2`
static inline bool TemplatedKindRelatedRangeType(
    const SignatureArgumentKind kind_1, const SignatureArgumentKind kind_2) {
  return (kind_1 == ARG_RANGE_TYPE_ANY_1 && IsRelatedToAny1(kind_2));
}

// Returns true if `kind_1` is a MAP templated type of `kind_2`
static inline bool TemplatedKindRelatedMapType(
    const SignatureArgumentKind kind_1, const SignatureArgumentKind kind_2) {
  return (kind_1 == ARG_MAP_TYPE_ANY_1_2 && IsRelatedToAny1(kind_2)) ||
         (kind_1 == ARG_MAP_TYPE_ANY_1_2 && IsRelatedToAny2(kind_2));
}

// Returns true if `kind_1` is a MEASURE templated type of `kind_2`
static inline bool TemplatedKindRelatedMeasureType(
    const SignatureArgumentKind kind_1, const SignatureArgumentKind kind_2) {
  return (kind_1 == ARG_MEASURE_TYPE_ANY_1 && IsRelatedToAny1(kind_2));
}

// Returns true if `kind_1` is a templated type containing `kind_2`
static inline bool TemplatedKindIsRelatedImpl(
    const SignatureArgumentKind kind_1, const SignatureArgumentKind kind_2) {
  return TemplatedKindRelatedArrayType(kind_1, kind_2) ||
         TemplatedKindRelatedProtoMapType(kind_1, kind_2) ||
         TemplatedKindRelatedRangeType(kind_1, kind_2) ||
         TemplatedKindRelatedMapType(kind_1, kind_2) ||
         TemplatedKindRelatedMeasureType(kind_1, kind_2);
}

}  // namespace

bool FunctionArgumentType::TemplatedKindIsRelated(
    SignatureArgumentKind kind) const {
  if (!IsTemplated()) {
    return false;
  }
  if (kind_ == ARG_TYPE_ARBITRARY || kind == ARG_TYPE_ARBITRARY) {
    return false;
  }
  if (kind_ == kind) {
    return true;
  }

  if (IsLambda()) {
    for (const FunctionArgumentType& arg_type : lambda().argument_types()) {
      if (arg_type.TemplatedKindIsRelated(kind)) {
        return true;
      }
    }
    if (lambda().body_type().TemplatedKindIsRelated(kind)) {
      return true;
    }
    return false;
  }

  return TemplatedKindIsRelatedImpl(kind_, kind) ||
         TemplatedKindIsRelatedImpl(kind, kind_);
}

absl::Status FunctionSignature::IsValid(ProductMode product_mode) const {
  if (result_type_.repeated() || result_type_.optional()) {
    return MakeSqlError() << "Result type cannot be repeated or optional";
  }

  // The result type can be ARBITRARY for template functions that have not
  // fully resolved the signature yet.
  //
  // For other templated result types (such as ANY_TYPE_1, ANY_PROTO, etc.)
  // the result's templated kind must match a templated kind from an argument
  // since the result type will be determined based on an argument type.
  if (result_type_.IsTemplated() && result_type_.kind() != ARG_TYPE_ARBITRARY &&
      !result_type_.IsRelation()) {
    // Templated map type must match both templated key and value arguments.
    if (result_type_.kind() == ARG_MAP_TYPE_ANY_1_2) {
      const bool has_arg_type_any_1 =
          absl::c_find_if(arguments_, [](auto& arg) {
            return arg.TemplatedKindIsRelated(ARG_TYPE_ANY_1);
          }) != arguments_.end();
      const bool has_arg_type_any_2 =
          absl::c_find_if(arguments_, [](auto& arg) {
            return arg.TemplatedKindIsRelated(ARG_TYPE_ANY_2);
          }) != arguments_.end();
      if (!has_arg_type_any_1 || !has_arg_type_any_2) {
        return MakeSqlError()
               << "Result map type template must match an argument type "
                  "template for both key and value: "
               << DebugString();
      }
    } else {
      bool result_type_matches_an_argument_type = false;
      for (const auto& arg : arguments_) {
        if (arg.TemplatedKindIsRelated(result_type_.kind())) {
          result_type_matches_an_argument_type = true;
          break;
        }
      }
      if (!result_type_matches_an_argument_type) {
        return MakeSqlError()
               << "Result type template must match an argument type template: "
               << DebugString();
      }
    }
  }

  // Optional arguments must be at the end of the argument list, and repeated
  // arguments must be consecutive.  Arguments must themselves be valid.
  bool saw_optional = false;
  bool saw_default_value = false;
  bool after_repeated_block = false;
  bool in_repeated_block = false;
  absl::flat_hash_set<SignatureArgumentKind> templated_kind_used_by_lambda;
  for (int arg_index = 0; arg_index < arguments().size(); arg_index++) {
    const auto& arg = arguments()[arg_index];
    GOOGLESQL_RETURN_IF_ERROR(arg.IsValid(product_mode));
    if (arg.IsVoid()) {
      return MakeSqlError()
             << "Arguments cannot have type VOID: " << DebugString();
    }
    if (arg.optional()) {
      saw_optional = true;
      if (arg.HasDefault()) {
        saw_default_value = true;
      } else if (saw_default_value) {
        return MakeSqlError() << "Optional arguments with default values must "
                                 "be at the end of the argument list: "
                              << DebugString();
      }
    } else if (saw_optional) {
      return MakeSqlError()
             << "Optional arguments must be at the end of the argument list: "
             << DebugString();
    }
    if (arg.repeated()) {
      if (after_repeated_block) {
        return MakeSqlError()
               << "Repeated arguments must be consecutive: " << DebugString();
      }
      in_repeated_block = true;
    } else if (in_repeated_block) {
      after_repeated_block = true;
      in_repeated_block = false;
    }

    if (arg.IsLambda()) {
      // We require each argument of function-type argument is related to a
      // previous argument. For example, the following function signature is
      // not allowed:
      //   Func(FUNCTION<T1->BOOL>, ARRAY(T1))
      // The concern is the above function requires two pass for readers and the
      // resolver of a function call to understand the call. All of the known
      // functions meets this requirement. Could be relaxed if the need arises.
      for (const auto& lambda_arg_type : arg.lambda().argument_types()) {
        bool has_templated_args = false;
        bool is_related_to_previous_function_arg = false;
        for (int j = 0; j < arg_index; j++) {
          if (!lambda_arg_type.IsTemplated()) {
            continue;
          }
          templated_kind_used_by_lambda.insert(lambda_arg_type.kind());
          has_templated_args = true;
          if (lambda_arg_type.TemplatedKindIsRelated(arguments()[j].kind())) {
            is_related_to_previous_function_arg = true;
          }
        }
        if (has_templated_args && !is_related_to_previous_function_arg) {
          return MakeSqlError()
                 << "Templated argument of function-type argument type must "
                    "match an argument type before the function-type argument. "
                    "Function signature: "
                 << DebugString();
        }
      }
    } else {
      if (templated_kind_used_by_lambda.contains(arg.kind())) {
        return MakeSqlError()
               << "Templated argument kind used by function-type argument "
                  "cannot be used by arguments to the right of the "
                  "function-type using it. Kind: "
               << FunctionArgumentType::SignatureArgumentKindToString(
                      arg.kind())
               << " at index: " << arg_index;
      }
    }
  }
  const int first_repeated = FirstRepeatedArgumentIndex();
  if (first_repeated >= 0) {
    const int last_repeated = LastRepeatedArgumentIndex();
    const int repeated_occurrences =
        arguments_[first_repeated].num_occurrences();
    for (int i = first_repeated + 1; i <= last_repeated; ++i) {
      if (arguments_[i].num_occurrences() != repeated_occurrences) {
        return MakeSqlError()
               << "Repeated arguments must have the same num_occurrences: "
               << DebugString();
      }
    }
    if (NumRepeatedArguments() <= NumOptionalArguments()) {
      return MakeSqlError()
             << "The number of repeated arguments (" << NumRepeatedArguments()
             << ") must be greater than the number of optional arguments ("
             << NumOptionalArguments() << ") for signature: " << DebugString();
    }
  }

  // Check if descriptor's table offset arguments point to valid table
  // arguments in the same TVF call.
  for (int i = 0; i < arguments_.size(); i++) {
    const FunctionArgumentType& argument_type = arguments_[i];
    if (argument_type.IsDescriptor() &&
        argument_type.options()
            .get_resolve_descriptor_names_table_offset()
            .has_value()) {
      int table_offset = argument_type.options()
                             .get_resolve_descriptor_names_table_offset()
                             .value();
      if (table_offset < 0 || table_offset >= arguments_.size() ||
          !arguments_[table_offset].IsRelation()) {
        return MakeSqlError()
               << "The table offset argument (" << table_offset
               << ") of descriptor at argument (" << i
               << ") should point to a valid table argument for signature: "
               << DebugString();
      }
    }
  }

  return absl::OkStatus();
}

absl::Status FunctionSignature::IsValidForFunction() const {
  // Arguments and result values may not have relation types. These are special
  // types reserved only for table-valued functions.
  // TODO: Add all other constraints required to make a signature
  // valid.
  for (const FunctionArgumentType& argument : arguments()) {
    GOOGLESQL_RET_CHECK(!argument.IsRelation())
        << "Relation arguments are only allowed in table-valued functions: "
        << DebugString();
  }
  GOOGLESQL_RET_CHECK(!result_type().IsRelation())
      << "Relation return types are only allowed in table-valued functions: "
      << DebugString();
  GOOGLESQL_RET_CHECK(!result_type().IsVoid())
      << "Function must have a return type: " << DebugString();
  return absl::OkStatus();
}

absl::Status FunctionSignature::IsValidForTableValuedFunction() const {
  // Repeated arguments before relation arguments are not
  // supported yet since ResolveTVF() currently requires that relation
  // arguments in the signature map positionally to the function call's
  // arguments.
  // TODO: Support repeated relation arguments at the end of the
  // function signature only, then update the GOOGLESQL_RET_CHECK below.
  bool seen_repeated_args = false;
  for (const FunctionArgumentType& argument : arguments()) {
    if (argument.IsRelation()) {
      GOOGLESQL_RET_CHECK(!argument.repeated())
          << "Repeated relation argument is not supported: " << DebugString();
      GOOGLESQL_RET_CHECK(!seen_repeated_args)
          << "Relation arguments cannot follow repeated arguments: "
          << DebugString();
      // If the relation argument has a required schema, make sure that the
      // column names are unique.
      if (argument.options().has_relation_input_schema()) {
        absl::btree_set<std::string, googlesql_base::CaseLess>
            column_names;
        for (const TVFRelation::Column& column :
             argument.options().relation_input_schema().columns()) {
          GOOGLESQL_RET_CHECK(googlesql_base::InsertIfNotPresent(&column_names, column.name))
              << DebugString();
        }
      }
    }
    if (argument.options().has_relation_input_schema()) {
      GOOGLESQL_RET_CHECK(argument.IsRelation()) << DebugString();
    }
    if (argument.repeated()) {
      seen_repeated_args = true;
    }
  }
  // The result type must be a relation type, since the table-valued function
  // returns a relation.
  GOOGLESQL_RET_CHECK(result_type().IsRelation())
      << "Table-valued functions must have relation return type: "
      << DebugString();
  return absl::OkStatus();
}

absl::Status FunctionSignature::IsValidForProcedure() const {
  for (const FunctionArgumentType& argument : arguments()) {
    GOOGLESQL_RET_CHECK(!argument.IsRelation())
        << "Relation arguments are only allowed in table-valued functions: "
        << DebugString();
  }
  GOOGLESQL_RET_CHECK(!result_type().IsRelation())
      << "Relation return types are only allowed in table-valued functions: "
      << DebugString();
  return absl::OkStatus();
}

int FunctionSignature::FirstRepeatedArgumentIndex() const {
  for (int idx = 0; idx < arguments_.size(); ++idx) {
    if (arguments_[idx].repeated()) {
      return idx;
    }
  }
  return -1;
}

int FunctionSignature::LastRepeatedArgumentIndex() const {
  for (int idx = arguments_.size() - 1; idx >= 0; --idx) {
    if (arguments_[idx].repeated()) {
      return idx;
    }
  }
  return -1;
}

int FunctionSignature::NumRequiredArguments() const {
  return arguments_.size() - NumRepeatedArguments() - NumOptionalArguments();
}

int FunctionSignature::ComputeNumRepeatedArguments() const {
  if (FirstRepeatedArgumentIndex() == -1) {
    return 0;
  }
  return LastRepeatedArgumentIndex() - FirstRepeatedArgumentIndex() + 1;
}

int FunctionSignature::ComputeNumOptionalArguments() const {
  int idx = arguments_.size();
  while (idx - 1 >= 0 && arguments_[idx - 1].optional()) {
    --idx;
  }
  return arguments_.size() - idx;
}

void FunctionSignature::SetConcreteResultType(const Type* type) {
  // The current signature result argument kind becomes the original kind.
  SetConcreteResultType(type, result_type_.kind());
}

void FunctionSignature::SetConcreteResultType(
    const Type* type, SignatureArgumentKind original_kind) {
  result_type_ =
      FunctionArgumentType(type, result_type_.options(), /*num_occurrences=*/1);
  result_type_.set_original_kind(original_kind);
  // Recompute <is_concrete_> since it now may have changed by setting a
  // concrete result type.
  is_concrete_ = ComputeIsConcrete();
}

bool FunctionSignature::HasEnabledRewriteImplementation() const {
  const std::optional<FunctionSignatureRewriteOptions>& rewrite_opts =
      options().rewrite_options();
  return rewrite_opts.has_value() && rewrite_opts->enabled();
}

bool FunctionSignature::HideInSupportedSignatureList(
    const LanguageOptions& language_options) const {
  return IsDeprecated() || IsInternal() || IsHidden() ||
         HasUnsupportedType(language_options) ||
         !options().CheckAllRequiredFeaturesAreEnabled(
             language_options.GetEnabledLanguageFeatures());
}

std::vector<std::string>
FunctionSignature::GetArgumentsUserFacingTextWithCardinality(
    const LanguageOptions& language_options,
    FunctionArgumentType::NamePrintingStyle print_style,
    bool print_template_details) const {
  std::vector<std::string> argument_texts;

  const int first_repeated = FirstRepeatedArgumentIndex();
  const int last_repeated = LastRepeatedArgumentIndex();
  std::string repeated_arg_text;

  for (int i = 0; i < arguments().size(); ++i) {
    const FunctionArgumentType& argument = arguments()[i];

    // If there are multiple repeated arguments, they are interpreted as a
    // repeated tuple in the matcher, so they should be grouped together in the
    // output. For example: [[T1, T2, T3], ...]
    if (i >= first_repeated && i <= last_repeated) {
      ABSL_DCHECK(argument.repeated());
      if (i != first_repeated) {
        absl::StrAppend(&repeated_arg_text, ", ");
      }
      absl::StrAppend(&repeated_arg_text,
                      argument.UserFacingName(language_options.product_mode(),
                                              print_template_details));
      if (i == last_repeated) {
        if (first_repeated != last_repeated) {
          repeated_arg_text = absl::StrCat("[", repeated_arg_text, "]");
        }
        argument_texts.push_back(
            absl::StrCat("[", repeated_arg_text, ", ...]"));
      }
    } else {
      argument_texts.push_back(argument.UserFacingNameWithCardinality(
          language_options.product_mode(), print_style,
          print_template_details));
    }
  }
  return argument_texts;
}

bool SignatureSupportsArgumentAliases(const FunctionSignature& signature) {
  for (const FunctionArgumentType& argument : signature.arguments()) {
    if (argument.options().argument_alias_kind() ==
        FunctionEnums::ARGUMENT_ALIASED) {
      return true;
    }
  }
  return false;
}

}  // namespace googlesql
