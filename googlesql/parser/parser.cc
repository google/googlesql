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

#include "googlesql/parser/parser.h"

#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "googlesql/base/arena.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/warning_sink.h"
#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/parser/macros/macro_catalog.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser_internal.h"
#include "googlesql/parser/parser_mode.h"
#include "googlesql/parser/parser_runtime_info.h"
#include "googlesql/parser/statement_properties.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/id_string.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_resume_location.h"
#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "googlesql/base/check.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

using parser::ParserMode;
using MacroCatalog = parser::macros::MacroCatalog;
using MacroExpansionMode = parser::MacroExpansionMode;

static ErrorMessageOptions GetDefaultErrorMessageOptions() {
  return {.mode = ERROR_MESSAGE_WITH_PAYLOAD,
          // The value of `attach_error_location_payload` doesn't matter when
          // the mode is `ERROR_MESSAGE_WITH_PAYLOAD`.
          .attach_error_location_payload = false,
          .stability = GetDefaultErrorMessageStability()};
}

ParserOptions::ParserOptions() : ParserOptions(LanguageOptions{}) {}

ParserOptions::ParserOptions(
    std::shared_ptr<IdStringPool> id_string_pool,
    std::shared_ptr<googlesql_base::UnsafeArena> arena, LanguageOptions language_options,
    ErrorMessageOptions error_message_options,
    parser::MacroExpansionMode macro_expansion_mode,
    const parser::macros::MacroCatalog* /*absl_nullable*/ macro_catalog)
    : arena_(arena != nullptr
                 ? std::move(arena)
                 : std::make_shared<googlesql_base::UnsafeArena>(/*block_size=*/4096)),
      id_string_pool_(id_string_pool != nullptr
                          ? std::move(id_string_pool)
                          : std::make_shared<IdStringPool>(arena_)),
      language_options_(std::move(language_options)),
      macro_expansion_mode_(macro_expansion_mode),
      macro_catalog_(macro_catalog),
      error_message_options_(std::move(error_message_options)) {}

ParserOptions::ParserOptions(LanguageOptions language_options,
                             MacroExpansionMode macro_expansion_mode,
                             const MacroCatalog* macro_catalog)
    : ParserOptions(/*id_string_pool=*/nullptr, /*arena=*/nullptr,
                    std::move(language_options),
                    GetDefaultErrorMessageOptions(), macro_expansion_mode,
                    macro_catalog) {}

ParserOptions::ParserOptions(std::shared_ptr<IdStringPool> id_string_pool,
                             std::shared_ptr<googlesql_base::UnsafeArena> arena,
                             LanguageOptions language_options,
                             MacroExpansionMode macro_expansion_mode,
                             const MacroCatalog* macro_catalog)
    : arena_(std::move(arena)),
      id_string_pool_(std::move(id_string_pool)),
      language_options_(std::move(language_options)),
      macro_expansion_mode_(macro_expansion_mode),
      macro_catalog_(macro_catalog),
      error_message_options_(GetDefaultErrorMessageOptions()) {}

ParserOptions::~ParserOptions() = default;

void ParserOptions::CreateDefaultArenasIfNotSet() {
  if (arena_ == nullptr) {
    arena_ = std::make_shared<googlesql_base::UnsafeArena>(/*block_size=*/4096);
  }
  if (id_string_pool_ == nullptr) {
    id_string_pool_ = std::make_shared<IdStringPool>(arena_);
  }
}

ParserOutput::ParserOutput(
    std::shared_ptr<IdStringPool> id_string_pool,
    std::shared_ptr<googlesql_base::UnsafeArena> arena,
    std::vector<std::unique_ptr<ASTNode>> other_allocated_ast_nodes,
    std::variant<std::unique_ptr<ASTStatement>, std::unique_ptr<ASTScript>,
                 std::unique_ptr<ASTType>, std::unique_ptr<ASTExpression>>
        node,
    WarningSink warnings, std::unique_ptr<ParserRuntimeInfo> runtime_info)
    : id_string_pool_(std::move(id_string_pool)),
      arena_(std::move(arena)),
      other_allocated_ast_nodes_(std::move(other_allocated_ast_nodes)),
      node_(std::move(node)),
      warnings_(std::move(warnings)),
      runtime_info_(std::move(runtime_info)) {}

ParserOutput::~ParserOutput() = default;

absl::Status ParseStatement(absl::string_view statement_string,
                            const ParserOptions& parser_options_in,
                            std::unique_ptr<ParserOutput>* output) {
  ParserOptions parser_options = parser_options_in;
  parser_options.CreateDefaultArenasIfNotSet();

  // TODO: Share implementation with ParseNextStatement. There are
  // subtle differences that make this difficult. One of them is that the error
  // messages from the parser change depending on whether a "next" statement is
  // expected to occur or not.
  std::unique_ptr<ASTNode> ast_node;
  std::vector<std::unique_ptr<ASTNode>> other_allocated_ast_nodes;
  auto runtime_info = std::make_unique<ParserRuntimeInfo>();
  WarningSink warning_sink(/*consider_location=*/true);
  absl::Status status = ParseInternal(
      ParserMode::kStatement, /*filename=*/absl::string_view(),
      statement_string, /*start_byte_offset=*/0,
      parser_options.id_string_pool().get(), parser_options.arena().get(),
      parser_options.language_options(), parser_options.macro_expansion_mode(),
      parser_options.macro_catalog(), &ast_node, *runtime_info, warning_sink,
      &other_allocated_ast_nodes, /*ast_statement_properties=*/nullptr,
      /*statement_end_byte_offset=*/nullptr);
  GOOGLESQL_RETURN_IF_ERROR(ConvertInternalErrorLocationAndAdjustErrorString(
      parser_options.error_message_options(), statement_string, status));
  GOOGLESQL_RET_CHECK(ast_node != nullptr);

  std::unique_ptr<ASTStatement> statement(
      ast_node.release()->GetAsOrDie<ASTStatement>());
  *output = std::make_unique<ParserOutput>(
      parser_options.id_string_pool(), parser_options.arena(),
      std::move(other_allocated_ast_nodes), std::move(statement),
      std::move(warning_sink), std::move(runtime_info));
  return absl::OkStatus();
}

absl::Status ParseScript(absl::string_view script_string,
                         const ParserOptions& parser_options_in,
                         ErrorMessageOptions error_message_options,
                         std::unique_ptr<ParserOutput>* output) {
  ParserOptions parser_options = parser_options_in;
  parser_options.CreateDefaultArenasIfNotSet();

  std::unique_ptr<ASTNode> ast_node;
  std::vector<std::unique_ptr<ASTNode>> other_allocated_ast_nodes;
  auto runtime_info = std::make_unique<ParserRuntimeInfo>();
  WarningSink warning_sink(/*consider_location=*/true);
  absl::Status status = ParseInternal(
      ParserMode::kScript, /*filename=*/absl::string_view(), script_string,
      /*start_byte_offset=*/0, parser_options.id_string_pool().get(),
      parser_options.arena().get(), parser_options.language_options(),
      parser_options.macro_expansion_mode(), parser_options.macro_catalog(),
      &ast_node, *runtime_info, warning_sink, &other_allocated_ast_nodes,
      /*ast_statement_properties=*/nullptr,
      /*statement_end_byte_offset=*/nullptr);

  std::unique_ptr<ASTScript> script;
  if (status.ok()) {
    GOOGLESQL_RET_CHECK_EQ(ast_node->node_kind(), AST_SCRIPT);
    script = absl::WrapUnique(ast_node.release()->GetAsOrDie<ASTScript>());
  }
  GOOGLESQL_RETURN_IF_ERROR(ConvertInternalErrorLocationAndAdjustErrorString(
      error_message_options, script_string, status));
  *output = std::make_unique<ParserOutput>(
      parser_options.id_string_pool(), parser_options.arena(),
      std::move(other_allocated_ast_nodes), std::move(script),
      std::move(warning_sink), std::move(runtime_info));
  return absl::OkStatus();
}

namespace {
absl::Status ParseNextStatementInternal(ParseResumeLocation* resume_location,
                                        const ParserOptions& parser_options_in,
                                        ParserMode mode,
                                        std::unique_ptr<ParserOutput>* output,
                                        bool* at_end_of_input) {
  ParserOptions parser_options = parser_options_in;
  parser_options.CreateDefaultArenasIfNotSet();

  *at_end_of_input = false;
  output->reset();
  GOOGLESQL_RETURN_IF_ERROR(resume_location->Validate());

  std::unique_ptr<ASTNode> ast_node;
  auto runtime_info = std::make_unique<ParserRuntimeInfo>();
  WarningSink warning_sink(/*consider_location=*/true);
  std::vector<std::unique_ptr<ASTNode>> other_allocated_ast_nodes;

  int next_statement_byte_offset = 0;

  absl::Status status = ParseInternal(
      mode, resume_location->filename(), resume_location->input(),
      resume_location->byte_position(), parser_options.id_string_pool().get(),
      parser_options.arena().get(), parser_options.language_options(),
      parser_options.macro_expansion_mode(), parser_options.macro_catalog(),
      &ast_node, *runtime_info, warning_sink, &other_allocated_ast_nodes,
      /*ast_statement_properties=*/nullptr, &next_statement_byte_offset);
  GOOGLESQL_RETURN_IF_ERROR(ConvertInternalErrorLocationAndAdjustErrorString(
      parser_options.error_message_options(), resume_location->input(),
      status));

  *at_end_of_input =
      (next_statement_byte_offset == -1 ||
       next_statement_byte_offset == resume_location->input().size());
  if (*at_end_of_input) {
    // Match JavaCC here, even though it doesn't matter at end-of-input.
    next_statement_byte_offset = resume_location->input().size();
  }
  GOOGLESQL_RET_CHECK(ast_node != nullptr);
  GOOGLESQL_RET_CHECK(ast_node->IsStatement());
  std::unique_ptr<ASTStatement> statement(
      ast_node.release()->GetAsOrDie<ASTStatement>());
  resume_location->set_byte_position(next_statement_byte_offset);

  *output = std::make_unique<ParserOutput>(
      parser_options.id_string_pool(), parser_options.arena(),
      std::move(other_allocated_ast_nodes), std::move(statement),
      std::move(warning_sink), std::move(runtime_info));
  return absl::OkStatus();
}
}  // namespace

absl::Status ParseNextScriptStatement(ParseResumeLocation* resume_location,
                                      const ParserOptions& parser_options_in,
                                      std::unique_ptr<ParserOutput>* output,
                                      bool* at_end_of_input) {
  return ParseNextStatementInternal(resume_location, parser_options_in,
                                    ParserMode::kNextScriptStatement, output,
                                    at_end_of_input);
}

absl::Status ParseNextStatement(ParseResumeLocation* resume_location,
                                const ParserOptions& parser_options_in,
                                std::unique_ptr<ParserOutput>* output,
                                bool* at_end_of_input) {
  return ParseNextStatementInternal(resume_location, parser_options_in,
                                    ParserMode::kNextStatement, output,
                                    at_end_of_input);
}

absl::Status ParseType(absl::string_view type_string,
                       const ParserOptions& parser_options_in,
                       std::unique_ptr<ParserOutput>* output) {
  ParserOptions parser_options = parser_options_in;
  parser_options.CreateDefaultArenasIfNotSet();

  std::unique_ptr<ASTNode> ast_node;
  auto runtime_info = std::make_unique<ParserRuntimeInfo>();
  WarningSink warning_sink(/*consider_location=*/true);
  std::vector<std::unique_ptr<ASTNode>> other_allocated_ast_nodes;
  absl::Status status = ParseInternal(
      ParserMode::kType, /* filename = */ absl::string_view(), type_string,
      0 /* offset */, parser_options.id_string_pool().get(),
      parser_options.arena().get(), parser_options.language_options(),
      parser_options.macro_expansion_mode(), parser_options.macro_catalog(),
      &ast_node, *runtime_info, warning_sink, &other_allocated_ast_nodes,
      /*ast_statement_properties=*/nullptr,
      /*statement_end_byte_offset=*/nullptr);
  GOOGLESQL_RETURN_IF_ERROR(ConvertInternalErrorLocationAndAdjustErrorString(
      parser_options.error_message_options(), type_string, status));
  GOOGLESQL_RET_CHECK(ast_node != nullptr);
  GOOGLESQL_RET_CHECK(ast_node->IsType());
  std::unique_ptr<ASTType> type(ast_node.release()->GetAsOrDie<ASTType>());

  *output = std::make_unique<ParserOutput>(
      parser_options.id_string_pool(), parser_options.arena(),
      std::move(other_allocated_ast_nodes), std::move(type),
      std::move(warning_sink), std::move(runtime_info));
  return absl::OkStatus();
}

absl::Status ParseExpression(absl::string_view expression_string,
                             const ParserOptions& parser_options_in,
                             std::unique_ptr<ParserOutput>* output) {
  ParserOptions parser_options = parser_options_in;
  parser_options.CreateDefaultArenasIfNotSet();

  std::unique_ptr<ASTNode> ast_node;
  auto runtime_info = std::make_unique<ParserRuntimeInfo>();
  WarningSink warning_sink(/*consider_location=*/true);
  std::vector<std::unique_ptr<ASTNode>> other_allocated_ast_nodes;
  absl::Status status = ParseInternal(
      ParserMode::kExpression, /* filename = */ absl::string_view(),
      expression_string, 0 /* offset */, parser_options.id_string_pool().get(),
      parser_options.arena().get(), parser_options.language_options(),
      parser_options.macro_expansion_mode(), parser_options.macro_catalog(),
      &ast_node, *runtime_info, warning_sink, &other_allocated_ast_nodes,
      /*ast_statement_properties=*/nullptr,
      /*statement_end_byte_offset=*/nullptr);
  GOOGLESQL_RETURN_IF_ERROR(ConvertInternalErrorLocationAndAdjustErrorString(
      parser_options.error_message_options(), expression_string, status));
  GOOGLESQL_RET_CHECK(ast_node != nullptr);
  GOOGLESQL_RET_CHECK(ast_node->IsExpression());
  std::unique_ptr<ASTExpression> expression(
      ast_node.release()->GetAsOrDie<ASTExpression>());

  *output = std::make_unique<ParserOutput>(
      parser_options.id_string_pool(), parser_options.arena(),
      std::move(other_allocated_ast_nodes), std::move(expression),
      std::move(warning_sink), std::move(runtime_info));
  return absl::OkStatus();
}

absl::Status ParseExpression(const ParseResumeLocation& resume_location,
                             const ParserOptions& parser_options_in,
                             std::unique_ptr<ParserOutput>* output) {
  ParserOptions parser_options = parser_options_in;
  parser_options.CreateDefaultArenasIfNotSet();

  std::unique_ptr<ASTNode> ast_node;
  auto runtime_info = std::make_unique<ParserRuntimeInfo>();
  WarningSink warning_sink(/*consider_location=*/true);
  std::vector<std::unique_ptr<ASTNode>> other_allocated_ast_nodes;
  absl::Status status = ParseInternal(
      ParserMode::kExpression, resume_location.filename(),
      resume_location.input(), resume_location.byte_position(),
      parser_options.id_string_pool().get(), parser_options.arena().get(),
      parser_options.language_options(), parser_options.macro_expansion_mode(),
      parser_options.macro_catalog(), &ast_node, *runtime_info, warning_sink,
      &other_allocated_ast_nodes,
      /*ast_statement_properties=*/nullptr,
      /*statement_end_byte_offset=*/nullptr);
  GOOGLESQL_RETURN_IF_ERROR(ConvertInternalErrorLocationAndAdjustErrorString(
      parser_options.error_message_options(), resume_location.input(), status));
  GOOGLESQL_RET_CHECK(ast_node != nullptr);
  std::unique_ptr<ASTExpression> expression(
      ast_node.release()->GetAsOrDie<ASTExpression>());

  *output = std::make_unique<ParserOutput>(
      parser_options.id_string_pool(), parser_options.arena(),
      std::move(other_allocated_ast_nodes), std::move(expression),
      std::move(warning_sink), std::move(runtime_info));
  return absl::OkStatus();
}

ASTNodeKind ParseStatementKind(absl::string_view input,
                               const LanguageOptions& language_options,
                               MacroExpansionMode macro_expansion_mode,
                               const MacroCatalog* macro_catalog,
                               bool* statement_is_ctas) {
  return ParseNextStatementKind(ParseResumeLocation::FromStringView(input),
                                language_options, macro_expansion_mode,
                                macro_catalog, statement_is_ctas);
}

ASTNodeKind ParseNextStatementKind(const ParseResumeLocation& resume_location,
                                   const LanguageOptions& language_options,
                                   MacroExpansionMode macro_expansion_mode,
                                   const MacroCatalog* macro_catalog,
                                   bool* next_statement_is_ctas) {
  GOOGLESQL_DCHECK_OK(resume_location.Validate());

  IdStringPool id_string_pool;
  googlesql_base::UnsafeArena arena(/*block_size=*/1024);
  auto runtime_info = std::make_unique<ParserRuntimeInfo>();
  WarningSink warning_sink(/*consider_location=*/true);
  std::vector<std::unique_ptr<ASTNode>> other_allocated_ast_nodes;
  parser::ASTStatementProperties ast_statement_properties;
  ParseInternal(ParserMode::kNextStatementKind, resume_location.filename(),
                resume_location.input(), resume_location.byte_position(),
                &id_string_pool, &arena, language_options, macro_expansion_mode,
                macro_catalog, /*output=*/nullptr, *runtime_info, warning_sink,
                &other_allocated_ast_nodes, &ast_statement_properties,
                /*statement_end_byte_offset=*/nullptr)
      // TODO: b/196226376 - Return this if is isn't a parsing error.
      .IgnoreError();
  *next_statement_is_ctas = ast_statement_properties.is_create_table_as_select;
  return ast_statement_properties.node_kind;
}

absl::Status ParseNextStatementProperties(
    const ParseResumeLocation& resume_location,
    const ParserOptions& parser_options,
    std::vector<std::unique_ptr<ASTNode>>* allocated_ast_nodes,
    parser::ASTStatementProperties* ast_statement_properties) {
  GOOGLESQL_RETURN_IF_ERROR(resume_location.Validate());
  GOOGLESQL_RET_CHECK(parser_options.AllArenasAreInitialized());

  std::unique_ptr<ASTNode> output;
  auto runtime_info = std::make_unique<ParserRuntimeInfo>();
  WarningSink warning_sink(/*consider_location=*/true);

  // Unlike ParseStatementKind above, it is not safe to ignore the output
  // status in this case. In this function we expect to be able to inspect the
  // ASTNode instances to get at statement level hints, and if there is a
  // parser error those nodes might not be initialized.
  //
  // We still do ignore errors here for the most part, we just don't look at the
  // ASTNodes when there is an error. Possibly we should be propagating this
  // error. That is hard to do right now because the parser's tests run
  // ParseNextStatementProperties on things that are not statements and expects
  // that to work.
  absl::Status parse_status = ParseInternal(
      ParserMode::kNextStatementKind, resume_location.filename(),
      resume_location.input(), resume_location.byte_position(),
      parser_options.id_string_pool().get(), parser_options.arena().get(),
      parser_options.language_options(), parser_options.macro_expansion_mode(),
      parser_options.macro_catalog(), &output, *runtime_info, warning_sink,
      allocated_ast_nodes, ast_statement_properties,
      /*statement_end_byte_offset=*/nullptr);

  // In kNextStatementKind mode, the bison parser places the statement level
  // hint in the output parameter.
  if (parse_status.ok() && output != nullptr) {
    GOOGLESQL_RET_CHECK(output->Is<ASTHint>());
    auto ast_hint = output->GetAsOrNull<ASTHint>();
    GOOGLESQL_RET_CHECK(ast_hint != nullptr);
    const absl::string_view sql_input = resume_location.input();
    GOOGLESQL_RETURN_IF_ERROR(ProcessStatementLevelHintsToMap(
        ast_hint, sql_input, ast_statement_properties->statement_level_hints));
  }

  // TODO: b/196226376 - Return `parse_status` if it is not a parsing error.
  return absl::OkStatus();
}

absl::Status ProcessStatementLevelHintsToMap(
    const ASTHint* ast_hint, absl::string_view sql_input,
    absl::flat_hash_map<std::string, std::string>& hints_map) {
  GOOGLESQL_RET_CHECK(ast_hint != nullptr);
  hints_map.clear();
  for (const ASTHintEntry* hint : ast_hint->hint_entries()) {
    std::string hint_name_text =
        (hint->qualifier() == nullptr
             ? hint->name()->GetAsString()
             : absl::StrCat(hint->qualifier()->GetAsStringView(), ".",
                            hint->name()->GetAsStringView()));

    // Get the start and end byte offset of the hint's value expression,
    // and use the text from the input string.
    const int start_offset = hint->value()->location().start().GetByteOffset();
    const int end_offset = hint->value()->location().end().GetByteOffset();
    absl::string_view hint_expr_text =
        sql_input.substr(start_offset, end_offset - start_offset);

    // Note that this method does not return an error if there are duplicates.
    // If there are duplicates, then this uses the last one.
    hints_map.emplace(std::move(hint_name_text), std::string(hint_expr_text));
  }
  return absl::OkStatus();
}

}  // namespace googlesql
