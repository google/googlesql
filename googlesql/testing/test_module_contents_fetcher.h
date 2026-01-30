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

#ifndef GOOGLESQL_TESTING_TEST_MODULE_CONTENTS_FETCHER_H_
#define GOOGLESQL_TESTING_TEST_MODULE_CONTENTS_FETCHER_H_

#include <memory>
#include <string>
#include <vector>

#include "googlesql/public/file_module_contents_fetcher.h"
#include "googlesql/public/module_contents_fetcher.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/descriptor.h"

namespace googlesql {
namespace testing {

// A ModuleContentsFetcher that can be used for testing. It behaves like a
// FileModuleContentsFetcher for most modules (loads them from test inputs on
// disk), but AddInMemoryModule can be called to store an in-memory module,
// which will be returned (instead of reading from disk) when
// FetchModuleContents is called for that module.
class TestModuleContentsFetcher : public ModuleContentsFetcher {
 public:
  TestModuleContentsFetcher() = default;
  // If `descriptor_pool` is not null, it will be used to fetch proto
  // descriptors for IMPORT PROTO statements. If `descriptor_pool` is null, any
  // attempt to fetch a proto descriptor will GOOGLESQL_RET_CHECK with an error.
  TestModuleContentsFetcher(google::protobuf::DescriptorPool* descriptor_pool,
                            const absl::string_view source_directory)
      : descriptor_pool_(descriptor_pool) {
    file_module_contents_fetcher_ =
        std::make_unique<FileModuleContentsFetcher>(source_directory);
  };

  // Not copyable or movable.
  TestModuleContentsFetcher(const TestModuleContentsFetcher&) = delete;
  TestModuleContentsFetcher& operator=(const TestModuleContentsFetcher&) =
      delete;

  // Fetches module contents for the given module_name_path. If this corresponds
  // to the `name` constructor parameter, then return the value passed via the
  // `module_contents` constructor parameter. Otherwise, module contents will be
  // fetched from the source directory provided via the `source_directory`
  // constructor parameter.
  absl::Status FetchModuleContents(
      const std::vector<std::string>& module_name_path,
      ModuleContentsInfo* module_info) override;

  // Fetch a FileDescriptor with the given name from `descriptor_pool_`.
  absl::Status FetchProtoFileDescriptor(
      const std::string& proto_file_name,
      const google::protobuf::FileDescriptor** proto_file_descriptor) override;

  // Add an in-memory module which will be returned (instead of reading from
  // disk) when FetchModuleContents is called for that module
  absl::Status AddInMemoryModule(std::vector<std::string> module_name_path,
                                 absl::string_view module_contents);

 private:
  absl::flat_hash_map<std::vector<std::string>, std::string> in_memory_modules_;
  google::protobuf::DescriptorPool* const descriptor_pool_;
  std::unique_ptr<FileModuleContentsFetcher> file_module_contents_fetcher_;
};

}  // namespace testing
}  // namespace googlesql

#endif  // GOOGLESQL_TESTING_TEST_MODULE_CONTENTS_FETCHER_H_
