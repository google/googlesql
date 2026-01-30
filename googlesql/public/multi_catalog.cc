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

#include "googlesql/public/multi_catalog.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/constant.h"
#include "googlesql/public/function.h"
#include "googlesql/public/types/type.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

class Procedure;
class TableValuedFunction;

absl::Status MultiCatalog::Create(
    absl::string_view name, const std::vector<Catalog*>& catalog_list,
    std::unique_ptr<MultiCatalog>* multi_catalog) {
  for (const Catalog* catalog : catalog_list) {
    GOOGLESQL_RET_CHECK_NE(nullptr, catalog)
        << "MultiCatalog does not support NULL catalogs";
  }
  multi_catalog->reset(new MultiCatalog(name, catalog_list));
  return absl::OkStatus();
}

absl::Status MultiCatalog::PrependCatalog(Catalog* catalog) {
  GOOGLESQL_RET_CHECK(catalog != nullptr) << "Catalog must not be null.";
  catalog_list_.insert(catalog_list_.begin(), catalog);
  return absl::OkStatus();
}

absl::Status MultiCatalog::AppendCatalog(Catalog* catalog) {
  GOOGLESQL_RET_CHECK(catalog != nullptr) << "Catalog must not be null.";
  catalog_list_.push_back(catalog);
  return absl::OkStatus();
}

absl::Status MultiCatalog::PrependOwnedCatalog(
    std::unique_ptr<Catalog> catalog) {
  GOOGLESQL_RETURN_IF_ERROR(PrependCatalog(catalog.get()));
  owned_catalogs_.insert(owned_catalogs_.begin(), std::move(catalog));
  return absl::OkStatus();
}

absl::Status MultiCatalog::AppendOwnedCatalog(
    std::unique_ptr<Catalog> catalog) {
  GOOGLESQL_RETURN_IF_ERROR(AppendCatalog(catalog.get()));
  owned_catalogs_.push_back(std::move(catalog));
  return absl::OkStatus();
}

bool MultiCatalog::RemoveCatalog(Catalog* catalog) {
  for (auto it = catalog_list_.begin(); it != catalog_list_.end(); ++it) {
    if (*it == catalog) {
      catalog_list_.erase(it);
      return true;
    }
  }
  return false;
}

absl::StatusOr<std::unique_ptr<Catalog>> MultiCatalog::RemoveOwnedCatalog(
    Catalog* catalog) {
  std::unique_ptr<Catalog> removed_owned_catalog = nullptr;
  int owned_catalog_index = 0;
  for (owned_catalog_index = 0; owned_catalog_index < owned_catalogs_.size();
       ++owned_catalog_index) {
    if (owned_catalogs_[owned_catalog_index].get() == catalog) {
      removed_owned_catalog = std::move(owned_catalogs_[owned_catalog_index]);
      break;
    }
  }

  bool found_and_removed_catalog = RemoveCatalog(catalog);
  if (found_and_removed_catalog) {
    if (removed_owned_catalog == nullptr) {
      return absl::InternalError(
          absl::StrCat("Catalog ", catalog->FullName(),
                       " is not owned by this MultiCatalog, but was found in "
                       "the general catalog list. Did you intend to "
                       "call RemoveCatalog() instead?"));
    }
    owned_catalogs_.erase(owned_catalogs_.begin() + owned_catalog_index);
    return removed_owned_catalog;
  } else if (removed_owned_catalog != nullptr) {
    // All owned_catalogs_ should also be in catalog_list_, since only
    // catalog_list_ is used for lookups. If this check fails,  where somehow
    // this MultiCatalog owns a Catalog that is not also in catalog_list_,
    // there may be some internal Catalog mismanagement within MultiCatalog.
    return absl::InternalError(
        absl::StrCat("Owned catalog ", catalog->FullName(),
                     " was not found in main catalog list, but should have "
                     "been present. Perhaps RemoveCatalog() was erroneously "
                     "called on the MultiCatalog-owned catalog?"));
  } else {
    // If the catalog was not found in catalog_list_ or owned_catalogs_,
    // then return an error, since there may be confusion over ownership of
    // the catalogue that should be identified and resolved.
    return absl::NotFoundError(
        absl::StrCat("Catalog ", catalog->FullName(),
                     " is not owned by this MultiCatalog."));
  }
}

absl::Status MultiCatalog::FindTable(const absl::Span<const std::string>& path,
                                     const Table** table,
                                     const FindOptions& options) {
  for (Catalog* catalog : catalog_list_) {
    const absl::Status find_status = catalog->FindTable(path, table, options);
    if (!absl::IsNotFound(find_status)) {
      return find_status;
    }
  }
  return TableNotFoundError(path);
}

absl::Status MultiCatalog::FindModel(const absl::Span<const std::string>& path,
                                     const Model** model,
                                     const FindOptions& options) {
  for (Catalog* catalog : catalog_list_) {
    const absl::Status find_status = catalog->FindModel(path, model, options);
    if (!absl::IsNotFound(find_status)) {
      return find_status;
    }
  }
  return ModelNotFoundError(path);
}

absl::Status MultiCatalog::FindFunction(
    const absl::Span<const std::string>& path, const Function** function,
    const FindOptions& options) {
  for (Catalog* catalog : catalog_list_) {
    const absl::Status find_status =
        catalog->FindFunction(path, function, options);
    if (!absl::IsNotFound(find_status)) {
      return find_status;
    }
  }
  return FunctionNotFoundError(path);
}

absl::Status MultiCatalog::FindTableValuedFunction(
    const absl::Span<const std::string>& path,
    const TableValuedFunction** function, const FindOptions& options) {
  for (Catalog* catalog : catalog_list_) {
    const absl::Status find_status =
        catalog->FindTableValuedFunction(path, function, options);
    if (!absl::IsNotFound(find_status)) {
      return find_status;
    }
  }
  return TableValuedFunctionNotFoundError(path);
}

absl::Status MultiCatalog::FindProcedure(
    const absl::Span<const std::string>& path, const Procedure** procedure,
    const FindOptions& options) {
  for (Catalog* catalog : catalog_list_) {
    const absl::Status find_status =
        catalog->FindProcedure(path, procedure, options);
    if (!absl::IsNotFound(find_status)) {
      return find_status;
    }
  }
  return ProcedureNotFoundError(path);
}

absl::Status MultiCatalog::FindType(const absl::Span<const std::string>& path,
                                    const Type** type,
                                    const FindOptions& options) {
  for (Catalog* catalog : catalog_list_) {
    const absl::Status find_status = catalog->FindType(path, type, options);
    if (!absl::IsNotFound(find_status)) {
      return find_status;
    }
  }
  return TypeNotFoundError(path);
}

absl::Status MultiCatalog::FindConstantWithPathPrefix(
    const absl::Span<const std::string> path, int* num_names_consumed,
    const Constant** constant, const FindOptions& options) {
  for (Catalog* catalog : catalog_list_) {
    const absl::Status find_status = catalog->FindConstantWithPathPrefix(
        path, num_names_consumed, constant, options);
    if (!absl::IsNotFound(find_status)) {
      return find_status;
    }
  }
  return TypeNotFoundError(path);
}

absl::Status MultiCatalog::FindTableWithPathPrefix(
    const absl::Span<const std::string> path, const FindOptions& options,
    int* num_names_consumed, const Table** table) {
  int max_names_consumed = 0;
  for (Catalog* catalog : catalog_list_) {
    const Table* result_table = nullptr;
    const absl::Status find_status = catalog->FindTableWithPathPrefix(
        path, options, num_names_consumed, &result_table);
    // Returns any error status that is not NOT_FOUND. Any serious error
    // surfaced in one of the catalog lookup should fail the query directly.
    if (!find_status.ok() && !absl::IsNotFound(find_status)) {
      return find_status;
    }
    if (result_table != nullptr && *num_names_consumed > max_names_consumed) {
      max_names_consumed = *num_names_consumed;
      *table = result_table;
    }
  }
  if (max_names_consumed > 0) {
    GOOGLESQL_RET_CHECK_NE(*table, nullptr);
    *num_names_consumed = max_names_consumed;
    return absl::OkStatus();
  }
  *num_names_consumed = 0;
  return TableNotFoundError(path);
}

absl::Status MultiCatalog::FindPropertyGraph(
    absl::Span<const std::string> path, const PropertyGraph*& property_graph,
    const FindOptions& options) {
  for (Catalog* catalog : catalog_list_) {
    const absl::Status find_status =
        catalog->FindPropertyGraph(path, property_graph, options);
    if (!absl::IsNotFound(find_status)) {
      return find_status;
    }
  }
  return PropertyGraphNotFoundError(path);
}

std::string MultiCatalog::SuggestTable(
    const absl::Span<const std::string>& mistyped_path) {
  for (Catalog* catalog : catalog_list_) {
    const std::string suggestion = catalog->SuggestTable(mistyped_path);
    if (!suggestion.empty()) {
      return suggestion;
    }
  }
  return "";
}

std::string MultiCatalog::SuggestFunction(
    const absl::Span<const std::string>& mistyped_path) {
  for (Catalog* catalog : catalog_list_) {
    const std::string suggestion = catalog->SuggestFunction(mistyped_path);
    if (!suggestion.empty()) {
      return suggestion;
    }
  }
  return "";
}

std::string MultiCatalog::SuggestTableValuedFunction(
    const absl::Span<const std::string>& mistyped_path) {
  for (Catalog* catalog : catalog_list_) {
    const std::string suggestion =
        catalog->SuggestTableValuedFunction(mistyped_path);
    if (!suggestion.empty()) {
      return suggestion;
    }
  }
  return "";
}

std::string MultiCatalog::SuggestConstant(
    const absl::Span<const std::string>& mistyped_path) {
  for (Catalog* catalog : catalog_list_) {
    const std::string suggestion = catalog->SuggestConstant(mistyped_path);
    if (!suggestion.empty()) {
      return suggestion;
    }
  }
  return "";
}

}  // namespace googlesql
