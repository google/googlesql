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

#include "googlesql/testdata/sample_annotation.h"

#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/simple_value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "absl/status/status.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

// TODO: Extracts helper functions and interfaces from this class.
namespace googlesql {

namespace {

// Recursively copies the annotation with given id, from the source
// AnnotationMap to the destination AnnotationMap.
//
// Returns error status if the type of the source AnnotationMap doesn't match
// with that of destination AnnotationMap.
absl::Status CopyAnnotationRecursively(int id,
                                       const AnnotationMap* from_annotated_map,
                                       AnnotationMap* to_annotated_map) {
  if (from_annotated_map == nullptr) {
    return absl::OkStatus();
  }
  const SimpleValue* from_value = from_annotated_map->GetAnnotation(id);
  if (from_value != nullptr) {
    to_annotated_map->SetAnnotation(id, *from_value);
  }
  if (from_annotated_map->IsArrayMap()) {
    GOOGLESQL_RET_CHECK(to_annotated_map->IsArrayMap());
    GOOGLESQL_RETURN_IF_ERROR(CopyAnnotationRecursively(
        id, from_annotated_map->AsStructMap()->field(0),
        to_annotated_map->AsStructMap()->mutable_field(0)));
  } else if (from_annotated_map->IsStructMap()) {
    GOOGLESQL_RET_CHECK(to_annotated_map->IsStructMap());
    GOOGLESQL_RET_CHECK_EQ(from_annotated_map->AsStructMap()->num_fields(),
                 to_annotated_map->AsStructMap()->num_fields());
    for (int i = 0; i < from_annotated_map->AsStructMap()->num_fields(); i++) {
      GOOGLESQL_RETURN_IF_ERROR(CopyAnnotationRecursively(
          id, from_annotated_map->AsStructMap()->field(i),
          to_annotated_map->AsStructMap()->mutable_field(i)));
    }
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status SampleAnnotation::CheckAndPropagateForColumnRef(
    const ResolvedColumnRef& column_ref, AnnotationMap* result_annotation_map) {
  if (column_ref.column().type_annotation_map() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(CopyAnnotationRecursively(
        GetId(), column_ref.column().type_annotation_map(),
        result_annotation_map));
  }
  return absl::OkStatus();
}

absl::Status SampleAnnotation::CheckAndPropagateForGetStructField(
    const ResolvedGetStructField& get_struct_field,
    AnnotationMap* result_annotation_map) {
  const AnnotationMap* struct_annotation_map =
      get_struct_field.expr()->type_annotation_map();
  if (struct_annotation_map != nullptr) {
    GOOGLESQL_RET_CHECK(struct_annotation_map->IsStructMap());
    int field_idx = get_struct_field.field_idx();
    GOOGLESQL_RET_CHECK_LT(field_idx, struct_annotation_map->AsStructMap()->num_fields());
    GOOGLESQL_RETURN_IF_ERROR(CopyAnnotationRecursively(
        GetId(), struct_annotation_map->AsStructMap()->field(field_idx),
        result_annotation_map));
  }
  return absl::OkStatus();
}

}  // namespace googlesql
