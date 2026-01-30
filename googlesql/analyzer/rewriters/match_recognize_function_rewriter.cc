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

#include <memory>
#include <string>
#include <utility>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output_properties.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/public/type.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_rewrite_visitor.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/rewrite_utils.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "googlesql/base/ret_check.h"

namespace googlesql {

class MatchRecognizeFunctionVisitor : public ResolvedASTRewriteVisitor {
 public:
  MatchRecognizeFunctionVisitor(Catalog& catalog, TypeFactory& type_factory,
                                const AnalyzerOptions& analyzer_options)
      : catalog_(catalog),
        type_factory_(type_factory),
        analyzer_options_(analyzer_options),
        fn_builder_(analyzer_options, catalog, type_factory) {}

  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedAggregateFunctionCall(
      std::unique_ptr<const ResolvedAggregateFunctionCall> node) override {
    if (!node->function()->IsGoogleSQLBuiltin()) {
      return node;
    }
    bool is_max = false;
    switch (node->signature().context_id()) {
      case FN_FIRST_AGG:
        is_max = false;
        break;
      case FN_LAST_AGG:
        is_max = true;
        break;
      default:
        return node;
    }

    GOOGLESQL_RET_CHECK_EQ(node->argument_list_size(), 1);
    GOOGLESQL_RET_CHECK_EQ(node->order_by_item_list_size(), 1);
    GOOGLESQL_RET_CHECK(node->limit() == nullptr);
    GOOGLESQL_RET_CHECK(!node->distinct());
    GOOGLESQL_RET_CHECK(node->having_expr() == nullptr);
    GOOGLESQL_RET_CHECK(node->having_modifier() == nullptr);

    ResolvedAggregateFunctionCallBuilder builder = ToBuilder(std::move(node));

    if (!analyzer_options_.language().LanguageFeatureEnabled(
            FEATURE_HAVING_IN_AGGREGATE)) {
      return absl::UnimplementedError(
          "The rewrite for MATCH_RECOGNIZE FIRST() and LAST() requires support "
          "for HAVING MIN/MAX in aggregates");
    }

    return fn_builder_.AnyValue(
        std::move(builder.release_argument_list()[0]),
        ToBuilder(std::move(builder.release_order_by_item_list()[0]))
            .release_column_ref(),
        is_max);
  }

  Catalog& catalog_;
  TypeFactory& type_factory_;
  const AnalyzerOptions& analyzer_options_;
  FunctionCallBuilder fn_builder_;
};

class MatchRecognizeFunctionRewriter : public Rewriter {
 public:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, std::unique_ptr<const ResolvedNode> input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    MatchRecognizeFunctionVisitor visitor(catalog, type_factory, options);
    return visitor.VisitAll(std::move(input));
  }

  std::string Name() const override { return "MatchRecognizeFunctionRewriter"; }
};

const Rewriter* GetMatchRecognizeFunctionRewriter() {
  static const auto* const kRewriter = new MatchRecognizeFunctionRewriter;
  return kRewriter;
}

}  // namespace googlesql
