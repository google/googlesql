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

// Conversion functions between Value and google::protobuf::Message.
//
// There are two use cases for these methods:
// 1) Engines that use googlesql's Type <-> google::protobuf::Descriptor conversion
//    methods (ConvertStructToProto()/ConvertArrayToProto() and
//    TypeFactory::MakeUnwrappedTypeFromProto()) can use these methods to
//    convert Values to the corresponding proto messages, respecting the wire
//    format annotations used in the corresponding google::protobuf::Descriptors.
//
// 2) Engines (particularly the reference implementation) can use some of these
//    methods to extract or populate Values corresponding to the fields of a
//    proto using googlesql query semantics (which ignore wire format
//    annotations).
#ifndef GOOGLESQL_PUBLIC_PROTO_VALUE_CONVERSION_H_
#define GOOGLESQL_PUBLIC_PROTO_VALUE_CONVERSION_H_

#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"
#include "googlesql/base/status.h"

namespace googlesql {

class Type;
class Value;

// Populates 'value_out' with a googlesql::Value representation with type 'type'
// of a particular proto field (which is NULL if the field is not set). If the
// field is repeated, then 'index' may be either the index of the element to
// convert, or -1 to convert the entire repeated field.
//
// The type of 'value' must match the type of 'field'. More formally, if 'field'
// is a wrapper (i.e., its type is a proto message annotated with
// googlesql.is_wrapper and 'use_wire_format_annotations' is true), then the
// type of 'value' must be the corresponding GoogleSQL type (or possibly an
// ARRAY of that type, if either 'field' is repeated or the wrapped field is
// repeated). If 'field' is not a wrapper, the type of 'value' must be the
// GoogleSQL type corresponding to the proto type (e.g., TYPE_INT64 for
// google::protobuf::FieldDescriptor::TYPE_INT64), or possibly an ARRAY of that type, if
// 'field' is repeated.  Examples:
// - 'field' = optional int64
//   'value' has TYPE_INT64
// - 'field' = repeated int64 and 'index' >= 0
//   'value' has TYPE_INT64
// - 'field' = repeated int64 and 'index' is -1
//   'value' has TYPE_ARRAY(INT64)
// - 'field' = {optional,repeated} FooMessage (non-wrapper)
//   'value' has TYPE_PROTO(FooMessage), or possibly
//   TYPE_ARRAY(TYPE_PROTO(FooMessage)) (if 'field' is repeated and
//                                       'index' is -1)
// - 'field' = optional NullableInt64 (wrapper for an optional int64)
//   'value' has TYPE_INT64
// - 'field' = repeated NullableInt64 (wrapper for an optional int64)
//             or optional NullableInt64Array (wrapper for a repeated int64)
//   'value' has TYPE_INT64 or TYPE_ARRAY(INT64)
//
// If `match_struct_fields_by_name` is false, we iterate through proto fields in
// descriptor field order and assume that the nth proto field matches the nth
// STRUCT field, for any encountered STRUCT (whether top-level or encountered
// recursively).  If true, we instead map proto fields to STRUCT fields by name,
// failing if there is no proto field corresponding to the name of the field
// in the corresponding `googlesql::StructType`.
absl::Status ProtoFieldToValue(const google::protobuf::Message& proto,
                               const google::protobuf::FieldDescriptor* field, int index,
                               const Type* type,
                               bool use_wire_format_annotations,
                               Value* value_out,
                               bool match_struct_fields_by_name = false);

// Merges the data in 'value' into the specified 'field' of 'proto_out'.
//
// This method RET_CHECKs that the caller does not attempt to place a NULL
// element inside a repeated field that does not support NULL elements.
//
// The type of 'value' must match the type of 'field', in the same sense as in
// ProtoFieldToValue().
//
// If 'value' is NULL (and does not represent a NULL element of a repeated field
// that does not support NULL elements), this method has no effect
// (it does not clear anything), except possibly for creating empty wrapper
// messages.
//
// 'message_factory' is used to construct messages if the value type is PROTO.
//
// Examples of NULL behavior:
// - 'field' = optional int64
//   - NULL 'value' has no effect.
// - 'field' = repeated int64
//   - NULL INT64 'value' causes a GOOGLESQL_RET_CHECK failure.
//   - NULL ARRAY<INT64> 'value' has no effect.
// - 'field' = optional FooMessage (non-wrapper)
//   - NULL 'value' has no effect.
// - 'field' = repeated FooMessage (non-wrapper)
//   - NULL PROTO 'value' causes a GOOGLESQL_RET_CHECK failure.
//   - NULL ARRAY<PROTO> 'value' has no effect.
// - 'field' = optional NullableInt64 (wrapper for an optional int64)
//   - NULL 'value' creates an empty NullableInt64, then recursively assigns
//     NULL to its wrapped field (to represent a NULL INT64).
// - 'field' = repeated NullableInt64
//   - NULL ARRAY 'value' has no effect, but NULL non-ARRAY 'value' creates a
//     new empty NullableInt64, then recursively assigns NULL to its wrapped
//     field (to represent a new NULL INT64).
// - 'field' = optional NullableInt64Array (wrapper for a repeated int64)
//   - NULL ARRAY 'value' has no effect, but NULL non-ARRAY 'value' recursively
//     assigns NULL to the wrapped field (to represent an empty array).
//
// If the type of 'value' is STRUCT or contains STRUCT, the descriptor for
// 'proto_out' must have been created using ConvertStructToProto()/
// ConvertArrayToProto().  The descriptor may or may not have array wrappers
// and element wrappers, but an error will result if 'value' contains a NULL
// for an array or array element that does not have a wrapper in the
// corresponding descriptor.
//
// STRUCT fields are matched with proto subfields by tag ids rather than by name
// (STRUCT fields can be anonymous): the value of the first STRUCT field will be
// assigned to the proto subfield with tag 1, the second STRUCT field to tag 2,
// and so on. The number of STRUCT fields must match the number of proto
// subfields.
absl::Status MergeValueToProtoField(const Value& value,
                                    const google::protobuf::FieldDescriptor* field,
                                    bool use_wire_format_annotations,
                                    google::protobuf::MessageFactory* message_factory,
                                    google::protobuf::Message* proto_out);
// Note that 'use_wire_format_annotations = false' corresponds to GoogleSQL
// query semantics, which does not match this method.
inline absl::Status MergeValueToProtoField(
    const Value& value, const google::protobuf::FieldDescriptor* field,
    google::protobuf::MessageFactory* message_factory, google::protobuf::Message* proto_out) {
  return MergeValueToProtoField(value, field,
                                /*use_wire_format_annotations=*/true,
                                message_factory, proto_out);
}

// Converts a non-NULL 'value' (of STRUCT or ARRAY type) to a proto and
// returns it in 'proto_out', previous content is cleared. 'message_factory'
// will be used to construct submessages of 'proto_out'.
//
// The descriptor for 'proto_out' must have been created using
// ConvertStructToProto()/ConvertArrayToProto().  The descriptor
// may or may not have array wrappers and element wrappers, but an error
// will result if 'value' contains a NULL for an array or array element that
// does not have a wrapper in the corresponding descriptor.
absl::Status ConvertStructOrArrayValueToProtoMessage(
    const Value& value, google::protobuf::MessageFactory* message_factory,
    google::protobuf::Message* proto_out);

// Converts 'proto' to a Value and returns it in 'value_out'.
// The type of the returned Value will be 'type', which should have been
// created using TypeFactory::MakeUnwrappedTypeFromProto() on the
// descriptor of 'proto' if googlesql wrapper annotations are present and
// expected to be honored.
//
// If `match_struct_fields_by_name` is false, we iterate through proto fields in
// descriptor field order and assume that the nth proto field matches the nth
// STRUCT field, for any encountered STRUCT (whether top-level or encountered
// recursively).  If true, we instead map proto fields to STRUCT fields by name,
// failing if there is no proto field corresponding to the name of the field
// in the corresponding `googlesql::StructType`.
absl::Status ConvertProtoMessageToStructOrArrayValue(
    const google::protobuf::Message& proto, const Type* type, Value* value_out,
    bool match_struct_fields_by_name = false);

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_PROTO_VALUE_CONVERSION_H_
