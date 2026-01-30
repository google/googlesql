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

#include "googlesql/analyzer/rewriters/update_constructor_rewriter.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output_properties.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_rewrite_visitor.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "absl/status/statusor.h"

namespace googlesql {
namespace {

// UpdateConstructorRewriteVisitor rewrites an update constructor to
// REPLACE_FIELDS and ARRAY_TRANSFORM.
class UpdateConstructorRewriteVisitor : public ResolvedASTRewriteVisitor {
 private:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedUpdateConstructor(
      std::unique_ptr<const ResolvedUpdateConstructor> node) override {
    ResolvedUpdateConstructorBuilder update_builder =
        ToBuilder(std::move(node));
    std::vector<std::unique_ptr<const ResolvedReplaceFieldItem>>
        replace_field_items;
    for (std::unique_ptr<const ResolvedUpdateFieldItem>& update :
         update_builder.release_update_field_item_list()) {
      ResolvedUpdateFieldItemBuilder update_item_builder =
          ToBuilder(std::move(update));
      replace_field_items.push_back(
          MakeResolvedReplaceFieldItem(update_item_builder.release_expr(), {},
                                       update_item_builder.proto_field_path()));
    }
    return MakeResolvedReplaceField(update_builder.type(),
                                    update_builder.release_expr(),
                                    std::move(replace_field_items));
  }
};

}  // namespace

class UpdateConstructorRewriter : public Rewriter {
 public:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, std::unique_ptr<const ResolvedNode> input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    UpdateConstructorRewriteVisitor visitor;
    return visitor.VisitAll(std::move(input));
  }

  std::string Name() const override { return "UpdateConstructorRewriter"; }
};

const Rewriter* GetUpdateConstructorRewriter() {
  static const auto* const kRewriter = new UpdateConstructorRewriter;
  return kRewriter;
}

}  // namespace googlesql
