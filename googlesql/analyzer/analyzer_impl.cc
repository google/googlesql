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

#include "googlesql/analyzer/analyzer_impl.h"

#include <iostream>
#include <memory>
#include <ostream>
#include <utility>

#include <thread>

#include "googlesql/base/logging.h"
#include "googlesql/analyzer/analyzer_output_mutator.h"
#include "googlesql/analyzer/resolver.h"
#include "googlesql/analyzer/rewrite_resolved_ast.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/internal_analyzer_options.h"
#include "googlesql/common/timer_util.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/types/annotation.h"
#include "googlesql/public/types/type.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/validator.h"
#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/status_macros.h"

// This provides a way to extract and look at the googlesql resolved AST
// from within some other test or tool.  It prints to cout rather than logging
// because the output is often too big to log without truncating.
ABSL_FLAG(bool, googlesql_print_resolved_ast, false,
          "Print resolved AST to stdout after resolving (for debugging)");

namespace googlesql {

namespace {

absl::Status AnalyzeExpressionImpl(absl::string_view sql,
                                   const AnalyzerOptions& options_in,
                                   Catalog* catalog, TypeFactory* type_factory,
                                   AnnotatedType target_type,
                                   std::unique_ptr<AnalyzerOutput>* output) {
  output->reset();
  internal::TimedValue overall_timed_value;
  {
    auto scoped_timer = internal::MakeScopedTimerStarted(&overall_timed_value);

    GOOGLESQL_VLOG(1) << "Parsing expression:\n" << sql;
    std::unique_ptr<AnalyzerOptions> copy;
    const AnalyzerOptions& options = GetOptionsWithArenas(&options_in, &copy);
    GOOGLESQL_RETURN_IF_ERROR(ValidateAnalyzerOptions(options));

    std::unique_ptr<ParserOutput> parser_output;
    ParserOptions parser_options = options.GetParserOptions();
    GOOGLESQL_RETURN_IF_ERROR(ParseExpression(sql, parser_options, &parser_output));
    const ASTExpression* expression = parser_output->expression();
    GOOGLESQL_VLOG(5) << "Parsed AST:\n" << expression->DebugString();

    GOOGLESQL_RETURN_IF_ERROR(InternalAnalyzeExpressionFromParserAST(
        *expression, std::move(parser_output), sql, options, catalog,
        type_factory, target_type, output));
  }
  AnalyzerOutputMutator(*output).overall_timed_value().Accumulate(
      overall_timed_value);
  return absl::OkStatus();
}

}  // namespace

absl::Status InternalAnalyzeExpression(
    absl::string_view sql, const AnalyzerOptions& options, Catalog* catalog,
    TypeFactory* type_factory, AnnotatedType target_type,
    std::unique_ptr<AnalyzerOutput>* output) {
  return ConvertInternalErrorLocationAndAdjustErrorString(
      options.error_message_options(), sql,
      AnalyzeExpressionImpl(sql, options, catalog, type_factory, target_type,
                            output));
}

absl::Status ConvertExprToTargetType(
    const ASTExpression& ast_expression, absl::string_view sql,
    const AnalyzerOptions& analyzer_options, Catalog* catalog,
    TypeFactory* type_factory, AnnotatedType target_type,
    std::unique_ptr<const ResolvedExpr>* resolved_expr) {
  Resolver resolver(catalog, type_factory, &analyzer_options);
  return ConvertInternalErrorPayloadsToExternal(
      resolver.CoerceExprToType(&ast_expression, target_type,
                                Resolver::kImplicitAssignment, resolved_expr),
      sql);
}

absl::Status InternalAnalyzeExpressionFromParserAST(
    const ASTExpression& ast_expression,
    std::unique_ptr<ParserOutput> parser_output, absl::string_view sql,
    const AnalyzerOptions& options, Catalog* catalog, TypeFactory* type_factory,
    AnnotatedType target_type, std::unique_ptr<AnalyzerOutput>* output) {
  AnalyzerRuntimeInfo analyzer_runtime_info;
  if (parser_output != nullptr) {
    // Add in the parser output, we _assume_ this is semantically part of this
    // analyzer run, but this assumption may be incorrect,
    // see AnalyzerRuntimeInfo for more docs.
    analyzer_runtime_info.parser_runtime_info() = parser_output->runtime_info();
  }
  {
    auto overall_timer = internal::MakeScopedTimerStarted(
        &analyzer_runtime_info.overall_timed_value());

    std::unique_ptr<const ResolvedExpr> resolved_expr;
    Resolver resolver(catalog, type_factory, &options);
    {
      auto resolver_timer = internal::MakeScopedTimerStarted(
          &analyzer_runtime_info.resolver_timed_value());
      GOOGLESQL_RETURN_IF_ERROR(
          resolver.ResolveStandaloneExpr(sql, &ast_expression, &resolved_expr));
      GOOGLESQL_VLOG(3) << "Resolved AST:\n" << resolved_expr->DebugString();

      if (target_type.type != nullptr ||
          (target_type.type == nullptr &&
           target_type.annotation_map != nullptr)) {
        GOOGLESQL_RETURN_IF_ERROR(ConvertExprToTargetType(ast_expression, sql, options,
                                                catalog, type_factory,
                                                target_type, &resolved_expr));
      }
    }

    if (InternalAnalyzerOptions::GetValidateResolvedAST(options)) {
      internal::ScopedTimer scoped_validator_timer = MakeScopedTimerStarted(
          &analyzer_runtime_info.validator_timed_value());
      Validator validator(options.language());
      GOOGLESQL_RETURN_IF_ERROR(
          validator.ValidateStandaloneResolvedExpr(resolved_expr.get()));
    }

    if (absl::GetFlag(FLAGS_googlesql_print_resolved_ast)) {
      std::cout << "Resolved AST from thread "
                << std::this_thread::get_id()
                << ":" << '\n'
                << resolved_expr->DebugString() << '\n';
    }

    if (options.language().error_on_deprecated_syntax() &&
        !resolver.deprecation_warnings().empty()) {
      return resolver.deprecation_warnings().front();
    }

    // Make sure we're starting from a clean state for CheckFieldsAccessed.
    resolved_expr->ClearFieldsAccessed();

    GOOGLESQL_ASSIGN_OR_RETURN(const QueryParametersMap& type_assignments,
                     resolver.AssignTypesToUndeclaredParameters());

    *output = std::make_unique<AnalyzerOutput>(
        options.id_string_pool(), options.arena(), std::move(resolved_expr),
        resolver.analyzer_output_properties(), std::move(parser_output),
        ConvertInternalErrorLocationsAndAdjustErrorStrings(
            options.error_message_options(), sql,
            resolver.deprecation_warnings()),
        type_assignments, resolver.undeclared_positional_parameters(),
        resolver.max_column_id(), resolver.has_graph_references());
    GOOGLESQL_RETURN_IF_ERROR(InternalRewriteResolvedAst(options, sql, catalog,
                                               type_factory, **output));
  }

  AnalyzerOutputMutator(*output).mutable_runtime_info().AccumulateAll(
      analyzer_runtime_info);
  return absl::OkStatus();
}
}  // namespace googlesql
