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

#ifndef GOOGLESQL_PUBLIC_CONVERT_TYPE_TO_PROTO_H_
#define GOOGLESQL_PUBLIC_CONVERT_TYPE_TO_PROTO_H_

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "google/protobuf/descriptor.pb.h"
#include "googlesql/public/proto/type_annotation.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/base/status.h"

namespace googlesql {

struct ConvertTypeToProtoOptions {
  // Message name to use for the generated Descriptor.
  std::string message_name = "ProtoValue";

  // This can be used to set the googlesql.format annotation to use for
  // scalar fields of a particular type.
  // e.g. This can be used to map TYPE_DATE to FieldFormat.DATE_DECIMAL.
  // The caller is responsible for choosing a valid Format for a given TypeKind.
  std::map<TypeKind, FieldFormat::Format> field_format_map;

  // This is a union object storing either a Descriptor or EnumDescriptor.
  // (We can't use std::variant because it doesn't work as a map value.)
  struct MessageOrEnumDescriptor {
    MessageOrEnumDescriptor() {}
    explicit MessageOrEnumDescriptor(const google::protobuf::Descriptor* desc)
        : descriptor(desc) {}
    explicit MessageOrEnumDescriptor(const google::protobuf::EnumDescriptor* enum_desc)
        : enum_descriptor(enum_desc) {}

    // Exactly one of these will be non-NULL in the map returned in
    // <output_field_descriptor_map>.
    const google::protobuf::Descriptor* descriptor = nullptr;
    const google::protobuf::EnumDescriptor* enum_descriptor = nullptr;
  };

  // If non-NULL, this map will be filled in with the map of
  // referenced Descriptors and EnumDescriptors.
  // This can be used by callers trying to stitch together Descriptors
  // that may have come from different DescriptorPools.
  //
  // The map will have an entry for each FieldDescriptorProto in the generated
  // output proto that came from a PROTO or ENUM type in the original input
  // type.  The map value will point to the original Descriptor for that field.
  //
  // Callers can ignore the map keys and just use the map values if they
  // only need to see the set of Descriptors without the field mapping.
  typedef absl::flat_hash_map<const google::protobuf::FieldDescriptorProto*,
                              MessageOrEnumDescriptor>
      FieldDescriptorMap;
  FieldDescriptorMap* output_field_descriptor_map = nullptr;

  // If true, add import statements to the generated FileDescriptorProto with
  // the filenames of all referenced protos.
  // If false, no imports are added, and the caller should set up their own,
  // probably based on output_field_descriptor_map.
  // "googlesql/public/proto/type_annotation.proto" is always required.
  bool add_import_statements = true;

  // If true, type wrappers will be generated to support nullable arrays. If
  // false, arrays will be represented as repeated fields in the output. See the
  // Convert... functions below for more details.
  bool generate_nullable_array_wrappers = true;

  // If true, type wrappers will be generated to support nullable array
  // elements. If false, array elements will be represented as repeated fields
  // in the output.
  bool generate_nullable_element_wrappers = true;

  // If true, the converter will reuse the generated STRUCT/wrapper message
  // types for each googlesql::Type, though it does not identify equivalent
  // instances of googlesql::Type (i.e., the cache is base off the Type*
  // pointer). If false, each field in a STRUCT/wrapper will have its own type.
  // A typical use case of setting this option to false is to mutate the
  // generated protos later (e.g., setting the REQUIRED labels for some fields).
  bool consolidate_constructed_types = true;

  // Options that only applies to SQL_TABLE table type.
  struct SqlTableOptions {
    // If true, the converter will add a googlesql.struct_field_name annotation
    // for the anonymous/duplicate field. If false, an error will be
    // returned if an anonymous/duplicate name is used.
    bool allow_anonymous_field_name = false;
    bool allow_duplicate_field_names = false;
  };

  SqlTableOptions sql_table_options;

  // The supported timestamp formats. For kMicrosInt64, the timestamp value will
  // be converted to INT64 in TIMESTAMP_MICROS format. For kTimestampPicosProto,
  // the value will be converted to BYTES with TIMESTAMP_PICOS format.
  enum class TimestampFormat {
    kMicrosInt64,
    kTimestampPicosProto,
  };

  // Options that only applies to TIMESTAMP type. If not set, the default value
  // is kMicrosInt64.
  struct TimestampFormatOptions {
    TimestampFormat timestamp_format = TimestampFormat::kMicrosInt64;
  };

  TimestampFormatOptions timestamp_format_options;
};

// Generate a proto message definition that can be used to store values of
// STRUCT type <struct_type> or ARRAY type <array_type>.
//
// These protos can be used by engines as wire formats for googlesql objects.
// NOTE: These annotations are NOT interpreted in queries by GoogleSQL
// analysis.  Inside queries, these annotations are ignored and the
// protos will just act like protos.  If an engine wants to convert these
// protos back to GoogleSQL types, it can convert them with
// TypeFactory.MakeUnwrappedTypeFromProto and then load the unwrapped Type
// into the Catalog.
//
// The generated protos will be annotated as necessary to handle
// googlesql types in the most general form so NULLs are supported anywhere,
// except for the outermost value itself.
//
// This is not expected to fail when called on a valid StructType or ArrayType.
// (Only internal errors could be produced.)
//
// This clears <file> and adds one message definition.
//
// This does not set package or filename (or any other file-level options).
// Callers should fill those in to make this a valid file descriptor.
//
// This fills in necessary import statements using filenames from the
// Descriptors.  To make this proto definition loadable, callers will need
// to arrange to build a FileDescriptorSet that includes those dependencies.
//
// Generated struct protos have the following properties:
//  - The proto is annotated with googlesql.is_struct.
//  - The proto is annotated with googlesql.use_field_defaults=false to
//    support NULL field values.
//  - The proto has N fields. Generally tag numbers are assigned incrementally
//    from 1,2 ... N for N struct fields, with the exception of the case when N
//    is >= 19000. Protobuf library reserves range [19000, 20000) and thus we
//    shift up 1000 when assigning tag numbers after field_19000. In this case,
//    the assigned tag numbers are 1, 2 ... 18999, 20000, 20001 ... N + 1000.
//  - Proto fields use either the struct field name, or a generic name
//    "_field_<tag>" with a googlesql.struct_field_name annotation indicating
//    the original name.
//  - Field names that are non-empty, unique, and valid identifiers not starting
//    with underscore will be stored using their original name in the proto.
//
// Generated array protos have the following properties when
// generate_nullable_array_wrappers is set:
//  - A wrapper message is generated around the array to support NULL arrays.
//  - Wrapper protos are annotated with googlesql.is_wrapper and have
//    exactly one field, with tag number 1, called "value".
//
// Generated array protos have the following properties when
// generate_nullable_element_wrappers is set:
//  - A wrapper message is generated around the array element to support
//    NULL array elements.
//  - Wrapper protos are annotated with googlesql.is_wrapper and have
//    exactly one field, with tag number 1, called "value".
//
// When neither generate_nullable_array_wrappers nor
// generate_nullable_element_wrappers is set, array protos will simply be
// repeated fields of the element type.
//
// It is unspecified what the generated message names are, or where those
// messages are stored, but a client using reflection to traverse a generated
// proto can do so using only the field names, without examining message names.
absl::Status ConvertStructToProto(
    const StructType* struct_type,
    google::protobuf::FileDescriptorProto* file,
    const ConvertTypeToProtoOptions& options = ConvertTypeToProtoOptions());

absl::Status ConvertArrayToProto(
    const ArrayType* array_type,
    google::protobuf::FileDescriptorProto* file,
    const ConvertTypeToProtoOptions& options = ConvertTypeToProtoOptions());

// Generate a proto message definition that can represent rows of a query
// result.  This proto encodes all column names and their GoogleSQL types, and
// whether the table is a value table, so this proto can be converted back into
// a googlesql::Table schema with the exact same schema.
// All annotations described in ConvertStructToProto above are also
// applied here.  The top-level proto will always be annotated with
// googlesql.table_type indicating if the table is a SQL_TABLE or
// VALUE_TABLE.  For SQL tables, the columns are encoded into a
// struct, and then a proto is generated to represent that struct.
//
// For struct and proto value tables, this does not generate a
// nullability wrapper, so the value table cannot store NULLs.
// For simple types, a wrapper is generated because a message
// must be present to contain the field.
//
// For SQL_TABLE, duplicate column names and empty column names
// are not allowed.
//
// This clears <file> and adds one message definition.
//
// This does not set package or filename (or any other file-level options).
// Callers should fill those in to make this a valid file descriptor.
//
// This fills in necessary import statements using filenames from the
// Descriptors.  To make this proto definition loadable, callers will need
// to arrange to build a FileDescriptorSet that includes those dependencies.
//
// SPECIAL CASE: For proto value tables, rather than generating an anonymous
// wrapper proto, it is preferable to just use the existing proto. The output
// proto will have the same name as the row_type proto rather than using
// options.message_name.  The proto will be unchanged except that a
// googlesql.table_type=VALUE_TABLE annotation will be added.
// Callers will need to handle this case specially.  Callers may choose to
// call AddValueTableAnnotationForProto directly for that case rather than
// calling these methods.
//
// If is_value_table is false (i.e. this is a SQL table), this version
// expects the row type to already have been converted to struct.
// The overload below will do the struct conversion automatically.
//
// This inverse conversion can be done with TableFromProto.  To round trip
// any GoogleSQL result (table) type through a proto encoding, generate the
// proto using ConvertTableToProto and then convert that proto back into
// a Catalog Table using TableFromProto.
absl::Status ConvertTableToProto(
    const Type* row_type,
    bool is_value_table,
    google::protobuf::FileDescriptorProto* file,
    const ConvertTypeToProtoOptions& options = ConvertTypeToProtoOptions());

// Same as previous, but does not require converting the row type for
// SQL tables to structs.  For value tables, <columns> must have
// exactly one column.
absl::Status ConvertTableToProto(
    absl::Span<const std::pair<std::string, const Type*>> columns,
    bool is_value_table, google::protobuf::FileDescriptorProto* file,
    const ConvertTypeToProtoOptions& options = ConvertTypeToProtoOptions());

// Given a FileDescriptorProto, find the message named <message_full_name> and
// set the [googlesql.table_type=VALUE_TABLE] annotation inside it.
// Return error if the message is not found.
//
// This is used for proto value tables, where we want to emit the user's
// original proto but we want to add the table_type annotation onto it.
//
// At read time, the query engine should strip off this annotation,
// giving back the user's original proto.
absl::Status AddValueTableAnnotationForProto(
    absl::string_view message_full_name, google::protobuf::FileDescriptorProto* file);

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_CONVERT_TYPE_TO_PROTO_H_
