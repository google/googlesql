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

#include "googlesql/tools/execute_query/selectable_catalog.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/examples/tpch/catalog/tpch_catalog.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/testdata/sample_catalog_impl.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {

static std::vector<SelectableCatalog*>* InitSelectableCatalogsVector() {
  auto catalogs = std::make_unique<std::vector<SelectableCatalog*>>();

  catalogs->push_back(new SelectableCatalog(
      "none", "Empty catalog",
      [](const LanguageOptions&, CatalogAcceptor* acceptor) {
        static auto catalog = new SimpleCatalog("simple_catalog");
        acceptor->Accept(catalog);
        return absl::OkStatus();
      }));

  catalogs->push_back(new SelectableCatalog(
      "sample",
      "Analyzer test schema",
      [](const LanguageOptions& language_options,
         CatalogAcceptor* acceptor) -> absl::Status {
        static auto* sample_catalog_map =
            new absl::flat_hash_map<LanguageOptions,
                                    std::unique_ptr<SampleCatalogImpl>>();
        if (sample_catalog_map->contains(language_options)) {
          acceptor->Accept((*sample_catalog_map)[language_options]->catalog());
          return absl::OkStatus();
        }

        auto sample_catalog = std::make_unique<SampleCatalogImpl>();
        GOOGLESQL_RETURN_IF_ERROR(sample_catalog->LoadCatalogImpl(
            BuiltinFunctionOptions(language_options)));
        Catalog* catalog = sample_catalog->catalog();
        (*sample_catalog_map)[language_options] = std::move(sample_catalog);
        acceptor->Accept(catalog);
        return absl::OkStatus();
      }));

  catalogs->push_back(new SelectableCatalog(
      "tpch",
      "TPCH tables",
      [](const LanguageOptions&, CatalogAcceptor* acceptor) -> absl::Status {
        static auto* catalog =
            new absl::StatusOr<std::unique_ptr<Catalog>>(MakeTpchCatalog());

        GOOGLESQL_RETURN_IF_ERROR(catalog->status());
        acceptor->Accept(catalog->value().get());
        return absl::OkStatus();
      }));

  catalogs->push_back(new SelectableCatalog(
      "tpch_graph", "TPCH tables with semantic graph",
      [](const LanguageOptions&, CatalogAcceptor* acceptor) -> absl::Status {
        static auto* catalog = new absl::StatusOr<std::unique_ptr<Catalog>>(
            MakeTpchCatalog(/*with_semantic_graph=*/true));

        GOOGLESQL_RETURN_IF_ERROR(catalog->status());
        acceptor->Accept(catalog->value().get());
        return absl::OkStatus();
      }));

  return catalogs.release();
}

const std::vector<SelectableCatalog*>& GetSelectableCatalogs() {
  static const auto* catalogs = InitSelectableCatalogsVector();
  return *catalogs;
}

}  // namespace

const std::vector<SelectableCatalogInfo>& GetSelectableCatalogsInfo() {
  static const std::vector<SelectableCatalogInfo>* catalogs_info = [] {
    auto* catalogs_info = new std::vector<SelectableCatalogInfo>();
    for (const SelectableCatalog* catalog : GetSelectableCatalogs()) {
      catalogs_info->push_back(
          {.name = catalog->name(), .description = catalog->description()});
    }
    return catalogs_info;
  }();
  return *catalogs_info;
}

std::string GetSelectableCatalogDescriptionsForFlag() {
  std::vector<std::string> values;
  for (const SelectableCatalogInfo& catalog_info :
       GetSelectableCatalogsInfo()) {
    values.push_back(
        absl::StrCat(catalog_info.name, ": ", catalog_info.description));
  }
  return absl::StrJoin(values, "\n");
}

absl::StatusOr<SelectableCatalog*> FindSelectableCatalog(
    absl::string_view name) {
  for (SelectableCatalog* catalog : GetSelectableCatalogs()) {
    if (catalog->name() == name) {
      return catalog;
    }
  }
  return googlesql_base::InvalidArgumentErrorBuilder() << "Catalog not found: " << name;
}

}  // namespace googlesql
