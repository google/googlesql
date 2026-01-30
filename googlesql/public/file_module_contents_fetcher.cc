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

#include "googlesql/public/file_module_contents_fetcher.h"

#include <memory>
#include <optional>
#include <stack>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/base/path.h"
#include "googlesql/common/proto_helper.h"
#include "googlesql/common/status_payload_utils.h"
#include "googlesql/public/module_contents_fetcher.h"
#include "googlesql/public/options.pb.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/file_util.h"
#include "google/protobuf/compiler/importer.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor_database.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {

struct LoadInstruction {
  explicit LoadInstruction(google::protobuf::FileDescriptorProto proto, int next_child)
      : proto(proto), next_child(next_child) {}
  google::protobuf::FileDescriptorProto proto;
  int next_child;
};

// If proto_file_name is not found in descriptor_pool, then fetch it from the
// descriptor_database and add it to the stack. Returns false if there was an
// error.
bool AddToStackIfMissing(const std::string& proto_file_name,
                         google::protobuf::DescriptorPool* descriptor_pool,
                         google::protobuf::DescriptorDatabase* descriptor_database,
                         std::stack<LoadInstruction>& output_stack) {
  if (descriptor_pool->FindFileByName(proto_file_name) == nullptr) {
    output_stack.push(LoadInstruction{{}, 0});
    if (!descriptor_database->FindFileByName(proto_file_name,
                                             &output_stack.top().proto)) {
      return false;
    }
  }
  return true;
}

// Iterative version to parse proto files, it includes the following:
//
// * Post Order Traversal (Building): a.proto is only built in the
//     DescriptorPool until after b.proto and c.proto, have been built.
//
// * Trimming branches: Once c.proto has been built, there is no need to
//     traverse it (nor its dependencies).
//
// Here is a simple example to keep in mind.
//
// a.proto
// | - b.proto
// | | c.proto
// | - c.proto
//
// DescriptorDatabase: Is responsible for parsing proto files into a
//   FileDescriptorProto (a proto).
//
// DescriptorPool: Is responsible for building FileDescriptor (a cpp
//   interconected object).
//
// If the file cannot be fetched, nullptr is returned. The root cause of the
// failure may be a (transitive) dependency. The file that caused the failure
// will be returned in the output parameter failed_file_name.
const google::protobuf::FileDescriptor* IterativeAddOrFindFile(
    const std::string& proto_file_name, google::protobuf::DescriptorPool* descriptor_pool,
    google::protobuf::DescriptorDatabase* descriptor_database,
    google::protobuf::DescriptorPool::ErrorCollector* error_collector,
    std::string& failed_file_name) {
  std::stack<LoadInstruction> stack;
  if (!AddToStackIfMissing(proto_file_name, descriptor_pool,
                           descriptor_database, stack)) {
    failed_file_name = proto_file_name;
    return nullptr;
  }

  while (!stack.empty()) {
    LoadInstruction& current = stack.top();

    int num_deps = current.proto.dependency_size()
        ;  // NOLINT

    if (current.next_child >= num_deps) {
      // There are no more children to build. We just finished building all the
      // child nodes. We should build this node now.
      if (descriptor_pool->BuildFileCollectingErrors(
              current.proto, error_collector) == nullptr) {
        failed_file_name = current.proto.name();
        return nullptr;
      }
      stack.pop();
    } else {
      // There is at least one child available.
      //
      // 1) Capture current_child to visit now.
      // 2) Advance next_child for the next time we come here.
      // 3) Add the first children of current_child if missing.
      int current_child = current.next_child;
      current.next_child++;
      const std::string* dependency;
      if (current_child < current.proto.dependency_size()) {
        dependency = &current.proto.dependency(current_child);
      }
      if (!AddToStackIfMissing(*dependency, descriptor_pool,
                               descriptor_database, stack)) {
        failed_file_name = *dependency;
        return nullptr;
      }
    }
  }

  // Get final descriptor
  return descriptor_pool->FindFileByName(proto_file_name);
}

}  // namespace

FileModuleContentsFetcher::FileModuleContentsFetcher(
    absl::string_view source_directory)
    : FileModuleContentsFetcher(
          std::vector<std::string>{std::string(source_directory)}) {}

FileModuleContentsFetcher::FileModuleContentsFetcher(
    const std::vector<std::string>& source_directories)
    : source_directories_(source_directories) {
  source_tree_ = std::make_unique<google::protobuf::compiler::DiskSourceTree>();
  for (auto& directory : source_directories) {
    source_tree_->MapPath("", directory);
  }
  auto st_descriptor_database =
      std::make_unique<google::protobuf::compiler::SourceTreeDescriptorDatabase>(
          source_tree_.get());
  st_descriptor_database->RecordErrorsTo(&error_collector_);
  descriptor_database_ = std::move(st_descriptor_database);
  owned_descriptor_pool_ = std::make_unique<google::protobuf::DescriptorPool>();
  descriptor_pool_ = owned_descriptor_pool_.get();
}

FileModuleContentsFetcher::FileModuleContentsFetcher(
    const std::vector<std::string>& source_directories,
    google::protobuf::DescriptorPool* descriptor_pool)
    : source_directories_(source_directories),
      descriptor_pool_(descriptor_pool) {
  source_tree_ = std::make_unique<google::protobuf::compiler::DiskSourceTree>();
  for (auto& directory : source_directories) {
    source_tree_->MapPath("", directory);
  }
  auto st_descriptor_database =
      std::make_unique<google::protobuf::compiler::SourceTreeDescriptorDatabase>(
          source_tree_.get());
  st_descriptor_database->RecordErrorsTo(&error_collector_);
  descriptor_database_ = std::move(st_descriptor_database);
}

absl::Status FileModuleContentsFetcher::FetchModuleContents(
    const std::vector<std::string>& module_name_path,
    ModuleContentsInfo* module_info) {
  const std::string module_name_string = absl::StrJoin(module_name_path, "/");
  const std::string extension = ".sqlm";

  std::vector<std::string> possible_locations;
  for (const auto& root_dir : source_directories_) {
    // For a <module_name_path> of 'a.b.c', first try to look it up at
    // 'a/b/c.sqlm'.  If we don't find it there, then try to look it up
    // at 'a/b/c/c.sqlm'.
    possible_locations.push_back(
        googlesql_base::JoinPath(root_dir, absl::StrCat(module_name_string, extension)));
    possible_locations.push_back(
        googlesql_base::JoinPath(root_dir, module_name_string,
                       absl::StrCat(module_name_path.back(), extension)));
  }

  std::vector<absl::Status> errors;
  for (const auto& module_location : possible_locations) {
    const absl::Status status =
        internal::GetContents(module_location, &(module_info->contents));
    if (status.ok()) {
      // Ignore <errors> occurred so far, since we found something in the end.
      module_info->module_name_path = module_name_path;
      module_info->filename = module_location;
      return absl::OkStatus();
    } else {
      errors.push_back(status);
    }
  }

  bool not_found = true;
  for (const auto& error : errors) {
    if (error.code() != absl::StatusCode::kNotFound) {
      not_found = false;
      break;
    }
  }

  if (not_found) {
    return googlesql_base::NotFoundErrorBuilder()
           << "Module " << absl::StrJoin(module_name_path, ".")
           << " not found in following locations:\n"
           << absl::StrJoin(possible_locations, ";\n");
  }

  // There were some unexpected errors, return all of them.
  // Note that returned error is still NOT_FOUND.
  // Consider returning the same error type as the first non-NOT_FOUND error.
  return googlesql_base::NotFoundErrorBuilder()
         << "Fetching module contents failed for module "
         << absl::StrJoin(module_name_path, ".") << " with errors:\n"
         << absl::StrJoin(errors, ";\n",
                          [](std::string* out, const absl::Status& status) {
                            absl::StrAppend(out,
                                            internal::StatusToString(status));
                          });
}

absl::Status FileModuleContentsFetcher::FetchProtoFileDescriptor(
    const std::string& proto_file_name,
    const google::protobuf::FileDescriptor** proto_file_descriptor) {
  GOOGLESQL_RET_CHECK_NE(descriptor_pool_, nullptr);
  GOOGLESQL_RET_CHECK_NE(descriptor_database_.get(), nullptr);
  error_collector_.ClearError();

  std::string failed_file_name;
  *proto_file_descriptor = IterativeAddOrFindFile(
      proto_file_name, descriptor_pool_, descriptor_database_.get(),
      &error_collector_, failed_file_name);
  if (*proto_file_descriptor == nullptr) {
    std::string error_detail;
    if (error_collector_.HasError()) {
      error_detail = absl::StrCat(" with error: ", error_collector_.GetError());
    } else {
      error_detail = absl::StrCat(
          "; searched in following locations:\n",
          absl::StrJoin(
              source_directories_, ";\n",
              [&failed_file_name](std::string* out, absl::string_view dir) {
                absl::StrAppend(out, googlesql_base::JoinPath(dir, failed_file_name));
              }));
    }
    return ::googlesql_base::InvalidArgumentErrorBuilder()
           << "Fetching proto file '" << failed_file_name << "' failed"
           << error_detail;
  }
  return absl::OkStatus();
}

void FileModuleContentsFetcher::ReplaceDescriptorPool(
    google::protobuf::DescriptorPool* descriptor_pool) {
  descriptor_pool_ = descriptor_pool;
  owned_descriptor_pool_ = std::make_unique<google::protobuf::DescriptorPool>();
}

}  // namespace googlesql
