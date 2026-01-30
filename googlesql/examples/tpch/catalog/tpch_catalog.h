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

#ifndef GOOGLESQL_EXAMPLES_TPCH_CATALOG_TPCH_CATALOG_H_
#define GOOGLESQL_EXAMPLES_TPCH_CATALOG_TPCH_CATALOG_H_

#include <memory>

#include "googlesql/public/simple_catalog.h"
#include "absl/status/statusor.h"

namespace googlesql {

// Make a SimpleCatalog containing the eight tables from TPC-H.
// The tables include a callback to return data.
// A new catalog is made each time, but data is shared across all instances.
// This uses 1MB of source data linked in as a cc_embed_data.
//
// If `with_semantic_graph` is true, the Tables are extended with join columns.
absl::StatusOr<std::unique_ptr<SimpleCatalog>> MakeTpchCatalog(
    bool with_semantic_graph = false);

}  // namespace googlesql

#endif  // GOOGLESQL_EXAMPLES_TPCH_CATALOG_TPCH_CATALOG_H_
