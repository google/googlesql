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

#ifndef GOOGLESQL_SCRIPTING_SERIALIZATION_HELPERS_H_
#define GOOGLESQL_SCRIPTING_SERIALIZATION_HELPERS_H_
#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include "googlesql/public/evaluator.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/scripting/script_executor.h"
#include "googlesql/scripting/script_executor_state.pb.h"
#include "googlesql/scripting/type_aliases.h"
#include "googlesql/scripting/variable.pb.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/repeated_ptr_field.h"
namespace googlesql {

using VariableProto = Variable;
using ParametersProto = ScriptExecutorStateProto::StackFrame::Parameters;
// Serializes <variables> and <type_params> into proto <variables_proto>.
absl::Status SerializeVariableProto(
    const VariableMap& variables, const VariableTypeParametersMap& type_params,
    google::protobuf::RepeatedPtrField<VariableProto>* variables_proto);

// Deserializes from <variables_proto> into <variables> and <type_params>.
absl::Status DeserializeVariableProto(
    const google::protobuf::RepeatedPtrField<VariableProto>& variables_proto,
    VariableMap* variables, VariableTypeParametersMap* variable_type_params,
    google::protobuf::DescriptorPool* descriptor_pool, IdStringPool* id_string_pool,
    TypeFactory* type_factory);

// Deserializes the procedure definition from <proto>.
absl::StatusOr<std::unique_ptr<ProcedureDefinition>>
DeserializeProcedureDefinitionProto(
    const ScriptExecutorStateProto::ProcedureDefinition& proto,
    const std::vector<const google::protobuf::DescriptorPool*>& pools,
    TypeFactory* factory);

// Serializes <procedure_definition> into <proto>.
absl::Status SerializeProcedureDefinitionProto(
    const ProcedureDefinition& procedure_definition,
    ScriptExecutorStateProto::ProcedureDefinition* proto,
    FileDescriptorSetMap* file_descriptor_set_map);

// Serialize <parameters> into <parameters_proto>
absl::Status SerializeParametersProto(
    const std::optional<std::variant<ParameterValueList, ParameterValueMap>>&
        parameters,
    ParametersProto* parameters_proto);

// Deserializes from <parameters_proto> into <parameters>.
absl::Status DeserializeParametersProto(
    const ParametersProto& parameters_proto,
    std::optional<std::variant<ParameterValueList, ParameterValueMap>>*
        parameters,
    google::protobuf::DescriptorPool* descriptor_pool, IdStringPool* id_string_pool,
    TypeFactory* type_factory);

}  // namespace googlesql

#endif  // GOOGLESQL_SCRIPTING_SERIALIZATION_HELPERS_H_
