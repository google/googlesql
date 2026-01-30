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

#ifndef GOOGLESQL_COMMON_SCOPE_ERROR_CATALOG_H_
#define GOOGLESQL_COMMON_SCOPE_ERROR_CATALOG_H_

#include <memory>
#include <string>
#include <string_view>

#include "googlesql/public/catalog.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/types/type.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql {

// Class to use for resolving builtin-scope module objects. Includes a reference
// to a catalog which contains global-scope objects, in order to provide a
// better error message when a global-scope reference is found (but not allowed)
// from a builtin-scope object.
class ScopeErrorCatalog : public Catalog {
 public:
  ScopeErrorCatalog(const ScopeErrorCatalog&) = delete;
  ScopeErrorCatalog& operator=(const ScopeErrorCatalog&) = delete;

  // Both `builtin_catalog` and `global_catalog` must be non-NULL.
  // `global_references_supported` controls error messages produced by this
  // catalog - see the comment below for the `global_references_supported_`
  // member variable. Note that a non-NULL `global_catalog` is required even if
  // global references are unsupported, as it may contain other module objects
  // which are defined as global-scope.
  static absl::StatusOr<std::unique_ptr<ScopeErrorCatalog>> Create(
      absl::string_view name, bool global_references_supported,
      Catalog* builtin_catalog, Catalog* global_catalog);

  std::string FullName() const override { return name_; }

  // The main Find*() functions invoke the corresponding Find*() function
  // on the builtin catalog, and will return that result if it's success or
  // error. If the builtin catalog returns kNotFound, then lookup is attempted
  // in the global catalog to potentially produce a relevant error to inform the
  // user that a global reference was found but is not allowed from a builtin
  // scope object.
  absl::Status FindTable(const absl::Span<const std::string>& path,
                         const Table** table,
                         const FindOptions& options = FindOptions()) override;
  absl::Status FindFunction(
      const absl::Span<const std::string>& path, const Function** function,
      const FindOptions& options = FindOptions()) override;
  absl::Status FindTableValuedFunction(
      const absl::Span<const std::string>& path,
      const TableValuedFunction** function,
      const FindOptions& options = FindOptions()) override;
  absl::Status FindProcedure(
      const absl::Span<const std::string>& path, const Procedure** procedure,
      const FindOptions& options = FindOptions()) override;
  absl::Status FindModel(const absl::Span<const std::string>& path,
                         const Model** model,
                         const FindOptions& options = FindOptions()) override;
  absl::Status FindType(const absl::Span<const std::string>& path,
                        const Type** type,
                        const FindOptions& options = FindOptions()) override;

  // These functions use a different signature than the above Find*() functions,
  // and builtin-global scope errors are not implemented - these functions
  // simply forward to the builtin catalog. If the global catalog may contain
  // these object types in the future, these will need to be updated to return
  // the same error as the other Find*() functions.
  absl::Status FindPropertyGraph(absl::Span<const std::string> path,
                                 const PropertyGraph*& property_graph,
                                 const FindOptions& options) override;
  absl::Status FindConstantWithPathPrefix(
      absl::Span<const std::string> path, int* num_names_consumed,
      const Constant** constant,
      const FindOptions& options = FindOptions()) override;

  // The Suggest*() functions just forward to the builtin catalog.
  std::string SuggestTable(
      const absl::Span<const std::string>& mistyped_path) override;
  std::string SuggestFunction(
      const absl::Span<const std::string>& mistyped_path) override;
  std::string SuggestTableValuedFunction(
      const absl::Span<const std::string>& mistyped_path) override;
  std::string SuggestConstant(
      const absl::Span<const std::string>& mistyped_path) override;

 protected:
  // This constructor isn't public to restrict users to use the static Create()
  // method to generate new instances (which validates the input).
  ScopeErrorCatalog(absl::string_view name, bool global_references_supported,
                    Catalog* builtin_catalog, Catalog* global_catalog)
      : name_(name),
        global_references_supported_(global_references_supported),
        builtin_catalog_(builtin_catalog),
        global_catalog_(global_catalog) {}

 private:
  // The name of this catalog.
  const std::string name_;

  // Whether or not global references are supported. This value controls the
  // error messages produced by this catalog: if supported, the message will
  // guide the user toward enabling global references on the module object.
  const bool global_references_supported_;

  // The builtin-scope catalog, against which lookups occur.
  Catalog* builtin_catalog_;

  // The global-scope catalog, used provide better error messages when lookup in
  // the builtin catalog returns a kNotFound error.
  Catalog* global_catalog_;

  template <typename ObjectType>
  using CatalogFindMethod = absl::Status (Catalog::*)(
      const absl::Span<const std::string>& path, const ObjectType** object,
      const FindOptions& options);

  // Internal common helper function for Find*() calls. Performs the logic of
  // attempting lookup in the builtin catalog, and if not found, attempting
  // lookup in the global catalog to produce a relevant error.
  // `ObjectType` is the Catalog object type, which must be one of Table,
  // Function, TableValuedFunction, Procedure, Model, or Type.
  // `object_type_name` is the name of the object type, for use in error
  // messages.
  // `builtin_find_object_method` is the function to invoke to look up the
  // object in the builtin catalog.
  // `global_find_object_method` is the function to invoke to look up the object
  // in the global catalog.
  // `path`, `object`, and `options` are the same as in the public Find*()
  // functions, and are passed through to the `builtin_find_object_method` and
  // `global_find_object_method` calls.
  template <class ObjectType>
  absl::Status FindObjectBuiltinOnly(absl::string_view object_type_name,
                                     absl::Span<const std::string> path,
                                     const ObjectType** object,
                                     const FindOptions& options,
                                     CatalogFindMethod<ObjectType> find_method);
};

}  // namespace googlesql

#endif  // GOOGLESQL_COMMON_SCOPE_ERROR_CATALOG_H_
