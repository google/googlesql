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

#ifndef GOOGLESQL_COMMON_PROTO_HELPER_H_
#define GOOGLESQL_COMMON_PROTO_HELPER_H_

#include <cstdint>
#include <optional>
#include <set>
#include <string>

#include "google/protobuf/descriptor.pb.h"
#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"
#include "google/protobuf/compiler/importer.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/base/status.h"

namespace googlesql {

// Simple implementation of proto ErrorCollector and MultiFileErrorCollector
// interfaces that collects errors encountered while parsing proto files, and
// provides accessors for the collected errors.  The two interfaces are
// supported because a single instance of this error collector is used
// by modules for both a DescriptorPool and its related ProtoFileParser.
class SimpleErrorCollector
    : public google::protobuf::DescriptorPool::ErrorCollector,
      public google::protobuf::compiler::MultiFileErrorCollector {
 public:
  SimpleErrorCollector() = default;

  // Not copyable or movable.
  SimpleErrorCollector(const SimpleErrorCollector&) = delete;
  SimpleErrorCollector& operator=(const SimpleErrorCollector&) = delete;

  // MultiFileErrorCollector interface method.
  void RecordError(absl::string_view filename, int line, int column,
                   absl::string_view message) override {
    absl::StrAppend(&error_, "Filename ", filename, " Line ", line, " Column ",
                    column, " :", message, "\n");
  }

  // ErrorCollector interface method.
  void RecordError(absl::string_view filename, absl::string_view element_name,
                   const google::protobuf::Message* descriptor, ErrorLocation location,
                   absl::string_view message) override {
    // This implementation is copied from AppendToStringErrorCollector(),
    // and <location> is ignored.  TODO: Figure out if <location>
    // is useful, and add it into the error string if so.  Looking at the
    // ErrorLocation enum, it is unclear if it is useful in this context.
    absl::StrAppend(&error_, !error_.empty() ? "\n" : "", filename, " : ",
                    element_name, " : ", message);
  }

  bool HasError() const { return !error_.empty(); }
  const std::string& GetError() const { return error_; }
  void ClearError() { error_.clear(); }

 private:
  std::string error_;
};

// Adds <file_descr> and all its dependent files to <file_descriptor_set> if
// they are not already present in <file_descriptors>. Referenced files will be
// added before referencing files. FileDescriptor dependencies do not allow
// circular dependencies, so this cannot recurse indefinitely.  Optionally
// takes a <file_descriptor_set_max_size_bytes> which sets a maximum size
// on the returned <file_descriptor_set>, and returns an error for an
// out-of-memory condition (this is also checked via ThreadHasEnoughStack()).
absl::Status PopulateFileDescriptorSet(
    const google::protobuf::FileDescriptor* file_descr,
    std::optional<int64_t> file_descriptor_set_max_size_bytes,
    google::protobuf::FileDescriptorSet* file_descriptor_set,
    std::set<const google::protobuf::FileDescriptor*>* file_descriptors);

// Deserialize a FileDescriptorSet and add all FileDescriptors into the given
// DescriptorPool.
// Return an error status if the FileDescriptorSet is incomplete or contain
// other error.
absl::Status AddFileDescriptorSetToPool(
    const google::protobuf::FileDescriptorSet* file_descriptor_set,
    google::protobuf::DescriptorPool* pool);

}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_PROTO_HELPER_H_
