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

#include "googlesql/public/types/annotation.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/annotation.pb.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/simple_value.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "absl/container/flat_hash_map.h"
#include "googlesql/base/check.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

std::string GetAnnotationKindName(AnnotationKind kind) {
  switch (kind) {
    case AnnotationKind::kCollation:
      return "Collation";
    case AnnotationKind::kTimestampPrecision:
      return "TimestampPrecision";
    case AnnotationKind::kSampleAnnotation:
      return "SampleAnnotation";
    case AnnotationKind::kMaxBuiltinAnnotationKind:
      return "MaxBuiltinAnnotationKind";
  }
}

absl::Status AnnotationMap::Serialize(AnnotationMapProto* proto) const {
  for (const auto& annotation_pair : annotations_) {
    AnnotationProto* annotation_proto = proto->add_annotations();
    annotation_proto->set_id(annotation_pair.first);
    GOOGLESQL_RETURN_IF_ERROR(
        annotation_pair.second.Serialize(annotation_proto->mutable_value()));
  }
  return absl::OkStatus();
}

void AnnotationMap::UnsetAnnotationRecursively(int id) {
  UnsetAnnotation(id);
  if (IsStructMap()) {
    for (int i = 0; i < AsStructMap()->num_fields(); i++) {
      if (AsStructMap()->field(i) != nullptr) {
        AsStructMap()->mutable_field(i)->UnsetAnnotationRecursively(id);
      }
    }
  }
}

// static
absl::StatusOr<std::unique_ptr<AnnotationMap>> AnnotationMap::Deserialize(
    const AnnotationMapProto& proto) {
  GOOGLESQL_RET_CHECK(!proto.is_null())
      << "is_null could only be true for struct field or array element";
  std::unique_ptr<AnnotationMap> annotation_map;
  // Recursively handle struct fields and array element.
  if (proto.struct_fields_size() > 0) {
    annotation_map = absl::WrapUnique(new StructAnnotationMap());
    for (int i = 0; i < proto.struct_fields_size(); i++) {
      std::unique_ptr<AnnotationMap> struct_field;
      if (!proto.struct_fields(i).is_null()) {
        GOOGLESQL_ASSIGN_OR_RETURN(struct_field, Deserialize(proto.struct_fields(i)));
      }
      annotation_map->AsStructMap()->fields_.push_back(std::move(struct_field));
    }
  } else if (proto.has_array_element()) {
    // Deserialize the deprecated ArrayAnnotationMap as a StructAnnotationMap.
    annotation_map = absl::WrapUnique(new StructAnnotationMap());
    if (!proto.array_element().is_null()) {
      GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<AnnotationMap> element,
                       Deserialize(proto.array_element()));
      annotation_map->AsStructMap()->fields_.push_back(std::move(element));
    }
  } else {
    annotation_map = absl::WrapUnique(new AnnotationMap());
  }
  // Deserialize annotation map.
  for (const auto& annotation_proto : proto.annotations()) {
    GOOGLESQL_ASSIGN_OR_RETURN(SimpleValue value,
                     SimpleValue::Deserialize(annotation_proto.value()));
    annotation_map->SetAnnotation(static_cast<int>(annotation_proto.id()),
                                  value);
  }
  return annotation_map;
}

// static
bool AnnotationMap::HasCompatibleStructure(const AnnotationMap* lhs,
                                           const AnnotationMap* rhs) {
  if (lhs == nullptr || rhs == nullptr) {
    return true;
  }
  if (lhs->IsStructMap()) {
    if (!rhs->IsStructMap() ||
        lhs->AsStructMap()->num_fields() != rhs->AsStructMap()->num_fields()) {
      return false;
    }
    for (int i = 0; i < lhs->AsStructMap()->num_fields(); i++) {
      if (!HasCompatibleStructure(lhs->AsStructMap()->field(i),
                                  rhs->AsStructMap()->field(i))) {
        return false;
      }
    }
    return true;
  }
  return !rhs->IsStructMap();
}

std::unique_ptr<AnnotationMap> AnnotationMap::Clone() const {
  std::unique_ptr<AnnotationMap> target;
  if (IsStructMap()) {
    target.reset(new StructAnnotationMap());
    target->AsStructMap()->fields_.resize(AsStructMap()->num_fields());
    for (int i = 0; i < AsStructMap()->num_fields(); i++) {
      if (AsStructMap()->field(i) != nullptr) {
        target->AsStructMap()->fields_[i] = AsStructMap()->field(i)->Clone();
      }
    }
  } else {
    target.reset(new AnnotationMap());
  }
  target->annotations_ = annotations_;
  return target;
}

bool AnnotationMap::HasCompatibleStructure(const Type* type) const {
  std::vector<const Type*> component_types = type->ComponentTypes();
  if (component_types.empty()) {
    return !IsStructMap();
  }

  if (!IsStructMap() || component_types.size() != AsStructMap()->num_fields()) {
    return false;
  }

  for (int i = 0; i < component_types.size(); i++) {
    if (AsStructMap()->field(i) != nullptr &&
        !AsStructMap()->field(i)->HasCompatibleStructure(component_types[i])) {
      return false;
    }
  }
  return true;
}

bool AnnotationMap::NormalizeInternal() {
  bool empty = annotations_.empty();
  if (IsStructMap()) {
    for (int i = 0; i < AsStructMap()->num_fields(); i++) {
      std::unique_ptr<AnnotationMap>& field_ptr = AsStructMap()->fields_[i];
      if (field_ptr != nullptr) {
        if (field_ptr->NormalizeInternal()) {
          // Set field pointer to nullptr if the AnnotationMap is empty.
          field_ptr.reset(nullptr);
        } else {
          empty = false;
        }
      }
    }
  }
  return empty;
}

bool AnnotationMap::IsNormalized() const {
  return IsNormalizedAndNonEmpty(/*check_non_empty=*/false);
}

bool AnnotationMap::IsNormalizedAndNonEmpty(bool check_non_empty) const {
  bool children_non_empty = false;
  if (IsStructMap()) {
    for (int i = 0; i < AsStructMap()->num_fields(); i++) {
      const AnnotationMap* ptr = AsStructMap()->field(i);
      // The normalized form is that a struct field is either null or non-empty.
      if (ptr != nullptr &&
          !ptr->IsNormalizedAndNonEmpty(/*check_non_empty=*/true)) {
        return false;
      }
      children_non_empty = children_non_empty || ptr != nullptr;
    }
  }
  if (!check_non_empty) {
    return true;
  }
  return children_non_empty || !annotations_.empty();
}

int64_t AnnotationMap::GetEstimatedOwnedMemoryBytesSize() const {
  int64_t total_size = 0;
  for (const auto& annotation : annotations_) {
    total_size += sizeof(annotation.first) +
                  annotation.second.GetEstimatedOwnedMemoryBytesSize();
  }
  if (IsStructMap()) {
    for (int i = 0; i < AsStructMap()->num_fields(); i++) {
      total_size +=
          sizeof(std::unique_ptr<AnnotationMap>) +
          (AsStructMap()->field(i) == nullptr
               ? 0
               : AsStructMap()->field(i)->GetEstimatedOwnedMemoryBytesSize());
    }
  }
  return total_size;
}

// static
bool AnnotationMap::SimpleValueEqualsHelper(const SimpleValue* lhs,
                                            const SimpleValue* rhs) {
  return (lhs == nullptr && rhs == nullptr) ||
         (lhs != nullptr && rhs != nullptr && lhs->Equals(*rhs));
}

// static
bool AnnotationMap::EqualsInternal(const AnnotationMap* lhs,
                                   const AnnotationMap* rhs,
                                   std::optional<int> annotation_spec_id) {
  if (lhs == nullptr) {
    return rhs == nullptr || rhs->EmptyInternal(annotation_spec_id);
  }
  if (rhs == nullptr) {
    return lhs->EmptyInternal(annotation_spec_id);
  }
  // lhs and rhs have been guaranteed to be non-null.
  if (annotation_spec_id.has_value()) {
    // If <annotation_spec_id> has value, only compares annotation value for the
    // given AnnotationSpec id.
    if (!SimpleValueEqualsHelper(
            lhs->GetAnnotation(annotation_spec_id.value()),
            rhs->GetAnnotation(annotation_spec_id.value()))) {
      return false;
    }
  } else if (lhs->annotations_ != rhs->annotations_) {
    return false;
  }
  if (lhs->IsStructMap()) {
    if (!rhs->IsStructMap() ||
        lhs->AsStructMap()->num_fields() != rhs->AsStructMap()->num_fields()) {
      return false;
    }
    for (int i = 0; i < lhs->AsStructMap()->num_fields(); i++) {
      if (!EqualsInternal(lhs->AsStructMap()->field(i),
                          rhs->AsStructMap()->field(i), annotation_spec_id)) {
        return false;
      }
    }
    return true;
  }
  // lhs is neither a struct nor an array.
  return !rhs->IsStructMap();
}

bool AnnotationMap::EmptyInternal(std::optional<int> annotation_spec_id) const {
  if (annotation_spec_id.has_value()) {
    if (GetAnnotation(annotation_spec_id.value()) != nullptr) {
      return false;
    }
  } else if (!annotations_.empty()) {
    return false;
  }
  if (IsStructMap()) {
    for (int i = 0; i < AsStructMap()->num_fields(); i++) {
      if (AsStructMap()->field(i) != nullptr &&
          !AsStructMap()->field(i)->EmptyInternal(annotation_spec_id)) {
        return false;
      }
    }
  }
  return true;
}

// Static
std::unique_ptr<AnnotationMap> AnnotationMap::Create(const Type* type) {
  // This special case should be removed once callers are migrated to use
  // the component_types() call.
  if (type->IsStruct()) {
    return absl::WrapUnique(new StructAnnotationMap(type->AsStruct()));
  }
  std::vector<const Type*> component_types = type->ComponentTypes();
  return component_types.empty()
             ? absl::WrapUnique(new AnnotationMap())
             : absl::WrapUnique(new StructAnnotationMap(component_types));
}

std::string AnnotationMap::DebugStringInternal(
    std::optional<int> annotation_spec_id) const {
  if (annotations_.empty()) {
    return "";
  }

  std::string out;
  if (annotation_spec_id.has_value()) {
    const SimpleValue* annotation = GetAnnotation(annotation_spec_id.value());
    if (annotation != nullptr) {
      out = annotation->DebugString();
    }
  } else {
    // Sort the annotations keys for determinism, but avoid copying Values.
    std::vector<int> sorted_annotations;
    sorted_annotations.reserve(annotations_.size());
    for (const auto& annotation : annotations_) {
      sorted_annotations.push_back(annotation.first);
    }
    std::sort(sorted_annotations.begin(), sorted_annotations.end());
    out = "{";
    absl::StrAppend(
        &out,
        absl::StrJoin(
            sorted_annotations, ", ", [this](std::string* out, const int key) {
              std::string annotation_id;
              if (key <=
                  static_cast<int>(AnnotationKind::kMaxBuiltinAnnotationKind)) {
                annotation_id =
                    GetAnnotationKindName(static_cast<AnnotationKind>(key));
              } else {
                annotation_id = std::to_string(key);
              }
              absl::StrAppend(out, annotation_id, ":",
                              annotations_.at(key).DebugString());
            }));
    absl::StrAppend(&out, "}");
  }
  return out;
}

// TODO: Leaving this temporarily to ease the migration of callers.
StructAnnotationMap::StructAnnotationMap(const StructType* struct_type) {
  for (const StructField& field : struct_type->fields()) {
    fields_.push_back(AnnotationMap::Create(field.type));
  }
}

StructAnnotationMap::StructAnnotationMap(
    absl::Span<const Type* const> component_types) {
  for (const Type* type : component_types) {
    fields_.push_back(AnnotationMap::Create(type));
  }
}

absl::Status StructAnnotationMap::Serialize(AnnotationMapProto* proto) const {
  // Serialize parent class AnnotationMap first.
  GOOGLESQL_RETURN_IF_ERROR(AnnotationMap::Serialize(proto));

  // Serialize annotation for each field.
  for (const auto& field : fields_) {
    auto* proto_field = proto->add_struct_fields();
    if (field == nullptr) {
      proto_field->set_is_null(true);
    } else {
      GOOGLESQL_RETURN_IF_ERROR(field->Serialize(proto_field));
    }
  }
  return absl::OkStatus();
}

std::string StructAnnotationMap::DebugStringInternal(
    std::optional<int> annotation_spec_id) const {
  std::string out(AnnotationMap::DebugStringInternal(annotation_spec_id));
  absl::StrAppend(&out, "<");
  for (int i = 0; i < num_fields(); i++) {
    std::string field_debug_string =
        field(i) == nullptr ? "_"
                            : field(i)->DebugStringInternal(annotation_spec_id);
    if (field_debug_string.empty() && !annotation_spec_id.has_value()) {
      field_debug_string = "{}";
    }

    absl::StrAppend(&out, field_debug_string);
    if (i != num_fields() - 1) {
      absl::StrAppend(&out, ",");
    }
  }
  absl::StrAppend(&out, ">");
  return out;
}

absl::Status StructAnnotationMap::CloneIntoField(int i,
                                                 const AnnotationMap* from) {
  GOOGLESQL_RET_CHECK_LT(i, num_fields());
  GOOGLESQL_RET_CHECK(HasCompatibleStructure(fields_[i].get(), from));
  if (from == nullptr) {
    fields_[i].reset(nullptr);
  } else {
    fields_[i] = from->Clone();
  }
  return absl::OkStatus();
}

}  // namespace googlesql
