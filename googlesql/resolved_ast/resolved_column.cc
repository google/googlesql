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

#include "googlesql/resolved_ast/resolved_column.h"

#include <string>

#include "googlesql/public/id_string.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/type.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/serialization.pb.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

std::string ResolvedColumn::DebugString() const {
  return absl::StrCat(
      table_name_.ToStringView(), ".", name_.ToStringView(), "#", column_id_,
      type_annotation_map() == nullptr ? ""
                                       : type_annotation_map()->DebugString());
}

std::string ResolvedColumn::ShortDebugString() const {
  return absl::StrCat(name_.ToStringView(), "#", column_id_);
}

absl::Status ResolvedColumn::SaveTo(
    FileDescriptorSetMap* file_descriptor_set_map,
    ResolvedColumnProto* proto) const {
  // Consider serializing ResolvedColumn in a separate table, indexed by
  // column_id_, and then only serialize the column_id_ in the AST.
  proto->set_table_name(table_name_.ToStringView());
  proto->set_name(name_.ToStringView());

  proto->set_column_id(column_id_);
  if (type_annotation_map() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(
        type_annotation_map()->Serialize(proto->mutable_annotation_map()));
  }
  return type()->SerializeToProtoAndDistinctFileDescriptors(
      proto->mutable_type(), file_descriptor_set_map);
}

absl::StatusOr<ResolvedColumn> ResolvedColumn::RestoreFrom(
    const ResolvedColumnProto& proto,
    const ResolvedNode::RestoreParams& params) {
  const IdString table_name = params.string_pool->Make(proto.table_name());
  const IdString column_name = params.string_pool->Make(proto.name());
  const Type* type;
  GOOGLESQL_RETURN_IF_ERROR(params.type_factory->DeserializeFromProtoUsingExistingPools(
      proto.type(), params.pools, &type));
  const AnnotationMap* annotation_map = nullptr;
  if (proto.has_annotation_map()) {
    GOOGLESQL_RETURN_IF_ERROR(params.type_factory->DeserializeAnnotationMap(
        proto.annotation_map(), &annotation_map));
  }
  return ResolvedColumn(static_cast<int>(proto.column_id()), table_name,
                        column_name, AnnotatedType(type, annotation_map));
}

std::string ResolvedColumnListToString(const ResolvedColumnList& columns) {
  if (columns.empty()) return "[]";
  const std::string& common_table_name = columns[0].table_name();
  // Use the regular format if we have only one column.
  bool use_common_table_name = (columns.size() > 1);
  for (int i = 1; i < columns.size(); ++i) {
    if (columns[i].table_name() != common_table_name) {
      use_common_table_name = false;
      break;
    }
  }

  std::string ret;
  if (use_common_table_name) {
    absl::StrAppend(&ret, common_table_name, ".[");
    for (const ResolvedColumn& column : columns) {
      if (&column != &columns[0]) ret += ", ";
      ret += column.ShortDebugString();
    }
  } else {
    ret += "[";
    for (const ResolvedColumn& column : columns) {
      if (&column != &columns[0]) ret += ", ";
      ret += column.DebugString();
    }
  }
  ret += "]";
  return ret;
}

}  // namespace googlesql
