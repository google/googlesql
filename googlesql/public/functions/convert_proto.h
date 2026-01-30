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

// This file declares functions that convert proto2 values between protos
// and TextFormat. Having shared implementations of these functions helps
// standardize printing format options and error handling.

#ifndef GOOGLESQL_PUBLIC_FUNCTIONS_CONVERT_PROTO_H_
#define GOOGLESQL_PUBLIC_FUNCTIONS_CONVERT_PROTO_H_

#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/message.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace functions {

// Convert 'value' to proto text format. The resulting text format proto is
// appended to 'out'.
bool ProtoToString(const google::protobuf::Message* value, absl::Cord* out,
                   absl::Status* error);

// Same as above, but uses multiline proto text representation.
bool ProtoToMultilineString(const google::protobuf::Message* value, absl::Cord* out,
                            absl::Status* error);

// Convert a text format proto from 'value' to a google::protobuf::Message that is
// populated into 'out'. The descriptors for all nested message types,
// extension types, and their associated file descriptors must be
// reachable from the DescriptorPool associated with 'out'. Additionally,
// the Reflection object accessible through 'out' must be available.
bool StringToProto(absl::string_view value, google::protobuf::Message* out,
                   absl::Status* error);

}  // namespace functions
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_FUNCTIONS_CONVERT_PROTO_H_
