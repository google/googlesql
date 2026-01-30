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

#include "googlesql/public/table_from_proto.h"

#include <string>

#include "google/protobuf/descriptor.pb.h"
#include "googlesql/public/proto/wire_format_annotation.pb.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/types/struct_type.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

TableFromProto::TableFromProto(absl::string_view name) : SimpleTable(name) {}

TableFromProto::~TableFromProto() = default;

absl::Status TableFromProto::Init(const google::protobuf::Descriptor* descriptor,
                                  TypeFactory* type_factory,
                                  const TableFromProtoOptions& options) {
  GOOGLESQL_RET_CHECK_EQ(0, NumColumns()) << "TableFromProto::Init called twice";

  const TableType table_type =
      descriptor->options().GetExtension(googlesql::table_type);

  const Type* row_type;
  if (table_type == DEFAULT_TABLE_TYPE) {
    // This table is not a googlesql table.  Just treat it as a proto value
    // table.
    GOOGLESQL_RETURN_IF_ERROR(type_factory->MakeProtoType(descriptor, &row_type));
    GOOGLESQL_RETURN_IF_ERROR(AddColumn(new SimpleColumn(FullName(), "value", row_type),
                              true /* is_owned */));
    set_is_value_table(true);
    return absl::OkStatus();
  }

  // This table is a googlesql table.  Convert the proto to a googlesql Type.
  GOOGLESQL_RETURN_IF_ERROR(
      type_factory->MakeUnwrappedTypeFromProto(descriptor, &row_type));

  if (table_type == VALUE_TABLE) {
    // TODO If it's a proto value table, we are supposed to strip off
    // the googlesql.table_type annotation.
    GOOGLESQL_RETURN_IF_ERROR(AddColumn(new SimpleColumn(FullName(), "value", row_type),
                              true /* is_owned */));
    set_is_value_table(true);
    return absl::OkStatus();
  }

  GOOGLESQL_RET_CHECK_EQ(table_type, SQL_TABLE);
  if (!row_type->IsStruct()) {
    return ::googlesql_base::UnknownErrorBuilder()
           << "Proto " << descriptor->full_name()
           << " decodes to non-struct type " << row_type->DebugString()
           << " so is not valid to use as a non-value table type";
  }
  if (descriptor->field_count() != row_type->AsStruct()->num_fields()) {
    return ::googlesql_base::UnknownErrorBuilder()
           << "Proto " << descriptor->full_name()
           << " decodes to a struct type with a different number of fields. "
              "Proto #fields: "
           << descriptor->field_count()
           << " struct #fields: " << row_type->AsStruct()->num_fields();
  }
  for (int i = 0; i < row_type->AsStruct()->num_fields(); ++i) {
    const StructType::StructField& field = row_type->AsStruct()->field(i);
    if (!AllowAnonymousColumnName() && field.name.empty()) {
      return ::googlesql_base::UnknownErrorBuilder()
             << "Proto " << descriptor->full_name()
             << " decodes to struct type " << row_type->DebugString()
             << " which has anonymous fields, so cannot be used as "
                "a SQL table";
    }
    const google::protobuf::FieldDescriptor* proto_field = descriptor->field(i);
    bool is_pseudo_column =
        proto_field->options().GetExtension(googlesql::is_hidden_column);
    GOOGLESQL_RETURN_IF_ERROR(
        AddColumn(new SimpleColumn(FullName(), field.name, field.type,
                                   {.is_pseudo_column = is_pseudo_column}),
                  /*is_owned=*/true));
  }

  return absl::OkStatus();
}

}  // namespace googlesql
