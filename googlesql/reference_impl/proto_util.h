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

#ifndef GOOGLESQL_REFERENCE_IMPL_PROTO_UTIL_H_
#define GOOGLESQL_REFERENCE_IMPL_PROTO_UTIL_H_

#include "googlesql/public/proto/type_annotation.pb.h"
#include "googlesql/public/value.h"
#include "absl/status/status.h"
#include "google/protobuf/io/coded_stream.h"

namespace googlesql {

class ProtoUtil {
 public:
  // Returns an error if 'format' is an unsupported format annotation,
  // or is not valid for <field>.
  // 'field' is optional and is used only for the error message.
  static absl::Status CheckIsSupportedFieldFormat(
      FieldFormat::Format format,
      const google::protobuf::FieldDescriptor* field);

  // Encodes 'value' in 'dst' as a field of type 'field_descr'. The type of
  // 'field_descr' must match the type of 'value'. In particular, if 'value'
  // is an array, 'field_descr' must be a repeated field. If writing this field
  // places an array value with any order kind other than "kPreservesOrder" into
  // a repeated proto field 'nondeterministic' will be set to "true". Otherwise
  // 'nondeterministic' is unchanged.
  struct WriteFieldOptions {
    // If false, it is an error to provide a null Value for any proto map entry
    // key or value.
    bool allow_null_map_keys = true;
  };
  static absl::Status WriteField(const WriteFieldOptions& options,
                                 const google::protobuf::FieldDescriptor* field_descr,
                                 FieldFormat::Format format, const Value& value,
                                 bool* nondeterministic,
                                 google::protobuf::io::CodedOutputStream* dst);
};

}  // namespace googlesql

#endif  // GOOGLESQL_REFERENCE_IMPL_PROTO_UTIL_H_
