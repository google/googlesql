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

#include "googlesql/public/module_factory.h"

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/module_contents_fetcher.h"
#include "googlesql/public/modules.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/types/type_factory.h"
#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/stl_util.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

ModuleFactory::ModuleFactory(const AnalyzerOptions& analyzer_options,
                             const ModuleFactoryOptions& module_factory_options,
                             ModuleContentsFetcher* module_contents_fetcher,
                             Catalog* builtin_catalog, Catalog* global_catalog,
                             TypeFactory* type_factory)
    : analyzer_options_(analyzer_options),
      module_factory_options_(module_factory_options),
      module_contents_fetcher_(module_contents_fetcher),
      builtin_catalog_(builtin_catalog),
      global_catalog_(global_catalog),
      type_factory_(type_factory) {}

ModuleFactory::ModuleFactory(
    const AnalyzerOptions& analyzer_options,
    const ModuleFactoryOptions& module_factory_options,
    std::unique_ptr<ModuleContentsFetcher> module_contents_fetcher,
    Catalog* builtin_catalog, Catalog* global_catalog,
    TypeFactory* type_factory)
    : analyzer_options_(analyzer_options),
      module_factory_options_(module_factory_options),
      module_contents_fetcher_owned_(std::move(module_contents_fetcher)),
      module_contents_fetcher_(module_contents_fetcher_owned_.get()),
      builtin_catalog_(builtin_catalog),
      global_catalog_(global_catalog),
      type_factory_(type_factory) {}

ModuleFactory::~ModuleFactory() {
  googlesql_base::STLDeleteContainerPairSecondPointers(catalog_name_map_.begin(),
                                            catalog_name_map_.end());
}

absl::Status ModuleFactory::CreateOrReturnModuleCatalog(
    const std::vector<std::string>& module_name_from_import,
    ModuleCatalog** module_catalog) {
  GOOGLESQL_RET_CHECK(module_contents_fetcher_ != nullptr);

  // If we are already in the process of creating a module with this name,
  // then we have a recursive import.
  // TODO: Consider whether ModuleFactory should support modules
  // with recursive imports, in which case we can remove this code.  Would
  // recursive imports potentially cause problems with BUILD rules?
  if (googlesql_base::ContainsKey(modules_under_construction_, module_name_from_import)) {
    std::vector<std::string> module_names;
    module_names.reserve(modules_under_construction_.size());
    for (const std::vector<std::string>& module_name :
         modules_under_construction_) {
      module_names.emplace_back(absl::StrJoin(module_name, "."));
    }
    if (module_names.size() > 1) {
      return googlesql_base::InvalidArgumentErrorBuilder()
             << "Recursive IMPORTs detected when creating module "
             << absl::StrJoin(module_name_from_import, ".")
             << ", which includes modules ("
             << absl::StrJoin(module_names, ", ") << ")";
    } else {
      return googlesql_base::InvalidArgumentErrorBuilder()
             << "Module " << absl::StrJoin(module_name_from_import, ".")
             << " recursively imports itself";
    }
  }
  // Remember this module name so that if we try to re-import this module
  // later it will fail.
  modules_under_construction_.insert(module_name_from_import);
  // Ensure that we remove the module name before returning.
  auto cleanup_modules_under_construction = absl::MakeCleanup(
      [&] { modules_under_construction_.erase(module_name_from_import); });

  // If the factory previously created a ModuleCatalog for this name then we
  // can return the created ModuleCatalog.
  if ((*module_catalog = GetModuleCatalog(module_name_from_import)) !=
      nullptr) {
    return absl::OkStatus();
  }

  ModuleContentsInfo module_info;
  GOOGLESQL_RETURN_IF_ERROR(module_contents_fetcher_->FetchModuleContents(
      module_name_from_import, &module_info));

  std::unique_ptr<ModuleCatalog> owned_module_catalog;
  GOOGLESQL_RETURN_IF_ERROR(ModuleCatalog::Create(
      module_name_from_import, module_factory_options_, module_info.filename,
      module_info.contents, analyzer_options_, this, builtin_catalog_,
      global_catalog_, type_factory_, &owned_module_catalog));
  GOOGLESQL_RET_CHECK(googlesql_base::InsertIfNotPresent(&catalog_name_map_, module_name_from_import,
                                    owned_module_catalog.release()));
  *module_catalog = GetModuleCatalog(module_name_from_import);
  return absl::OkStatus();
}

absl::Status ModuleFactory::GetProtoFileDescriptor(
    const std::string& proto_file_name,
    const google::protobuf::FileDescriptor** proto_file_descriptor) {
  return module_contents_fetcher_->FetchProtoFileDescriptor(
      proto_file_name, proto_file_descriptor);
}

ModuleCatalog* ModuleFactory::GetModuleCatalog(
    const std::vector<std::string>& module_name) const {
  return googlesql_base::FindPtrOrNull(catalog_name_map_, module_name);
}

}  // namespace googlesql
