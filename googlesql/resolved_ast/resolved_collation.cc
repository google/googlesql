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

#include "googlesql/resolved_ast/resolved_collation.h"

#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/array_type.h"
#include "googlesql/public/types/simple_value.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/resolved_ast/serialization.pb.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

// static
ResolvedCollation ResolvedCollation::MakeScalar(
    absl::string_view collation_name) {
  ResolvedCollation resolved_collation;
  resolved_collation.collation_name_ =
      SimpleValue::String(std::string(collation_name));
  return resolved_collation;
}

// static
absl::StatusOr<ResolvedCollation> ResolvedCollation::MakeResolvedCollation(
    const AnnotationMap& annotation_map) {
  ResolvedCollation resolved_collation;
  if (annotation_map.IsStructMap()) {
    bool empty = true;
    for (int i = 0; i < annotation_map.AsStructMap()->num_fields(); i++) {
      const AnnotationMap* field = annotation_map.AsStructMap()->field(i);
      ResolvedCollation child;
      if (field != nullptr) {
        GOOGLESQL_ASSIGN_OR_RETURN(child, MakeResolvedCollation(*field));
        if (!child.Empty()) {
          empty = false;
        }
      }
      resolved_collation.child_list_.push_back(std::move(child));
    }
    // The ResolvedCollation for a struct is set to empty if the struct only has
    // empty children.
    if (empty) {
      resolved_collation.child_list_.resize(0);
    }
  } else if (annotation_map.IsArrayMap()) {
    const AnnotationMap* element = annotation_map.AsStructMap()->field(0);
    if (element != nullptr) {
      GOOGLESQL_ASSIGN_OR_RETURN(ResolvedCollation child,
                       MakeResolvedCollation(*element));
      if (!child.Empty()) {
        resolved_collation.child_list_.push_back(std::move(child));
      }
    }
  } else {
    const SimpleValue* collation_name = annotation_map.GetAnnotation(
        static_cast<int>(AnnotationKind::kCollation));
    if (collation_name != nullptr) {
      GOOGLESQL_RET_CHECK(collation_name->has_string_value());
      if (!collation_name->string_value().empty()) {
        resolved_collation.collation_name_ = *collation_name;
      }
    }
  }
  return resolved_collation;
}

bool ResolvedCollation::Equals(const ResolvedCollation& that) const {
  return collation_name_ == that.collation_name_ &&
         child_list_ == that.child_list_;
}

absl::Status ResolvedCollation::Serialize(ResolvedCollationProto* proto) const {
  if (HasCollation()) {
    proto->set_collation_name(CollationName());
  }
  for (int i = 0; i < child_list_.size(); i++) {
    GOOGLESQL_RETURN_IF_ERROR(child_list_[i].Serialize(proto->add_child_list()));
  }
  return absl::OkStatus();
}

// static
absl::StatusOr<ResolvedCollation> ResolvedCollation::Deserialize(
    const ResolvedCollationProto& proto) {
  ResolvedCollation resolved_collation;
  if (proto.has_collation_name()) {
    resolved_collation.collation_name_ =
        SimpleValue::String(std::string(proto.collation_name()));
  }
  for (int i = 0; i < proto.child_list_size(); i++) {
    GOOGLESQL_ASSIGN_OR_RETURN(ResolvedCollation child, Deserialize(proto.child_list(i)));
    resolved_collation.child_list_.push_back(std::move(child));
  }
  return resolved_collation;
}

std::string ResolvedCollation::DebugString() const {
  if (child_list_.empty()) {
    // Print "_" when collation is empty. "_" is not a valid collation name so
    // it shouldn't cause confusion.
    return CollationName().empty() ? "_" : std::string(CollationName());
  } else {
    return absl::StrCat(
        CollationName(), "[",
        absl::StrJoin(
            child_list_, ",",
            [](std::string* out, const ResolvedCollation& resolved_collation) {
              absl::StrAppend(out, resolved_collation.DebugString());
            }),
        "]");
  }
}

bool ResolvedCollation::HasCompatibleStructure(const Type* type) const {
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

std::string ResolvedCollation::ToString(
    const std::vector<ResolvedCollation>& resolved_collation_list) {
  std::string joined = absl::StrJoin(
      resolved_collation_list, ",",
      [](std::string* out, const ResolvedCollation& resolved_collation) {
        absl::StrAppend(out, resolved_collation.DebugString());
      });
  return absl::StrCat("[", joined, "]");
}
}  // namespace googlesql
