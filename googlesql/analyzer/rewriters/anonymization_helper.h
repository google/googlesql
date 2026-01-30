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

#ifndef GOOGLESQL_ANALYZER_REWRITERS_ANONYMIZATION_HELPER_H_
#define GOOGLESQL_ANALYZER_REWRITERS_ANONYMIZATION_HELPER_H_

#include <memory>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "absl/status/statusor.h"

namespace googlesql {

absl::StatusOr<std::unique_ptr<const ResolvedNode>> RewriteHelper(
    const ResolvedNode& tree, AnalyzerOptions options,
    ColumnFactory& column_factory, Catalog& catalog, TypeFactory& type_factory);

}  // namespace googlesql

#endif  // GOOGLESQL_ANALYZER_REWRITERS_ANONYMIZATION_HELPER_H_
