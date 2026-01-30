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

#include "googlesql/common/type_visitors.h"

#include <memory>
#include <utility>
#include <vector>

#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/graph_element_type.h"
#include "googlesql/public/types/type.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

static absl::StatusOr<const StructAnnotationMap*> GetAsStructAnnotationMap(
    const AnnotationMap* annotation_map, int num_components) {
  if (annotation_map == nullptr || num_components == 0) {
    return nullptr;
  }
  GOOGLESQL_RET_CHECK(annotation_map->IsStructMap());
  const StructAnnotationMap* composite_annotation_map =
      annotation_map->AsStructMap();
  GOOGLESQL_RET_CHECK_EQ(composite_annotation_map->num_fields(), num_components);
  return composite_annotation_map;
}

static absl::StatusOr<const Type*> ReconstructFromComponents(
    const Type* type, absl::Span<const AnnotatedType> rewritten_components,
    TypeFactory& type_factory) {
  if (type->IsSimpleType() || type->IsEnum() || type->IsProto()) {
    GOOGLESQL_RET_CHECK(rewritten_components.empty());
    return type;
  }

  switch (type->kind()) {
    case TYPE_ARRAY: {
      const Type* output;
      GOOGLESQL_RETURN_IF_ERROR(
          type_factory.MakeArrayType(rewritten_components[0].type, &output));
      return output;
    }
    case TYPE_STRUCT: {
      std::vector<StructType::StructField> rewritten_fields;
      rewritten_fields.reserve(rewritten_components.size());
      for (int i = 0; i < rewritten_components.size(); ++i) {
        rewritten_fields.emplace_back(type->AsStruct()->field(i).name,
                                      rewritten_components[i].type);
      }
      const Type* output;
      GOOGLESQL_RETURN_IF_ERROR(
          type_factory.MakeStructType(std::move(rewritten_fields), &output));
      return output;
    }
    case TYPE_RANGE: {
      const Type* output;
      GOOGLESQL_RETURN_IF_ERROR(
          type_factory.MakeRangeType(rewritten_components[0].type, &output));
      return output;
    }
    case TYPE_MAP: {
      return type_factory.MakeMapType(rewritten_components[0].type,
                                      rewritten_components[1].type);
    }
    case TYPE_MEASURE: {
      return type_factory.MakeMeasureType(rewritten_components[0].type);
    }
    case TYPE_GRAPH_ELEMENT: {
      const GraphElementType* graph_element_type = type->AsGraphElement();
      std::vector<PropertyType> rewritten_fields;
      rewritten_fields.reserve(rewritten_components.size());
      for (int i = 0; i < rewritten_components.size(); ++i) {
        rewritten_fields.emplace_back(
            graph_element_type->property_types()[i].name,
            rewritten_components[i].type);
      }
      const GraphElementType* output;
      GOOGLESQL_RETURN_IF_ERROR(type_factory.MakeGraphElementType(
          graph_element_type->graph_reference(),
          graph_element_type->element_kind(), rewritten_fields, &output));
      return output;
    }
    case TYPE_GRAPH_PATH: {
      const GraphPathType* output;
      GOOGLESQL_RET_CHECK(rewritten_components[0].type->IsGraphElement());
      GOOGLESQL_RET_CHECK(rewritten_components[1].type->IsGraphElement());
      GOOGLESQL_RETURN_IF_ERROR(type_factory.MakeGraphPathType(
          rewritten_components[0].type->AsGraphElement(),
          rewritten_components[1].type->AsGraphElement(), &output));
      return output;
    }
    default:
      return googlesql_base::UnimplementedErrorBuilder()
             << "The rewriter does not support type kind: " << type->kind();
  }
}

absl::Status TypeVisitor::Visit(AnnotatedType annotated_type) {
  const auto [type, annotation_map] = annotated_type;

  GOOGLESQL_RET_CHECK(type != nullptr);

  std::vector<const Type*> component_types = type->ComponentTypes();

  GOOGLESQL_ASSIGN_OR_RETURN(
      const StructAnnotationMap* composite_annotation_map,
      GetAsStructAnnotationMap(annotation_map,
                               static_cast<int>(component_types.size())));

  for (int i = 0; i < component_types.size(); ++i) {
    const Type* component_type = component_types[i];
    const AnnotationMap* component_annotation_map =
        composite_annotation_map == nullptr
            ? nullptr
            : composite_annotation_map->field(i);
    GOOGLESQL_RETURN_IF_ERROR(
        Visit(AnnotatedType(component_type, component_annotation_map)));
  }

  return PostVisit(annotated_type);
}

absl::StatusOr<AnnotatedType> TypeRewriter::Visit(
    AnnotatedType annotated_type) {
  const auto [type, annotation_map] = annotated_type;

  GOOGLESQL_RET_CHECK(type != nullptr);

  std::vector<const Type*> component_types = type->ComponentTypes();
  std::vector<AnnotatedType> rewritten_components;
  rewritten_components.reserve(component_types.size());

  GOOGLESQL_ASSIGN_OR_RETURN(
      const StructAnnotationMap* composite_annotation_map,
      GetAsStructAnnotationMap(annotation_map,
                               static_cast<int>(component_types.size())));

  for (int i = 0; i < component_types.size(); ++i) {
    const Type* component_type = component_types[i];
    const AnnotationMap* component_annotation_map =
        composite_annotation_map == nullptr
            ? nullptr
            : composite_annotation_map->field(i);

    GOOGLESQL_ASSIGN_OR_RETURN(
        AnnotatedType rewritten_component,
        Visit(AnnotatedType(component_type, component_annotation_map)));
    rewritten_components.push_back(std::move(rewritten_component));
  }

  std::unique_ptr<AnnotationMap> output_annotation_map =
      annotation_map == nullptr ? AnnotationMap::Create(type)
                                : annotation_map->Clone();
  if (output_annotation_map != nullptr && !rewritten_components.empty()) {
    GOOGLESQL_RET_CHECK(output_annotation_map->IsStructMap());
    StructAnnotationMap* composite_map = output_annotation_map->AsStructMap();
    GOOGLESQL_RET_CHECK_EQ(composite_map->num_fields(), rewritten_components.size());
    for (int i = 0; i < rewritten_components.size(); ++i) {
      GOOGLESQL_RETURN_IF_ERROR(composite_map->CloneIntoField(
          i, rewritten_components[i].annotation_map));
    }
  }

  output_annotation_map->Normalize();

  const AnnotationMap* output_annotations = nullptr;
  if (output_annotation_map == nullptr || output_annotation_map->Empty()) {
    output_annotation_map = nullptr;
    output_annotations = nullptr;
  } else {
    GOOGLESQL_ASSIGN_OR_RETURN(output_annotations, type_factory_.TakeOwnership(
                                             std::move(output_annotation_map)));
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      const Type* output_type,
      ReconstructFromComponents(type, rewritten_components, type_factory_));

  return PostVisit(AnnotatedType(output_type, output_annotations));
}

}  // namespace googlesql
