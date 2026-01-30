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

#ifndef GOOGLESQL_PUBLIC_FILE_MODULE_CONTENTS_FETCHER_H_
#define GOOGLESQL_PUBLIC_FILE_MODULE_CONTENTS_FETCHER_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "googlesql/common/proto_helper.h"
#include "googlesql/proto/module_options.pb.h"
#include "googlesql/public/module_contents_fetcher.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/compiler/importer.h"
#include "google/protobuf/descriptor.h"

namespace googlesql {

// A ModuleContentsFetcher implementation that loads module contents from a file
// directory or set of directories.
//
// Consider a module imported via:
//   IMPORT MODULE a.b.c.d AS my_module;
//
// The module contents will be fetched from file "a/b/c/d.sqlm" or, if not found
// there, from "a/b/c/d/d.sqlm" relative to the fetcher's `source_directories_`.
//
// The fetcher can search for modules in multiple source directories. In this
// case, the fetcher returns the first found module file even if the module
// exists in multiple locations. The source directories are checked in the order
// they were passed to the constructor.
//
// The fetcher imports protos from the same `source_directories_` as modules. As
// with modules, the fetcher reads the first proto file found even if the same
// filename exists in multiple locations. The source directories are checked in
// the order they were passed to the constructor.
//
// This class includes a single DescriptorPool into which all fetched
// FileDescriptors are stored.
//
// Note - this class assumes (but does not enforce) that the files in the
// file system are not changing underneath this class, so that a consistent
// set of files are retrieved when processing multiple FetchModulesContents()
// or FetchProtoContents() calls.
class FileModuleContentsFetcher : public ModuleContentsFetcher {
 public:
  explicit FileModuleContentsFetcher(absl::string_view source_directory);

  explicit FileModuleContentsFetcher(
      const std::vector<std::string>& source_directories);

  // Similar to previous constructors, but uses an external descriptor pool. The
  // provided descriptor pool must outlive this fetcher, or be replaced by
  // another external pool or an owned pool by calling `ReplaceDescriptorPool`
  // or `ResetDescriptorPool` respectively.
  explicit FileModuleContentsFetcher(
      const std::vector<std::string>& source_directories,
      google::protobuf::DescriptorPool* descriptor_pool);

  // Not copyable or movable.
  FileModuleContentsFetcher(const FileModuleContentsFetcher&) = delete;
  FileModuleContentsFetcher& operator=(const FileModuleContentsFetcher&) =
      delete;

  absl::Status FetchModuleContents(
      const std::vector<std::string>& module_name_path,
      ModuleContentsInfo* module_info) override;

  absl::Status FetchProtoFileDescriptor(
      const std::string& proto_file_name,
      const google::protobuf::FileDescriptor** proto_file_descriptor) override;

  // Replaces the descriptor pool with an external one, and discards the owned
  // descriptor pool, if it was previously in use.
  void ReplaceDescriptorPool(google::protobuf::DescriptorPool* descriptor_pool);

 protected:
  // Returns a pointer to the proto descriptor pool.
  const google::protobuf::DescriptorPool* descriptor_pool() const {
    return descriptor_pool_;
  }

 private:
  const std::vector<std::string> source_directories_;

  // SourceTree provided to SourceTreeDescriptorDatabase which specifies the
  // file directories to search for protos.
  std::unique_ptr<google::protobuf::compiler::DiskSourceTree> source_tree_;

  // Collects errors related to proto file lookups.
  SimpleErrorCollector error_collector_;

  // A DescriptorDatabase used for the <descriptor_pool_>.
  std::unique_ptr<google::protobuf::DescriptorDatabase> descriptor_database_;

  // Owns all the FileDescriptors that are (transitively) fetched via
  // FetchProtoFileDescriptor().
  std::unique_ptr<google::protobuf::DescriptorPool> owned_descriptor_pool_;

  // Unowned descriptor pool.
  google::protobuf::DescriptorPool* descriptor_pool_;  // Not owned.
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_FILE_MODULE_CONTENTS_FETCHER_H_
