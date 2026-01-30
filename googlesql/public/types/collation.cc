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

#include "googlesql/public/types/collation.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/collation.pb.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/simple_value.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

// static
Collation Collation::MakeScalar(absl::string_view collation_name) {
  Collation collation;
  collation.collation_name_ = SimpleValue::String(std::string(collation_name));
  return collation;
}

// static
absl::StatusOr<Collation> Collation::MakeCollation(
    const AnnotationMap& annotation_map) {
  Collation collation;
  if (annotation_map.IsStructMap()) {
    bool empty = true;
    for (int i = 0; i < annotation_map.AsStructMap()->num_fields(); i++) {
      const AnnotationMap* field = annotation_map.AsStructMap()->field(i);
      Collation child;
      if (field != nullptr) {
        GOOGLESQL_ASSIGN_OR_RETURN(child, MakeCollation(*field));
        if (!child.Empty()) {
          empty = false;
        }
      }
      collation.child_list_.push_back(std::move(child));
    }
    // The Collation for a struct is set to empty if the struct only has
    // empty children.
    if (empty) {
      collation.child_list_.resize(0);
    }
  } else if (annotation_map.IsArrayMap()) {
    const AnnotationMap* element = annotation_map.AsStructMap()->field(0);
    if (element != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(Collation child, MakeCollation(*element));
      if (!child.Empty()) {
        collation.child_list_.push_back(std::move(child));
      }
    }
  } else {
    const SimpleValue* collation_name = annotation_map.GetAnnotation(
        static_cast<int>(AnnotationKind::kCollation));
    if (collation_name != nullptr) {
      GOOGLESQL_RET_CHECK(collation_name->has_string_value());
      if (!collation_name->string_value().empty()) {
        collation.collation_name_ = *collation_name;
      }
    }
  }
  return collation;
}

// Static
Collation Collation::MakeCollationWithChildList(
    std::vector<Collation> child_list) {
  if (child_list.empty()) {
    return Collation();
  }
  bool all_empty = true;
  for (const Collation& collation : child_list) {
    if (!collation.Empty()) {
      all_empty = false;
      break;
    }
  }
  if (all_empty) {
    return Collation();
  }
  return Collation(/*collation_name=*/{}, std::move(child_list));
}

bool Collation::Equals(const Collation& that) const {
  return collation_name_ == that.collation_name_ &&
         child_list_ == that.child_list_;
}

absl::Status Collation::Serialize(CollationProto* proto) const {
  if (HasCollation()) {
    proto->set_collation_name(CollationName());
  }
  for (int i = 0; i < child_list_.size(); i++) {
    GOOGLESQL_RETURN_IF_ERROR(child_list_[i].Serialize(proto->add_child_list()));
  }
  return absl::OkStatus();
}

// static
absl::StatusOr<Collation> Collation::Deserialize(const CollationProto& proto) {
  Collation collation;
  if (proto.has_collation_name()) {
    collation.collation_name_ =
        SimpleValue::String(std::string(proto.collation_name()));
  }
  for (int i = 0; i < proto.child_list_size(); i++) {
    GOOGLESQL_ASSIGN_OR_RETURN(Collation child, Deserialize(proto.child_list(i)));
    collation.child_list_.push_back(std::move(child));
  }
  return collation;
}

std::string Collation::DebugString() const {
  if (child_list_.empty()) {
    // Print "_" when collation is empty. "_" is not a valid collation name so
    // it shouldn't cause confusion.
    return CollationName().empty() ? "_" : std::string(CollationName());
  } else {
    return absl::StrCat(
        CollationName(), "[",
        absl::StrJoin(child_list_, ",",
                      [](std::string* out, const Collation& collation) {
                        absl::StrAppend(out, collation.DebugString());
                      }),
        "]");
  }
}

bool Collation::HasCompatibleStructure(const Type* type) const {
  if (Empty()) {
    return true;
  }
  if (HasCollation()) {
    return type->IsString();
  }
  // At this point, this instance has no collation name and a non-empty child
  // list.
  if (type->IsStruct()) {
    if (type->AsStruct()->num_fields() != num_children()) {
      return false;
    }
    for (int i = 0; i < num_children(); i++) {
      if (!child(i).HasCompatibleStructure(type->AsStruct()->field(i).type)) {
        return false;
      }
    }
    return true;
  } else if (type->IsArray()) {
    return num_children() == 1 &&
           child(0).HasCompatibleStructure(type->AsArray()->element_type());
  }
  return false;
}

absl::StatusOr<bool> Collation::EqualsCollationAnnotation(
    const AnnotationMap* annotation_map) const {
  if (annotation_map == nullptr) {
    return Empty();
  }

  GOOGLESQL_ASSIGN_OR_RETURN(Collation collation_from_annotation_map,
                   Collation::MakeCollation(*annotation_map));
  return Equals(collation_from_annotation_map);
}

// Populates the given <annotation_map> with collation annotations given in
// <collation>.
absl::Status Collation::PopulateAnnotationMap(
    AnnotationMap& annotation_map) const {
  uint64_t child_collation_num = this->num_children();
  if (annotation_map.IsStructMap()) {
    // Annotation Map uses struct map to represent all compound types including
    // structs and arrays.
    GOOGLESQL_RET_CHECK(!this->HasCollation() &&
              annotation_map.GetAnnotation(
                  static_cast<int>(AnnotationKind::kCollation)) == nullptr);
    // If there is no child collation in input <collation>, we do not need to
    // set collation annotations for the field annotation maps.
    if (child_collation_num == 0) {
      return absl::OkStatus();
    }
    StructAnnotationMap* struct_annotation_map = annotation_map.AsStructMap();
    GOOGLESQL_RET_CHECK_EQ(child_collation_num, struct_annotation_map->num_fields());
    for (int i = 0; i < struct_annotation_map->num_fields(); ++i) {
      GOOGLESQL_RETURN_IF_ERROR(this->child(i).PopulateAnnotationMap(
          *(struct_annotation_map->mutable_field(i))));
    }
  } else {
    GOOGLESQL_RET_CHECK_EQ(child_collation_num, 0);
    if (this->HasCollation()) {
      annotation_map.SetAnnotation(
          static_cast<int>(AnnotationKind::kCollation),
          SimpleValue::String(std::string(this->CollationName())));
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<AnnotationMap>> Collation::ToAnnotationMap(
    const Type* type) const {
  std::unique_ptr<AnnotationMap> annotation_map = AnnotationMap::Create(type);
  GOOGLESQL_RETURN_IF_ERROR(PopulateAnnotationMap(*annotation_map));
  annotation_map->Normalize();
  return std::move(annotation_map);
}

}  // namespace googlesql
