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

#include "googlesql/analyzer/rewriters/nulliferror_function_rewriter.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output_properties.h"
#include "googlesql/public/builtin_function.pb.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_builder.h"
#include "googlesql/resolved_ast/resolved_ast_rewrite_visitor.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/rewrite_utils.h"
#include "absl/status/statusor.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

// A visitor that rewrites NULLIFERROR(expr) to IFERROR(expr, NULL).
class NullIfErrorFunctionRewriteVisitor : public ResolvedASTRewriteVisitor {
 public:
  NullIfErrorFunctionRewriteVisitor(const AnalyzerOptions& analyzer_options,
                                    Catalog& catalog, TypeFactory& type_factory)
      : fn_builder_(analyzer_options, catalog, type_factory) {}

 private:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>>
  PostVisitResolvedFunctionCall(
      std::unique_ptr<const ResolvedFunctionCall> node) override;

  absl::StatusOr<std::unique_ptr<const ResolvedNode>> RewriteNullIfError(
      std::unique_ptr<const ResolvedFunctionCall> node);

  FunctionCallBuilder fn_builder_;
};

absl::StatusOr<std::unique_ptr<const ResolvedNode>>
NullIfErrorFunctionRewriteVisitor::PostVisitResolvedFunctionCall(
    std::unique_ptr<const ResolvedFunctionCall> node) {
  if (!IsBuiltInFunctionIdEq(node.get(), FN_NULLIFERROR)) {
    return node;
  }
  if (node->hint_list_size() > 0) {
    return ::googlesql_base::UnimplementedErrorBuilder()
           << "The NULLIFERROR() operator does not support hints.";
  }
  return RewriteNullIfError(std::move(node));
}

absl::StatusOr<std::unique_ptr<const ResolvedNode>>
NullIfErrorFunctionRewriteVisitor::RewriteNullIfError(
    std::unique_ptr<const ResolvedFunctionCall> node) {
  GOOGLESQL_RET_CHECK_EQ(node->argument_list_size(), 1)
      << "NULLIFERROR should have 1 expression argument. Got: "
      << node->DebugString();
  std::vector<std::unique_ptr<const ResolvedExpr>> argument_list =
      ToBuilder(std::move(node)).release_argument_list();
  std::unique_ptr<const ResolvedExpr> try_expr = std::move(argument_list[0]);
  GOOGLESQL_RET_CHECK(try_expr != nullptr);
  GOOGLESQL_ASSIGN_OR_RETURN(std::unique_ptr<const ResolvedExpr> null_literal,
                   ResolvedLiteralBuilder()
                       .set_type(try_expr->type())
                       .set_value(Value::Null(try_expr->type()))
                       .set_has_explicit_type(true)
                       .Build());
  return fn_builder_.IfError(std::move(try_expr), std::move(null_literal));
}

}  // namespace

class NullIfErrorFunctionRewriter : public Rewriter {
 public:
  std::string Name() const override { return "NullIfErrorFunctionRewriter"; }

  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, std::unique_ptr<const ResolvedNode> input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    GOOGLESQL_RET_CHECK(options.id_string_pool() != nullptr);
    GOOGLESQL_RET_CHECK(options.column_id_sequence_number() != nullptr);
    NullIfErrorFunctionRewriteVisitor rewriter(options, catalog, type_factory);
    return rewriter.VisitAll(std::move(input));
  };
};

const Rewriter* GetNullIfErrorFunctionRewriter() {
  static const auto* const kRewriter = new NullIfErrorFunctionRewriter;
  return kRewriter;
}

}  // namespace googlesql
