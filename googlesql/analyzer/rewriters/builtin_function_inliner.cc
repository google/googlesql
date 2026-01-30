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
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/analyzer/builtin_only_catalog.h"
#include "googlesql/analyzer/substitute.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output_properties.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_ast_deep_copy_visitor.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/rewrite_utils.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace {

class BuiltinFunctionInlinerVisitor : public ResolvedASTDeepCopyVisitor {
 public:
  BuiltinFunctionInlinerVisitor(const AnalyzerOptions& analyzer_options,
                                Catalog* catalog, TypeFactory* type_factory)
      : analyzer_options_(analyzer_options),
        catalog_(catalog),
        type_factory_(type_factory) {}

 private:
  absl::Status ValidateFunctionArgumentTypeOptions(
      const FunctionArgumentTypeOptions& options, int arg_idx) {
    GOOGLESQL_RET_CHECK(options.has_argument_name())
        << "Functions with configured inlining must provide argument names. "
        << "Missing argument name for argument " << arg_idx;
    return absl::OkStatus();
  }
  absl::Status Rewrite(const ResolvedFunctionCall* node,
                       absl::string_view rewrite_template,
                       bool allow_table_references,
                       std::vector<std::string> allowed_function_groups) {
    // generic_argument list is only set if at least one argument of this
    // function call is not a ResolvedExpr e.g. a ResolvedInlineLambda
    // otherwise argument_list is used and all arguments are ResolvedExprs.
    bool use_generic_arguments = node->generic_argument_list_size() != 0;
    int num_arguments = use_generic_arguments
                            ? node->generic_argument_list_size()
                            : node->argument_list_size();
    GOOGLESQL_RET_CHECK_EQ(num_arguments, node->signature().NumConcreteArguments())
        << "Number of arguments provided " << num_arguments
        << " does not match the number of arguments expected by the function "
        << " signature " << node->signature().NumConcreteArguments();

    absl::flat_hash_map<std::string, const ResolvedInlineLambda*> lambdas;
    absl::flat_hash_map<std::string, const ResolvedExpr*> variables;
    std::vector<std::unique_ptr<ResolvedExpr>> processed_args;
    std::vector<std::unique_ptr<ResolvedInlineLambda>> processed_lambdas;
    for (int i = 0; i < num_arguments; ++i) {
      const ResolvedFunctionArgument* arg =
          use_generic_arguments ? node->generic_argument_list(i) : nullptr;
      if (use_generic_arguments && arg->inline_lambda() != nullptr) {
        const ResolvedInlineLambda* lambda = arg->inline_lambda();
        GOOGLESQL_ASSIGN_OR_RETURN(processed_lambdas.emplace_back(), ProcessNode(lambda));

        const FunctionArgumentTypeOptions& arg_options =
            node->signature().ConcreteArgument(i).options();
        GOOGLESQL_RETURN_IF_ERROR(ValidateFunctionArgumentTypeOptions(arg_options, i));

        const auto& [_, no_conflict] = lambdas.try_emplace(
            arg_options.argument_name(), processed_lambdas.back().get());
        GOOGLESQL_RET_CHECK(no_conflict)
            << "Duplicate lambda argument name not allowed for inlined"
            << "built-in function: " << arg_options.argument_name();
      } else {
        GOOGLESQL_RET_CHECK(!use_generic_arguments ||
                  (use_generic_arguments && arg->expr() != nullptr))
            << "REWRITE_BUILTIN_FUNCTION_INLINER only supports normal "
            << "expression arguments or function-typed arguments.";
        const ResolvedExpr* arg = use_generic_arguments
                                      ? node->generic_argument_list(i)->expr()
                                      : node->argument_list(i);
        GOOGLESQL_RET_CHECK_NE(arg, nullptr);
        GOOGLESQL_ASSIGN_OR_RETURN(processed_args.emplace_back(), ProcessNode(arg));

        const FunctionArgumentTypeOptions& arg_options =
            node->signature().ConcreteArgument(i).options();
        GOOGLESQL_RETURN_IF_ERROR(ValidateFunctionArgumentTypeOptions(arg_options, i));

        const auto& [_, no_conflict] = variables.try_emplace(
            arg_options.argument_name(), processed_args.back().get());
        GOOGLESQL_RET_CHECK(no_conflict)
            << "Duplicate argument name not allowed for inlined built-in "
            << "function: " << arg_options.argument_name();
      }
    }

    bool is_safe =
        node->error_mode() == ResolvedFunctionCallBase::SAFE_ERROR_MODE;
    if (is_safe) {
      GOOGLESQL_RETURN_IF_ERROR(CheckCatalogSupportsSafeMode(
          node->function()->SQLName(), analyzer_options_, *catalog_));
    }

    // A generic template to handle SAFE version function expression.
    constexpr absl::string_view kSafeExprTemplate = "NULLIFERROR($0)";

    Catalog* catalog_for_substitution = catalog_;
    std::optional<BuiltinOnlyCatalog> builtin_catalog;
    bool disable_validation =
        analyzer_options_.language().LanguageFeatureEnabled(
            FEATURE_DISABLE_VALIDATE_REWRITERS_REFER_TO_BUILTINS);
    if (!disable_validation) {
      // Apply a BuiltinOnlyCatalog wrapper to ensure that only builtin objects
      // are referenced when resolving the SQL template.
      builtin_catalog.emplace("builtin_catalog", *catalog_);
      builtin_catalog->set_allow_tables(allow_table_references);
      builtin_catalog->set_allowed_function_groups(
          std::move(allowed_function_groups));
      catalog_for_substitution = &*builtin_catalog;
    }

    GOOGLESQL_ASSIGN_OR_RETURN(
        std::unique_ptr<ResolvedExpr> rewritten_expr,
        AnalyzeSubstitute(
            analyzer_options_, *catalog_for_substitution, *type_factory_,
            is_safe ? absl::Substitute(kSafeExprTemplate, rewrite_template)
                    : rewrite_template,
            variables, lambdas, node->annotated_type()));
    PushNodeToStack(std::move(rewritten_expr));
    return absl::OkStatus();
  }

  absl::Status VisitResolvedFunctionCall(
      const ResolvedFunctionCall* node) override {
    if (!node->signature().options().rewrite_options().has_value()) {
      return CopyVisitResolvedFunctionCall(node);
    }
    const FunctionSignatureRewriteOptions& rewrite_options =
        node->signature().options().rewrite_options().value();
    if (rewrite_options.enabled() &&
        rewrite_options.rewriter() == REWRITE_BUILTIN_FUNCTION_INLINER) {
      return Rewrite(node, rewrite_options.sql(),
                     rewrite_options.allow_table_references(),
                     rewrite_options.allowed_function_groups());
    }
    return CopyVisitResolvedFunctionCall(node);
  }

  const AnalyzerOptions& analyzer_options_;
  Catalog* catalog_;
  TypeFactory* type_factory_;
};

class BuiltinFunctionInliner : public Rewriter {
 public:
  absl::StatusOr<std::unique_ptr<const ResolvedNode>> Rewrite(
      const AnalyzerOptions& options, const ResolvedNode& input,
      Catalog& catalog, TypeFactory& type_factory,
      AnalyzerOutputProperties& output_properties) const override {
    GOOGLESQL_RET_CHECK_NE(options.id_string_pool(), nullptr);
    GOOGLESQL_RET_CHECK_NE(options.column_id_sequence_number(), nullptr);
    BuiltinFunctionInlinerVisitor rewriter(options, &catalog, &type_factory);
    GOOGLESQL_RETURN_IF_ERROR(input.Accept(&rewriter));
    return rewriter.ConsumeRootNode<ResolvedNode>();
  }

  std::string Name() const override { return "BuiltinFunctionInliner"; }

  // This rewriter is provided an unfiltered Catalog by the GoogleSQL analyzer,
  // and so filtering must be done by the rewriter itself to ensure that
  // non-builtin Catalog objects are not referenced. This is required as engines
  // may provide SQL templates that are handled by this rewriter, and those
  // templates may reference engine-builtin functions.
  bool ProvideUnfilteredCatalogToBuiltinRewriter() const override {
    return true;
  }
};

}  // namespace

const Rewriter* GetBuiltinFunctionInliner() {
  static const auto* const kRewriter = new BuiltinFunctionInliner;
  return kRewriter;
}

}  // namespace googlesql
