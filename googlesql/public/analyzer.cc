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

#include "googlesql/public/analyzer.h"

#include <iostream>
#include <memory>
#include <thread>
#include <ostream>
#include <string>
#include <utility>

#include "googlesql/base/logging.h"
#include "googlesql/analyzer/all_rewriters.h"
#include "googlesql/analyzer/analyzer_impl.h"
#include "googlesql/analyzer/analyzer_output_mutator.h"
#include "googlesql/analyzer/function_resolver.h"
#include "googlesql/analyzer/resolver.h"
#include "googlesql/analyzer/rewrite_resolved_ast.h"
#include "googlesql/analyzer/rewriters/anonymization_rewriter.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/internal_analyzer_options.h"
#include "googlesql/common/status_payload_utils.h"
#include "googlesql/common/timer_util.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/cycle_detector.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_helpers.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/public/table_name_resolver.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/resolved_ast/column_factory.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_node_kind.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/resolved_ast/validator.h"
#include "absl/base/nullability.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

// This provides a way to extract and look at the googlesql resolved AST
// from within some other test or tool.  It prints to cout rather than logging
// because the output is often too big to log without truncating.
ABSL_DECLARE_FLAG(bool, googlesql_print_resolved_ast);

namespace googlesql {

// Sets <has_default_resolution_time> to true for every table name in
// 'table_names' if it does not have "FOR SYSTEM_TIME AS OF" expression.
static void EnsureResolutionTimeInfoForEveryTable(
    const TableNamesSet& table_names,
    TableResolutionTimeInfoMap* table_resolution_time_info_map) {
  for (const auto& table_name : table_names) {
    TableResolutionTimeInfo& expressions =
        (*table_resolution_time_info_map)[table_name];
    if (expressions.exprs.empty()) {
      expressions.has_default_resolution_time = true;
    }
  }
}

static absl::Status RewriteResolvedAstImpl(
    const AnalyzerOptions& analyzer_options, absl::string_view sql,
    Catalog* catalog, TypeFactory* type_factory,
    AnalyzerOutput& analyzer_output) {
  // InternalRewriteResolvedAst cannot call RegisterBuiltinRewriters because it
  // would create a dependency cycle.
  RegisterBuiltinRewriters();
  return InternalRewriteResolvedAst(analyzer_options, sql, catalog,
                                    type_factory, analyzer_output);
}

// Common post-parsing work for AnalyzeStatement() series.
static absl::Status FinishResolveStatementImpl(
    absl::string_view sql, const ASTStatement& ast_statement,
    Resolver* resolver, const AnalyzerOptions& options, Catalog* catalog,
    TypeFactory* type_factory, AnalyzerRuntimeInfo* analyzer_runtime_info,
    std::unique_ptr<const ResolvedStatement>* resolved_statement) {
  GOOGLESQL_VLOG(5) << "Parsed AST:\n" << ast_statement.DebugString();
  {
    internal::ScopedTimer scoped_resolver_timer =
        internal::MakeScopedTimerStarted(
            &analyzer_runtime_info->resolver_timed_value());
    GOOGLESQL_RETURN_IF_ERROR(
        resolver->ResolveStatement(sql, &ast_statement, resolved_statement));
  }
  GOOGLESQL_VLOG(3) << "Resolved AST:\n" << (*resolved_statement)->DebugString();

  if (InternalAnalyzerOptions::GetValidateResolvedAST(options)) {
    internal::ScopedTimer scoped_validator_timer =
        internal::MakeScopedTimerStarted(
            &analyzer_runtime_info->validator_timed_value());
    ValidatorOptions validator_options{.allowed_hints_and_options =
                                           options.allowed_hints_and_options()};
    Validator validator(options.language(), std::move(validator_options));
    GOOGLESQL_RETURN_IF_ERROR(
        validator.ValidateResolvedStatement(resolved_statement->get()));
  }

  internal::ScopedTimer scoped_resolver_timer =
      internal::MakeScopedTimerStarted(
          &analyzer_runtime_info->resolver_timed_value());

  if (absl::GetFlag(FLAGS_googlesql_print_resolved_ast)) {
    std::cout << "Resolved AST from thread "
              << std::this_thread::get_id()
              << ":" << std::endl
              << (*resolved_statement)->DebugString() << std::endl;
  }

  if (options.language().error_on_deprecated_syntax() &&
      !resolver->deprecation_warnings().empty()) {
    return resolver->deprecation_warnings().front();
  }

  if (options.fields_accessed_mode() ==
      AnalyzerOptions::FieldsAccessedMode::LEGACY_FIELDS_ACCESSED_MODE) {
    // In legacy mode, we maintain this exact (buggy) early call to clear.
    (*resolved_statement)->ClearFieldsAccessed();
  }

  return absl::OkStatus();
}

static absl::Status UnsupportedStatementErrorOrStatus(
    const absl::Status& status, const ParseResumeLocation& resume_location,
    const AnalyzerOptions& options) {
  // TODO: b/196226376 - Change this function so that it cannot hide errors that
  //                     are not kIllegalArgument.
  GOOGLESQL_RET_CHECK(!status.ok()) << "Expected an error status";
  const ResolvedNodeKind kind = GetNextStatementKind(resume_location);
  if (kind != RESOLVED_LITERAL
      && !options.language().SupportsStatementKind(kind)) {
    ParseLocationPoint location_point = ParseLocationPoint::FromByteOffset(
        resume_location.filename(), resume_location.byte_position());
    return ConvertInternalErrorLocationAndAdjustErrorString(
        options.error_message_options(), resume_location.input(),
        MakeSqlErrorAtPoint(location_point)
            << "Statement not supported: " << ResolvedNodeKindToString(kind));
  }
  return status;
}

static absl::Status AnalyzeStatementImpl(
    absl::string_view sql, const AnalyzerOptions& options, Catalog* catalog,
    TypeFactory* type_factory, std::unique_ptr<const AnalyzerOutput>* output) {
  output->reset();
  internal::TimedValue overall_timed_value;
  {
    auto scoped_timer = internal::MakeScopedTimerStarted(&overall_timed_value);

    GOOGLESQL_RETURN_IF_ERROR(ValidateAnalyzerOptions(options));

    GOOGLESQL_VLOG(1) << "Parsing statement:\n" << sql;
    std::unique_ptr<ParserOutput> parser_output;
    absl::Status status =
        ParseStatement(sql, options.GetParserOptions(), &parser_output);
    if (!status.ok()) {
      return UnsupportedStatementErrorOrStatus(
          status, ParseResumeLocation::FromStringView(sql), options);
    }

    GOOGLESQL_RETURN_IF_ERROR(AnalyzeStatementFromParserOutputOwnedOnSuccess(
        &parser_output, options, sql, catalog, type_factory, output));
  }
  (*output)->runtime_info().overall_timed_value().Accumulate(
      overall_timed_value);
  return absl::OkStatus();
}

absl::Status AnalyzeStatement(
    absl::string_view sql, const AnalyzerOptions& options_in,
    Catalog* /*absl_nonnull*/ catalog, TypeFactory* /*absl_nonnull*/ type_factory,
    std::unique_ptr<const AnalyzerOutput>* /*absl_nonnull*/ output) {
  std::unique_ptr<AnalyzerOptions> copy;
  const AnalyzerOptions& options = GetOptionsWithArenas(&options_in, &copy);
  return AnalyzeStatementImpl(sql, options, catalog, type_factory, output);
}

static absl::Status AnalyzeNextStatementImpl(
    ParseResumeLocation* resume_location, const AnalyzerOptions& options,
    Catalog* catalog, TypeFactory* type_factory,
    std::unique_ptr<const AnalyzerOutput>* output, bool* at_end_of_input) {
  output->reset();
  GOOGLESQL_RETURN_IF_ERROR(ValidateAnalyzerOptions(options));

  if (resume_location->byte_position() == 0) {
    GOOGLESQL_VLOG(1) << "Parsing first statement from:\n" << resume_location->input();
  } else {
    GOOGLESQL_VLOG(2) << "Parsing next statement at position "
            << resume_location->byte_position();
  }

  std::unique_ptr<ParserOutput> parser_output;
  absl::Status status =
      ParseNextStatement(resume_location, options.GetParserOptions(),
                         &parser_output, at_end_of_input);
  if (!status.ok()) {
    return UnsupportedStatementErrorOrStatus(status, *resume_location, options);
  }
  GOOGLESQL_RET_CHECK(parser_output != nullptr);

  return AnalyzeStatementFromParserOutputOwnedOnSuccess(
      &parser_output, options, resume_location->input(), catalog, type_factory,
      output);
}

absl::Status AnalyzeNextStatement(
    ParseResumeLocation* /*absl_nonnull*/ resume_location,
    const AnalyzerOptions& options_in, Catalog* /*absl_nonnull*/ catalog,
    TypeFactory* /*absl_nonnull*/ type_factory,
    std::unique_ptr<const AnalyzerOutput>* /*absl_nonnull*/ output,
    bool* /*absl_nonnull*/ at_end_of_input) {
  std::unique_ptr<AnalyzerOptions> copy;
  const AnalyzerOptions& options = GetOptionsWithArenas(&options_in, &copy);
  return AnalyzeNextStatementImpl(resume_location, options, catalog,
                                  type_factory, output, at_end_of_input);
}

// All AnalyzeStatement* APIs eventually call through this method, so this
// is the right place to do any required setup of per-request AnalyzerOptions.
// This takes an `owned_options` which is an owned pointer for `options`, used
// if `options` has already been copied, so we can avoid copying it again if we
// need to mutate it further here.
static absl::Status AnalyzeStatementHelper(
    const ASTStatement& ast_statement, const AnalyzerOptions* options,
    std::unique_ptr<AnalyzerOptions> owned_options, absl::string_view sql,
    Catalog* catalog, TypeFactory* type_factory,
    std::unique_ptr<ParserOutput>* statement_parser_output,
    bool take_parser_output_ownership_on_success,
    std::unique_ptr<AnalyzerOutput>* output) {
  output->reset();
  if (owned_options != nullptr) {
    GOOGLESQL_RET_CHECK_EQ(options, owned_options.get());
  }

  AnalyzerRuntimeInfo analyzer_runtime_info;
  {
    std::unique_ptr<CycleDetector> local_cycle_detector;
    if (options->find_options().cycle_detector() == nullptr) {
      if (owned_options == nullptr) {
        owned_options = std::make_unique<AnalyzerOptions>(*options);
        options = owned_options.get();
      }

      local_cycle_detector = std::make_unique<CycleDetector>();
      owned_options->mutable_find_options()->set_cycle_detector(
          local_cycle_detector.get());
    }

    internal::ScopedTimer scoped_overall_timer =
        internal::MakeScopedTimerStarted(
            &analyzer_runtime_info.overall_timed_value());

    GOOGLESQL_RET_CHECK(options->AllArenasAreInitialized());
    std::unique_ptr<const ResolvedStatement> resolved_statement;
    auto resolver = std::make_unique<Resolver>(catalog, type_factory, options);
    absl::Status status = FinishResolveStatementImpl(
        sql, ast_statement, resolver.get(), *options, catalog, type_factory,
        &analyzer_runtime_info, &resolved_statement);

    const absl::StatusOr<QueryParametersMap>& type_assignments =
        resolver->AssignTypesToUndeclaredParameters();

    internal::UpdateStatus(&status, type_assignments.status());
    if (!status.ok()) {
      return ConvertInternalErrorLocationAndAdjustErrorString(
          options->error_message_options(), sql, status);
    }

    std::unique_ptr<ParserOutput> owned_parser_output;
    if (take_parser_output_ownership_on_success) {
      owned_parser_output = std::move(*statement_parser_output);
      analyzer_runtime_info.parser_runtime_info().AccumulateAll(
          owned_parser_output->runtime_info());
    }

    *output = std::make_unique<AnalyzerOutput>(
        options->id_string_pool(), options->arena(),
        std::move(resolved_statement), resolver->analyzer_output_properties(),
        std::move(owned_parser_output),
        ConvertInternalErrorLocationsAndAdjustErrorStrings(
            options->error_message_options(), sql,
            resolver->deprecation_warnings()),
        *type_assignments, resolver->undeclared_positional_parameters(),
        resolver->max_column_id(), resolver->has_graph_references());
    GOOGLESQL_RETURN_IF_ERROR(
        RewriteResolvedAstImpl(*options, sql, catalog, type_factory, **output));
    if (options->fields_accessed_mode() ==
        AnalyzerOptions::FieldsAccessedMode::CLEAR_FIELDS) {
      // Always clear fields before return.
      AnalyzerOutputMutator(*output).resolved_node()->ClearFieldsAccessed();
    }
  }
  AnalyzerOutputMutator(*output).mutable_runtime_info().AccumulateAll(
      analyzer_runtime_info);
  return absl::OkStatus();
}

static absl::Status AnalyzeStatementFromParserOutputImpl(
    std::unique_ptr<ParserOutput>* statement_parser_output,
    bool take_ownership_on_success, const AnalyzerOptions& input_options,
    absl::string_view sql, Catalog* catalog, TypeFactory* type_factory,
    std::unique_ptr<AnalyzerOutput>* output) {
  const AnalyzerOptions* options = &input_options;
  std::unique_ptr<AnalyzerOptions> local_options;

  // Only copy the AnalyzerOptions if necessary so we can mutate it.
  if (options->arena() == nullptr || options->id_string_pool() == nullptr) {
    local_options = std::make_unique<AnalyzerOptions>(*options);

    // If the arena and IdStringPool are not set in <options>, use the
    // arena and IdStringPool from the parser output by default.
    if (options->arena() == nullptr) {
      GOOGLESQL_RET_CHECK((*statement_parser_output)->arena() != nullptr);
      local_options->set_arena((*statement_parser_output)->arena());
    }
    if (options->id_string_pool() == nullptr) {
      GOOGLESQL_RET_CHECK((*statement_parser_output)->id_string_pool() != nullptr);
      local_options->set_id_string_pool(
          (*statement_parser_output)->id_string_pool());
    }
    options = local_options.get();
  }

  const ASTStatement* ast_statement = (*statement_parser_output)->statement();
  return AnalyzeStatementHelper(
      *ast_statement, options, std::move(local_options), sql, catalog,
      type_factory, statement_parser_output, take_ownership_on_success, output);
}

absl::Status AnalyzeStatementFromParserOutputOwnedOnSuccess(
    std::unique_ptr<ParserOutput>* /*absl_nonnull*/ statement_parser_output,
    const AnalyzerOptions& options, absl::string_view sql,
    Catalog* /*absl_nonnull*/ catalog, TypeFactory* /*absl_nonnull*/ type_factory,
    std::unique_ptr<const AnalyzerOutput>* /*absl_nonnull*/ output) {
  std::unique_ptr<AnalyzerOutput> mutable_output;
  GOOGLESQL_RETURN_IF_ERROR(AnalyzeStatementFromParserOutputImpl(
      statement_parser_output, /*take_ownership_on_success=*/true, options, sql,
      catalog, type_factory, &mutable_output));
  GOOGLESQL_ASSIGN_OR_RETURN(*output, AnalyzerOutputMutator::FinalizeAnalyzerOutput(
                                std::move(mutable_output)));
  return absl::OkStatus();
}

absl::Status AnalyzeStatementFromParserOutputUnowned(
    std::unique_ptr<ParserOutput>* /*absl_nonnull*/ statement_parser_output,
    const AnalyzerOptions& options, absl::string_view sql,
    Catalog* /*absl_nonnull*/ catalog, TypeFactory* /*absl_nonnull*/ type_factory,
    std::unique_ptr<const AnalyzerOutput>* /*absl_nonnull*/ output) {
  std::unique_ptr<AnalyzerOutput> mutable_output;
  GOOGLESQL_RETURN_IF_ERROR(AnalyzeStatementFromParserOutputImpl(
      statement_parser_output, /*take_ownership_on_success=*/false, options,
      sql, catalog, type_factory, &mutable_output));
  GOOGLESQL_ASSIGN_OR_RETURN(*output, AnalyzerOutputMutator::FinalizeAnalyzerOutput(
                                std::move(mutable_output)));
  return absl::OkStatus();
}

absl::Status AnalyzeStatementFromParserAST(
    const ASTStatement& statement, const AnalyzerOptions& options,
    absl::string_view sql, Catalog* /*absl_nonnull*/ catalog,
    TypeFactory* /*absl_nonnull*/ type_factory,
    std::unique_ptr<const AnalyzerOutput>* /*absl_nonnull*/ output) {
  std::unique_ptr<AnalyzerOptions> local_options;
  const AnalyzerOptions& options_with_arenas =
      GetOptionsWithArenas(&options, &local_options);
  std::unique_ptr<AnalyzerOutput> mutable_output;
  GOOGLESQL_RETURN_IF_ERROR(AnalyzeStatementHelper(
      statement, &options_with_arenas, std::move(local_options), sql, catalog,
      type_factory,
      /*statement_parser_output=*/nullptr,
      /*take_parser_output_ownership_on_success=*/false, &mutable_output));
  GOOGLESQL_ASSIGN_OR_RETURN(*output, AnalyzerOutputMutator::FinalizeAnalyzerOutput(
                                std::move(mutable_output)));
  return absl::OkStatus();
}

absl::Status AnalyzeExpression(
    absl::string_view sql, const AnalyzerOptions& options,
    Catalog* /*absl_nonnull*/ catalog, TypeFactory* /*absl_nonnull*/ type_factory,
    std::unique_ptr<const AnalyzerOutput>* /*absl_nonnull*/ output) {
  // The internal analyzer cannot call RegisterBuiltinRewriters because it
  // would create a dependency cycle.
  RegisterBuiltinRewriters();
  std::unique_ptr<AnalyzerOutput> mutable_output;
  GOOGLESQL_RETURN_IF_ERROR(InternalAnalyzeExpression(
      sql, options, catalog, type_factory,
      AnnotatedType(/*type=*/nullptr, /*annotation_map=*/nullptr),
      &mutable_output));
  GOOGLESQL_ASSIGN_OR_RETURN(*output, AnalyzerOutputMutator::FinalizeAnalyzerOutput(
                                std::move(mutable_output)));
  return absl::OkStatus();
}

absl::Status AnalyzeExpressionForAssignmentToType(
    absl::string_view sql, const AnalyzerOptions& options,
    Catalog* /*absl_nonnull*/ catalog, TypeFactory* /*absl_nonnull*/ type_factory,
    const Type* /*absl_nullable*/ target_type,
    std::unique_ptr<const AnalyzerOutput>* /*absl_nonnull*/ output) {
  // The internal analyzer cannot call RegisterBuiltinRewriters because it
  // would create a dependency cycle.
  RegisterBuiltinRewriters();
  std::unique_ptr<AnalyzerOutput> mutable_output;
  GOOGLESQL_RETURN_IF_ERROR(InternalAnalyzeExpression(
      sql, options, catalog, type_factory,
      AnnotatedType(target_type, /*annotation_map=*/nullptr), &mutable_output));
  GOOGLESQL_ASSIGN_OR_RETURN(*output, AnalyzerOutputMutator::FinalizeAnalyzerOutput(
                                std::move(mutable_output)));
  return absl::OkStatus();
}

absl::Status AnalyzeExpressionFromParserAST(
    const ASTExpression& ast_expression, const AnalyzerOptions& options_in,
    absl::string_view sql, TypeFactory* /*absl_nonnull*/ type_factory,
    Catalog* /*absl_nonnull*/ catalog,
    std::unique_ptr<const AnalyzerOutput>* /*absl_nonnull*/ output) {
  return AnalyzeExpressionFromParserASTForAssignmentToType(
      ast_expression, options_in, sql, type_factory, catalog,
      /*target_type=*/nullptr, output);
}

absl::Status AnalyzeExpressionFromParserASTForAssignmentToType(
    const ASTExpression& ast_expression, const AnalyzerOptions& options_in,
    absl::string_view sql, TypeFactory* /*absl_nonnull*/ type_factory,
    Catalog* /*absl_nonnull*/ catalog, const Type* /*absl_nullable*/ target_type,
    std::unique_ptr<const AnalyzerOutput>* /*absl_nonnull*/ output) {
  std::unique_ptr<AnalyzerOptions> copy;
  const AnalyzerOptions& options = GetOptionsWithArenas(&options_in, &copy);
  // The internal analyzer cannot call RegisterBuiltinRewriters because it
  // would create a dependency cycle.
  RegisterBuiltinRewriters();
  std::unique_ptr<AnalyzerOutput> mutable_output;
  const absl::Status status = InternalAnalyzeExpressionFromParserAST(
      ast_expression, /*parser_output=*/nullptr, sql, options, catalog,
      type_factory, AnnotatedType(target_type, /*annotation_map=*/nullptr),
      &mutable_output);
  if (status.ok()) {
    GOOGLESQL_ASSIGN_OR_RETURN(*output, AnalyzerOutputMutator::FinalizeAnalyzerOutput(
                                  std::move(mutable_output)));
  }
  return ConvertInternalErrorLocationAndAdjustErrorString(
      options.error_message_options(), sql, status);
}

static absl::Status AnalyzeTypeImpl(const std::string& type_name,
                                    const AnalyzerOptions& options,
                                    Catalog* catalog, TypeFactory* type_factory,
                                    const Type** output_type,
                                    TypeModifiers* output_type_modifiers) {
  *output_type = nullptr;

  GOOGLESQL_RETURN_IF_ERROR(ValidateAnalyzerOptions(options));
  GOOGLESQL_VLOG(1) << "Resolving type: " << type_name;

  Resolver resolver(catalog, type_factory, &options);
  GOOGLESQL_RETURN_IF_ERROR(
      resolver.ResolveTypeName(type_name, output_type, output_type_modifiers));

  GOOGLESQL_VLOG(3) << "Resolved type: " << (*output_type)->DebugString();
  return absl::OkStatus();
}

absl::Status AnalyzeType(const std::string& type_name,
                         const AnalyzerOptions& options_in,
                         Catalog* /*absl_nonnull*/ catalog,
                         TypeFactory* /*absl_nonnull*/ type_factory,
                         const Type** /*absl_nonnull*/ output_type) {
  TypeModifiers type_modifiers;
  return AnalyzeType(type_name, options_in, catalog, type_factory, output_type,
                     &type_modifiers);
}

absl::Status AnalyzeType(const std::string& type_name,
                         const AnalyzerOptions& options_in,
                         Catalog* /*absl_nonnull*/ catalog,
                         TypeFactory* /*absl_nonnull*/ type_factory,
                         const Type** /*absl_nonnull*/ output_type,
                         TypeModifiers* /*absl_nonnull*/ output_type_modifiers) {
  std::unique_ptr<AnalyzerOptions> copy;
  const AnalyzerOptions& options = GetOptionsWithArenas(&options_in, &copy);
  const absl::Status status =
      AnalyzeTypeImpl(type_name, options, catalog, type_factory, output_type,
                      output_type_modifiers);
  return ConvertInternalErrorLocationAndAdjustErrorString(
      options.error_message_options(), type_name, status);
}

static absl::Status ExtractTableNamesFromStatementImpl(
    absl::string_view sql, const AnalyzerOptions& options,
    TableNamesSet* table_names, TableNamesSet* tvf_names) {
  GOOGLESQL_RETURN_IF_ERROR(ValidateAnalyzerOptions(options));
  GOOGLESQL_VLOG(3) << "Extracting table names from statement:\n" << sql;
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_RETURN_IF_ERROR(
      ParseStatement(sql, options.GetParserOptions(), &parser_output));
  GOOGLESQL_VLOG(5) << "Parsed AST:\n" << parser_output->statement()->DebugString();

  return table_name_resolver::FindTables(sql, *parser_output->statement(),
                                         options, table_names, tvf_names);
}

static absl::Status ExtractTableResolutionTimeFromStatementImpl(
    absl::string_view sql, const AnalyzerOptions& options,
    TypeFactory* type_factory, Catalog* catalog,
    TableResolutionTimeInfoMap* table_resolution_time_info_map,
    std::unique_ptr<ParserOutput>* parser_output) {
  GOOGLESQL_RETURN_IF_ERROR(ValidateAnalyzerOptions(options));
  GOOGLESQL_VLOG(3) << "Extracting table resolution time from statement:\n" << sql;
  GOOGLESQL_RETURN_IF_ERROR(
      ParseStatement(sql, options.GetParserOptions(), parser_output));
  GOOGLESQL_VLOG(5) << "Parsed AST:\n" << (*parser_output)->statement()->DebugString();

  TableNamesSet table_names;
  GOOGLESQL_RETURN_IF_ERROR(table_name_resolver::FindTableNamesAndResolutionTime(
      sql, *(*parser_output)->statement(), options, type_factory, catalog,
      &table_names, table_resolution_time_info_map));
  // Note that "FOR SYSTEM_TIME AS OF ..." expressions are only valid inside
  // table path expressions. However, we want to have an entry in the output
  // map for every source table,
  EnsureResolutionTimeInfoForEveryTable(table_names,
                                        table_resolution_time_info_map);
  return absl::OkStatus();
}

absl::Status ExtractTableNamesFromStatement(
    absl::string_view sql, const AnalyzerOptions& options_in,
    TableNamesSet* /*absl_nonnull*/ table_names,
    TableNamesSet* /*absl_nullable*/ tvf_names) {
  std::unique_ptr<AnalyzerOptions> copy;
  const AnalyzerOptions& options = GetOptionsWithArenas(&options_in, &copy);

  const absl::Status status =
      ExtractTableNamesFromStatementImpl(sql, options, table_names, tvf_names);
  return ConvertInternalErrorLocationAndAdjustErrorString(
      options.error_message_options(), sql, status);
}

absl::Status ExtractTableResolutionTimeFromStatement(
    absl::string_view sql, const AnalyzerOptions& options_in,
    TypeFactory* /*absl_nullable*/ type_factory, Catalog* /*absl_nullable*/ catalog,
    TableResolutionTimeInfoMap* /*absl_nonnull*/ table_resolution_time_info_map,
    std::unique_ptr<ParserOutput>* /*absl_nonnull*/ parser_output) {
  GOOGLESQL_RET_CHECK(catalog == nullptr || type_factory != nullptr)
      << "If catalog is non-null then type_factory must be non-null";
  std::unique_ptr<AnalyzerOptions> copy;
  const AnalyzerOptions& options = GetOptionsWithArenas(&options_in, &copy);
  const absl::Status status = ExtractTableResolutionTimeFromStatementImpl(
      sql, options, type_factory, catalog, table_resolution_time_info_map,
      parser_output);
  return ConvertInternalErrorLocationAndAdjustErrorString(
      options.error_message_options(), sql, status);
}

static absl::Status ExtractTableNamesFromNextStatementImpl(
    ParseResumeLocation* resume_location, const AnalyzerOptions& options,
    TableNamesSet* table_names, bool* at_end_of_input,
    TableNamesSet* tvf_names) {
  GOOGLESQL_RETURN_IF_ERROR(ValidateAnalyzerOptions(options));

  GOOGLESQL_VLOG(2) << "Extracting table names from next statement at position "
          << resume_location->byte_position();
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_RETURN_IF_ERROR(ParseNextStatement(resume_location,
                                     options.GetParserOptions(), &parser_output,
                                     at_end_of_input));
  GOOGLESQL_VLOG(5) << "Parsed AST:\n" << parser_output->statement()->DebugString();

  return table_name_resolver::FindTables(resume_location->input(),
                                         *parser_output->statement(), options,
                                         table_names, tvf_names);
}

absl::Status ExtractTableNamesFromNextStatement(
    ParseResumeLocation* /*absl_nonnull*/ resume_location,
    const AnalyzerOptions& options_in, TableNamesSet* /*absl_nonnull*/ table_names,
    bool* /*absl_nonnull*/ at_end_of_input,
    TableNamesSet* /*absl_nullable*/ tvf_names) {
  std::unique_ptr<AnalyzerOptions> copy;
  const AnalyzerOptions& options = GetOptionsWithArenas(&options_in, &copy);
  const absl::Status status = ExtractTableNamesFromNextStatementImpl(
      resume_location, options, table_names, at_end_of_input, tvf_names);
  return ConvertInternalErrorLocationAndAdjustErrorString(
      options.error_message_options(), resume_location->input(), status);
}

absl::Status ExtractTableNamesFromASTStatement(
    const ASTStatement& ast_statement, const AnalyzerOptions& options_in,
    absl::string_view sql, TableNamesSet* /*absl_nonnull*/ table_names,
    TableNamesSet* /*absl_nullable*/ tvf_names) {
  std::unique_ptr<AnalyzerOptions> copy;
  const AnalyzerOptions& options = GetOptionsWithArenas(&options_in, &copy);
  const absl::Status status = table_name_resolver::FindTables(
      sql, ast_statement, options, table_names, tvf_names);
  return ConvertInternalErrorLocationAndAdjustErrorString(
      options.error_message_options(), sql, status);
}

static absl::Status ExtractTableResolutionTimeFromASTStatementImpl(
    absl::string_view sql, const AnalyzerOptions& options,
    const ASTStatement& ast_statement, TypeFactory* type_factory,
    Catalog* catalog,
    TableResolutionTimeInfoMap* table_resolution_time_info_map) {
  GOOGLESQL_RETURN_IF_ERROR(ValidateAnalyzerOptions(options));
  GOOGLESQL_VLOG(3) << "Extracting table resolution time from parsed AST statement:\n"
          << ast_statement.DebugString();

  TableNamesSet table_names;
  GOOGLESQL_RETURN_IF_ERROR(table_name_resolver::FindTableNamesAndResolutionTime(
      sql, ast_statement, options, type_factory, catalog, &table_names,
      table_resolution_time_info_map));
  // Note that "FOR SYSTEM_TIME AS OF ..." expressions are only valid inside
  // table path expressions. However, we want to have an entry in the output
  // map for every source table,
  EnsureResolutionTimeInfoForEveryTable(table_names,
                                        table_resolution_time_info_map);
  return absl::OkStatus();
}

absl::Status ExtractTableResolutionTimeFromASTStatement(
    const ASTStatement& ast_statement, const AnalyzerOptions& options_in,
    absl::string_view sql, TypeFactory* /*absl_nullable*/ type_factory,
    Catalog* /*absl_nullable*/ catalog,
    TableResolutionTimeInfoMap* /*absl_nonnull*/ table_resolution_time_info_map) {
  GOOGLESQL_RET_CHECK(catalog == nullptr || type_factory != nullptr)
      << "If catalog is non-null then type_factory must be non-null";
  std::unique_ptr<AnalyzerOptions> copy;
  const AnalyzerOptions& options = GetOptionsWithArenas(&options_in, &copy);
  const absl::Status status = ExtractTableResolutionTimeFromASTStatementImpl(
      sql, options, ast_statement, type_factory, catalog,
      table_resolution_time_info_map);
  return ConvertInternalErrorLocationAndAdjustErrorString(
      options.error_message_options(), sql, status);
}

absl::Status ExtractTableNamesFromScript(
    absl::string_view sql, const AnalyzerOptions& options_in,
    TableNamesSet* /*absl_nonnull*/ table_names,
    TableNamesSet* /*absl_nullable*/ tvf_names) {
  GOOGLESQL_RETURN_IF_ERROR(ValidateAnalyzerOptions(options_in));
  GOOGLESQL_VLOG(3) << "Extracting table names from script:\n" << sql;
  std::unique_ptr<AnalyzerOptions> copy;
  const AnalyzerOptions& options = GetOptionsWithArenas(&options_in, &copy);
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_RETURN_IF_ERROR(ParseScript(
      sql, options.GetParserOptions(),
      {.mode = options.error_message_mode(),
       .attach_error_location_payload = options.attach_error_location_payload(),
       .stability = GetDefaultErrorMessageStability()},
      &parser_output));
  GOOGLESQL_VLOG(5) << "Parsed AST:\n" << parser_output->script()->DebugString();

  absl::Status status = table_name_resolver::FindTableNamesInScript(
      sql, *(parser_output->script()), options, table_names, tvf_names);
  return ConvertInternalErrorLocationAndAdjustErrorString(
      options.error_message_options(), sql, status);
}

absl::Status ExtractTableNamesFromASTScript(
    const ASTScript& ast_script, const AnalyzerOptions& options_in,
    absl::string_view sql, TableNamesSet* /*absl_nonnull*/ table_names,
    TableNamesSet* /*absl_nullable*/ tvf_names) {
  std::unique_ptr<AnalyzerOptions> copy;
  const AnalyzerOptions& options = GetOptionsWithArenas(&options_in, &copy);
  const absl::Status status = table_name_resolver::FindTableNamesInScript(
      sql, ast_script, options, table_names, tvf_names);
  return ConvertInternalErrorLocationAndAdjustErrorString(
      options.error_message_options(), sql, status);
}

absl::StatusOr<std::unique_ptr<const AnalyzerOutput>> RewriteForAnonymization(
    const std::unique_ptr<const AnalyzerOutput>& analyzer_output,
    const AnalyzerOptions& analyzer_options, Catalog* /*absl_nonnull*/ catalog,
    TypeFactory* /*absl_nonnull*/ type_factory) {
  GOOGLESQL_RET_CHECK_NE(analyzer_output.get(), nullptr);
  return RewriteForAnonymization(*analyzer_output, analyzer_options, catalog,
                                 type_factory);
}

absl::StatusOr<std::unique_ptr<const AnalyzerOutput>> RewriteForAnonymization(
    const AnalyzerOutput& analyzer_output,
    const AnalyzerOptions& analyzer_options, Catalog* /*absl_nonnull*/ catalog,
    TypeFactory* /*absl_nonnull*/ type_factory) {
  GOOGLESQL_RET_CHECK_NE(analyzer_output.resolved_statement(), nullptr);

  ColumnFactory column_factory(analyzer_output.max_column_id(),
                               analyzer_output.id_string_pool().get(),
                               analyzer_options.column_id_sequence_number());
  AnalyzerRuntimeInfo runtime_info = analyzer_output.runtime_info();
  internal::TimedValue rewriter_timed_value;
  std::unique_ptr<AnalyzerOutput> ret;
  {
    internal::ScopedTimer rewriter_timer =
        internal::MakeScopedTimerStarted(&rewriter_timed_value);

    GOOGLESQL_ASSIGN_OR_RETURN(RewriteForAnonymizationOutput anonymized_output,
                     RewriteForAnonymization(
                         *analyzer_output.resolved_statement(), catalog,
                         type_factory, analyzer_options, column_factory));
    {
      internal::ScopedTimer scoped_validator_timer =
          internal::MakeScopedTimerStarted(
              &runtime_info.validator_timed_value());
      ValidatorOptions validator_options{
          .allowed_hints_and_options =
              analyzer_options.allowed_hints_and_options()};
      Validator validator(analyzer_options.language(), validator_options);
      GOOGLESQL_RET_CHECK(anonymized_output.node->Is<ResolvedStatement>());
      GOOGLESQL_RETURN_IF_ERROR(validator.ValidateResolvedStatement(
          anonymized_output.node->GetAs<ResolvedStatement>()));
    }

    // We have a rewritten AST, so create a new AnalyzerOutput with the
    // rewritten AST.  The new AnalyzerOutput uses the (shared) IdStringPool and
    // Arena from <analyzer_output>, and we also copy the deprecation warnings
    // and parameter info from the <analyzer_output>.
    ret = std::make_unique<AnalyzerOutput>(
        analyzer_output.id_string_pool(), analyzer_output.arena(),
        absl::WrapUnique(
            anonymized_output.node.release()->GetAs<ResolvedStatement>()),
        analyzer_output.analyzer_output_properties(),
        /*parser_output=*/nullptr, analyzer_output.deprecation_warnings(),
        analyzer_output.undeclared_parameters(),
        analyzer_output.undeclared_positional_parameters(),
        column_factory.max_column_id(), analyzer_output.has_graph_references());
    if (analyzer_options.fields_accessed_mode() ==
        AnalyzerOptions::FieldsAccessedMode::CLEAR_FIELDS) {
      AnalyzerOutputMutator(ret).resolved_node()->ClearFieldsAccessed();
    }
  }

  AnalyzerRuntimeInfo::RewriterDetails& rewriter_details =
      runtime_info.rewriters_details(REWRITE_ANONYMIZATION);

  rewriter_details.count++;

  rewriter_details.timed_value.Accumulate(rewriter_timed_value);
  runtime_info.overall_timed_value().Accumulate(rewriter_timed_value);
  runtime_info.rewriters_timed_value().Accumulate(rewriter_timed_value);

  AnalyzerOutputMutator(ret).mutable_runtime_info().AccumulateAll(runtime_info);
  return AnalyzerOutputMutator::FinalizeAnalyzerOutput(std::move(ret));
}

absl::Status RewriteResolvedAst(const AnalyzerOptions& analyzer_options,
                                absl::string_view sql,
                                Catalog* /*absl_nonnull*/ catalog,
                                TypeFactory* /*absl_nonnull*/ type_factory,
                                AnalyzerOutput& analyzer_output) {
  GOOGLESQL_RETURN_IF_ERROR(RewriteResolvedAstImpl(analyzer_options, sql, catalog,
                                         type_factory, analyzer_output));
  if (analyzer_options.fields_accessed_mode() ==
      AnalyzerOptions::FieldsAccessedMode::CLEAR_FIELDS) {
    AnalyzerOutputMutator(&analyzer_output)
        .resolved_node()
        ->ClearFieldsAccessed();
  }
  return absl::OkStatus();
}

}  // namespace googlesql
