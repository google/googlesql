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

#include "googlesql/analyzer/rewriters/anonymization_rewriter.h"

#include <memory>
#include <string>

#include "googlesql/analyzer/rewriters/anonymization_helper.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "absl/status/statusor.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

class AnonymizationRewriter : public Rewriter {
 public:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, const ResolvedNode& input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    GOOGLESQL_RET_CHECK(options.AllArenasAreInitialized());
    ColumnFactory column_factory(0, options.id_string_pool().get(),
                                 options.column_id_sequence_number());
    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<const ResolvedNode> node,
        RewriteHelper(input, options, column_factory, catalog, type_factory));
    return node;
  }

  std::string Name() const override { return "AnonymizationRewriter"; }
};

absl::StatusOr<RewriteForAnonymizationOutput> RewriteForAnonymization(
    const ResolvedNode& query, Catalog* catalog, TypeFactory* type_factory,
    const AnalyzerOptions& analyzer_options, ColumnFactory& column_factory) {
  RewriteForAnonymizationOutput result;
  GOOGLESQL_ASSIGN_OR_RETURN(result.node,
                   RewriteHelper(query, analyzer_options, column_factory,
                                 *catalog, *type_factory));
  return result;
}

const Rewriter* GetAnonymizationRewriter() {
  static const Rewriter* kRewriter = new AnonymizationRewriter;
  return kRewriter;
}

}  // namespace googlesql
