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

#include "googlesql/common/scope_error_catalog.h"

#include <memory>
#include <string>
#include <string_view>

#include "googlesql/public/catalog.h"
#include "googlesql/public/property_graph.h"
#include "googlesql/public/strings.h"
#include "googlesql/public/types/type.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_builder.h"

namespace googlesql {

namespace {
// Error messages to return when a global reference is found but not allowed.
// The specific error text depends on whether global references are supported,
// as it doesn't make sense to instruct users on how to enable global references
// if this would just lead to an error.

// Global references supported. $0 is the object type, $1 is the path.
constexpr absl::string_view kGlobalReferenceErrorSupported =
    "Invalid reference to global-scope $0 '$1' from a context which only "
    "allows builtin references. If this is an intended reference from a module "
    "function or TVF, the module function or TVF may be missing the option "
    "allowed_references='global' to allow global references";

// Global references not supported. $0 is the object type, $1 is the path.
constexpr absl::string_view kGlobalReferenceErrorUnsupported =
    "Invalid reference to global-scope $0 '$1' from a context which only "
    "allows builtin references. This execution environment does not allow "
    "global references";

}  // namespace

absl::StatusOr<std::unique_ptr<ScopeErrorCatalog>> ScopeErrorCatalog::Create(
    absl::string_view name, bool global_references_supported,
    Catalog* builtin_catalog, Catalog* global_catalog) {
  GOOGLESQL_RET_CHECK_NE(builtin_catalog, nullptr);
  GOOGLESQL_RET_CHECK_NE(global_catalog, nullptr);
  std::unique_ptr<ScopeErrorCatalog> scope_error_catalog =
      absl::WrapUnique(new ScopeErrorCatalog(name, global_references_supported,
                                             builtin_catalog, global_catalog));
  return scope_error_catalog;
}

absl::Status ScopeErrorCatalog::FindTable(
    const absl::Span<const std::string>& path, const Table** table,
    const FindOptions& options) {
  return FindObjectBuiltinOnly<Table>("table", path, table, options,
                                      &Catalog::FindTable);
}
absl::Status ScopeErrorCatalog::FindFunction(
    const absl::Span<const std::string>& path, const Function** function,
    const FindOptions& options) {
  return FindObjectBuiltinOnly<Function>("function", path, function, options,
                                         &Catalog::FindFunction);
}
absl::Status ScopeErrorCatalog::FindTableValuedFunction(
    const absl::Span<const std::string>& path,
    const TableValuedFunction** function, const FindOptions& options) {
  return FindObjectBuiltinOnly<TableValuedFunction>(
      "table valued function", path, function, options,
      &Catalog::FindTableValuedFunction);
}
absl::Status ScopeErrorCatalog::FindProcedure(
    const absl::Span<const std::string>& path, const Procedure** procedure,
    const FindOptions& options) {
  return FindObjectBuiltinOnly<Procedure>("procedure", path, procedure, options,
                                          &Catalog::FindProcedure);
}
absl::Status ScopeErrorCatalog::FindModel(
    const absl::Span<const std::string>& path, const Model** model,
    const FindOptions& options) {
  return FindObjectBuiltinOnly<Model>("model", path, model, options,
                                      &Catalog::FindModel);
}
absl::Status ScopeErrorCatalog::FindType(
    const absl::Span<const std::string>& path, const Type** type,
    const FindOptions& options) {
  return FindObjectBuiltinOnly<Type>("type", path, type, options,
                                     &Catalog::FindType);
}
absl::Status ScopeErrorCatalog::FindPropertyGraph(
    absl::Span<const std::string> path, const PropertyGraph*& property_graph,
    const FindOptions& options) {
  // If global catalog ever contains property graphs, and we want to return the
  // same error as with other types, this will need to be updated. Currently not
  // updated, as the signature is different and this would have no benefit.
  return builtin_catalog_->FindPropertyGraph(path, property_graph, options);
}

absl::Status ScopeErrorCatalog::FindConstantWithPathPrefix(
    const absl::Span<const std::string> path, int* num_names_consumed,
    const Constant** constant, const FindOptions& options) {
  // If global catalog ever contains constants, and we want to return the same
  // error as with other types, this will need to be updated. Currently not
  // updated, as the signature is different and this would have no benefit.
  return builtin_catalog_->FindConstantWithPathPrefix(path, num_names_consumed,
                                                      constant, options);
}

template <class ObjectType>
absl::Status ScopeErrorCatalog::FindObjectBuiltinOnly(
    const absl::string_view object_type_name,
    const absl::Span<const std::string> path, const ObjectType** object,
    const FindOptions& options, CatalogFindMethod<ObjectType> find_method) {
  absl::Status find_status =
      (builtin_catalog_->*find_method)(path, object, options);
  if (!absl::IsNotFound(find_status)) {
    return find_status;
  }
  // If not found in builtin catalog, check global catalog to enrich error.
  const ObjectType* unused_global_find_result;
  const absl::Status global_find_status = (global_catalog_->*find_method)(
      path, &unused_global_find_result, options);
  if (absl::IsNotFound(global_find_status)) {
    // Not a reference to a global-scope object, no changes to error.
    return find_status;
  }
  return ::googlesql_base::InvalidArgumentErrorBuilder() << absl::Substitute(
             global_references_supported_ ? kGlobalReferenceErrorSupported
                                          : kGlobalReferenceErrorUnsupported,
             object_type_name, IdentifierPathToString(path));
}

// The Suggest*() functions just forward to the builtin catalog.
std::string ScopeErrorCatalog::SuggestTable(
    const absl::Span<const std::string>& mistyped_path) {
  return builtin_catalog_->SuggestTable(mistyped_path);
}
std::string ScopeErrorCatalog::SuggestFunction(
    const absl::Span<const std::string>& mistyped_path) {
  return builtin_catalog_->SuggestFunction(mistyped_path);
}
std::string ScopeErrorCatalog::SuggestTableValuedFunction(
    const absl::Span<const std::string>& mistyped_path) {
  return builtin_catalog_->SuggestTableValuedFunction(mistyped_path);
}
std::string ScopeErrorCatalog::SuggestConstant(
    const absl::Span<const std::string>& mistyped_path) {
  return builtin_catalog_->SuggestConstant(mistyped_path);
}

}  // namespace googlesql
