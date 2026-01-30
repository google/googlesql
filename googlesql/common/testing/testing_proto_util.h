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

#ifndef GOOGLESQL_COMMON_TESTING_TESTING_PROTO_UTIL_H_
#define GOOGLESQL_COMMON_TESTING_TESTING_PROTO_UTIL_H_

#include <memory>

#include "googlesql/base/path.h"
#include "google/protobuf/compiler/importer.h"
#include "google/protobuf/message.h"
#include "gtest/gtest.h"
#include "absl/base/macros.h"
#include "absl/strings/cord.h"

namespace googlesql {

ABSL_DEPRECATED("Inline me!")
inline absl::Cord SerializeToCord(const google::protobuf::Message& pb) {
  absl::Cord bytes;
  ABSL_CHECK(pb.SerializeToCord(&bytes));
  return bytes;
}

ABSL_DEPRECATED("Inline me!")
inline absl::Cord SerializePartialToCord(const google::protobuf::Message& pb) {
  absl::Cord bytes;
  ABSL_CHECK(pb.SerializePartialToCord(&bytes));
  return bytes;
}

inline std::unique_ptr<google::protobuf::compiler::DiskSourceTree>
CreateProtoSourceTree() {
  auto source_tree = std::make_unique<google::protobuf::compiler::DiskSourceTree>();
  // Adding external files to source tree. The external dependencies
  // are located under canonical name folder. For bazel version < 8, the
  // format is `module_name~` if there is only one version of the module.
  //
  // This should be updated to use rlocation() since canonical name is not a
  // reliable API, according to
  // https://bazel.build/versions/7.6.0/external/module#repository_names_and_strict_deps.
  for (std::string vproto :
       {"any_proto",
        "api_proto",
        "descriptor_proto",
        "duration_proto",
        "empty_proto",
        "field_mask_proto",
        "source_context_proto",
        "struct_proto",
        "timestamp_proto",
        "type_proto",
        "wrappers_proto"}) {
    source_tree->MapPath("",
      googlesql_base::JoinPath(getenv("TEST_SRCDIR"), "protobuf~",
                             "src", "google", "protobuf", "_virtual_imports",
                             vproto));
  }
  source_tree->MapPath(
      "", googlesql_base::JoinPath(getenv("TEST_SRCDIR"), "googleapis~"));
  source_tree->MapPath(
      "", googlesql_base::JoinPath(getenv("TEST_SRCDIR"), "_main"));
  // clang-format on
  return source_tree;
}

}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_TESTING_TESTING_PROTO_UTIL_H_
