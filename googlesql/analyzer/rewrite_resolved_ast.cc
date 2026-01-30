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

#include "googlesql/analyzer/rewrite_resolved_ast.h"

#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

#include "googlesql/base/atomic_sequence_num.h"
#include "googlesql/base/logging.h"
#include "googlesql/analyzer/analyzer_output_mutator.h"
#include "googlesql/analyzer/builtin_only_catalog.h"
#include "googlesql/analyzer/rewriters/registration.h"
#include "googlesql/analyzer/rewriters/rewriter_relevance_checker.h"
#include "googlesql/analyzer/rewriters/templated_function_call_rewriter.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/internal_analyzer_options.h"
#include "googlesql/common/timer_util.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/rewriter_interface.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/resolved_ast/resolved_node.h"
#include "googlesql/resolved_ast/validator.h"
#include "absl/algorithm/container.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/btree_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

namespace {

// Returns a ResolvedNode from AnalyzerOutput. This function assumes one
// of resolved_statement() and resolved_expr() is non-null and returns that.
const ResolvedNode* NodeFromAnalyzerOutput(const AnalyzerOutput& output) {
  if (output.resolved_statement() != nullptr) {
    return output.resolved_statement();
  }
  return output.resolved_expr();
}

// Returns an AnalyzerOptions suitable for passing to rewriters. Most of the
// settings are copied from <analyzer_options>, which is the options used to
// analyze the outer statement. Some settings are overridden as required by
// the rewriter implementation.
std::unique_ptr<AnalyzerOptions> AnalyzerOptionsForRewrite(
    const AnalyzerOptions& analyzer_options,
    const AnalyzerOutput& analyzer_output,
    googlesql_base::SequenceNumber& fallback_sequence_number) {
  auto options_for_rewrite =
      std::make_unique<AnalyzerOptions>(analyzer_options);

  // Require that rewrite substitution fragments are written in strict name
  // resolution mode so that column names are qualified. In theory, we could
  // relax this to DEFAULT at the cost of some robustness of the rewriting
  // rules. We cannot remove this line and allow the engine's selection to be
  // passed through. In that case, a rewriting rule written without column name
  // qualification might pass tests and work on most query engines but produce
  // incoherant error messages on engines that operate in strict resolution
  // mode.
  options_for_rewrite->mutable_language()->set_name_resolution_mode(
      NameResolutionMode::NAME_RESOLUTION_STRICT);

  // Turn on certain default features for all rewriters. This only affects the
  // rewriter itself, and does not impact the language feature set used when
  // resolving the user facing query. Only features which themselves can be
  // rewritten into basic SQL should be enabled.
  options_for_rewrite->mutable_language()->EnableLanguageFeature(
      FEATURE_UNNEST_AND_FLATTEN_ARRAYS);
  options_for_rewrite->mutable_language()->EnableLanguageFeature(
      FEATURE_WITH_EXPRESSION);

  // Rewriter fragment substitution uses named query parameters as an
  // implementation detail. We override settings that are required to enable
  // named query parameters.
  options_for_rewrite->set_allow_undeclared_parameters(false);
  options_for_rewrite->set_parameter_mode(ParameterMode::PARAMETER_NAMED);
  options_for_rewrite->set_statement_context(StatementContext::CONTEXT_DEFAULT);

  // Arenas are set to match those in <analyzer_output>, overriding any arenas
  // previously used by the AnalyzerOptions.
  options_for_rewrite->set_arena(analyzer_output.arena());
  options_for_rewrite->set_id_string_pool(analyzer_output.id_string_pool());

  // No internal GoogleSQL rewrites should depend on the expression columns
  // in the user-provided AnalyzerOptions. And, such expression columns might
  // conflict with columns used in AnalyzeSubstitute calls in various
  // ResolvedASTRewrite rules, which is an error. Therefore, we clear the
  // expression columns before executing rewriting.
  InternalAnalyzerOptions::ClearExpressionColumns(*options_for_rewrite);

  // If <analyzer_options> does not have a column_id_sequence_number(), sets the
  // sequence number to <fallback_sequence_number>. Also,
  // <fallback_sequence_number> is advanced until it is greater than
  // <analyzer_output.max_column_id()>. In this case, the
  // <fallback_sequence_number> must outlive the returned options.
  if (analyzer_options.column_id_sequence_number() == nullptr) {
    // Advance the sequence number so that the column ids generated are unique
    // with respect to the AnalyzerOutput so far.
    while (fallback_sequence_number.GetNext() <
           analyzer_output.max_column_id()) {
    }
    options_for_rewrite->set_column_id_sequence_number(
        &fallback_sequence_number);
  }
  return options_for_rewrite;
}

absl::StatusOr<std::unique_ptr<const ResolvedNode>>
ApplyBuiltinRewritersToFixedPoint(
    const AnalyzerOptions& analyzer_options,
    AnalyzerRuntimeInfo& runtime_info, Catalog* catalog,
    TypeFactory& type_factory, AnalyzerOutputProperties& output_properties,
    std::unique_ptr<const ResolvedNode> rewriter_input, bool& rewrite_applied) {
  GOOGLESQL_ASSIGN_OR_RETURN(
      absl::btree_set<ResolvedASTRewrite> detected_rewrites,
      FindRelevantRewriters(rewriter_input.get(),
                            /*check_templated_function_calls=*/false));

  if (detected_rewrites.empty()) {
    return std::move(rewriter_input);
  }

  absl::btree_set<ResolvedASTRewrite> rewrites_to_apply;
  absl::c_set_intersection(
      analyzer_options.enabled_rewrites(), detected_rewrites,
      std::inserter(rewrites_to_apply, rewrites_to_apply.end()));

  if (rewrites_to_apply.empty()) {
    return std::move(rewriter_input);
  }

  auto filtered_catalog =
      std::make_unique<BuiltinOnlyCatalog>("builtin_catalog", *catalog);

  int64_t iterations = 0;
  // The default value is not meant to be restrictive, and should be increased
  // when enough features are rewrite driven that valid queries approach this
  // number of rewriter iterations.
  // TODO: Make this an AnalyzerOption before removing
  //     in_development from inlining rules.
  static const int64_t kMaxIterations = 25;
  do {
    if (++iterations > kMaxIterations) {
      // The maximum number of iterations is controlled by a flag that engines
      // can set
      return absl::ResourceExhaustedError(absl::StrCat(
          "Query exceeded configured maximum number of rewriter iterations (",
          kMaxIterations, ") without converging."));
    }

    const RewriteRegistry& rewrite_registry =
        RewriteRegistry::global_instance();
    for (ResolvedASTRewrite ast_rewrite :
         rewrite_registry.registration_order()) {
      if (!rewrites_to_apply.contains(ast_rewrite)) {
        continue;
      }
      const Rewriter* rewriter =
          RewriteRegistry::global_instance().Get(ast_rewrite);
      GOOGLESQL_RET_CHECK(rewriter != nullptr)
          << "Requested rewriter was not present in the registry: "
          << ResolvedASTRewrite_Name(ast_rewrite);

      AnalyzerRuntimeInfo::RewriterDetails& runtime_rewriter_details =
          runtime_info.rewriters_details(ast_rewrite);
      internal::ScopedTimer rewriter_details_scoped_timer =
          MakeScopedTimerStarted(&runtime_rewriter_details.timed_value);
      runtime_rewriter_details.count++;

      GOOGLESQL_VLOG(2) << "Running rewriter " << rewriter->Name();
      // By default, provide builtin rewriters with a filtered catalog which
      // prevents them from resolving non-builtin Catalog objects. However,
      // some rewriters disable this filtering, and so provide the unfiltered
      // catalog in that case.
      Catalog* catalog_for_rewrite;
      if (analyzer_options.language().LanguageFeatureEnabled(
              FEATURE_DISABLE_VALIDATE_REWRITERS_REFER_TO_BUILTINS) ||
          rewriter->ProvideUnfilteredCatalogToBuiltinRewriter()) {
        catalog_for_rewrite = catalog;
      } else {
        catalog_for_rewrite = filtered_catalog.get();
      }
      GOOGLESQL_ASSIGN_OR_RETURN(
          rewriter_input,
          rewriter->Rewrite(analyzer_options, std::move(rewriter_input),
                            *catalog_for_rewrite, type_factory,
                            output_properties));
      rewrite_applied = true;

      GOOGLESQL_RET_CHECK(rewriter_input != nullptr)
          << "Rewriter " << rewriter->Name() << " returned nullptr on input\n";

      // For the time being, any rewriter that we call Rewrite on is making
      // meaningful changes to the ResolvedAST tree, so we unconditionally
      // record that it activates. When rewriters are cheaper on no-op, that
      // will likely change such that a Rewriter might choose not to change
      // anything when Rewrite is called. In that case, we need to let Rewrite
      // signal that it made no meaningful change.
      // TODO: Add a way for Rewrite to signal that it made no
      //     meaningful change.
    }

    rewrites_to_apply.clear();
    GOOGLESQL_ASSIGN_OR_RETURN(
        absl::btree_set<ResolvedASTRewrite> checker_detected_rewrites,
        FindRelevantRewriters(rewriter_input.get(),
                              /*check_templated_function_calls=*/false));
    absl::c_set_intersection(
        analyzer_options.enabled_rewrites(), checker_detected_rewrites,
        std::inserter(rewrites_to_apply, rewrites_to_apply.end()));
    // The checker currently cannot distinguish the output of the
    // anonymization rewriter from its input.
    // TODO: Improve the checker to avoid false positives.
    rewrites_to_apply.erase(REWRITE_ANONYMIZATION);
    // TODO: Add a rewrite state enum to remove this. Currently
    // needed to halt rewriter iterations before resource is exhausted.
    rewrites_to_apply.erase(REWRITE_AGGREGATION_THRESHOLD);
  } while (!rewrites_to_apply.empty());

  return std::move(rewriter_input);
}

absl::StatusOr<bool> TemplatedFunctionCallsNeedRewrite(
    const AnalyzerOptions& analyzer_options,
    const ResolvedNode* rewrite_input) {
  if (!analyzer_options.enabled_rewrites().contains(
          ResolvedASTRewrite::
              REWRITE_APPLY_ENABLED_REWRITES_TO_TEMPLATED_FUNCTION_CALLS)) {
    return false;
  }

  // This recursively checks nested templated function calls for rewrites.
  GOOGLESQL_ASSIGN_OR_RETURN(absl::btree_set<ResolvedASTRewrite> detected_rewrites,
                   FindRelevantRewriters(
                       rewrite_input, /*check_templated_function_calls=*/true));

  if (detected_rewrites.empty()) {
    return false;
  }

  // Check at least one registered rewriter is detected and enabled.
  const RewriteRegistry& rewrite_registry = RewriteRegistry::global_instance();
  for (ResolvedASTRewrite ast_rewrite : rewrite_registry.registration_order()) {
    if (detected_rewrites.contains(ast_rewrite) &&
        analyzer_options.enabled_rewrites().contains(ast_rewrite)) {
      return true;
    }
  }
  return false;
}

// Applies leading rewriters, built-in rewriters, and then trailing rewriters.
// Leading and trailing rewriters are engine specified and are only run once.
// Built-in rewriters are run until a fixed point or a maximum number of
// iterations is reached.
absl::StatusOr<std::unique_ptr<const ResolvedNode>> ApplyRewriters(
    const AnalyzerOptions& analyzer_options,
    AnalyzerRuntimeInfo& runtime_info, Catalog* catalog,
    TypeFactory& type_factory, AnalyzerOutputProperties& output_properties,
    std::unique_ptr<const ResolvedNode> rewriter_input, bool& rewrite_applied) {
  // Run non-built-in rewriters. Each of these rewriters is run only once.
  for (const std::shared_ptr<Rewriter>& rewriter :
       analyzer_options.leading_rewriters()) {
    rewrite_applied = true;
    GOOGLESQL_ASSIGN_OR_RETURN(
        rewriter_input,
        rewriter->Rewrite(analyzer_options, std::move(rewriter_input), *catalog,
                          type_factory, output_properties));
  }

  GOOGLESQL_ASSIGN_OR_RETURN(
      rewriter_input,
      ApplyBuiltinRewritersToFixedPoint(
          analyzer_options, runtime_info, catalog, type_factory,
          output_properties, std::move(rewriter_input), rewrite_applied));

  // Run non-built-in rewriters. Each of these rewriters is run only once.
  for (const std::shared_ptr<Rewriter>& rewriter :
       analyzer_options.trailing_rewriters()) {
    GOOGLESQL_ASSIGN_OR_RETURN(
        rewriter_input,
        rewriter->Rewrite(analyzer_options, std::move(rewriter_input), *catalog,
                          type_factory, output_properties));
    rewrite_applied = true;
  }
  return rewriter_input;
}

}  // namespace

namespace {
absl::Status InternalRewriteResolvedAstNoConvertErrorLocation(
    const AnalyzerOptions& analyzer_options, Catalog* catalog,
    TypeFactory* type_factory, AnalyzerOutput& analyzer_output) {
  AnalyzerOutputMutator output_mutator(&analyzer_output);
  AnalyzerRuntimeInfo& runtime_info = output_mutator.mutable_runtime_info();

  // Start a timer and store the value on return.
  internal::ElapsedTimer rewriter_timer = internal::MakeTimerStarted();
  absl::Cleanup accumulate_rewriter_timer = [&runtime_info, &rewriter_timer]() {
    runtime_info.rewriters_timed_value().Accumulate(rewriter_timer);
  };

  GOOGLESQL_VLOG(3) << "Enabled rewriters: "
          << absl::StrJoin(analyzer_options.enabled_rewrites(), " ",
                           [](std::string* s, ResolvedASTRewrite r) {
                             absl::StrAppend(s, ResolvedASTRewrite_Name(r));
                           });

  googlesql_base::SequenceNumber fallback_sequence_number;
  std::unique_ptr<const ResolvedNode> last_rewrite_result =
      output_mutator.release_output_node();
  std::unique_ptr<AnalyzerOptions> options_for_rewrite =
      AnalyzerOptionsForRewrite(analyzer_options, analyzer_output,
                                fallback_sequence_number);

  // Execute built-in rewriters until a fixed point or a maximum number of
  // iterations is reached.
  bool any_rewriters_applied = false;
  GOOGLESQL_ASSIGN_OR_RETURN(
      last_rewrite_result,
      ApplyRewriters(*options_for_rewrite, runtime_info, catalog, *type_factory,
                     output_mutator.mutable_output_properties(),
                     std::move(last_rewrite_result), any_rewriters_applied));

  // Rewrite templated function calls if needed.
  GOOGLESQL_ASSIGN_OR_RETURN(const bool templated_function_calls_need_rewrite,
                   TemplatedFunctionCallsNeedRewrite(
                       *options_for_rewrite, last_rewrite_result.get()));
  if (templated_function_calls_need_rewrite) {
    GOOGLESQL_ASSIGN_OR_RETURN(last_rewrite_result,
                     RewriteTemplatedFunctionCalls(
                         *options_for_rewrite,
                         [&](const AnalyzerOptions&
                                 options_for_templated_function_call_rewrite,
                             std::unique_ptr<const ResolvedNode> node) {
                           return ApplyRewriters(
                               options_for_templated_function_call_rewrite,
                               runtime_info, catalog, *type_factory,
                               output_mutator.mutable_output_properties(),
                               std::move(node), any_rewriters_applied);
                         },
                         std::move(last_rewrite_result)));
  }

  // Accumulate the rewriter timer before running the validator.
  std::move(accumulate_rewriter_timer).Invoke();

  // Do not mutate `column_id_sequence_number` if no rewriters were applied.
  const int max_column_id =
      any_rewriters_applied
          ? static_cast<int>(
                options_for_rewrite->column_id_sequence_number()->GetNext() - 1)
          : analyzer_output.max_column_id();

  GOOGLESQL_RETURN_IF_ERROR(
      output_mutator.Update(std::move(last_rewrite_result), max_column_id));

  if (any_rewriters_applied) {
    if (InternalAnalyzerOptions::GetValidateResolvedAST(*options_for_rewrite)) {
      internal::ScopedTimer validator_scoped_timer =
          MakeScopedTimerStarted(&runtime_info.validator_timed_value());
      // Make sure the generated ResolvedAST is valid.
      ValidatorOptions validator_options{
          .allowed_hints_and_options =
              analyzer_options.allowed_hints_and_options()};
      Validator validator(analyzer_options.language(), validator_options);
      if (analyzer_output.resolved_statement() != nullptr) {
        GOOGLESQL_RETURN_IF_ERROR(validator.ValidateResolvedStatement(
            analyzer_output.resolved_statement()));
      } else {
        GOOGLESQL_RET_CHECK(analyzer_output.resolved_expr() != nullptr);
        GOOGLESQL_RETURN_IF_ERROR(validator.ValidateStandaloneResolvedExpr(
            analyzer_output.resolved_expr()));
      }
    }
    if (analyzer_options.fields_accessed_mode() ==
        AnalyzerOptions::FieldsAccessedMode::LEGACY_FIELDS_ACCESSED_MODE) {
      const ResolvedNode* node = NodeFromAnalyzerOutput(analyzer_output);
      if (node != nullptr) {
        node->MarkFieldsAccessed();
      }
    }
  }
  GOOGLESQL_RET_CHECK(analyzer_output.resolved_statement() != nullptr ||
            analyzer_output.resolved_expr() != nullptr);
  return absl::OkStatus();
}

}  // namespace

absl::Status InternalRewriteResolvedAst(const AnalyzerOptions& analyzer_options,
                                        absl::string_view sql, Catalog* catalog,
                                        TypeFactory* type_factory,
                                        AnalyzerOutput& analyzer_output) {
  if (analyzer_options.pre_rewrite_callback() != nullptr) {
    GOOGLESQL_RETURN_IF_ERROR(analyzer_options.pre_rewrite_callback()(analyzer_output));
  }

  if (analyzer_options.enabled_rewrites().empty() ||
      (analyzer_output.resolved_statement() == nullptr &&
       analyzer_output.resolved_expr() == nullptr)) {
    return absl::OkStatus();
  }

  return ConvertInternalErrorLocationAndAdjustErrorString(
      analyzer_options.error_message_options(), sql,
      InternalRewriteResolvedAstNoConvertErrorLocation(
          analyzer_options, catalog, type_factory, analyzer_output));
}

}  // namespace googlesql
