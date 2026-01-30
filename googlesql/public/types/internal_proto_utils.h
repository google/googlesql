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

#ifndef GOOGLESQL_PUBLIC_TYPES_INTERNAL_PROTO_UTILS_H_
#define GOOGLESQL_PUBLIC_TYPES_INTERNAL_PROTO_UTILS_H_

#include <stddef.h>

#include "googlesql/base/logging.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/types/type.h"
#include "absl/status/status.h"
#include "google/protobuf/descriptor.h"

namespace googlesql {
namespace internal {  //   For internal use only

// Adds the file descriptor and all of its dependencies to the given map of file
// descriptor sets, indexed by the file descriptor's pool. Returns the 0-based
// <file_descriptor_set_index> corresponding to file descriptor set to which
// the dependencies were added.  Returns an error on out-of-memory.
absl::Status PopulateDistinctFileDescriptorSets(
    const BuildFileDescriptorMapOptions& options,
    const google::protobuf::FileDescriptor* file_descr,
    FileDescriptorSetMap* file_descriptor_set_map,
    int* file_descriptor_set_index);

}  // namespace internal
}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_TYPES_INTERNAL_PROTO_UTILS_H_
