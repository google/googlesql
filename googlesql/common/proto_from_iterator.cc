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

#include "googlesql/common/proto_from_iterator.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"
#include "googlesql/public/convert_type_to_proto.h"
#include "googlesql/public/evaluator_table_iterator.h"
#include "googlesql/public/proto_value_conversion.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/value.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

IteratorProtoDescriptorOptions::IteratorProtoDescriptorOptions() {
  // Ensure there are no default values for names
  convert_type_to_proto_options.message_name.clear();
}

absl::StatusOr<IteratorProtoDescriptors> ConvertIteratorToProto(
    const EvaluatorTableIterator& iter,
    const IteratorProtoDescriptorOptions& options,
    google::protobuf::DescriptorPool& pool) {
  // Building the descriptors would fail if these options were missing.
  GOOGLESQL_RET_CHECK(!options.filename.empty());
  GOOGLESQL_RET_CHECK(!options.table_message_name.empty());
  GOOGLESQL_RET_CHECK(!options.table_row_field_name.empty());
  GOOGLESQL_RET_CHECK(!options.convert_type_to_proto_options.message_name.empty());

  std::vector<std::pair<std::string, const Type*>> columns;
  columns.reserve(iter.NumColumns());
  for (int i = 0; i < iter.NumColumns(); ++i) {
    columns.emplace_back(iter.GetColumnName(i), iter.GetColumnType(i));
  }

  google::protobuf::FileDescriptorProto fdp;

  GOOGLESQL_RETURN_IF_ERROR(ConvertTableToProto(columns, false, &fdp,
                                      options.convert_type_to_proto_options));

  {
    google::protobuf::DescriptorProto* msg = fdp.add_message_type();
    msg->set_name(options.table_message_name);

    google::protobuf::FieldDescriptorProto* field = msg->add_field();
    field->set_number(options.table_row_field_number);
    field->set_name(options.table_row_field_name);
    field->set_type(google::protobuf::FieldDescriptorProto::TYPE_MESSAGE);
    field->set_type_name(options.convert_type_to_proto_options.message_name);
    field->set_label(google::protobuf::FieldDescriptorProto::LABEL_REPEATED);
  }

  fdp.set_name(options.filename);
  if (options.package.has_value() && !options.package->empty()) {
    fdp.set_package(*options.package);
  } else {
    fdp.clear_package();
  }

  const google::protobuf::FileDescriptor* fd = pool.BuildFile(fdp);

  if (fd == nullptr) {
    return absl::UnknownError("Building file descriptor failed");
  }

  IteratorProtoDescriptors result;
  result.table = fd->FindMessageTypeByName(options.table_message_name);
  result.row = fd->FindMessageTypeByName(
      options.convert_type_to_proto_options.message_name);

  return result;
}

absl::Status MergeRowToProto(const EvaluatorTableIterator& iter,
                             google::protobuf::MessageFactory& message_factory,
                             google::protobuf::Message& proto_out) {
  GOOGLESQL_RET_CHECK_OK(iter.Status());

  const google::protobuf::Descriptor* row = proto_out.GetDescriptor();

  GOOGLESQL_RET_CHECK_NE(row, nullptr);
  GOOGLESQL_RET_CHECK_GE(row->field_count(), iter.NumColumns());

  for (int i = 0; i < iter.NumColumns(); ++i) {
    const Value& value = iter.GetValue(i);
    const google::protobuf::FieldDescriptor* field_desc = row->field(i);

    GOOGLESQL_RET_CHECK_NE(field_desc, nullptr);

    GOOGLESQL_RETURN_IF_ERROR(MergeValueToProtoField(value, field_desc, true,
                                           &message_factory, &proto_out));
  }

  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<google::protobuf::Message>> ProtoFromIterator(
    EvaluatorTableIterator& iter, const google::protobuf::Descriptor& table,
    google::protobuf::MessageFactory& message_factory) {
  GOOGLESQL_RET_CHECK_GE(table.field_count(), 1) << table.DebugString();

  const google::protobuf::FieldDescriptor* field = table.field(0);

  GOOGLESQL_RET_CHECK(field->is_repeated());
  GOOGLESQL_RET_CHECK_EQ(field->type(), google::protobuf::FieldDescriptor::TYPE_MESSAGE)
      << field->DebugString();

  const google::protobuf::Message* table_prototype = message_factory.GetPrototype(&table);
  const google::protobuf::Reflection* table_reflection = table_prototype->GetReflection();

  auto table_msg = absl::WrapUnique(table_prototype->New());

  while (true) {
    if (!iter.NextRow()) {
      GOOGLESQL_RETURN_IF_ERROR(iter.Status());
      break;
    }

    // Create one row message for each query result row
    google::protobuf::Message* row_msg =
        table_reflection->AddMessage(table_msg.get(), field);
    GOOGLESQL_RETURN_IF_ERROR(MergeRowToProto(iter, message_factory, *row_msg));
  }

  return table_msg;
}

}  // namespace googlesql
