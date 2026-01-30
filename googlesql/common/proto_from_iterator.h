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

#ifndef GOOGLESQL_COMMON_PROTO_FROM_ITERATOR_H_
#define GOOGLESQL_COMMON_PROTO_FROM_ITERATOR_H_

#include <stdint.h>

#include <memory>
#include <optional>
#include <string>

#include "googlesql/public/convert_type_to_proto.h"
#include "googlesql/public/evaluator_table_iterator.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace googlesql {

// Options for generating protobuf descriptors from an EvaluatorTableIterator.
// Resulting structure:
//
// <filename>.proto:
//   package <package>; // optional
//   message <convert_type_to_proto_options.row_message_name> {
//     // Columns in iterator order with appropriate types
//   };
//   message <table_message_name> {
//     repeated <convert_type_to_proto_options.row_message_name>
//       <table_row_field_name> = <table_row_field_number>;
//   };
struct IteratorProtoDescriptorOptions {
  IteratorProtoDescriptorOptions();

  // Filename of the generated FileDescriptor in the target pool. Must be unique
  // among all files in the pool.
  std::string filename;

  // Optional package for proto types used as a prefix to the message names.
  std::optional<std::string> package;

  // Settings for the top-level "table" message which contains all rows from an
  // iterator.
  std::string table_message_name;
  int32_t table_row_field_number = 1;
  std::string table_row_field_name;

  // Additional settings for the conversion, including the message name for the
  // row type (convert_type_to_proto_options.message_name).
  ConvertTypeToProtoOptions convert_type_to_proto_options;
};

struct IteratorProtoDescriptors {
  // Descriptor of a message containing a single repeated field of the row
  // message type.
  const google::protobuf::Descriptor* table = nullptr;

  // Descriptor of the row message type generated from iterator columns.
  const google::protobuf::Descriptor* row = nullptr;
};

// Build protobuf descriptor representing columns in a given table iterator. See
// IteratorProtoDescriptorOptions for details on the generated structure and how
// to control them.
absl::StatusOr<IteratorProtoDescriptors> ConvertIteratorToProto(
    const EvaluatorTableIterator& iter,
    const IteratorProtoDescriptorOptions& options,
    google::protobuf::DescriptorPool& pool);

// Populate the given proto message with values from the current row in the
// iterator. The message field types must match up with the columns.
absl::Status MergeRowToProto(const EvaluatorTableIterator& iter,
                             google::protobuf::MessageFactory& message_factory,
                             google::protobuf::Message& proto_out);

// Create a new proto message containing all rows from the given iterator. The
// given proto descriptor must have a repeated message type field as its first
// field. Its type is used to create a new message for each row.
absl::StatusOr<std::unique_ptr<google::protobuf::Message>> ProtoFromIterator(
    EvaluatorTableIterator& iter, const google::protobuf::Descriptor& table_desc,
    google::protobuf::MessageFactory& message_factory);

}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_PROTO_FROM_ITERATOR_H_
