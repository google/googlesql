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

#include "googlesql/common/proto_helper.h"

#include <cstdint>
#include <optional>
#include <set>
#include <string>

#include "googlesql/base/logging.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/thread_stack.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {

// Need to implement this to catch importing errors.
class StringAppendErrorCollector :
      public google::protobuf::DescriptorPool::ErrorCollector {
 public:
  StringAppendErrorCollector() = default;
  StringAppendErrorCollector(const StringAppendErrorCollector&) = delete;
  StringAppendErrorCollector& operator=(const StringAppendErrorCollector&)
      = delete;

  void RecordError(absl::string_view filename, absl::string_view element_name,
                   const google::protobuf::Message* descriptor, ErrorLocation location,
                   absl::string_view message) override {
    absl::StrAppend(&error_, HasError() ? "\n" : "", filename, ": ",
                    element_name, ": ", message);
  }
  bool HasError() const { return !error_.empty(); }
  const std::string& GetError() const { return error_; }

 private:
  std::string error_;
};

}  // namespace

absl::Status PopulateFileDescriptorSet(
    const google::protobuf::FileDescriptor* file_descr,
    std::optional<int64_t> file_descriptor_set_max_size_bytes,
    google::protobuf::FileDescriptorSet* file_descriptor_set,
    std::set<const google::protobuf::FileDescriptor*>* file_descriptors) {
  GOOGLESQL_RET_CHECK(file_descriptor_set != nullptr);
  GOOGLESQL_RET_CHECK(file_descriptors != nullptr);
  if (googlesql_base::InsertIfNotPresent(file_descriptors, file_descr)) {
    for (int idx = 0; idx < file_descr->dependency_count(); ++idx) {
      GOOGLESQL_RETURN_IF_ERROR(PopulateFileDescriptorSet(
          file_descr->dependency(idx), file_descriptor_set_max_size_bytes,
          file_descriptor_set, file_descriptors));
    }
    file_descr->CopyTo(file_descriptor_set->add_file());
  }
  if (file_descriptor_set_max_size_bytes.has_value() &&
      file_descriptor_set->ByteSizeLong() >
          file_descriptor_set_max_size_bytes.value()) {
    return MakeSqlError()
        << "Serializing proto descriptors failed due to maximum "
        << "FileDescriptorSet size exceeded, max = "
        << file_descriptor_set_max_size_bytes.value() << ", size = "
        << file_descriptor_set->ByteSizeLong();
  }
  return absl::OkStatus();
}

absl::Status AddFileDescriptorSetToPool(
    const google::protobuf::FileDescriptorSet* file_descriptor_set,
    google::protobuf::DescriptorPool* pool) {

  StringAppendErrorCollector error_collector;
  for (const google::protobuf::FileDescriptorProto& file : file_descriptor_set->file()) {
    // Skip if the file has already been added.
    if (pool->FindFileByName(file.name()) != nullptr) {
      continue;
    }
    pool->BuildFileCollectingErrors(file, &error_collector);
    if (error_collector.HasError()) {
      return MakeSqlError()
             << "Error(s) encountered during protocol buffer analysis: "
             << error_collector.GetError();
    }
  }
  return absl::OkStatus();
}

}  // namespace googlesql
