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

#ifndef GOOGLESQL_PUBLIC_MODULE_FACTORY_H_
#define GOOGLESQL_PUBLIC_MODULE_FACTORY_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/proto/module_options.pb.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/constant_evaluator.h"
#include "googlesql/public/module_contents_fetcher.h"
#include "googlesql/public/modules.h"
#include "googlesql/public/remote_tvf_factory.h"
#include "googlesql/public/type.h"
#include "absl/base/macros.h"
#include "google/protobuf/descriptor.h"

namespace googlesql {

struct ModuleFactoryOptions {
  // An engine callback for evaluating constant expressions. May be null.
  // DEPRECATED: Use `AnalyzerOptions::constant_evaluator()` instead.
  // TODO: b/277365877 - Remove once all uses are migrated to
  // `AnalyzerOptions::constant_evaluator`.
  ConstantEvaluator* const constant_evaluator = nullptr;

  // If 'store_import_stmt_analyzer_output' is true then store all of a module's
  // 'import' statement analyzer output into the related ModuleCatalog..
  bool store_import_stmt_analyzer_output = false;

  // The module options set by clients. It overrides module options defined in
  // GoogleSQL module files.
  ModuleOptions module_options_overrides;

  // An engine callback for creating remote TVFs in modules. If this is null,
  // remote TVFs are disabled.
  RemoteTvfFactory* const remote_tvf_factory = nullptr;
};

// A ModuleFactory is used to create all ModuleCatalogs
// related to module imports (including nested imports).
//
// Creating a ModuleFactory requires all information that
// is necessary for fetching and creating module catalogs.  This includes:
// 1) analyzer_options - used for resolving module statements
// 2) module_factory_options - provides engine callbacks (e.g., for evaluating
//    constant expressions)
// 3) module_contents_fetcher - An interface that is called to fetch
//    module contents from the engine, including proto contents (for
//    IMPORT PROTO statements)
// 4) builtin_catalog - the catalog of builtin names available to use
//    when resolving module statements.  Typically only contains
//    an engine's builtin functions.
// 5) global_catalog - the (optional) catalog of objects available to use when
//    when resolving module statements with `allowed_references='global'`.
//    Typically contains tables and other globally-defined objects.
// 6) type_factory - used for statement resolution
// TODO: Combine parameters that get passed on to
// ModuleCatalog::Create() in ModuleFactoryOptions (namely, <builtin_catalog>,
// <type_factory>).
//
// CreateOrReturnModuleCatalog() is called with a module name to load
// the related catalog (if not already loaded) and return it.
//
// This function executes the following steps:
// 1) If the ModuleCatalog is already loaded, return it
// 2) If not, then the <module_contents_fetcher_> is used to fetch module
//    contents using <module_name_from_import>
// 3) The module contents are parsed, and a LazyResolutionCatalog is
//    initialized
// 4) CreateOrReturnModuleCatalog() is recursively invoked on imported
//    modules (so imported module subcatalogs are initialized)
// 5) The created ModuleCatalog (LazyResolutionCatalog) is returned
//
// CreateOrReturnModuleCatalog() may fail for a few fatal reasons, such
// as not finding the module, or there not being exactly one
// MODULE statement in the module contents.  If any statements inside the
// module have initialization errors (parse errors, or unsupported statements
// or features), CreateOrReturnModuleCatalog() returns OK and the statement
// errors are collected in module_errors().
//
// The caller could treat module_errors() as warnings or errors as
// desired.  To fully validate the module, the caller would need to resolve
// all objects in the ModuleCatalog by calling ResolveAllStatements().
//
// TODO: The caller can call ResolveAllStatements() to proactively
// resolve all statements in the returned ModuleCatalog, but module_errors()
// only returns module initialization errors, not all the resolver errors.
// Add a new function that will return all ModuleCatalog resolution errors.
//
// Each ModuleCatalog must have a unique (multi-part, case-insensitive) name,
// and ModuleCatalogs can be fetched (case-insensitively) from the factory by
// name.
//
// The ModuleFactory owns all of the created ModuleCatalogs.
class ModuleFactory {
 public:
  // Constructs a ModuleFactory. Requires that `module_contents_fetcher` is not
  // NULL.
  ModuleFactory(const AnalyzerOptions& analyzer_options,
                const ModuleFactoryOptions& module_factory_options,
                ModuleContentsFetcher* module_contents_fetcher,
                Catalog* builtin_catalog, Catalog* global_catalog,
                TypeFactory* type_factory);

  // The same as above, but applies default `nullptr` for `global_catalog`.
  ABSL_DEPRECATED("Inline me!")
  ModuleFactory(const AnalyzerOptions& analyzer_options,
                const ModuleFactoryOptions& module_factory_options,
                ModuleContentsFetcher* module_contents_fetcher,
                Catalog* builtin_catalog, TypeFactory* type_factory)
      : ModuleFactory(analyzer_options, module_factory_options,
                      module_contents_fetcher, builtin_catalog,
                      /*global_catalog=*/nullptr, type_factory) {};

  // The same as above, but applies default `module_factory_options`.
  ABSL_DEPRECATED("Inline me!")
  ModuleFactory(const AnalyzerOptions& analyzer_options,
                ModuleContentsFetcher* module_contents_fetcher,
                Catalog* builtin_catalog, TypeFactory* type_factory)
      : ModuleFactory(analyzer_options, /*module_factory_options=*/{},
                      module_contents_fetcher, builtin_catalog,
                      /*global_catalog=*/nullptr, type_factory) {};

  // Similar to the previous constructor, but takes ownership of
  // `module_contents_fetcher`.
  ModuleFactory(const AnalyzerOptions& analyzer_options,
                const ModuleFactoryOptions& module_factory_options,
                std::unique_ptr<ModuleContentsFetcher> module_contents_fetcher,
                Catalog* builtin_catalog, Catalog* global_catalog,
                TypeFactory* type_factory);

  // The same as above, but applies default `nullptr` for `global_catalog`.
  ABSL_DEPRECATED("Inline me!")
  ModuleFactory(const AnalyzerOptions& analyzer_options,
                const ModuleFactoryOptions& module_factory_options,
                std::unique_ptr<ModuleContentsFetcher> module_contents_fetcher,
                Catalog* builtin_catalog, TypeFactory* type_factory)
      : ModuleFactory(analyzer_options, module_factory_options,
                      std::move(module_contents_fetcher), builtin_catalog,
                      /*global_catalog=*/nullptr, type_factory) {};

  // The same as above, but applies default `module_factory_options`.
  ABSL_DEPRECATED("Inline me!")
  ModuleFactory(const AnalyzerOptions& analyzer_options,
                std::unique_ptr<ModuleContentsFetcher> module_contents_fetcher,
                Catalog* builtin_catalog, TypeFactory* type_factory)
      : ModuleFactory(analyzer_options, /*module_factory_options=*/{},
                      std::move(module_contents_fetcher), builtin_catalog,
                      /*global_catalog=*/nullptr, type_factory) {};

  ~ModuleFactory();

  // Creates a ModuleCatalog (if it hasn't already been loaded) given the
  // unique <module_name_from_import>.  If not loaded yet, passes
  // <module_name_from_import> to <module_contents_fetcher_> in order to
  // retrieve the module contents and build the ModuleCatalog.
  // This function will return an error if the module cannot be loaded.
  // If the module loads but statements have errors, the errors are
  // returned in module_errors().
  // See class comments for further detail.
  absl::Status CreateOrReturnModuleCatalog(
      const std::vector<std::string>& module_name_from_import,
      ModuleCatalog** module_catalog);

  // Fetches a proto FileDescriptor given the <proto_file_name>, by invoking
  // the <module_contents_fetcher_>.  Returns an error if the <proto_file_name>
  // is not found.  Caller does not take ownership of the returned
  // FileDescriptor.
  absl::Status GetProtoFileDescriptor(
      const std::string& proto_file_name,
      const google::protobuf::FileDescriptor** proto_file_descriptor);

 private:
  AnalyzerOptions analyzer_options_;
  ModuleFactoryOptions module_factory_options_;
  std::unique_ptr<ModuleContentsFetcher> module_contents_fetcher_owned_;
  ModuleContentsFetcher* const module_contents_fetcher_;  // Not owned
  Catalog* const builtin_catalog_;                        // Not owned
  Catalog* const global_catalog_;                         // Not owned
  TypeFactory* const type_factory_;                       // Not owned

  // Helper function that takes a module name and looks up (case insensitively)
  // the associated ModuleCatalog.  Returns nullptr if not found.
  ModuleCatalog* GetModuleCatalog(
      const std::vector<std::string>& module_name) const;

  // Simple hack to detect recursive imports, allowing us to error out.
  // The CycleDetector is not used here because it currently keys on a
  // pointer, and here we need to key on a (multi-part) name.
  // Provides case-insensitive matching.
  // TODO: Consider supporting recursive imports in the
  // ModuleFactory.  If so we can remove this.
  // TODO: Consider making this a vector to capture the
  // import order.  This would allow better error messages that include
  // the import order.
  std::set<std::vector<std::string>, StringVectorCaseLess>
      modules_under_construction_;

  // Owns the ModuleCatalogs.  Each ModuleCatalog must have a unique
  // (lowercased) name.
  std::map<std::vector<std::string>, ModuleCatalog*, StringVectorCaseLess>
      catalog_name_map_;
};

}  // namespace googlesql

#endif  // GOOGLESQL_PUBLIC_MODULE_FACTORY_H_
