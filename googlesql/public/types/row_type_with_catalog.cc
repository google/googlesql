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

#include <functional>
#include <string>

#include "googlesql/public/catalog.h"
#include "googlesql/public/types/row_type.h"
#include "googlesql/public/types/type.h"

namespace googlesql {

static Type::HasFieldResult TableHasColumn(const Table* table,
                                           const std::string& name) {
  if (table->GetColumnListMode() == Table::ColumnListMode::DEFAULT) {
    // With non-lazy tables, we can check for columns cheaply and accurately.
    const Column* column = table->FindColumnByName(name);
    return column == nullptr ? Type::HAS_NO_FIELD : Type::HAS_FIELD;
  } else {
    // For lazy-column tables, assume we always potentially have a field with
    // that name.
    // We'll resolve it or give errors at the point when the analyzer tries to
    // actually use the field.
    //
    // This causes a few issues:
    // * NameScopes assume that all column names exist, which means that in
    //   queries with more than one table, all unqualified names look
    //   ambiguous.
    // * `SELECT * EXCEPT (name)` doesn't check that `name` actually exists.
    //
    // TODO Make this use FindLazyColumn so it works all tables.
    // This requires propagating error handling through HasField callers.
    return Type::HAS_FIELD;
  }
}

// This is the setter in row_type.cc used to install the callback.
using HasColumnCallbackType =
    std::function<Type::HasFieldResult(const Table*, const std::string&)>;
void SetRowTypeHasColumnColumnCallback(HasColumnCallbackType callback);

// Install callbacks to call Catalog methods.
// To avoid a circular dependency, these are installed from a static
// initializer in the `:type_with_catalog_impl` build target.
static void RegisterCatalogCallbacks() {
  SetRowTypeHasColumnColumnCallback(&TableHasColumn);
}

namespace {
static bool module_initialization_complete = []() {
  RegisterCatalogCallbacks();
  return true;
} ();
}  // namespace

}  // namespace googlesql
