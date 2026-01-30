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

#ifndef GOOGLESQL_TESTDATA_SPECIAL_CATALOG_H_
#define GOOGLESQL_TESTDATA_SPECIAL_CATALOG_H_

#include <memory>

#include "googlesql/public/simple_catalog.h"

namespace googlesql {

// This function returns a SimpleCatalog loaded with a table with some hardcoded
// columns with anonymous / duplicated column names, used by select_star test.
// Look at the .cc file to see what's in the Catalog.
std::unique_ptr<SimpleCatalog> GetSpecialCatalog();

}  // namespace googlesql

#endif  // GOOGLESQL_TESTDATA_SPECIAL_CATALOG_H_
