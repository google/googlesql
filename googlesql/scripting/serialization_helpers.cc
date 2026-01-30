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

#include "googlesql/scripting/serialization_helpers.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/public/evaluator.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_deserializer.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/types/type_parameters.h"
#include "googlesql/public/value.h"
#include "googlesql/scripting/procedure_extension.pb.h"
#include "googlesql/scripting/script_executor.h"
#include "googlesql/scripting/script_executor_state.pb.h"
#include "googlesql/scripting/type_aliases.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

absl::Status SerializeVariableProto(
    const VariableMap& variables, const VariableTypeParametersMap& type_params,
    google::protobuf::RepeatedPtrField<VariableProto>* variables_proto) {
  for (const auto& pair : variables) {
    const IdString name = pair.first;
    const Value& value = pair.second;
    VariableProto* variable_proto = variables_proto->Add();
    variable_proto->set_name(name.ToString());
    GOOGLESQL_RETURN_IF_ERROR(value.Serialize(variable_proto->mutable_value()));
    // TODO: Use SerializeToProtoAndFileDescriptors to serialize a
    // type with a deduplicated collection of FileDescriptorProtos.
    GOOGLESQL_RETURN_IF_ERROR(value.type()->SerializeToSelfContainedProto(
        variable_proto->mutable_type()));

    auto it = type_params.find(name);
    if (it != type_params.end()) {
      GOOGLESQL_RETURN_IF_ERROR(
          it->second.Serialize(variable_proto->mutable_type_params()));
    }
  }
  return absl::OkStatus();
}

absl::Status DeserializeVariableProto(
    const google::protobuf::RepeatedPtrField<VariableProto>& variables_proto,
    VariableMap* variables, VariableTypeParametersMap* variable_type_params,
    google::protobuf::DescriptorPool* descriptor_pool, IdStringPool* id_string_pool,
    TypeFactory* type_factory) {
  for (const VariableProto& variable_proto : variables_proto) {
    IdString var_name = id_string_pool->Make(variable_proto.name());
    GOOGLESQL_RET_CHECK(!variables->contains(var_name))
        << "Duplicate variable " << var_name.ToStringView();
    const Type* type;
    GOOGLESQL_RETURN_IF_ERROR(type_factory->DeserializeFromSelfContainedProto(
        variable_proto.type(), descriptor_pool, &type));
    GOOGLESQL_ASSIGN_OR_RETURN(googlesql::Value value, googlesql::Value::Deserialize(
                                                 variable_proto.value(), type));
    googlesql_base::InsertOrDie(variables, var_name, value);

    if (variable_proto.has_type_params()) {
      GOOGLESQL_ASSIGN_OR_RETURN(
          TypeParameters type_params,
          TypeParameters::Deserialize(variable_proto.type_params()));
      googlesql_base::InsertOrDie(variable_type_params, var_name, type_params);
    }
  }
  return absl::OkStatus();
}

// static
absl::StatusOr<std::unique_ptr<ProcedureDefinition>>
DeserializeProcedureDefinitionProto(
    const ScriptExecutorStateProto::ProcedureDefinition& proto,
    const std::vector<const google::protobuf::DescriptorPool*>& pools,
    TypeFactory* factory) {
  std::unique_ptr<FunctionSignature> function_signature;
  GOOGLESQL_ASSIGN_OR_RETURN(function_signature,
                   FunctionSignature::Deserialize(
                       proto.signature(), TypeDeserializer(factory, pools)));

  if (proto.is_dynamic_sql()) {
    return std::make_unique<ProcedureDefinition>(*function_signature,
                                                 proto.body());
  } else {
    std::vector<std::string> argument_name_list(
        proto.argument_name_list().begin(), proto.argument_name_list().end());
    std::unique_ptr<ProcedureExtension> extension = nullptr;
    if (proto.has_extension()) {
      extension = std::make_unique<ProcedureExtension>(proto.extension());
    }
    return std::make_unique<ProcedureDefinition>(
        proto.name(), *function_signature, std::move(argument_name_list),
        proto.body(), nullptr, std::move(extension));
  }
}

absl::Status SerializeProcedureDefinitionProto(
    const ProcedureDefinition& procedure_definition,
    ScriptExecutorStateProto::ProcedureDefinition* proto,
    FileDescriptorSetMap* file_descriptor_set_map) {
  proto->set_name(procedure_definition.name());
  GOOGLESQL_RETURN_IF_ERROR(procedure_definition.signature().Serialize(
      file_descriptor_set_map, proto->mutable_signature()));
  *proto->mutable_argument_name_list() = {
      procedure_definition.argument_name_list().begin(),
      procedure_definition.argument_name_list().end()};
  proto->set_body(procedure_definition.body());
  proto->set_is_dynamic_sql(procedure_definition.is_dynamic_sql());
  if (procedure_definition.extension() != nullptr) {
    *proto->mutable_extension() = *procedure_definition.extension();
  }
  return absl::OkStatus();
}

absl::Status SerializeParametersProto(
    const std::optional<std::variant<ParameterValueList, ParameterValueMap>>&
        parameters,
    ParametersProto* parameters_proto) {
  if (!parameters) {
    parameters_proto->set_mode(ParametersProto::NONE);
  } else if (std::holds_alternative<ParameterValueMap>(*parameters)) {
    parameters_proto->set_mode(ParametersProto::NAMED);
    for (const auto& [name, value] : std::get<ParameterValueMap>(*parameters)) {
      VariableProto* variable_proto =
          parameters_proto->mutable_variables()->Add();
      variable_proto->set_name(name);
      GOOGLESQL_RETURN_IF_ERROR(value.Serialize(variable_proto->mutable_value()));
      GOOGLESQL_RETURN_IF_ERROR(value.type()->SerializeToSelfContainedProto(
          variable_proto->mutable_type()));
    }
  } else {
    parameters_proto->set_mode(ParametersProto::POSITIONAL);
    for (const Value& value : std::get<ParameterValueList>(*parameters)) {
      VariableProto* variable_proto =
          parameters_proto->mutable_variables()->Add();
      GOOGLESQL_RETURN_IF_ERROR(value.Serialize(variable_proto->mutable_value()));
      GOOGLESQL_RETURN_IF_ERROR(value.type()->SerializeToSelfContainedProto(
          variable_proto->mutable_type()));
    }
  }
  return absl::OkStatus();
}

absl::Status DeserializeParametersProto(
    const ParametersProto& parameters_proto,
    std::optional<std::variant<ParameterValueList, ParameterValueMap>>*
        parameters,
    google::protobuf::DescriptorPool* descriptor_pool, IdStringPool* id_string_pool,
    TypeFactory* type_factory) {
  if (parameters_proto.mode() == ParametersProto::NONE) {
    *parameters = {};
  } else if (parameters_proto.mode() == ParametersProto::NAMED) {
    ParameterValueMap map;
    for (const VariableProto& variable_proto : parameters_proto.variables()) {
      GOOGLESQL_RET_CHECK(!googlesql_base::ContainsKey(map, variable_proto.name()))
          << "Duplicate variable " << variable_proto.name();
      const Type* type;
      GOOGLESQL_RETURN_IF_ERROR(type_factory->DeserializeFromSelfContainedProto(
          variable_proto.type(), descriptor_pool, &type));
      GOOGLESQL_ASSIGN_OR_RETURN(
          googlesql::Value value,
          googlesql::Value::Deserialize(variable_proto.value(), type));
      googlesql_base::InsertOrDie(&map, variable_proto.name(), value);
    }
    *parameters = map;
  } else {
    ParameterValueList list;
    for (const VariableProto& variable_proto : parameters_proto.variables()) {
      const Type* type;
      GOOGLESQL_RETURN_IF_ERROR(type_factory->DeserializeFromSelfContainedProto(
          variable_proto.type(), descriptor_pool, &type));
      GOOGLESQL_ASSIGN_OR_RETURN(
          googlesql::Value value,
          googlesql::Value::Deserialize(variable_proto.value(), type));
      list.push_back(value);
    }
    *parameters = list;
  }
  return absl::OkStatus();
}

}  // namespace googlesql
