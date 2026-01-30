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

#ifndef GOOGLESQL_PUBLIC_MODULE_CONTENTS_FETCHER_H_
#define GOOGLESQL_PUBLIC_MODULE_CONTENTS_FETCHER_H_

#include <map>
#include <string>
#include <vector>

#include "googlesql/proto/module_options.pb.h"
#include "googlesql/public/analyzer_options.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/types/span.h"
#include "google/protobuf/descriptor.h"

namespace googlesql {

// Information related to a module, including its name path (how it can be
// referenced in IMPORT MODULE statements), an optional 'filename' that
// indicates where the module came from, and the module's contents.
struct ModuleContentsInfo {
  // The module's name path that was used to fetch this module.
  std::vector<std::string> module_name_path;
  // The module's file name (or other source/identity information), to be
  // used in error messages.
  std::string filename;
  std::string contents;  // The module's SQL statement contents
};

// Interface for fetching module contents, including contents for IMPORT
// MODULE and IMPORT PROTO statements.
//
// TODO: Add an api that allows customization of the ErrorLocation
// 'filename' string.  This virtual method would likely take the module
// name path and return a string.
//
// For example, for module a.b.c the file-based fetcher would produce the
// relative path filename as:
//   a/b/c.sqlm
// ... so that ErrorLocations in messages look like:
//   [at a/b/c.sqlm:5:32]
//
// And the fetcher for the F1 QueryRequest might produce a 'filename' string
// like:
//   <inline module a.b.c from QueryRequest>
// ... so that ErrorLocations in messages look like:
//   [<inline module a.b.c from QueryRequest>:5:32]
class ModuleContentsFetcher {
 public:
  ModuleContentsFetcher() = default;
  virtual ~ModuleContentsFetcher() = default;

  // Not copyable or movable.
  ModuleContentsFetcher(const ModuleContentsFetcher&) = delete;
  ModuleContentsFetcher& operator=(const ModuleContentsFetcher&) = delete;

  // Fetches module contents given a name path, corresponding to the statement:
  //   IMPORT MODULE module.name.path.to.module_contents [as alias];
  // Returns an error if the module contents are not found.
  virtual absl::Status FetchModuleContents(
      const std::vector<std::string>& module_name_path,
      ModuleContentsInfo* module_contents) = 0;

  // Fetches the FileDescriptor for an IMPORT PROTO statement, such as:
  //   IMPORT PROTO "path/to/proto_file.proto";
  //
  // The default implementation of this method returns a 'not supported' error,
  // and implementations that want to provide this functionality must override
  // this method.
  //
  // Returns an error if the IMPORT PROTO statement is not supported or an
  // error occurs (e.g., file not found or the proto file failed parsing).
  //
  // Note that the process of creating a FileDescriptor in a DescriptorPool
  // implicitly loads all protos it depends upon into the DescriptorPool as
  // well.  So errors in any of the transitively imported protos are also
  // returned from this method.
  //
  // The caller does not take ownership of <proto_file_descriptor>, and
  // this class must outlive the returned <proto_file_descriptor> (therefore
  // overrides of this method must ensure that the <proto_file_descriptor>
  // stays alive as long as this class).
  virtual absl::Status FetchProtoFileDescriptor(
      const std::string& proto_file_name,
      const google::protobuf::FileDescriptor** proto_file_descriptor);
};

struct ProtoContentsInfo {
  // The proto's file name (or other source/identity information), to be
  // used in error messages.
  std::string filename;

  // The proto descriptor definition.
  google::protobuf::FileDescriptorProto file_descriptor_proto;
};

// The <string> is the proto file name that was looked up, which is case
// sensitive.
typedef absl::flat_hash_map<std::string, ProtoContentsInfo>
    ProtoContentsInfoMap;

// The std::vector<string> is the module name path that was looked up, which
// is case insensitive.
typedef std::map<std::vector<std::string>, ModuleContentsInfo,
                 StringVectorCaseLess>
    ModuleContentsInfoMap;

// Fetches module contents related to the specified <module_name_path>, as well
// as all transitively imported module contents. If a module is imported
// multiple times, it appears only once in the returned list. In order to find
// nested imports, module contents are parsed looking for IMPORT MODULE
// statements. If a parse error is found while parsing module contents, parsing
// stops on that module and processing continues for any remaining imported
// modules.  Clears <errors>, and populates <errors> with all errors found
// during processing (normally these would be parse errors or module file lookup
// errors). If an error is found then returns the first error status inserted
// into <errors>, otherwise returns OK.
absl::Status FetchAllModuleContents(
    const std::vector<std::string>& module_name_path,
    ModuleContentsFetcher* module_contents_fetcher,
    ModuleContentsInfoMap* module_contents_info_map,
    std::vector<absl::Status>* errors);

// Similar to the previous function, but takes a list of <module_name_paths>
// instead of a single <module_name_path>.
absl::Status FetchAllModuleContents(
    absl::Span<const std::vector<std::string>> module_name_paths,
    ModuleContentsFetcher* module_contents_fetcher,
    ModuleContentsInfoMap* module_contents_info_map,
    std::vector<absl::Status>* errors);

// Similar to the previous functions, but also returns a ProtoContentsInfoMap
// that includes all the protos that were (transitively) imported from
// the modules.
absl::Status FetchAllModuleAndProtoContents(
    absl::Span<const std::vector<std::string>> module_name_paths,
    ModuleContentsFetcher* module_contents_fetcher,
    ModuleContentsInfoMap* module_contents_info_map,
    ProtoContentsInfoMap* proto_contents_info_map,
    std::vector<absl::Status>* errors);

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_MODULE_CONTENTS_FETCHER_H_
