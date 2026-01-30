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

#include "googlesql/tools/execute_query/execute_query_proto_writer.h"

#include <functional>
#include <memory>
#include <ostream>
#include <string>

#include "googlesql/base/logging.h"
#include "googlesql/common/proto_from_iterator.h"
#include "googlesql/public/convert_type_to_proto.h"
#include "googlesql/public/evaluator_table_iterator.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/dynamic_message.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/message.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/util/json_util.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

ExecuteQueryStreamProtobufWriter::ExecuteQueryStreamProtobufWriter(
    const google::protobuf::DescriptorPool* parent_descriptor_pool,
    std::function<absl::Status(const google::protobuf::Message& msg)> proto_writer_func)
    : parent_descriptor_pool_{parent_descriptor_pool},
      proto_writer_func_{proto_writer_func} {
  if (!parent_descriptor_pool_) {
    ABSL_LOG(WARNING) << "Parent descriptor pool is missing; encoding of "
                    "non-primitive types may be incomplete";
  }
}

absl::Status ExecuteQueryStreamProtobufWriter::log(absl::string_view message) {
  // TODO: Handle logs for real. This is a no-op for now only to
  // support some output for output_mode=textproto or json
  return absl::OkStatus();
}

void ExecuteQueryStreamProtobufWriter::FlushStatement(bool at_end,
                                                      std::string error_msg) {
  // TODO: Handle error messages for real. This is a no-op for now
  // only to support some output for output_mode=textproto or json
}

absl::Status ExecuteQueryStreamProtobufWriter::executed(
    const ResolvedNode& ast, std::unique_ptr<EvaluatorTableIterator> iter) {
  // Use a new pool every time to not retain the generated proto descriptors
  google::protobuf::DescriptorPool pool{parent_descriptor_pool_};
  pool.AllowUnknownDependencies();

  IteratorProtoDescriptorOptions options;

  options.filename = "<generated>";
  options.table_message_name = "Table";
  options.table_row_field_name = "row";
  options.convert_type_to_proto_options.message_name = "Row";

  {
    auto& cttpo = options.convert_type_to_proto_options;
    cttpo.generate_nullable_array_wrappers = false;
    cttpo.generate_nullable_element_wrappers = false;
    cttpo.sql_table_options.allow_anonymous_field_name = true;
    cttpo.sql_table_options.allow_duplicate_field_names = true;
  }

  GOOGLESQL_ASSIGN_OR_RETURN(const IteratorProtoDescriptors descriptors,
                   ConvertIteratorToProto(*iter, options, pool));

  google::protobuf::DynamicMessageFactory message_factory;
  message_factory.SetDelegateToGeneratedFactory(true);

  GOOGLESQL_ASSIGN_OR_RETURN(
      const std::unique_ptr<google::protobuf::Message> msg,
      ProtoFromIterator(*iter, *descriptors.table, message_factory));

  return proto_writer_func_(*msg);
}

absl::Status ExecuteQueryWriteTextproto(const google::protobuf::Message& msg,
                                        std::ostream& stream) {
  google::protobuf::io::OstreamOutputStream zerocopy{&stream};

  if (!google::protobuf::TextFormat::Print(msg, &zerocopy)) {
    return absl::UnknownError("Printing as textproto failed");
  }

  return absl::OkStatus();
}

absl::Status ExecuteQueryWriteJson(const google::protobuf::Message& msg,
                                   std::ostream& stream) {
  google::protobuf::util::JsonPrintOptions options;
  options.add_whitespace = true;
  options.always_print_fields_with_no_presence = true;

  std::string buf;

  const auto status = google::protobuf::util::MessageToJsonString(msg, &buf, options);

  if (!status.ok()) {
    return absl::UnknownError(status.ToString());
  }

  stream << buf;

  return absl::OkStatus();
}

}  // namespace googlesql
