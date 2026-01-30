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

#include "googlesql/public/types/type_deserializer.h"

#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "googlesql/common/errors.h"
#include "googlesql/common/proto_helper.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/enum_type.h"
#include "googlesql/public/types/extended_type.h"
#include "googlesql/public/types/map_type.h"
#include "googlesql/public/types/proto_type.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/text_format.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {

absl::Status ValidateTypeProto(const TypeProto& type_proto) {
  if (!type_proto.has_type_kind() ||
      (type_proto.type_kind() == TYPE_ARRAY) != type_proto.has_array_type() ||
      (type_proto.type_kind() == TYPE_ENUM) != type_proto.has_enum_type() ||
      (type_proto.type_kind() == TYPE_PROTO) != type_proto.has_proto_type() ||
      (type_proto.type_kind() == TYPE_STRUCT) != type_proto.has_struct_type() ||
      (type_proto.type_kind() == TYPE_RANGE) != type_proto.has_range_type() ||
      (type_proto.type_kind() == TYPE_MAP) != type_proto.has_map_type() ||
      (type_proto.type_kind() == TYPE_GRAPH_ELEMENT) !=
          type_proto.has_graph_element_type() ||
      (type_proto.type_kind() == TYPE_GRAPH_PATH) !=
          type_proto.has_graph_path_type() ||
      (type_proto.type_kind() == TYPE_MEASURE) !=
          type_proto.has_measure_type() ||
      type_proto.type_kind() == __TypeKind__switch_must_have_a_default__) {
    if (type_proto.type_kind() != TYPE_GEOGRAPHY) {
      std::string type_proto_debug_str;
      google::protobuf::TextFormat::PrintToString(type_proto, &type_proto_debug_str);
      if (type_proto_debug_str.empty()) {
        type_proto_debug_str = "(empty proto)";
      }
      return MakeSqlError()
             << "Invalid TypeProto provided for deserialization: "
             << type_proto_debug_str;
    }
  }

  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<const GraphElementType*>
TypeDeserializer::DeserializeGraphElementType(
    const GraphElementTypeProto& graph_element_type_proto) const {
  if (graph_element_type_proto.graph_reference_size() == 0) {
    return googlesql_base::InvalidArgumentErrorBuilder()
           << "GraphElementType must have a non-empty graph reference";
  }
  GraphElementType::ElementKind element_kind;
  switch (graph_element_type_proto.kind()) {
    case GraphElementTypeProto::KIND_NODE:
      element_kind = GraphElementType::ElementKind::kNode;
      break;
    case GraphElementTypeProto::KIND_EDGE:
      element_kind = GraphElementType::ElementKind::kEdge;
      break;
    default:
      return googlesql_base::InvalidArgumentErrorBuilder()
             << "Invalid element kind: " << graph_element_type_proto.kind();
  }
  std::vector<GraphElementType::PropertyType> property_types;
  property_types.reserve(graph_element_type_proto.property_type_size());
  for (const auto& property_type_proto :
       graph_element_type_proto.property_type()) {
    GOOGLESQL_ASSIGN_OR_RETURN(const Type* value_type,
                     Deserialize(property_type_proto.value_type()));
    if (value_type->IsGraphElement()) {
      return googlesql_base::InvalidArgumentErrorBuilder()
             << "Property value type cannot be GraphElementType";
    }
    property_types.emplace_back(property_type_proto.name(), value_type);
  }
  const GraphElementType* graph_element_type;
  if (graph_element_type_proto.is_dynamic()) {
    GOOGLESQL_RETURN_IF_ERROR(type_factory_->MakeDynamicGraphElementType(
        std::vector<std::string>{
            graph_element_type_proto.graph_reference().begin(),
            graph_element_type_proto.graph_reference().end()},
        element_kind, std::move(property_types), &graph_element_type));
  } else {
    GOOGLESQL_RETURN_IF_ERROR(type_factory_->MakeGraphElementType(
        std::vector<std::string>{
            graph_element_type_proto.graph_reference().begin(),
            graph_element_type_proto.graph_reference().end()},
        element_kind, std::move(property_types), &graph_element_type));
  }
  return graph_element_type;
}

absl::StatusOr<const Type*> TypeDeserializer::Deserialize(
    const TypeProto& type_proto) const {
  GOOGLESQL_RET_CHECK_NE(type_factory_, nullptr);
  GOOGLESQL_RETURN_IF_ERROR(ValidateTypeProto(type_proto));

  if (Type::IsSimpleType(type_proto.type_kind())) {
    return type_factory_->MakeSimpleType(type_proto.type_kind());
  }

  switch (type_proto.type_kind()) {
    case TYPE_ARRAY: {
      GOOGLESQL_ASSIGN_OR_RETURN(const Type* element_type,
                       Deserialize(type_proto.array_type().element_type()));
      const ArrayType* array_type;
      GOOGLESQL_RETURN_IF_ERROR(type_factory_->MakeArrayType(element_type, &array_type));
      return array_type;
    }

    case TYPE_STRUCT: {
      std::vector<StructType::StructField> fields;
      fields.reserve(type_proto.struct_type().field_size());
      for (const StructFieldProto& field_proto :
           type_proto.struct_type().field()) {
        GOOGLESQL_ASSIGN_OR_RETURN(const Type* field_type,
                         Deserialize(field_proto.field_type()));
        fields.emplace_back(std::string(field_proto.field_name()),
                            field_type);
      }
      const StructType* struct_type;
      GOOGLESQL_RETURN_IF_ERROR(type_factory_->MakeStructTypeFromVector(std::move(fields),
                                                              &struct_type));
      return struct_type;
    }

    case TYPE_ENUM: {
      const EnumType* enum_type;
      const int set_index = type_proto.enum_type().file_descriptor_set_index();
      if (set_index < 0 || set_index >= descriptor_pools().size()) {
        return MakeSqlError()
               << "Descriptor pool index " << set_index
               << " is out of range for the provided pools of size "
               << descriptor_pools().size();
      }
      const google::protobuf::DescriptorPool* pool = descriptor_pools()[set_index];
      const google::protobuf::EnumDescriptor* enum_descr =
          pool->FindEnumTypeByName(type_proto.enum_type().enum_name());
      if (enum_descr == nullptr) {
        return MakeSqlError()
               << "Enum type name not found in the specified DescriptorPool: "
               << type_proto.enum_type().enum_name();
      }
      if (enum_descr->file()->name() !=
          type_proto.enum_type().enum_file_name()) {
        return MakeSqlError()
               << "Enum " << type_proto.enum_type().enum_name() << " found in "
               << enum_descr->file()->name() << ", not "
               << type_proto.enum_type().enum_file_name() << " as specified.";
      }
      const google::protobuf::RepeatedPtrField<std::string>& catalog_name_path =
          type_proto.enum_type().catalog_name_path();
      if (type_proto.enum_type().is_opaque()) {
        absl::string_view opaque_enum_name =
            enum_descr->options()
                .GetExtension(opaque_enum_type_options)
                .sql_opaque_enum_name();
        if (opaque_enum_name.empty()) {
          return MakeSqlError() << "Enum " << type_proto.enum_type().enum_name()
                                << " is marked as an opaque enum but its "
                                   "descriptor does not have a "
                                   "sql_opaque_enum_name option set.";
        }
        GOOGLESQL_ASSIGN_OR_RETURN(const EnumType* opaque_enum_type,
                         types::GetOpaqueEnumTypeFromSqlName(opaque_enum_name));
        GOOGLESQL_RET_CHECK_NE(opaque_enum_type, nullptr)
            << "Did you forget to update GetOpaqueEnumTypeFromSqlName when "
               "adding a new opaque enum type?";
        return types::GetOpaqueEnumTypeFromSqlName(opaque_enum_name);
      } else {
        GOOGLESQL_RETURN_IF_ERROR(type_factory_->MakeEnumType(
            enum_descr, &enum_type,
            std::vector<std::string>{catalog_name_path.begin(),
                                     catalog_name_path.end()}));
      }
      return enum_type;
    }

    case TYPE_PROTO: {
      const ProtoType* proto_type;
      const int set_index = type_proto.proto_type().file_descriptor_set_index();
      if (set_index < 0 || set_index >= descriptor_pools().size()) {
        return MakeSqlError()
               << "Descriptor pool index " << set_index
               << " is out of range for the provided pools of size "
               << descriptor_pools().size();
      }
      const google::protobuf::DescriptorPool* pool = descriptor_pools()[set_index];
      const google::protobuf::Descriptor* proto_descr =
          pool->FindMessageTypeByName(type_proto.proto_type().proto_name());
      if (proto_descr == nullptr) {
        return MakeSqlError()
               << "Proto type name not found in the specified DescriptorPool: "
               << type_proto.proto_type().proto_name();
      }
      if (proto_descr->file()->name() !=
          type_proto.proto_type().proto_file_name()) {
        return MakeSqlError()
               << "Proto " << type_proto.proto_type().proto_name()
               << " found in " << proto_descr->file()->name() << ", not "
               << type_proto.proto_type().proto_file_name() << " as specified.";
      }
      const google::protobuf::RepeatedPtrField<std::string>& catalog_name_path =
          type_proto.proto_type().catalog_name_path();
      GOOGLESQL_RETURN_IF_ERROR(type_factory_->MakeProtoType(
          proto_descr, &proto_type,
          std::vector<std::string>{catalog_name_path.begin(),
                                   catalog_name_path.end()}));
      return proto_type;
    }

    case TYPE_EXTENDED: {
      if (!extended_type_deserializer_) {
        return MakeSqlError()
               << "Extended type found in TypeProto, but extended type "
                  "deserializer is not set of TypeDeserializer";
      }

      return extended_type_deserializer_->Deserialize(type_proto, *this);
    }

    case TYPE_RANGE: {
      GOOGLESQL_ASSIGN_OR_RETURN(const Type* element_type,
                       Deserialize(type_proto.range_type().element_type()));
      const RangeType* range_type;
      GOOGLESQL_RETURN_IF_ERROR(type_factory_->MakeRangeType(element_type, &range_type));
      return range_type;
    }

    case TYPE_GRAPH_ELEMENT: {
      return DeserializeGraphElementType(type_proto.graph_element_type());
    }
    case TYPE_GRAPH_PATH: {
      GOOGLESQL_ASSIGN_OR_RETURN(
          auto node_type,
          DeserializeGraphElementType(type_proto.graph_path_type().node_type()),
          _ << "while deserializing the node type");
      GOOGLESQL_ASSIGN_OR_RETURN(
          auto edge_type,
          DeserializeGraphElementType(type_proto.graph_path_type().edge_type()),
          _ << "while deserializing the edge type");
      const GraphPathType* graph_path_type;
      GOOGLESQL_RETURN_IF_ERROR(type_factory_->MakeGraphPathType(node_type, edge_type,
                                                       &graph_path_type));
      return graph_path_type;
    }
    case TYPE_MAP: {
      GOOGLESQL_ASSIGN_OR_RETURN(const Type* key_type,
                       Deserialize(type_proto.map_type().key_type()));
      GOOGLESQL_ASSIGN_OR_RETURN(const Type* value_type,
                       Deserialize(type_proto.map_type().value_type()));
      return type_factory_->MakeMapType(key_type, value_type);
    }
    case TYPE_MEASURE: {
      GOOGLESQL_ASSIGN_OR_RETURN(const Type* result_type,
                       Deserialize(type_proto.measure_type().result_type()));
      return type_factory_->MakeMeasureType(result_type);
    }
    default:
      return ::googlesql_base::UnimplementedErrorBuilder()
             << "Making Type of kind "
             << Type::TypeKindToString(type_proto.type_kind(), PRODUCT_INTERNAL)
             << " from TypeProto is not implemented.";
  }
}

absl::Status TypeDeserializer::DeserializeDescriptorPoolsFromSelfContainedProto(
    const TypeProto& type_proto,
    absl::Span<google::protobuf::DescriptorPool* const> pools) {
  if (!type_proto.file_descriptor_set().empty() &&
      type_proto.file_descriptor_set_size() != pools.size()) {
    return MakeSqlError()
           << "Expected the number of provided FileDescriptorSets "
              "and DescriptorPools to match. Found "
           << type_proto.file_descriptor_set_size()
           << " FileDescriptorSets and " << pools.size() << " DescriptorPools";
  }
  for (int i = 0; i < type_proto.file_descriptor_set_size(); ++i) {
    GOOGLESQL_RETURN_IF_ERROR(AddFileDescriptorSetToPool(
        &type_proto.file_descriptor_set(i), pools[i]));
  }
  return absl::OkStatus();
}

}  // namespace googlesql
