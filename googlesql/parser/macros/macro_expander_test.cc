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

#include "googlesql/parser/macros/macro_expander.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "googlesql/base/arena.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/parser/macros/diagnostic.h"
#include "googlesql/parser/macros/macro_catalog.h"
#include "googlesql/parser/macros/quoting.h"
#include "googlesql/parser/macros/standalone_macro_expansion.h"
#include "googlesql/parser/macros/token_provider.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser.h"
#include "googlesql/parser/parser_mode.h"
#include "googlesql/parser/tm_token.h"
#include "googlesql/parser/token_with_location.h"
#include "googlesql/public/error_location.pb.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/parse_resume_location.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace parser {
namespace macros {

using Location = ParseLocationRange;

using FrameType = ::googlesql::parser::StackFrame::FrameType;

using ::testing::_;
using ::testing::AllOf;
using ::testing::Bool;
using ::testing::Combine;
using ::testing::Conditional;
using ::testing::Contains;
using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::ExplainMatchResult;
using ::testing::FieldsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsNull;
using ::testing::Not;
using ::testing::NotNull;
using ::testing::Pair;
using ::testing::Pointwise;
using ::testing::SizeIs;
using ::testing::Values;
using ::testing::ValuesIn;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;

// Template specializations to print stack frames in failed test messages.
// Note: This is for tests only. It ignores the `error_source` field.
void PrintTo(const StackFrame& stack_frame, std::ostream* os) {
  *os << absl::StrFormat("{ name: %s, frame_type: %d, location: %s }",
                         stack_frame.name, stack_frame.frame_type,
                         stack_frame.location.GetString());
}

// Returns a string representation of the given stack frame and its ancestors.
std::string StackFrameToString(StackFrame* stack_frame) {
  std::stringstream os;
  os << "StackFrames: [";
  while (stack_frame != nullptr) {
    PrintTo(*stack_frame, &os);
    stack_frame = stack_frame->parent;
    if (stack_frame != nullptr) {
      os << ", ";
    }
  }
  os << "]\n";
  return os.str();
}
// Template specializations to print tokens & warnings in failed test messages.
static void PrintTo(const TokenWithLocation& token, std::ostream* os,
                    bool print_stack_frame = false) {
  *os << absl::StrFormat(
      "(kind: %i, location: %s, topmost_invocation_location: %s, text: '%s', "
      "prev_spaces: '%s'",
      token.kind, token.location.GetString(),
      token.topmost_invocation_location.GetString(), token.text,
      token.preceding_whitespaces);
  if (print_stack_frame) {
    *os << absl::StrFormat(", stack_frame: %s)",
                           StackFrameToString(token.stack_frame));
  }
  *os << ")";
}

template <typename T>
static void PrintTo(const std::vector<T>& tokens, std::ostream* os,
                    bool print_stack_frame = false) {
  *os << "Tokens: [\n";
  for (const auto& token : tokens) {
    *os << "  ";
    PrintTo(token, os, print_stack_frame);
    *os << "\n";
  }
  *os << "]\n";
}

template <typename T>
static std::string ToString(const std::vector<T>& tokens,
                            bool print_stack_frame = false) {
  std::stringstream os;
  PrintTo(tokens, &os, print_stack_frame);
  return os.str();
}

static void PrintTo(const std::vector<absl::Status>& warnings,
                    std::ostream* os) {
  *os << "Warnings: [\n";
  for (const auto& warning : warnings) {
    *os << "  " << warning << "\n";
  }
  *os << "]\n";
}

static void PrintTo(const ExpansionOutput& expansion_output, std::ostream* os) {
  *os << "ExpansionOutput: {\n";
  PrintTo(expansion_output.expanded_tokens, os);
  PrintTo(expansion_output.warnings, os);
  *os << "}\n";
}

StackFrame* /*absl_nullable*/ CreateChainedStackFrames(
    std::vector<StackFrame> stack_frames,
    std::vector<std::unique_ptr<StackFrame>>& stack_frame_container) {
  std::reverse(stack_frames.begin(), stack_frames.end());
  StackFrame* prev_frame = nullptr;
  for (auto& frame : stack_frames) {
    StackFrame* current_frame =
        stack_frame_container.emplace_back(std::make_unique<StackFrame>(frame))
            .get();
    current_frame->parent = prev_frame;
    prev_frame = current_frame;
  }
  return prev_frame;
}

// FrameEq only checks the name,type and location of the two stack frames.
// It does not check the chained parent stack frames.
MATCHER_P(FrameEq, expected, "") {
  return ExplainMatchResult(
      FieldsAre(Eq(expected.name), Eq(expected.frame_type),
                Eq(expected.location), _ /*input_text*/,
                _ /*offset_in_original_input*/, _ /*input_start_line_offset*/,
                _ /*input_start_column_offset*/, _ /*parent*/,
                Conditional(arg.frame_type == StackFrame::FrameType::kMacroArg,
                            NotNull(), IsNull()) /*invocation_frame*/),
      arg, result_listener);
}

MATCHER(FrameEq, "") {
  return ExplainMatchResult(FrameEq(::testing::get<0>(arg)),
                            ::testing::get<1>(arg), result_listener);
}

std::vector<StackFrame> GetStackFrames(StackFrame* /*absl_nullable*/ stack_frame) {
  std::vector<StackFrame> stack_frames;
  while (stack_frame != nullptr) {
    stack_frames.push_back(*stack_frame);
    stack_frame = stack_frame->parent;
  }
  return stack_frames;
}

// ChainedStackFrameEq checks the name,type and location of the two stack
// frames and the chained parent stack frames.
MATCHER_P(ChainedStackFrameEq, expected, "") {
  return ExplainMatchResult(Pointwise(FrameEq(), GetStackFrames(expected)),
                            GetStackFrames(arg), result_listener);
}

MATCHER_P(TokenIs, expected, "") {
  return ExplainMatchResult(
      FieldsAre(Eq(expected.kind), Eq(expected.location), Eq(expected.text),
                Eq(expected.preceding_whitespaces),
                Eq(expected.topmost_invocation_location),
                _ /*ChainedStackFrameEq(expected.stack_frame)*/),
      arg, result_listener);
}

MATCHER_P(TokenIgnoringLocationIs, expected, "") {
  return ExplainMatchResult(FieldsAre(Eq(expected.kind), Eq(expected.text),
                                      Eq(expected.preceding_whitespaces)),
                            arg, result_listener);
}

MATCHER(TokenEq, "") {
  return ExplainMatchResult(TokenIs(::testing::get<0>(arg)),
                            ::testing::get<1>(arg), result_listener);
}

MATCHER_P(TokensEq, expected, ToString(expected)) {
  return ExplainMatchResult(Pointwise(TokenEq(), expected), arg.expanded_tokens,
                            result_listener);
}

MATCHER_P(TokenWithFrameIs, expected, "") {
  return ExplainMatchResult(
      FieldsAre(Eq(expected.kind), Eq(expected.location), Eq(expected.text),
                Eq(expected.preceding_whitespaces),
                Eq(expected.topmost_invocation_location),
                ChainedStackFrameEq(expected.stack_frame)),
      arg, result_listener);
}

MATCHER(TokenWithFrameEq, "") {
  return ExplainMatchResult(TokenWithFrameIs(::testing::get<0>(arg)),
                            ::testing::get<1>(arg), result_listener);
}

MATCHER_P(TokensWithFrameEq, expected,
          ToString(expected, /*print_stack_frame=*/true)) {
  return ExplainMatchResult(Pointwise(TokenWithFrameEq(), expected),
                            arg.expanded_tokens, result_listener);
}

template <typename M>
std::string StatusMatcherToString(const M& m) {
  std::stringstream os;
  ((::testing::Matcher<std::vector<absl::Status>>)m).DescribeTo(&os);
  return os.str();
}

MATCHER_P(HasTokens, m,
          absl::StrCat("Has expanded tokens that ", StatusMatcherToString(m))) {
  return ExplainMatchResult(m, arg.expanded_tokens, result_listener);
}

MATCHER_P(HasWarnings, m,
          absl::StrCat("Has warnings that ", StatusMatcherToString(m))) {
  return ExplainMatchResult(m, arg.warnings, result_listener);
}

static Location MakeLocation(absl::string_view filename, int start_offset,
                             int end_offset) {
  return Location(ParseLocationPoint::FromByteOffset(filename, start_offset),
                  ParseLocationPoint::FromByteOffset(filename, end_offset));
}

static absl::string_view kTopFileName = "top_file.sql";
static absl::string_view kDefsFileName = "defs.sql";

static Location MakeLocation(int start_offset, int end_offset) {
  return MakeLocation(kTopFileName, start_offset, end_offset);
}

static absl::StatusOr<ExpansionOutput> ExpandMacros(
    const absl::string_view text, const MacroCatalog& macro_catalog,
    MacroExpanderOptions macro_expander_options = {
        .diagnostic_options = {
            .error_message_options = {
                .mode = ErrorMessageMode::ERROR_MESSAGE_ONE_LINE}}}) {
  return MacroExpander::ExpandMacros(
      std::make_unique<TokenProvider>(kTopFileName, text, /*start_offset=*/0,
                                      /*end_offset=*/std::nullopt,
                                      /*offset_in_original_input=*/0),
      macro_catalog, macro_expander_options);
}

static absl::StatusOr<ExpansionOutput> ExpandMacros(
    const absl::string_view text, const MacroCatalog& macro_catalog,
    bool is_strict,
    DiagnosticOptions diagnostic_options = {
        .error_message_options = {
            .mode = ErrorMessageMode::ERROR_MESSAGE_ONE_LINE}}) {
  return MacroExpander::ExpandMacros(
      std::make_unique<TokenProvider>(kTopFileName, text,
                                      /*start_offset=*/0,
                                      /*end_offset=*/std::nullopt,
                                      /*offset_in_original_input=*/0),
      macro_catalog,
      {
          .is_strict = is_strict,
          .diagnostic_options = diagnostic_options,
      });
}

// This function needs to return void due to the assertions.
static void RegisterMacros(absl::string_view source,
                           MacroCatalog& macro_catalog) {
  ParseResumeLocation location =
      ParseResumeLocation::FromStringView(kDefsFileName, source);
  bool at_end_of_input = false;
  while (!at_end_of_input) {
    std::unique_ptr<ParserOutput> output;

    // Record the byte offset before ParseNextStatement() updates it to the
    // end of the statement.
    int start_offset = location.byte_position();
    GOOGLESQL_ASSERT_OK(ParseNextStatement(
        &location,
        ParserOptions(LanguageOptions(), MacroExpansionMode::kLenient), &output,
        &at_end_of_input));
    ASSERT_TRUE(output->statement() != nullptr);
    auto def_macro_stmt =
        output->statement()->GetAsOrNull<ASTDefineMacroStatement>();
    ASSERT_TRUE(def_macro_stmt != nullptr);
    GOOGLESQL_ASSERT_OK(macro_catalog.RegisterMacro(
        {.source_text = source,
         .location = def_macro_stmt->location(),
         .name_location = def_macro_stmt->name()->location(),
         .body_location = def_macro_stmt->body()->location(),
         .definition_start_offset = start_offset}));
  }
}

static void RegisterMacroAt(absl::string_view source,
                            MacroCatalog& macro_catalog, int start_offset,
                            int original_start_line,
                            int original_start_column) {
  ParseResumeLocation location =
      ParseResumeLocation::FromStringView(kDefsFileName, source);
  bool at_end_of_input = false;
  std::unique_ptr<ParserOutput> output;
  GOOGLESQL_ASSERT_OK(ParseNextStatement(
      &location, ParserOptions(LanguageOptions(), MacroExpansionMode::kLenient),
      &output, &at_end_of_input));
  ASSERT_TRUE(output->statement() != nullptr);
  auto def_macro_stmt =
      output->statement()->GetAsOrNull<ASTDefineMacroStatement>();
  ASSERT_TRUE(def_macro_stmt != nullptr);
  GOOGLESQL_ASSERT_OK(macro_catalog.RegisterMacro({
      .source_text = source,
      .location = def_macro_stmt->location(),
      .name_location = def_macro_stmt->name()->location(),
      .body_location = def_macro_stmt->body()->location(),
      .definition_start_offset = start_offset,
      .definition_start_line = original_start_line,
      .definition_start_column = original_start_column,
  }));
}

// This test is to ensure that the size of the StackFrame struct does not
// change unexpectedly. If it does, it may indicate that the size of the
// StackFrame struct has increased unexpectedly, which could have performance
// implications.
TEST(StackFrameTest, SizeOfStackFrame) { EXPECT_EQ(sizeof(StackFrame), 120); }

TEST(MacroExpanderTest, ExpandsEmptyMacros) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO empty", macro_catalog);

  EXPECT_THAT(ExpandMacros("\t$empty\r\n$empty$empty", macro_catalog,
                           /*is_strict=*/false),
              IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
                  {Token::EOI, MakeLocation(21, 21), "", "\t\r\n"}})));
}

TEST(MacroExpanderTest, TrailingWhitespaceIsMovedToEofToken) {
  MacroCatalog macro_catalog;
  EXPECT_THAT(ExpandMacros(";\t", macro_catalog,
                           /*is_strict=*/false),
              IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
                  {Token::SEMICOLON, MakeLocation(0, 1), ";", ""},
                  {Token::EOI, MakeLocation(2, 2), "", "\t"}})));
}

TEST(MacroExpanderTest, ErrorsCanPrintLocation) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO m $unknown", macro_catalog);

  EXPECT_THAT(
      ExpandMacros("\t$empty\r\n$empty$empty", macro_catalog,
                   /*is_strict=*/true),
      StatusIs(_, Eq("Macro 'empty' not found. [at top_file.sql:1:9]")));
}

TEST(MacroExpanderTest, ErrorsCanPrintLocation_EmptyFileNames) {
  MacroCatalog macro_catalog;
  GOOGLESQL_ASSERT_OK(macro_catalog.RegisterMacro({
      .source_text = "DEFINE MACRO m $unknown",
      .location = MakeLocation("", 0, 23),
      .name_location = MakeLocation("", 13, 14),
      .body_location = MakeLocation("", 14, 23),
  }));
  GOOGLESQL_ASSERT_OK(macro_catalog.RegisterMacro({
      .source_text = "DEFINE MACRO m2 $m()",
      .location = MakeLocation("", 0, 20),
      .name_location = MakeLocation("", 13, 15),
      .body_location = MakeLocation("", 15, 20),
  }));

  EXPECT_THAT(
      MacroExpander::ExpandMacros(
          std::make_unique<TokenProvider>("", "$m2()",
                                          /*start_offset=*/0,
                                          /*end_offset=*/std::nullopt,
                                          /*offset_in_original_input=*/0),
          macro_catalog,
          {.is_strict = true,
           .diagnostic_options =
               {.error_message_options =
                    {.mode = ErrorMessageMode::ERROR_MESSAGE_ONE_LINE}}}),
      StatusIs(_, Eq("Macro 'unknown' not found. [at 1:16]; "
                     "Expanded from macro:m [at 1:17]; "
                     "Expanded from macro:m2 [at 1:1]")));
}

TEST(MacroExpanderTest, ErrorsOrWarningsDoNotTruncateCaretContext) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO one 1;\n"
      "DEFINE MACRO two 2;\n"
      "DEFINE MACRO add $1 + $2;\n"
      "DEFINE MACRO outer $add($one, $two);  # Extra comment",
      macro_catalog);

  EXPECT_THAT(
      ExpandMacros(
          "$outer()", macro_catalog, /*is_strict=*/false,
          {{.mode = ErrorMessageMode::ERROR_MESSAGE_MULTI_LINE_WITH_CARET}}),
      IsOkAndHolds(HasWarnings(ElementsAre(
          StatusIs(_,
                   Eq("Invocation of macro 'one' missing argument list. [at "
                      "defs.sql:4:29]\n"
                      "DEFINE MACRO outer $add($one, $two);  # Extra comment\n"
                      "                            ^\n"
                      "Which is arg:$1, and got created at [at defs.sql:4:25]\n"
                      "DEFINE MACRO outer $add($one, $two);  # Extra comment\n"
                      "                        ^\n"
                      "Expanded from macro:add [at defs.sql:4:20]\n"
                      "DEFINE MACRO outer $add($one, $two);  # Extra comment\n"
                      "                   ^\n"
                      "Expanded from macro:outer [at top_file.sql:1:1]\n"
                      "$outer()\n"
                      "^")),
          StatusIs(_,
                   Eq("Invocation of macro 'two' missing argument list. [at "
                      "defs.sql:4:35]\n"
                      "DEFINE MACRO outer $add($one, $two);  # Extra comment\n"
                      "                                  ^\n"
                      "Which is arg:$2, and got created at [at defs.sql:4:30]\n"
                      "DEFINE MACRO outer $add($one, $two);  # Extra comment\n"
                      "                             ^\n"
                      "Expanded from macro:add [at defs.sql:4:20]\n"
                      "DEFINE MACRO outer $add($one, $two);  # Extra comment\n"
                      "                   ^\n"
                      "Expanded from macro:outer [at top_file.sql:1:1]\n"
                      "$outer()\n"
                      "^"))))));

  EXPECT_THAT(
      ExpandMacros(
          "$outer()", macro_catalog, /*is_strict=*/true,
          {{.mode = ErrorMessageMode::ERROR_MESSAGE_MULTI_LINE_WITH_CARET}}),
      StatusIs(_, Eq("Invocation of macro 'one' missing argument list. [at "
                     "defs.sql:4:29]\n"
                     "DEFINE MACRO outer $add($one, $two);  # Extra comment\n"
                     "                            ^\n"
                     "Which is arg:$1, and got created at [at defs.sql:4:25]\n"
                     "DEFINE MACRO outer $add($one, $two);  # Extra comment\n"
                     "                        ^\n"
                     "Expanded from macro:add [at defs.sql:4:20]\n"
                     "DEFINE MACRO outer $add($one, $two);  # Extra comment\n"
                     "                   ^\n"
                     "Expanded from macro:outer [at top_file.sql:1:1]\n"
                     "$outer()\n"
                     "^")));
}

TEST(MacroExpanderTest, TracksCountOfUnexpandedTokensConsumedIncludingEOF) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO empty", macro_catalog);

  auto token_provider = std::make_unique<TokenProvider>(
      kTopFileName, "\t$empty\r\n$empty$empty",
      /*start_offset=*/0, /*end_offset=*/std::nullopt,
      /*offset_in_original_input=*/0);
  auto arena = std::make_unique<googlesql_base::UnsafeArena>(/*block_size=*/1024);
  StackFrame::StackFrameFactory stack_frame_factory;
  MacroExpander expander(std::move(token_provider), macro_catalog, arena.get(),
                         stack_frame_factory, MacroExpanderOptions{},
                         /*parent_location=*/nullptr);

  ASSERT_THAT(expander.GetNextToken(),
              IsOkAndHolds(TokenIs(TokenWithLocation{
                  Token::EOI, MakeLocation(21, 21), "", "\t\r\n"})));
  EXPECT_EQ(expander.num_unexpanded_tokens_consumed(), 4);
}

TEST(MacroExpanderTest,
     TracksCountOfUnexpandedTokensConsumedButNotFromDefinitions) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO m 1 2 3", macro_catalog);

  auto token_provider = std::make_unique<TokenProvider>(
      kTopFileName, "$m", /*start_offset=*/0,
      /*end_offset=*/std::nullopt, /*offset_in_original_input=*/0);
  auto arena = std::make_unique<googlesql_base::UnsafeArena>(/*block_size=*/1024);
  StackFrame::StackFrameFactory stack_frame_factory;
  MacroExpander expander(std::move(token_provider), macro_catalog, arena.get(),
                         stack_frame_factory, MacroExpanderOptions{},
                         /*parent_location=*/nullptr);

  ASSERT_THAT(expander.GetNextToken(), IsOkAndHolds(TokenIs(TokenWithLocation{
                                           Token::DECIMAL_INTEGER_LITERAL,
                                           MakeLocation(kDefsFileName, 15, 16),
                                           "1", "", MakeLocation(0, 2)})));
  ASSERT_THAT(expander.GetNextToken(), IsOkAndHolds(TokenIs(TokenWithLocation{
                                           Token::DECIMAL_INTEGER_LITERAL,
                                           MakeLocation(kDefsFileName, 17, 18),
                                           "2", " ", MakeLocation(0, 2)})));
  ASSERT_THAT(expander.GetNextToken(), IsOkAndHolds(TokenIs(TokenWithLocation{
                                           Token::DECIMAL_INTEGER_LITERAL,
                                           MakeLocation(kDefsFileName, 19, 20),
                                           "3", " ", MakeLocation(0, 2)})));
  ASSERT_THAT(expander.GetNextToken(),
              IsOkAndHolds(TokenIs(
                  TokenWithLocation{Token::EOI, MakeLocation(2, 2), "", ""})));

  // We count 2 unexpanded tokens: $m and YYEOF. Tokens in $m's definition
  // do not count.
  EXPECT_EQ(expander.num_unexpanded_tokens_consumed(), 2);
}

TEST(MacroExpanderTest, ExpandsEmptyMacrosSplicedWithIntLiterals) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO empty ;\n"
      "DEFINE MACRO int 123;\n",
      macro_catalog);

  EXPECT_THAT(
      ExpandMacros("\n$empty()1", macro_catalog,
                   /*is_strict=*/false),
      IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
          {Token::DECIMAL_INTEGER_LITERAL, MakeLocation(9, 10), "1", "\n"},
          {Token::EOI, MakeLocation(10, 10), "", ""}})));

  EXPECT_THAT(
      ExpandMacros("\n$empty()$int", macro_catalog,
                   /*is_strict=*/false),
      IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
          // Location is from the macro it was pulled from.
          // We can stack it later for nested invocations.
          {Token::DECIMAL_INTEGER_LITERAL, MakeLocation(kDefsFileName, 58, 61),
           "123", "\n", MakeLocation(9, 13)},
          {Token::EOI, MakeLocation(13, 13), "", ""}})));

  EXPECT_THAT(
      ExpandMacros("\n1$empty()", macro_catalog,
                   /*is_strict=*/false),
      IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
          {Token::DECIMAL_INTEGER_LITERAL, MakeLocation(1, 2), "1", "\n"},
          {Token::EOI, MakeLocation(10, 10), "", ""}})));

  EXPECT_THAT(
      ExpandMacros("\n$int()$empty", macro_catalog,
                   /*is_strict=*/false),
      IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
          {Token::DECIMAL_INTEGER_LITERAL, MakeLocation(kDefsFileName, 58, 61),
           "123", "\n", MakeLocation(1, 7)},
          {Token::EOI, MakeLocation(13, 13), "", ""}})));
}

TEST(MacroExpanderTest,
     ExpandsIdentifiersSplicedWithEmptyMacrosAndIntLiterals) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO identifier abc;\n"
      "DEFINE MACRO empty;\n"
      "DEFINE MACRO int 123;\n",
      macro_catalog);

  EXPECT_THAT(ExpandMacros("\na$empty()1", macro_catalog,
                           /*is_strict=*/false),
              IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
                  {Token::IDENTIFIER, MakeLocation(10, 11), "a1", "\n"},
                  {Token::EOI, MakeLocation(11, 11), "", ""}})));

  EXPECT_THAT(ExpandMacros("\na$empty()$int", macro_catalog,
                           /*is_strict=*/false),
              IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
                  {Token::IDENTIFIER, MakeLocation(kDefsFileName, 114, 117),
                   "a123", "\n"},
                  {Token::EOI, MakeLocation(14, 14), "", ""}})));

  EXPECT_THAT(ExpandMacros("\n$identifier$empty()1", macro_catalog,
                           /*is_strict=*/false),
              IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
                  {Token::IDENTIFIER, MakeLocation(kDefsFileName, 24, 27),
                   "abc1", "\n", MakeLocation(1, 12)},
                  {Token::EOI, MakeLocation(21, 21), "", ""}})));

  EXPECT_THAT(ExpandMacros("\n$identifier$empty()$int", macro_catalog,
                           /*is_strict=*/false),
              IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
                  {Token::IDENTIFIER, MakeLocation(kDefsFileName, 24, 27),
                   "abc123", "\n", MakeLocation(1, 12)},
                  {Token::EOI, MakeLocation(24, 24), "", ""}})));
}

TEST(MacroExpanderTest, CanExpandWithoutArgsAndNoSplicing) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO prefix xyz;\n"
      "DEFINE MACRO suffix1 123;\n"
      "DEFINE MACRO suffix2 456 abc;\n"
      "DEFINE MACRO empty ;\n",
      macro_catalog);

  EXPECT_THAT(
      ExpandMacros("select abc tbl_\t$empty+ $suffix1 $suffix2 $prefix\t",
                   macro_catalog, /*is_strict=*/false),
      IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
          {Token::KW_SELECT, MakeLocation(0, 6), "select", ""},
          {Token::IDENTIFIER, MakeLocation(7, 10), "abc", " "},
          {Token::IDENTIFIER, MakeLocation(11, 15), "tbl_", " "},
          {Token::PLUS, MakeLocation(22, 23), "+", "\t"},
          {Token::DECIMAL_INTEGER_LITERAL, MakeLocation(kDefsFileName, 70, 73),
           "123", " ", MakeLocation(24, 32)},
          {Token::DECIMAL_INTEGER_LITERAL,
           MakeLocation(kDefsFileName, 122, 125), "456", " ",
           MakeLocation(33, 41)},
          {Token::IDENTIFIER, MakeLocation(kDefsFileName, 126, 129), "abc", " ",
           MakeLocation(33, 41)},
          {Token::IDENTIFIER, MakeLocation(kDefsFileName, 20, 23), "xyz", " ",
           MakeLocation(42, 49)},
          {Token::EOI, MakeLocation(50, 50), "", "\t"}})));
}

TEST(MacroExpanderTest, CanExpandWithoutArgsWithSplicing) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO identifier xyz;\n"
      "DEFINE MACRO numbers 123;\n"
      "DEFINE MACRO multiple_tokens 456 pq abc;\n"
      "DEFINE MACRO empty  ;\n",
      macro_catalog);

  EXPECT_THAT(
      ExpandMacros("select tbl_$numbers$multiple_tokens$empty$identifier "
                   "$empty$identifier$numbers$empty a+b",
                   macro_catalog, /*is_strict=*/false),
      IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
          {Token::KW_SELECT, MakeLocation(0, 6), "select", ""},
          {Token::IDENTIFIER, MakeLocation(kDefsFileName, 78, 81), "tbl_123456",
           " "},
          {Token::IDENTIFIER, MakeLocation(kDefsFileName, 142, 144), "pq", " ",
           MakeLocation(19, 35)},
          {Token::IDENTIFIER, MakeLocation(kDefsFileName, 145, 148), "abcxyz",
           " ", MakeLocation(19, 35)},
          {Token::IDENTIFIER, MakeLocation(kDefsFileName, 24, 27), "xyz123",
           " ", MakeLocation(59, 70)},
          {Token::IDENTIFIER, MakeLocation(85, 86), "a", " "},
          {Token::PLUS, MakeLocation(86, 87), "+", ""},
          {Token::IDENTIFIER, MakeLocation(87, 88), "b", ""},
          {Token::EOI, MakeLocation(88, 88), "", ""},
      })));
}

TEST(MacroExpanderTest, KeywordsCanSpliceToFormIdentifiers) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO suffix 123;\n"
      "DEFINE MACRO empty    ;\n",
      macro_catalog);

  EXPECT_THAT(
      ExpandMacros("FROM$suffix  $empty()FROM$suffix  FROM$empty$suffix  "
                   "$empty()FROM$empty()2",
                   macro_catalog, /*is_strict=*/false),
      IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
          // Note: spliced tokens take the first unexpanded location
          {Token::IDENTIFIER, MakeLocation(kDefsFileName, 20, 23), "FROM123",
           ""},
          {Token::IDENTIFIER, MakeLocation(kDefsFileName, 20, 23), "FROM123",
           "  "},
          {Token::IDENTIFIER, MakeLocation(kDefsFileName, 20, 23), "FROM123",
           "  "},
          {Token::IDENTIFIER, MakeLocation(73, 74), "FROM2", "  "},
          {Token::EOI, MakeLocation(74, 74), "", ""}})));
}

TEST(MacroExpanderTest, SpliceMacroInvocationWithIdentifier_Lenient) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO m  a  ", macro_catalog);

  EXPECT_THAT(
      ExpandMacros("$m()b", macro_catalog,
                   /*is_strict=*/false),
      IsOkAndHolds(AllOf(
          TokensEq(std::vector<TokenWithLocation>{
              {Token::IDENTIFIER, MakeLocation(kDefsFileName, 16, 17), "ab", "",
               MakeLocation(0, 4)},
              {Token::EOI, MakeLocation(5, 5), "", ""}}),
          HasWarnings(ElementsAre(StatusIs(
              _, Eq("Splicing tokens (a) and (b) [at top_file.sql:1:5]")))))));
}

TEST(MacroExpanderTest, CanSuppressWarningOnIdentifierSplicing) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO m  a  ", macro_catalog);

  EXPECT_THAT(
      ExpandMacros("$m()b", macro_catalog,
                   /*is_strict=*/false, {.warn_on_identifier_splicing = false}),
      IsOkAndHolds(HasWarnings(IsEmpty())));
}

TEST(MacroExpanderTest, CanSuppressWarningOnInvocationsWithNoParens) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO m  a  ", macro_catalog);

  EXPECT_THAT(ExpandMacros("$m", macro_catalog, /*is_strict=*/false,
                           {.warn_on_macro_invocation_with_no_parens = false}),
              IsOkAndHolds(HasWarnings(IsEmpty())));
}

TEST(MacroExpanderTest, SpliceMacroInvocationWithIdentifier_Strict) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO m  a  ", macro_catalog);

  std::vector<absl::Status> warnings;
  EXPECT_THAT(
      ExpandMacros("$m()b", macro_catalog,
                   /*is_strict=*/true),
      StatusIs(_, Eq("Splicing tokens (a) and (b) [at top_file.sql:1:5]")));
}

TEST(MacroExpanderTest, StrictProducesErrorOnIncompatibleQuoting) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO single_quoted 'sq'", macro_catalog);

  ASSERT_THAT(
      ExpandMacros("select `ab$single_quoted`", macro_catalog,
                   /*is_strict=*/true),
      IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
          {Token::KW_SELECT, MakeLocation(0, 6), "select", ""},
          {Token::IDENTIFIER, MakeLocation(7, 25), "`ab$single_quoted`", " "},
          {Token::EOI, MakeLocation(25, 25), "", ""},
      })));
}

TEST(MacroExpanderTest, LenientCanHandleMixedQuoting) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO single_quoted 'sq'", macro_catalog);

  ASSERT_THAT(
      ExpandMacros("select `ab$single_quoted`", macro_catalog,
                   /*is_strict=*/false),
      StatusIs(_,
               Eq("Cannot expand a string literal(single "
                  "quote) into a quoted identifier. [at top_file.sql:1:11]")));
}

TEST(MacroExpanderTest, CanHandleUnicode) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO unicode '😀'", macro_catalog);

  EXPECT_THAT(ExpandMacros("'$😀$unicode'", macro_catalog,
                           /*is_strict=*/false),
              IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
                  {Token::STRING_LITERAL, MakeLocation(kDefsFileName, 21, 27),
                   "'$😀😀'", ""},
                  {Token::EOI, MakeLocation(15, 15), "", ""},
              })));
}

TEST(MacroExpanderTest, ExpandsWithinQuotedIdentifiers_SingleToken) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO identifier xyz;\n"
      "DEFINE MACRO numbers 456;\n"
      "DEFINE MACRO inner_quoted_id `bq`;\n"
      "DEFINE MACRO empty     ",
      macro_catalog);

  EXPECT_THAT(
      ExpandMacros("select `ab$identifier$numbers$empty$inner_quoted_id`",
                   macro_catalog, /*is_strict=*/false),
      IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
          {Token::KW_SELECT, MakeLocation(0, 6), "select", ""},
          {Token::IDENTIFIER, MakeLocation(kDefsFileName, 24, 27),
           "`abxyz456bq`", " "},
          {Token::EOI, MakeLocation(52, 52), "", ""},
      })));
}

TEST(MacroExpanderTest, ExpandsWithinQuotedIdentifiers_SingleToken_WithSpaces) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO identifier xyz;\n"
      "DEFINE MACRO numbers 456;\n"
      "DEFINE MACRO inner_quoted_id `bq`;\n"
      "DEFINE MACRO empty     ",
      macro_catalog);

  EXPECT_THAT(
      ExpandMacros("select `  ab$identifier$numbers$empty$inner_quoted_id  `\n",
                   macro_catalog, /*is_strict=*/false),
      IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
          {Token::KW_SELECT, MakeLocation(0, 6), "select", ""},
          {Token::IDENTIFIER, MakeLocation(kDefsFileName, 24, 27),
           "`  abxyz456bq  `", " "},
          {Token::EOI, MakeLocation(57, 57), "", "\n"},
      })));
}

TEST(MacroExpanderTest, ExpandsWithinQuotedIdentifiers_MultipleTokens) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO identifier xyz;\n"
      "DEFINE MACRO numbers 123 456;\n"
      "DEFINE MACRO inner_quoted_id `bq`;\n"
      "DEFINE MACRO empty     ",
      macro_catalog);

  EXPECT_THAT(
      ExpandMacros(
          "select\n`  ab$identifier$numbers$empty$inner_quoted_id  cd\t\t`\n",
          macro_catalog, /*is_strict=*/false),

      IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
          {Token::KW_SELECT, MakeLocation(0, 6), "select", ""},
          {Token::IDENTIFIER, MakeLocation(kDefsFileName, 24, 27),
           "`  abxyz123 456bq  cd\t\t`", "\n"},
          {Token::EOI, MakeLocation(61, 61), "", "\n"},
      })));
}

TEST(MacroExpanderTest, UnknownMacrosAreLeftUntouched) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO ints\t\t\t1 2\t\t\t", macro_catalog);

  EXPECT_THAT(
      ExpandMacros("  $x$y(a\n,\t$ints\n\t,\t$z,\n$w(\t\tb\n\n,\t\r)\t\n)   ",
                   macro_catalog, /*is_strict=*/false),
      IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
          {Token::MACRO_INVOCATION, MakeLocation(2, 4), "$x", "  "},
          {Token::MACRO_INVOCATION, MakeLocation(4, 6), "$y", ""},
          {Token::LPAREN, MakeLocation(6, 7), "(", ""},
          {Token::IDENTIFIER, MakeLocation(7, 8), "a", ""},
          {Token::COMMA, MakeLocation(9, 10), ",", "\n"},
          {Token::DECIMAL_INTEGER_LITERAL, MakeLocation(kDefsFileName, 20, 21),
           "1", "\t", MakeLocation(11, 16)},
          {Token::DECIMAL_INTEGER_LITERAL, MakeLocation(kDefsFileName, 22, 23),
           "2", " ", MakeLocation(11, 16)},
          {Token::COMMA, MakeLocation(18, 19), ",", "\n\t"},
          {Token::MACRO_INVOCATION, MakeLocation(20, 22), "$z", "\t"},
          {Token::COMMA, MakeLocation(22, 23), ",", ""},
          {Token::MACRO_INVOCATION, MakeLocation(24, 26), "$w", "\n"},
          {Token::LPAREN, MakeLocation(26, 27), "(", ""},
          {Token::IDENTIFIER, MakeLocation(29, 30), "b", "\t\t"},
          {Token::COMMA, MakeLocation(32, 33), ",", "\n\n"},
          {Token::RPAREN, MakeLocation(35, 36), ")", "\t\r"},
          {Token::RPAREN, MakeLocation(38, 39), ")", "\t\n"},
          {Token::EOI, MakeLocation(42, 42), "", "   "}})));
}

TEST(MacroExpanderTest, UnknownMacrosAreLeftUntouched_EmptyArgList) {
  MacroCatalog macro_catalog;

  EXPECT_THAT(ExpandMacros("\t$w(\n$z(\n)\t)   ", macro_catalog,
                           /*is_strict=*/false),
              IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
                  {Token::MACRO_INVOCATION, MakeLocation(1, 3), "$w", "\t"},
                  {Token::LPAREN, MakeLocation(3, 4), "(", ""},
                  {Token::MACRO_INVOCATION, MakeLocation(5, 7), "$z", "\n"},
                  {Token::LPAREN, MakeLocation(7, 8), "(", ""},
                  {Token::RPAREN, MakeLocation(9, 10), ")", "\n"},
                  {Token::RPAREN, MakeLocation(11, 12), ")", "\t"},
                  {Token::EOI, MakeLocation(15, 15), "", "   "}})));
}

TEST(MacroExpanderTest, LeavesPlxParamsUndisturbed) {
  MacroCatalog macro_catalog;

  EXPECT_THAT(ExpandMacros("  a${x}   ", macro_catalog,
                           /*is_strict=*/false),
              IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
                  {Token::IDENTIFIER, MakeLocation(2, 3), "a", "  "},
                  {Token::DOLLAR_SIGN, MakeLocation(3, 4), "$", ""},
                  {Token::LBRACE, MakeLocation(4, 5), "{", ""},
                  {Token::IDENTIFIER, MakeLocation(5, 6), "x", ""},
                  {Token::RBRACE, MakeLocation(6, 7), "}", ""},
                  {Token::EOI, MakeLocation(10, 10), "", "   "}})));
}

TEST(MacroExpanderTest, DoesNotStrictlyTokenizeLiteralContents) {
  MacroCatalog macro_catalog;
  EXPECT_THAT(
      ExpandMacros(R"("30d\a's")", macro_catalog,
                   /*is_strict=*/false),
      IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
          {Token::STRING_LITERAL, MakeLocation(0, 9), R"("30d\a's")", ""},
          {Token::EOI, MakeLocation(9, 9), "", ""}})));
}

TEST(MacroExpanderTest, SkipsQuotesInLiterals) {
  MacroCatalog macro_catalog;

  EXPECT_THAT(ExpandMacros(R"(SELECT "Doesn't apply")", macro_catalog,
                           /*is_strict=*/false),
              IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
                  {Token::KW_SELECT, MakeLocation(0, 6), "SELECT", ""},
                  {Token::STRING_LITERAL, MakeLocation(7, 22),
                   R"("Doesn't apply")", " "},
                  {Token::EOI, MakeLocation(22, 22), "", ""}})));
}

// Regression test for b/400237235
TEST(MacroExpanderTest, ExpansionOfWhitespaceAndEmptyStringsIntoAnotherString) {
  MacroCatalog macro_catalog;
  RegisterMacros(R"sql(DEFINE MACRO m "" "";)sql", macro_catalog);

  EXPECT_THAT(ExpandMacros("SELECT ''' $m '''", macro_catalog,
                           /*is_strict=*/false),
              IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
                  {Token::KW_SELECT, MakeLocation(0, 6), "SELECT", ""},
                  {Token::STRING_LITERAL, MakeLocation(kDefsFileName, 15, 17),
                   R"sql(''' ''')sql", " "},
                  {Token::STRING_LITERAL, MakeLocation(kDefsFileName, 15, 17),
                   R"sql("")sql", " ", MakeLocation(11, 13)},
                  {Token::STRING_LITERAL, MakeLocation(kDefsFileName, 15, 17),
                   R"sql(''' ''')sql", " "},
                  {Token::STRING_LITERAL, MakeLocation(kDefsFileName, 18, 20),
                   R"sql("")sql", " ", MakeLocation(11, 13)},
                  {Token::STRING_LITERAL, MakeLocation(kDefsFileName, 15, 17),
                   R"sql(''' ''')sql", " "},
                  {Token::EOI, MakeLocation(17, 17), "", ""}})));
}

TEST(MacroExpanderTest, SeparateParenthesesAreNotArgLists) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO m   bc   ;", macro_catalog);

  EXPECT_THAT(
      ExpandMacros("a$m (x, y)", macro_catalog,
                   /*is_strict=*/false),
      IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
          {Token::IDENTIFIER, MakeLocation(kDefsFileName, 17, 19), "abc", ""},
          {Token::LPAREN, MakeLocation(4, 5), "(", " "},
          {Token::IDENTIFIER, MakeLocation(5, 6), "x", ""},
          {Token::COMMA, MakeLocation(6, 7), ",", ""},
          {Token::IDENTIFIER, MakeLocation(8, 9), "y", " "},
          {Token::RPAREN, MakeLocation(9, 10), ")", ""},
          {Token::EOI, MakeLocation(10, 10), "", ""}})));
}

TEST(MacroExpanderTest,
     ProducesCorrectErrorOnUnbalancedParenthesesInMacroArgumentLists) {
  MacroCatalog macro_catalog;

  EXPECT_THAT(
      ExpandMacros("a$m((x, y)", macro_catalog,
                   /*is_strict=*/false),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          Eq("Unbalanced parentheses in macro argument list. Make sure that "
             "parentheses are balanced even inside macro arguments. [at "
             "top_file.sql:1:11]")));

  EXPECT_THAT(
      ExpandMacros("$m(x;)", macro_catalog,
                   /*is_strict=*/false),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          Eq("Unbalanced parentheses in macro argument list. Make sure that "
             "parentheses are balanced even inside macro arguments. [at "
             "top_file.sql:1:5]")));
}

TEST(MacroExpanderTest, ArgsNotAllowedInsideLiterals) {
  MacroCatalog macro_catalog;

  EXPECT_THAT(
      ExpandMacros("'$a()'", macro_catalog,
                   /*is_strict=*/false),
      IsOkAndHolds(HasWarnings(ElementsAre(
          StatusIs(_, Eq("Argument lists are not allowed inside "
                         "literals [at top_file.sql:1:4]")),
          StatusIs(_,
                   Eq("Macro expansion in literals is deprecated. Strict mode "
                      "does not expand literals [at top_file.sql:1:2]")),
          StatusIs(_,
                   HasSubstr("Macro 'a' not found. [at top_file.sql:1:2]"))))));

  EXPECT_THAT(ExpandMacros("'$a('", macro_catalog,
                           /*is_strict=*/false),
              StatusIs(_, Eq("Nested macro argument lists inside literals are "
                             "not allowed [at top_file.sql:1:4]")));
}

TEST(MacroExpanderTest, SpliceArgWithIdentifier) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO m $1a;", macro_catalog);

  EXPECT_THAT(
      ExpandMacros("$m(x)", macro_catalog,
                   /*is_strict=*/false),
      IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
          {Token::IDENTIFIER, MakeLocation(3, 4), "xa", "", MakeLocation(0, 5)},
          {Token::EOI, MakeLocation(5, 5), "", ""},
      })));
}

TEST(MacroExpanderTest, ExpandsArgs) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO select_list a , $1, $2  ;\n"
      "DEFINE MACRO from_clause FROM tbl_$1$empty;\n"
      "DEFINE MACRO splice    $1$2   ;\n"
      "DEFINE MACRO numbers 123 456;\n"
      "DEFINE MACRO inner_quoted_id `bq`;\n"
      "DEFINE MACRO empty      ;\n"
      "DEFINE MACRO MakeLiteral '$1';",
      macro_catalog);

  EXPECT_THAT(ExpandMacros("select $MakeLiteral(  x  ) $splice(  b  ,   89  ), "
                           "$select_list(  b, c )123 $from_clause(  123  ) ",
                           macro_catalog, /*is_strict=*/false),
              IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
                  {Token::KW_SELECT, MakeLocation(0, 6), "select", ""},
                  {Token::STRING_LITERAL, MakeLocation(22, 23), "'x'", " ",
                   MakeLocation(7, 26)},
                  {Token::IDENTIFIER, MakeLocation(37, 38), "b89", " ",
                   MakeLocation(27, 49)},
                  {Token::COMMA, MakeLocation(49, 50), ",", ""},
                  {Token::IDENTIFIER, MakeLocation(kDefsFileName, 25, 26), "a",
                   " ", MakeLocation(51, 72)},
                  {Token::COMMA, MakeLocation(kDefsFileName, 27, 28), ",", " ",
                   MakeLocation(51, 72)},
                  {Token::IDENTIFIER, MakeLocation(66, 67), "b", " ",
                   MakeLocation(51, 72)},
                  {Token::COMMA, MakeLocation(kDefsFileName, 31, 32), ",", "",
                   MakeLocation(51, 72)},
                  {Token::IDENTIFIER, MakeLocation(69, 70), "c123", " ",
                   MakeLocation(51, 72)},
                  {Token::KW_FROM, MakeLocation(kDefsFileName, 102, 106),
                   "FROM", " ", MakeLocation(76, 97)},
                  {Token::IDENTIFIER, MakeLocation(91, 94), "tbl_123", " ",
                   MakeLocation(76, 97)},
                  {Token::EOI, MakeLocation(98, 98), "", " "},
              })));
}

TEST(MacroExpanderTest, ExpandsArgsThatHaveParensAndCommas) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO repeat $1, $1, $2, $2;", macro_catalog);

  EXPECT_THAT(
      ExpandMacros("select $repeat(1, (2,3))", macro_catalog,
                   /*is_strict=*/true),
      IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
          {Token::KW_SELECT, MakeLocation(0, 6), "select", ""},
          {Token::DECIMAL_INTEGER_LITERAL, MakeLocation(15, 16), "1", " ",
           MakeLocation(7, 24)},
          {Token::COMMA, MakeLocation(kDefsFileName, 22, 23), ",", "",
           MakeLocation(7, 24)},
          {Token::DECIMAL_INTEGER_LITERAL, MakeLocation(15, 16), "1", " ",
           MakeLocation(7, 24)},
          {Token::COMMA, MakeLocation(kDefsFileName, 26, 27), ",", "",
           MakeLocation(7, 24)},
          {Token::LPAREN, MakeLocation(18, 19), "(", " ", MakeLocation(7, 24)},
          {Token::DECIMAL_INTEGER_LITERAL, MakeLocation(19, 20), "2", "",
           MakeLocation(7, 24)},
          {Token::COMMA, MakeLocation(20, 21), ",", "", MakeLocation(7, 24)},
          {Token::DECIMAL_INTEGER_LITERAL, MakeLocation(21, 22), "3", "",
           MakeLocation(7, 24)},
          {Token::RPAREN, MakeLocation(22, 23), ")", "", MakeLocation(7, 24)},
          {Token::COMMA, MakeLocation(kDefsFileName, 30, 31), ",", "",
           MakeLocation(7, 24)},
          {Token::LPAREN, MakeLocation(18, 19), "(", " ", MakeLocation(7, 24)},
          {Token::DECIMAL_INTEGER_LITERAL, MakeLocation(19, 20), "2", "",
           MakeLocation(7, 24)},
          {Token::COMMA, MakeLocation(20, 21), ",", "", MakeLocation(7, 24)},
          {Token::DECIMAL_INTEGER_LITERAL, MakeLocation(21, 22), "3", "",
           MakeLocation(7, 24)},
          {Token::RPAREN, MakeLocation(22, 23), ")", "", MakeLocation(7, 24)},
          {Token::EOI, MakeLocation(24, 24), "", ""}})));
}

TEST(MacroExpanderTest, ExtraArgsProduceWarningOrError) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO empty;", macro_catalog);

  EXPECT_THAT(ExpandMacros("$empty(x)", macro_catalog,
                           /*is_strict=*/false),
              IsOkAndHolds(AllOf(
                  TokensEq(std::vector<TokenWithLocation>{
                      {Token::EOI, MakeLocation(9, 9), "", ""}}),
                  HasWarnings(ElementsAre(StatusIs(
                      _, Eq("Macro invocation has too many arguments (1) "
                            "while the definition only references up to 0 "
                            "arguments [at top_file.sql:1:1]")))))));

  EXPECT_THAT(
      ExpandMacros("$empty(x)", macro_catalog,
                   /*is_strict=*/true),
      StatusIs(_, Eq("Macro invocation has too many arguments (1) while "
                     "the definition only references up to 0 arguments "
                     "[at top_file.sql:1:1]")));
}

TEST(MacroExpanderTest, UnknownArgsAreLeftUntouched) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO empty;\n"
      "DEFINE MACRO unknown_not_in_a_literal\n"
      "x$3\n"
      ";\n"
      "DEFINE MACRO unknown_inside_literal'\ty$3\t';\n",
      macro_catalog);

  EXPECT_THAT(
      ExpandMacros(
          "  a$empty()$1$unknown_not_in_a_literal  '$unknown_not_in_a_literal' "
          "$unknown_inside_literal\t$1\n'$2'",
          macro_catalog, /*is_strict=*/false,
          {.error_message_options =
               {.mode = ErrorMessageMode::ERROR_MESSAGE_ONE_LINE},
           .max_warning_count = 10}),
      IsOkAndHolds(AllOf(
          TokensEq(std::vector<TokenWithLocation>{
              {Token::IDENTIFIER, MakeLocation(2, 3), "a", "  "},
              {Token::MACRO_ARGUMENT_REFERENCE, MakeLocation(11, 13), "$1", ""},
              {Token::IDENTIFIER, MakeLocation(kDefsFileName, 77, 78), "x", "",
               MakeLocation(13, 38)},
              {Token::STRING_LITERAL, MakeLocation(kDefsFileName, 77, 78),
               "'x'", "  "},
              {Token::STRING_LITERAL, MakeLocation(kDefsFileName, 162, 169),
               "'\ty\t'", " ", MakeLocation(68, 91)},
              {Token::MACRO_ARGUMENT_REFERENCE, MakeLocation(92, 94), "$1",
               "\t"},
              {Token::STRING_LITERAL, MakeLocation(96, 98), "'$2'", "\n"},
              {Token::EOI, MakeLocation(99, 99), "", ""}}),
          HasWarnings(ElementsAre(
              StatusIs(_, Eq("Invocation of macro 'unknown_not_in_a_literal' "
                             "missing argument list. [at top_file.sql:1:39]")),
              StatusIs(_, Eq("Argument index $3 out of range. Invocation was "
                             "provided only 0 arguments. [at defs.sql:3:2]; "
                             "Expanded from macro:unknown_not_in_a_literal [at "
                             "top_file.sql:1:14]")),
              StatusIs(
                  _,
                  Eq("Macro expansion in literals is deprecated. Strict mode "
                     "does not expand literals [at top_file.sql:1:42]")),
              StatusIs(_, Eq("Invocation of macro 'unknown_not_in_a_literal' "
                             "missing argument list. [at top_file.sql:1:67]")),
              StatusIs(_, Eq("Argument index $3 out of range. Invocation was "
                             "provided only 0 arguments. [at defs.sql:3:2]; "
                             "Expanded from macro:unknown_not_in_a_literal [at "
                             "top_file.sql:1:42]")),
              StatusIs(_, Eq("Invocation of macro 'unknown_inside_literal' "
                             "missing argument list. [at top_file.sql:1:92]")),
              StatusIs(_,
                       Eq("Macro expansion in literals is deprecated. Strict "
                          "mode does not expand literals [at defs.sql:5:42]; "
                          "Expanded from macro:unknown_inside_literal [at "
                          "top_file.sql:1:69]")),
              StatusIs(_, Eq("Argument index $3 out of range. Invocation was "
                             "provided only 0 arguments. [at defs.sql:5:42]; "
                             "Expanded from macro:unknown_inside_literal [at "
                             "top_file.sql:1:69]")),
              StatusIs(
                  _,
                  Eq("Macro expansion in literals is deprecated. Strict mode "
                     "does not expand literals [at top_file.sql:2:2]")))))));

  EXPECT_THAT(
      ExpandMacros("unknown_inside_literal() $unknown_not_in_a_literal()",
                   macro_catalog, /*is_strict=*/true),
      StatusIs(_, Eq("Argument index $3 out of range. Invocation was provided "
                     "only 1 arguments. [at defs.sql:3:2]; "
                     "Expanded from macro:unknown_not_in_a_literal [at "
                     "top_file.sql:1:26]")));
}

TEST(MacroExpanderTest, ExpandsStandaloneDollarSignsAtTopLevel) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO empty\n\n;", macro_catalog);

  EXPECT_THAT(ExpandMacros("\t\t$empty${a}   \n$$$empty${b}$", macro_catalog,
                           /*is_strict=*/false),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Builtin macro 'empty' not found")));

  EXPECT_THAT(ExpandMacros("\t\t$empty${a}   \n$empty${b}$", macro_catalog,
                           /*is_strict=*/false),
              IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
                  {Token::DOLLAR_SIGN, MakeLocation(8, 9), "$", "\t\t"},
                  {Token::LBRACE, MakeLocation(9, 10), "{", ""},
                  {Token::IDENTIFIER, MakeLocation(10, 11), "a", ""},
                  {Token::RBRACE, MakeLocation(11, 12), "}", ""},
                  {Token::DOLLAR_SIGN, MakeLocation(22, 23), "$", "   \n"},
                  {Token::LBRACE, MakeLocation(23, 24), "{", ""},
                  {Token::IDENTIFIER, MakeLocation(24, 25), "b", ""},
                  {Token::RBRACE, MakeLocation(25, 26), "}", ""},
                  {Token::DOLLAR_SIGN, MakeLocation(26, 27), "$", ""},
                  {Token::EOI, MakeLocation(27, 27), "", ""}})));
}

TEST(MacroExpanderTest, ProducesWarningOrErrorOnMissingArgumentLists) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO m 1;", macro_catalog);

  EXPECT_THAT(ExpandMacros("$m", macro_catalog,
                           /*is_strict=*/false),
              IsOkAndHolds(AllOf(
                  TokensEq(std::vector<TokenWithLocation>{
                      {Token::DECIMAL_INTEGER_LITERAL,
                       MakeLocation(kDefsFileName, 15, 16), "1", "",
                       MakeLocation(0, 2)},
                      {Token::EOI, MakeLocation(2, 2), "", ""}}),
                  HasWarnings(ElementsAre(StatusIs(
                      _, Eq("Invocation of macro 'm' missing argument list. "
                            "[at top_file.sql:1:3]")))))));

  EXPECT_THAT(
      ExpandMacros("$m", macro_catalog, /*is_strict=*/true),
      StatusIs(_, Eq("Invocation of macro 'm' missing argument list. [at "
                     "top_file.sql:1:3]")));
}

TEST(MacroExpanderTest, WarningsAndErrorsAccountForExternalOffsets) {
  MacroCatalog macro_catalog;
  RegisterMacros(R"(   #line 1
   -- line 2
DEFINE MACRO one 1;
DEFINE MACRO repeat $1, $1;
# extra line
/*indent a bit*/ DEFINE MACRO m $repeat( /*stuff*/ $one  --more stuff
  );
)",
                 macro_catalog);

  EXPECT_THAT(
      ExpandMacros("$m()", macro_catalog,
                   /*is_strict=*/false),
      IsOkAndHolds(AllOf(
          TokensEq(std::vector<TokenWithLocation>{
              {Token::DECIMAL_INTEGER_LITERAL,
               MakeLocation(kDefsFileName, 41, 42), "1", "",
               MakeLocation(0, 4)},
              {Token::COMMA, MakeLocation(kDefsFileName, 109, 110), ",", "",
               MakeLocation(0, 4)},
              {Token::DECIMAL_INTEGER_LITERAL,
               MakeLocation(kDefsFileName, 41, 42), "1", " ",
               MakeLocation(0, 4)},
              {Token::EOI, MakeLocation(4, 4), "", ""}}),
          HasWarnings(ElementsAre(StatusIs(
              _, Eq("Invocation of macro 'one' missing argument list. [at "
                    "defs.sql:6:56]; "
                    "Which is arg:$1, and got created at [at defs.sql:6:41]; "
                    "Expanded from macro:repeat [at defs.sql:6:33]; "
                    "Expanded from macro:m [at top_file.sql:1:1]")))))));

  EXPECT_THAT(
      ExpandMacros("$m()", macro_catalog,
                   /*is_strict=*/true),
      StatusIs(_, Eq("Invocation of macro 'one' missing argument list. "
                     "[at defs.sql:6:56]; "
                     "Which is arg:$1, and got created at [at defs.sql:6:41]; "
                     "Expanded from macro:repeat [at defs.sql:6:33]; "
                     "Expanded from macro:m [at top_file.sql:1:1]")));
}

TEST(MacroExpanderTest, ExpandsAllFormsOfLiterals) {
  MacroCatalog macro_catalog;
  std::vector<std::string> string_literals{
      "'a'",  "'''a'''",  R"("a")",  R"("""a""")",
      "r'a'", "r'''a'''", R"(r"a")", R"(r"""a""")",
  };

  for (const std::string& string_literal : string_literals) {
    int literal_length = static_cast<int>(string_literal.length());
    EXPECT_THAT(ExpandMacros(string_literal, macro_catalog,
                             /*is_strict=*/false),
                IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
                    {Token::STRING_LITERAL, MakeLocation(0, literal_length),
                     string_literal, ""},
                    {Token::EOI, MakeLocation(literal_length, literal_length),
                     "", ""}})));
  }

  std::vector<std::string> byte_literals{
      "b'''a'''",  R"(b"a")",  R"(b"""a""")",  "rb'a'",
      "rb'''a'''", R"(rb"a")", R"(rb"""a""")",
  };

  for (const std::string& byte_literal : byte_literals) {
    int literal_length = static_cast<int>(byte_literal.length());
    EXPECT_THAT(ExpandMacros(byte_literal, macro_catalog,
                             /*is_strict=*/false),
                IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
                    {Token::BYTES_LITERAL, MakeLocation(0, literal_length),
                     byte_literal, ""},
                    {Token::EOI, MakeLocation(literal_length, literal_length),
                     "", ""}})));
  }
}

static Token FinalTokenKind(const QuotingSpec quoting_spec) {
  switch (quoting_spec.literal_kind()) {
    case LiteralTokenKind::kNonLiteral:
      // Tests using this helper only use int literals
      return Token::DECIMAL_INTEGER_LITERAL;
    case LiteralTokenKind::kBacktickedIdentifier:
      return Token::IDENTIFIER;
    case LiteralTokenKind::kStringLiteral:
      return Token::STRING_LITERAL;
    case LiteralTokenKind::kBytesLiteral:
      return Token::BYTES_LITERAL;
  }
}

class PositiveNestedLiteralTest
    : public ::testing::TestWithParam<std::tuple<
          /*outer*/ QuotingSpec, /*inner*/ QuotingSpec>> {};

TEST_P(PositiveNestedLiteralTest, PositiveNestedLiteralTestParameterized) {
  const QuotingSpec outer_quoting = std::get<0>(GetParam());
  const QuotingSpec inner_quoting = std::get<1>(GetParam());

  std::string macro_def = QuoteText("1", inner_quoting);
  MacroCatalog catalog;
  absl::string_view macro_prefix = "DEFINE MACRO a ";
  std::string macro_def_source = absl::StrCat(macro_prefix, macro_def);
  RegisterMacros(macro_def_source, catalog);

  absl::string_view invocation = "$a";
  std::string unexpanded_sql = QuoteText(invocation, outer_quoting);
  int unexpanded_length = static_cast<int>(unexpanded_sql.length());

  std::string expanded = QuoteText("1", outer_quoting);

  Token final_token_kind = FinalTokenKind(outer_quoting);
  if (inner_quoting.literal_kind() == LiteralTokenKind::kNonLiteral ||
      (outer_quoting.quote_kind() == inner_quoting.quote_kind() &&
       outer_quoting.prefix().length() == inner_quoting.prefix().length())) {
    EXPECT_THAT(
        ExpandMacros(unexpanded_sql, catalog,
                     /*is_strict=*/false),
        IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
            {final_token_kind,
             MakeLocation(kDefsFileName,
                          static_cast<int>(macro_prefix.length()),
                          static_cast<int>(macro_def_source.length())),
             QuoteText("1", outer_quoting), ""},
            {Token::EOI, MakeLocation(unexpanded_length, unexpanded_length), "",
             ""},
        })));
  } else {
    ASSERT_TRUE(outer_quoting.literal_kind() == inner_quoting.literal_kind());
    size_t outer_quote_length = (unexpanded_sql.length() - invocation.length() -
                                 outer_quoting.prefix().length()) /
                                2;
    int unexpanded_start_offset =
        static_cast<int>(outer_quoting.prefix().length() + outer_quote_length);
    EXPECT_THAT(
        ExpandMacros(unexpanded_sql, catalog,
                     /*is_strict=*/false),
        IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
            {final_token_kind,
             MakeLocation(kDefsFileName,
                          static_cast<int>(macro_prefix.length()),
                          static_cast<int>(macro_def_source.length())),
             QuoteText("", outer_quoting), ""},
            {final_token_kind,
             MakeLocation(kDefsFileName,
                          static_cast<int>(macro_prefix.length()),
                          static_cast<int>(macro_def_source.length())),
             macro_def, " ",
             MakeLocation(unexpanded_start_offset,
                          unexpanded_start_offset +
                              static_cast<int>(invocation.length()))},
            {final_token_kind,
             MakeLocation(kDefsFileName,
                          static_cast<int>(macro_prefix.length()),
                          static_cast<int>(macro_def_source.length())),
             QuoteText("", outer_quoting), " "},
            {Token::EOI, MakeLocation(unexpanded_length, unexpanded_length), "",
             ""},
        })));
  }
}

class NegativeNestedLiteralTest
    : public ::testing::TestWithParam<std::tuple<
          /*outer*/ QuotingSpec, /*inner*/ QuotingSpec>> {};

TEST_P(NegativeNestedLiteralTest, NegativeNestedLiteralTestParameterized) {
  const QuotingSpec outer_quoting = std::get<0>(GetParam());
  const QuotingSpec inner_quoting = std::get<1>(GetParam());

  std::string macro_def =
      absl::StrCat("DEFINE MACRO a ", QuoteText("1", inner_quoting));
  MacroCatalog catalog;
  RegisterMacros(macro_def, catalog);

  std::string unexpanded_sql = QuoteText("$a", outer_quoting);

  EXPECT_THAT(ExpandMacros(unexpanded_sql, catalog,
                           /*is_strict=*/false),
              StatusIs(_, HasSubstr("Cannot expand a ")));
}

const QuoteKind kAllLiteralQuoteKinds[] = {
    QuoteKind::kOneSingleQuote, QuoteKind::kThreeSingleQuotes,
    QuoteKind::kOneDoubleQuote, QuoteKind::kThreeDoubleQuotes};

static std::vector<QuotingSpec> AllStringLiteralQuotingSpecs() {
  const std::vector<absl::string_view> string_literal_prefixes{"", "r", "R"};
  std::vector<QuotingSpec> string_literals;

  for (QuoteKind quote_kind : kAllLiteralQuoteKinds) {
    for (absl::string_view prefix : string_literal_prefixes) {
      string_literals.push_back(QuotingSpec::StringLiteral(prefix, quote_kind));
    }
  }

  return string_literals;
}

static std::vector<QuotingSpec> AllBytesLiteralQuotingSpecs() {
  const std::vector<absl::string_view> bytes_literal_prefixes{
      "b", "B", "br", "bR", "Br", "BR", "rb", "rB", "Rb", "RB",
  };
  std::vector<QuotingSpec> bytes_literals;

  for (QuoteKind quote_kind : kAllLiteralQuoteKinds) {
    for (absl::string_view prefix : bytes_literal_prefixes) {
      bytes_literals.push_back(QuotingSpec::BytesLiteral(prefix, quote_kind));
    }
  }

  return bytes_literals;
}

static std::vector<QuotingSpec> AllStringsAndBytesLiteralsQuotingSpecs() {
  std::vector<QuotingSpec> quoting_specs;
  for (const QuotingSpec& string_spec : AllStringLiteralQuotingSpecs()) {
    quoting_specs.push_back(string_spec);
  }
  for (const QuotingSpec& bytes_spec : AllBytesLiteralQuotingSpecs()) {
    quoting_specs.push_back(bytes_spec);
  }
  return quoting_specs;
}

// Excludes NON_LITERAL
static std::vector<QuotingSpec> AllQuotingSpecs() {
  std::vector<QuotingSpec> quoting_specs;
  quoting_specs.push_back(QuotingSpec::BacktickedIdentifier());
  for (const QuotingSpec& bytes_spec :
       AllStringsAndBytesLiteralsQuotingSpecs()) {
    quoting_specs.push_back(bytes_spec);
  }

  return quoting_specs;
}

// NON_LITERAL expanded within literal.
INSTANTIATE_TEST_SUITE_P(NonLiteralCanBeNestedEverywhere,
                         PositiveNestedLiteralTest,
                         Combine(ValuesIn(AllQuotingSpecs()),
                                 Values(QuotingSpec::NonLiteral())));

// Backticked identifier only works with backticked identifier on both sides
// (Interaction with non-literals is already covered above).
INSTANTIATE_TEST_SUITE_P(BacktickedIdentifierAcceptsItself,
                         PositiveNestedLiteralTest,
                         Combine(Values(QuotingSpec::BacktickedIdentifier()),
                                 Values(QuotingSpec::BacktickedIdentifier())));
INSTANTIATE_TEST_SUITE_P(
    NestingInsideBacktickedIdentifierIsDisallowed, NegativeNestedLiteralTest,
    Combine(Values(QuotingSpec::BacktickedIdentifier()),
            ValuesIn(AllStringsAndBytesLiteralsQuotingSpecs())));

static std::vector<std::tuple<QuotingSpec, QuotingSpec>>
IdenticalLiteralTestCases() {
  std::vector<std::tuple<QuotingSpec, QuotingSpec>> test_cases;
  for (const QuotingSpec& quoting : AllStringsAndBytesLiteralsQuotingSpecs()) {
    test_cases.push_back(std::make_tuple(quoting, quoting));
  }
  return test_cases;
}

INSTANTIATE_TEST_SUITE_P(LiteralsCanNestInIdenticalQuoting,
                         PositiveNestedLiteralTest,
                         ValuesIn(IdenticalLiteralTestCases()));

static std::vector<std::tuple<QuotingSpec, QuotingSpec>>
DifferentLiteralKindTestCases() {
  std::vector<QuotingSpec> literal_quoting_specs =
      AllStringsAndBytesLiteralsQuotingSpecs();

  std::vector<std::tuple<QuotingSpec, QuotingSpec>> test_cases;
  for (const QuotingSpec& outer : literal_quoting_specs) {
    for (const QuotingSpec& inner : literal_quoting_specs) {
      if (outer.literal_kind() != inner.literal_kind()) {
        test_cases.push_back(std::make_tuple(outer, inner));
      }
    }
  }

  return test_cases;
}

INSTANTIATE_TEST_SUITE_P(LiteralsCannotNestInDifferentTokenKinds,
                         NegativeNestedLiteralTest,
                         ValuesIn(DifferentLiteralKindTestCases()));

INSTANTIATE_TEST_SUITE_P(StringLiteralsCanExpandInAnyStringLiteral,
                         PositiveNestedLiteralTest,
                         Combine(ValuesIn(AllStringLiteralQuotingSpecs()),
                                 ValuesIn(AllStringLiteralQuotingSpecs())));
INSTANTIATE_TEST_SUITE_P(BytesLiteralsCanExpandInAnyBytesLiteral,
                         PositiveNestedLiteralTest,
                         Combine(ValuesIn(AllBytesLiteralQuotingSpecs()),
                                 ValuesIn(AllBytesLiteralQuotingSpecs())));

struct LiteralExpansionTestCase {
  std::string text_unquoted;
  std::string expected_expansion_unquoted;

  std::string ExpectedExpansion(const QuotingSpec& quoting) const {
    return QuoteText(expected_expansion_unquoted, quoting);
  }
};

class LiteralExpansionTestBase
    : public ::testing::TestWithParam<
          std::tuple<LiteralExpansionTestCase, QuotingSpec>> {
 protected:
  static constexpr absl::string_view kBuiltinReferenceWarning =
      "Builtin macros are not expanded inside literals";

  absl::StatusOr<ExpansionOutput> ExpandLiteral(
      const LiteralExpansionTestCase& test_case, const QuotingSpec& quoting) {
    MacroCatalog macro_catalog;
    RegisterMacros("DEFINE MACRO FOO foo_expansion;", macro_catalog);
    const std::string test_macro_def =
        absl::StrFormat("DEFINE MACRO MACRO_UNDER_TEST %s;",
                        QuoteText(test_case.text_unquoted, quoting));
    RegisterMacros(test_macro_def, macro_catalog);
    return ExpandMacros("$MACRO_UNDER_TEST(arg_1_expansion)", macro_catalog,
                        /*is_strict=*/false);
  }
};

class LiteralExpansionWithBuiltinReferenceTest
    : public LiteralExpansionTestBase {};

TEST_P(LiteralExpansionWithBuiltinReferenceTest, EmitsWarnings) {
  const LiteralExpansionTestCase& test_case = std::get<0>(GetParam());
  const QuotingSpec& quoting = std::get<1>(GetParam());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ExpansionOutput& result,
                       ExpandLiteral(test_case, quoting));
  EXPECT_THAT(TokensToString(result.expanded_tokens),
              Eq(test_case.ExpectedExpansion(quoting)));
  EXPECT_THAT(
      result,
      HasWarnings(Contains(StatusIs(_, HasSubstr(kBuiltinReferenceWarning)))));
}

class LiteralExpansionWithoutBuiltinReferenceTest
    : public LiteralExpansionTestBase {};

TEST_P(LiteralExpansionWithoutBuiltinReferenceTest, DoesNotEmitWarnings) {
  const LiteralExpansionTestCase& test_case = std::get<0>(GetParam());
  const QuotingSpec& quoting = std::get<1>(GetParam());
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const ExpansionOutput& result,
                       ExpandLiteral(test_case, quoting));
  EXPECT_THAT(TokensToString(result.expanded_tokens),
              Eq(test_case.ExpectedExpansion(quoting)));
  EXPECT_THAT(result, HasWarnings(Not(Contains(
                          StatusIs(_, HasSubstr(kBuiltinReferenceWarning))))));
}

INSTANTIATE_TEST_SUITE_P(
    LiteralExpansionWithBuiltinReferenceTestSuite,
    LiteralExpansionWithBuiltinReferenceTest,
    Combine(
        ValuesIn(std::vector<LiteralExpansionTestCase>{
            {
                .text_unquoted = "$$FOO",
                .expected_expansion_unquoted = "$foo_expansion",
            },
            {
                .text_unquoted = "$$FOO$$FOO",
                .expected_expansion_unquoted = "$foo_expansion$foo_expansion",
            },
            {
                .text_unquoted = "$$$FOO$$$FOO",
                .expected_expansion_unquoted = "$$foo_expansion$$foo_expansion",
            },
            {
                .text_unquoted = "$$$FOO",
                .expected_expansion_unquoted = "$$foo_expansion",
            },
            {
                .text_unquoted = "$$FOO$$",
                .expected_expansion_unquoted = "$foo_expansion$$",
            },
            {
                .text_unquoted = "$$FOO$$1",
                .expected_expansion_unquoted = "$foo_expansion$arg_1_expansion",
            },
        }),
        ValuesIn(AllStringsAndBytesLiteralsQuotingSpecs())));

INSTANTIATE_TEST_SUITE_P(
    LiteralExpansionWithoutBuiltinReferenceTestSuite,
    LiteralExpansionWithoutBuiltinReferenceTest,
    Combine(ValuesIn(std::vector<LiteralExpansionTestCase>{
                {
                    .text_unquoted = "$FOO",
                    .expected_expansion_unquoted = "foo_expansion",
                },
                {
                    .text_unquoted = "$ FOO",
                    .expected_expansion_unquoted = "$ FOO",
                },
                {
                    .text_unquoted = "$$ FOO",
                    .expected_expansion_unquoted = "$$ FOO",
                },
                {
                    .text_unquoted = "$ $FOO",
                    .expected_expansion_unquoted = "$ foo_expansion",
                },
                {
                    .text_unquoted = "$FOO$",
                    .expected_expansion_unquoted = "foo_expansion$",
                },
                {
                    .text_unquoted = "$1",
                    .expected_expansion_unquoted = "arg_1_expansion",
                },
                {
                    .text_unquoted = "$$1",
                    .expected_expansion_unquoted = "$arg_1_expansion",
                },
                {
                    .text_unquoted = "$$1$$1",
                    .expected_expansion_unquoted =
                        "$arg_1_expansion$arg_1_expansion",
                },
                {
                    .text_unquoted = "$$$1$$$1",
                    .expected_expansion_unquoted =
                        "$$arg_1_expansion$$arg_1_expansion",
                },
                {
                    .text_unquoted = "$$$1",
                    .expected_expansion_unquoted = "$$arg_1_expansion",
                },
                {
                    .text_unquoted = "$ 1",
                    .expected_expansion_unquoted = "$ 1",
                },
                {
                    .text_unquoted = "$$ 1",
                    .expected_expansion_unquoted = "$$ 1",
                },
                {
                    .text_unquoted = "$ $1",
                    .expected_expansion_unquoted = "$ arg_1_expansion",
                },
                {
                    .text_unquoted = "$1$",
                    .expected_expansion_unquoted = "arg_1_expansion$",
                },
                {
                    .text_unquoted = "$$1$$",
                    .expected_expansion_unquoted = "$arg_1_expansion$$",
                },
                {
                    .text_unquoted = "$",
                    .expected_expansion_unquoted = "$",
                },
                {
                    .text_unquoted = "$$",
                    .expected_expansion_unquoted = "$$",
                },
            }),
            ValuesIn(AllStringsAndBytesLiteralsQuotingSpecs())));

class MacroBuiltinsTest : public ::testing::TestWithParam<bool> {};

TEST_P(MacroBuiltinsTest, TestMacroBuiltinInvocation) {
  EXPECT_THAT(ExpandMacros("$$TEST", MacroCatalog(), /*is_strict=*/GetParam()),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Builtin macro 'TEST' not found")));
}

INSTANTIATE_TEST_SUITE_P(MacroBuiltinsTestSuite, MacroBuiltinsTest, Bool());

TEST(MacroExpanderTest, DoesNotReexpand) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO select_list a , $1, $2  ;\n"
      "DEFINE MACRO splice_invoke    $$1$2   $$$1$2;\n"
      "DEFINE MACRO inner_quoted_id `bq`;\n",
      macro_catalog);

  EXPECT_THAT(ExpandMacros("$splice_invoke(  inner_\t,\t\tquoted_id   ), "
                           "$ $select_list(  b, c  )123\t",
                           macro_catalog, /*is_strict=*/false),
              IsOkAndHolds(TokensEq(std::vector<TokenWithLocation>{
                  // Note: the dollar sign is not spliced with the identifier
                  // into a new
                  // macro invocation token, because we do not reexpand.
                  {Token::DOLLAR_SIGN, MakeLocation(kDefsFileName, 107, 108),
                   "$", "", MakeLocation(0, 40)},
                  {Token::IDENTIFIER, MakeLocation(17, 23), "inner_quoted_id",
                   "", MakeLocation(0, 40)},
                  {Token::DOLLAR_SIGN, MakeLocation(kDefsFileName, 115, 116),
                   "$", "   ", MakeLocation(0, 40)},
                  {Token::DOLLAR_SIGN, MakeLocation(kDefsFileName, 116, 117),
                   "$", "", MakeLocation(0, 40)},
                  {Token::IDENTIFIER, MakeLocation(17, 23), "inner_quoted_id",
                   "", MakeLocation(0, 40)},
                  {Token::COMMA, MakeLocation(40, 41), ",", ""},
                  {Token::DOLLAR_SIGN, MakeLocation(42, 43), "$", " "},
                  {Token::IDENTIFIER, MakeLocation(kDefsFileName, 25, 26), "a",
                   " ", MakeLocation(44, 66)},
                  {Token::COMMA, MakeLocation(kDefsFileName, 27, 28), ",", " ",
                   MakeLocation(44, 66)},
                  {Token::IDENTIFIER, MakeLocation(59, 60), "b", " ",
                   MakeLocation(44, 66)},
                  // The next comma originates from the macro def
                  {Token::COMMA, MakeLocation(kDefsFileName, 31, 32), ",", "",
                   MakeLocation(44, 66)},
                  {Token::IDENTIFIER, MakeLocation(62, 63), "c123", " ",
                   MakeLocation(44, 66)},
                  {Token::EOI, MakeLocation(70, 70), "", "\t"},
              })));
}

TEST(MacroExpanderTest,
     CapsWarningCountAndAddsSentinelWarningWhenTooManyAreProduced) {
  MacroCatalog macro_catalog;

  int num_slashes = 30;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      ExpansionOutput result,
      ExpandMacros(std::string(num_slashes, '\\'), macro_catalog,
                   /*is_strict=*/false));

  // Add 1 for the YYEOF
  ASSERT_EQ(result.expanded_tokens.size(), num_slashes + 1);
  for (int i = 0; i < num_slashes - 1; ++i) {
    EXPECT_EQ(result.expanded_tokens[i].kind, Token::BACKSLASH);
  }
  EXPECT_EQ(result.expanded_tokens.back().kind, Token::EOI);

  // The number of warnings is the cap (5) + 1 for the sentinel indicating that
  // more warnings were truncated.
  EXPECT_EQ(result.warnings.size(), 6);
  absl::Status sentinel_status = std::move(result.warnings.back());
  result.warnings.pop_back();
  EXPECT_THAT(
      result.warnings,
      Each(StatusIs(
          _,
          HasSubstr(
              R"(Invalid token (\). Did you mean to use it in a literal?)"))));
  EXPECT_THAT(
      sentinel_status,
      StatusIs(_,
               Eq("Warning count limit reached. Truncating further warnings")));
}

// Ignores location
static TokenWithLocation MakeToken(
    Token kind, absl::string_view text,
    absl::string_view preceding_whitespaces = "") {
  return {.kind = kind,
          .location = MakeLocation(-1, -1),
          .text = text,
          .preceding_whitespaces = preceding_whitespaces};
}

TEST(TokensToStringTest, CanGenerateStringFromTokens) {
  std::vector<TokenWithLocation> tokens = {
      MakeToken(Token::IDENTIFIER, "a", "\t"),
      MakeToken(Token::KW_FROM, "FROM"), MakeToken(Token::EOI, "", "\n")};

  // Note the forced space between `a` and `FROM`.
  EXPECT_EQ(TokensToString(tokens), "\ta FROM\n");
}

TEST(TokensToStringTest, DoesNotSpliceTokensEvenWhenNoOriginalSpacesExist) {
  std::vector<TokenWithLocation> tokens = {
      MakeToken(Token::IDENTIFIER, "a", "\t"),
      MakeToken(Token::KW_FROM, "FROM"),
      MakeToken(Token::DECIMAL_INTEGER_LITERAL, "0x1A"),
      MakeToken(Token::IDENTIFIER, "b", "\t"),
      MakeToken(Token::KW_FROM, "FROM"),
      MakeToken(Token::FLOATING_POINT_LITERAL, "1."),
      MakeToken(Token::IDENTIFIER, "x", "\t"),
      MakeToken(Token::MACRO_INVOCATION, "$a"),
      MakeToken(Token::DECIMAL_INTEGER_LITERAL, "123"),
      MakeToken(Token::MACRO_INVOCATION, "$1"),
      MakeToken(Token::DECIMAL_INTEGER_LITERAL, "23"),
      MakeToken(Token::MULT, "*"),
      MakeToken(Token::EOI, "", "\n")};

  // Note the forced spaces
  EXPECT_EQ(TokensToString(tokens),
            "\ta FROM 0x1A\tb FROM 1.\tx$a 123$1 23*\n");
}

TEST(TokensToStringTest, DoesNotCauseCommentOuts) {
  std::vector<TokenWithLocation> tokens = {
      MakeToken(Token::MINUS, "-"), MakeToken(Token::MINUS, "-"),
      MakeToken(Token::DIV, "/"),   MakeToken(Token::DIV, "/"),
      MakeToken(Token::DIV, "/"),   MakeToken(Token::MULT, "*"),
      MakeToken(Token::EOI, "")};

  // Note the forced spaces, except for -/
  EXPECT_EQ(TokensToString(tokens), "- -/ / / *");
}

TEST(TokensToStringTest, AlwaysSeparatesNumericLiterals) {
  std::vector<TokenWithLocation> tokens = {
      MakeToken(Token::DECIMAL_INTEGER_LITERAL, "0x1"),
      MakeToken(Token::DECIMAL_INTEGER_LITERAL, "2"),
      MakeToken(Token::DECIMAL_INTEGER_LITERAL, "3"),
      MakeToken(Token::FLOATING_POINT_LITERAL, "4."),
      MakeToken(Token::DECIMAL_INTEGER_LITERAL, "5"),
      MakeToken(Token::FLOATING_POINT_LITERAL, ".6"),
      MakeToken(Token::DECIMAL_INTEGER_LITERAL, "7"),
      MakeToken(Token::FLOATING_POINT_LITERAL, ".8e9"),
      MakeToken(Token::DECIMAL_INTEGER_LITERAL, "10"),
      MakeToken(Token::STRING_LITERAL, "'11'"),
      MakeToken(Token::EOI, "")};

  // Note the forced spaces
  EXPECT_EQ(TokensToString(tokens), "0x1 2 3 4. 5 .6 7 .8e9 10'11'");
}

class MacroExpanderLenientTokensTest
    : public ::testing::TestWithParam<absl::string_view> {};

TEST_P(MacroExpanderLenientTokensTest, LenientTokensAllowedOnlyWhenLenient) {
  absl::string_view text = GetParam();
  std::string macro_def = absl::StrCat("DEFINE MACRO m ", text);
  MacroCatalog macro_catalog;
  RegisterMacros(macro_def, macro_catalog);

  int expected_token_count = 2;  // One token + YYEOF
  if (absl::StrContains(text, '.')) {
    // The dot itself, and maybe the other part
    expected_token_count += absl::StartsWith(text, ".") ? 1 : 2;
    if (absl::StrContains(text, "e-")) {
      expected_token_count +=
          2;  // The negative exponent adds '-' & the exp part
    }
  }

  EXPECT_THAT(ExpandMacros(text, macro_catalog,
                           /*is_strict=*/false),
              IsOkAndHolds(HasTokens(SizeIs(expected_token_count))));

  EXPECT_THAT(ExpandMacros("$m", macro_catalog,
                           /*is_strict=*/false),
              IsOkAndHolds(HasTokens(SizeIs(expected_token_count))));

  // When preceding with 'a', we need to update the expected token count as the
  // 'a' will splice with the expanded text unless it's a symbol.
  if (!std::isalpha(text.front()) && !std::isdigit(text.front())) {
    expected_token_count++;
  }

  EXPECT_THAT(ExpandMacros("a$m", macro_catalog,
                           /*is_strict=*/false),
              IsOkAndHolds(HasTokens(SizeIs(expected_token_count))));
}

INSTANTIATE_TEST_SUITE_P(LenientTokens, MacroExpanderLenientTokensTest,
                         Values("\\"));

struct LiteralsFollowedByAdjacentIdentifiersTestCase {
  // The macro body text, for example ".123a".
  absl::string_view text;
  // Whether to enable the STRICT macro mode.
  bool is_strict;
  // The expected number of tokens corresponding to `text`.
  int token_count;
  // The expected number of tokens corresponding to `absl::StrCat("a", text)`.
  // This is to test the identifier splicing during macro expansions.
  int token_count_after_splicing;
};

class LiteralsFollowedByAdjacentIdentifiersTest
    : public ::testing::TestWithParam<
          LiteralsFollowedByAdjacentIdentifiersTestCase> {};

TEST_P(LiteralsFollowedByAdjacentIdentifiersTest,
       LiteralsFollowedByAdjacentIdentifiers) {
  absl::string_view text = GetParam().text;
  bool is_strict = GetParam().is_strict;
  int expected_token_count = GetParam().token_count;
  int expected_token_count_after_splicing =
      GetParam().token_count_after_splicing;

  MacroCatalog macro_catalog;
  std::string macro_def = absl::StrCat("DEFINE MACRO m ", text);
  RegisterMacros(macro_def, macro_catalog);

  EXPECT_THAT(ExpandMacros(text, macro_catalog, is_strict),
              IsOkAndHolds(HasTokens(SizeIs(expected_token_count))));

  EXPECT_THAT(ExpandMacros("$m()", macro_catalog, is_strict),
              IsOkAndHolds(HasTokens(SizeIs(expected_token_count))));

  // Test identifier splicing.
  EXPECT_THAT(
      ExpandMacros("a$m()", macro_catalog, is_strict),
      IsOkAndHolds(HasTokens(SizeIs(expected_token_count_after_splicing))));
}

std::vector<LiteralsFollowedByAdjacentIdentifiersTestCase>
GetLiteralsFollowedByAdjacentIdentifiersTestCases() {
  std::vector<LiteralsFollowedByAdjacentIdentifiersTestCase> cases;
  for (bool is_strict : {false, true}) {
    cases.push_back({
        .text = ".4dab",
        .is_strict = is_strict,
        // Tokens are: . 4 dab YYEOF
        .token_count = 4,
        // After prepending "a", tokens are: a . 4 dab YYEOF
        .token_count_after_splicing = 5,
    });
  }
  // Identifier splicing is only allowed under lenient mode.
  cases.push_back({
      .text = "3d",
      .is_strict = false,
      // Tokens are: 3 d YYEOF
      .token_count = 3,
      // After prepending "a", tokens are: a3 d YYEOF
      .token_count_after_splicing = 3,
  });
  cases.push_back({
      .text = "12abc",
      .is_strict = false,
      // Tokens are: 12 abc YYEOF
      .token_count = 3,
      // After prepending "a", tokens are: a12 abc YYEOF
      .token_count_after_splicing = 3,
  });
  cases.push_back({
      .text = "0x123xyz",
      .is_strict = false,
      // Tokens are: 0x123 xyz YYEOF
      .token_count = 3,
      // After prepending "a", tokens are: a0x123 xyz YYEOF
      .token_count_after_splicing = 3,
  });
  cases.push_back({
      .text = "3.4dab",
      .is_strict = false,
      // Tokens are: 3 . 4 dab YYEOF
      .token_count = 5,
      // After prepending "a", tokens are: a3 . 4 dab YYEOF
      .token_count_after_splicing = 5,
  });
  cases.push_back({
      .text = "1.2e3ab",
      .is_strict = false,
      // Tokens are: 1 . 2 e3ab YYEOF
      .token_count = 5,
      // After prepending "a", tokens are: a1 . 2e3 ab YYEOF
      .token_count_after_splicing = 5,
  });
  cases.push_back({
      .text = "1.2e-3ab",
      .is_strict = false,
      // Tokens are: 1 . 2 e - 3 ab YYEOF
      .token_count = 8,
      // After prepending "a", tokens are: a1 . 2 e - 3 ab YYEOF
      .token_count_after_splicing = 8,
  });
  return cases;
}

INSTANTIATE_TEST_SUITE_P(
    LiteralsFollowedByAdjacentIdentifiers,
    LiteralsFollowedByAdjacentIdentifiersTest,
    ValuesIn(GetLiteralsFollowedByAdjacentIdentifiersTestCases()));

struct SplicingNotAllowedTestCase {
  absl::string_view macro_def;
  absl::string_view text;
  absl::string_view error_message;
};

std::vector<SplicingNotAllowedTestCase> GetSplicingNotAllowedTestCases() {
  return {
      {
          .macro_def = "DEFINE MACRO m 3d",
          .text = "a$m()",
          .error_message = "Splicing tokens (a) and (3)",
      },
      {
          .macro_def = "DEFINE MACRO m 12abc",
          .text = "a$m()",
          .error_message = "Splicing tokens (a) and (12)",
      },
      {
          .macro_def = "DEFINE MACRO m 0x123xyz",
          .text = "a$m()",
          .error_message = "Splicing tokens (a) and (0x123)",
      },
  };
}

class SplicingNotAllowedUnderStrictModeTest
    : public ::testing::TestWithParam<SplicingNotAllowedTestCase> {};

TEST_P(SplicingNotAllowedUnderStrictModeTest,
       SplicingNotAllowedUnderStrictMode) {
  MacroCatalog macro_catalog;
  RegisterMacros(GetParam().macro_def, macro_catalog);
  EXPECT_THAT(ExpandMacros(GetParam().text, macro_catalog, /*is_strict=*/true),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr(GetParam().error_message)));
}

INSTANTIATE_TEST_SUITE_P(SplicingNotAllowedUnderStrictMode,
                         SplicingNotAllowedUnderStrictModeTest,
                         ValuesIn(GetSplicingNotAllowedTestCases()));

TEST(MacroExpanderTest, ProducesWarningOnLenientTokens) {
  MacroCatalog macro_catalog;

  EXPECT_THAT(ExpandMacros("\\", macro_catalog,
                           /*is_strict=*/false),
              IsOkAndHolds(HasWarnings(ElementsAre(
                  StatusIs(_, Eq("Invalid token (\\). Did you mean to use it "
                                 "in a literal? [at top_file.sql:1:1]"))))));
}

class MacroExpanderParameterizedTest : public ::testing::TestWithParam<bool> {};

TEST_P(MacroExpanderParameterizedTest, DoesNotExpandDefineMacroStatements) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO x a;\n"
      "DEFINE MACRO y b;\n",
      macro_catalog);

  EXPECT_THAT(
      ExpandMacros("DEFINE MACRO $x $y; DEFINE MACRO $y $x", macro_catalog,
                   /*is_strict=*/GetParam()),
      IsOkAndHolds(AllOf(
          TokensEq(std::vector<TokenWithLocation>{
              {Token::KW_DEFINE_FOR_MACROS, MakeLocation(0, 6), "DEFINE", ""},
              {Token::KW_MACRO, MakeLocation(7, 12), "MACRO", " "},
              {Token::MACRO_INVOCATION, MakeLocation(13, 15), "$x", " "},
              {Token::MACRO_INVOCATION, MakeLocation(16, 18), "$y", " "},
              {Token::SEMICOLON, MakeLocation(18, 19), ";", ""},
              {Token::KW_DEFINE_FOR_MACROS, MakeLocation(20, 26), "DEFINE",
               " "},
              {Token::KW_MACRO, MakeLocation(27, 32), "MACRO", " "},
              {Token::MACRO_INVOCATION, MakeLocation(33, 35), "$y", " "},
              {Token::MACRO_INVOCATION, MakeLocation(36, 38), "$x", " "},
              {Token::EOI, MakeLocation(38, 38), "", ""},
          }),
          HasWarnings(IsEmpty()))));
}

TEST_P(MacroExpanderParameterizedTest,
       DefineStatementsAreNotSpecialExceptAtTheStart) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO x a;\n", macro_catalog);

  // This will expand the definition, but it will be a syntax error when sent
  // to the parser, because the KW_DEFINE has not been marked as the special
  // KW_DEFINE_FOR_MACROS which has to be at the start, and original (not
  // expanded from a macro).
  EXPECT_THAT(
      ExpandMacros("SELECT DEFINE MACRO $x() 1", macro_catalog,
                   /*is_strict=*/GetParam()),
      IsOkAndHolds(AllOf(
          TokensEq(std::vector<TokenWithLocation>{
              {Token::KW_SELECT, MakeLocation(0, 6), "SELECT", ""},
              {Token::KW_DEFINE, MakeLocation(7, 13), "DEFINE", " "},
              {Token::KW_MACRO, MakeLocation(14, 19), "MACRO", " "},
              {Token::IDENTIFIER, MakeLocation(kDefsFileName, 15, 16), "a", " ",
               MakeLocation(20, 24)},
              {Token::DECIMAL_INTEGER_LITERAL, MakeLocation(25, 26), "1", " "},
              {Token::EOI, MakeLocation(26, 26), "", ""},
          }),
          HasWarnings(IsEmpty()))));
}

TEST_P(MacroExpanderParameterizedTest,
       DoesNotRecognizeGeneratedDefineMacroStatements) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO def DEFINE MACRO;\n", macro_catalog);

  EXPECT_THAT(
      ExpandMacros("$def() a 1", macro_catalog,
                   /*is_strict=*/GetParam()),
      IsOkAndHolds(AllOf(
          TokensEq(std::vector<TokenWithLocation>{
              // Note that the first token is KW_DEFINE, not
              // KW_DEFINE_FOR_MACROS
              {Token::KW_DEFINE, MakeLocation(kDefsFileName, 17, 23), "DEFINE",
               "", MakeLocation(0, 6)},
              {Token::KW_MACRO, MakeLocation(kDefsFileName, 24, 29), "MACRO",
               " ", MakeLocation(0, 6)},
              {Token::IDENTIFIER, MakeLocation(7, 8), "a", " "},
              {Token::DECIMAL_INTEGER_LITERAL, MakeLocation(9, 10), "1", " "},
              {Token::EOI, MakeLocation(10, 10), "", ""},
          }),
          HasWarnings(IsEmpty()))));
}

TEST_P(MacroExpanderParameterizedTest, HandlesInfiniteRecursion) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO a $b();\n"
      "DEFINE MACRO b $a();\n",
      macro_catalog);

  EXPECT_THAT(ExpandMacros("$a()", macro_catalog,
                           /*is_strict=*/GetParam()),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Cycle detected in macro expansion")));
}

static void PrintTo(const Expansion& expansion, std::ostream* os) {
  *os << absl::StrFormat("{macro_name: %s, invocation: %s, expansion: %s}",
                         expansion.macro_name, expansion.full_match,
                         expansion.expansion);
}

TEST_P(MacroExpanderParameterizedTest, GeneratesCorrectLocationMap) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO a\n"
      "$b(  /*nothing*/  )\n"
      ";\n"
      "DEFINE MACRO b\n"
      "1;\n",
      macro_catalog);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      ExpansionOutput output,
      ExpandMacros("\t\t$a( /*just a comment*/  )\t\t", macro_catalog,
                   /*is_strict=*/GetParam()));
  EXPECT_THAT(output, TokensEq(std::vector<TokenWithLocation>{
                          {Token::DECIMAL_INTEGER_LITERAL,
                           MakeLocation(kDefsFileName, 88, 89), "1", "\t\t",
                           MakeLocation(2, 27)},
                          {Token::EOI, MakeLocation(29, 29), "", "\t\t"}}));
  EXPECT_EQ(TokensToString(output.expanded_tokens), "\t\t1\t\t");

  // Only the top-level expansions are stored in the map.
  EXPECT_THAT(
      output.location_map,
      ElementsAre(Pair(2, FieldsAre("a", "$a( /*just a comment*/  )", "1"))));
}

TEST_P(MacroExpanderParameterizedTest, TokensToStringTwoGreaterThanSymbols) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO gt >;\n"
      "DEFINE MACRO right_shift >>;\n"
      "DEFINE MACRO splice $1$2;\n"
      "DEFINE MACRO repeat $1 $1\n",
      macro_catalog);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      ExpansionOutput output,
      ExpandMacros("1 $gt()$gt() 2 $right_shift() 3 $splice(>,>) 4 $repeat(>>)",
                   macro_catalog,
                   /*is_strict=*/GetParam()));
  EXPECT_EQ(TokensToString(output.expanded_tokens), "1 > > 2 >> 3 > > 4 >> >>");
}

TEST_P(MacroExpanderParameterizedTest, TokensToStringAdjacentTokens) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO m a+b;", macro_catalog);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ExpansionOutput output,
                       ExpandMacros("$m()", macro_catalog,
                                    /*is_strict=*/GetParam()));
  EXPECT_EQ(TokensToString(output.expanded_tokens), "a+b");
}

TEST_P(MacroExpanderParameterizedTest, GeneratesCorrectLocationMap_Multiple) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO macro xyz;\n"
      "DEFINE MACRO macrowithargs $3, $2, $1, $2;\n"
      "DEFINE MACRO macrowithmacro owner.$macro();\n"
      "DEFINE MACRO limit 10;\n",
      macro_catalog);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      ExpansionOutput output,
      ExpandMacros("select $macro(), $macrowithargs(a, b, c) from "
                   "$macrowithmacro() limit $limit()",
                   macro_catalog,
                   /*is_strict=*/GetParam()));
  EXPECT_EQ(TokensToString(output.expanded_tokens),
            "select xyz, c, b, a, b from owner.xyz limit 10");

  // Only the top-level expansions are stored in the map.
  EXPECT_THAT(
      output.location_map,
      ElementsAre(Pair(7, FieldsAre("macro", "$macro()", "xyz")),
                  Pair(17, FieldsAre("macrowithargs", "$macrowithargs(a, b, c)",
                                     "c, b, a, b")),
                  Pair(46, FieldsAre("macrowithmacro", "$macrowithmacro()",
                                     "owner.xyz")),
                  Pair(70, FieldsAre("limit", "$limit()", "10"))));
}

TEST_P(MacroExpanderParameterizedTest,
       EmptyArgumentAcceptedWhenMacroAcceptsZeroOrSingleArgument) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO no_args 'nullary';\n"
      "DEFINE MACRO single_arg $1;\n",
      macro_catalog);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ExpansionOutput output,
                       ExpandMacros("$no_args( /*comment1*/   /*comment2*/)\n"
                                    "$single_arg(  /*comment3*/  /*comment4*/)",
                                    macro_catalog,
                                    /*is_strict=*/GetParam()));
  EXPECT_THAT(output,
              TokensEq(std::vector<TokenWithLocation>{
                  {Token::STRING_LITERAL, MakeLocation(kDefsFileName, 21, 30),
                   "'nullary'", "", MakeLocation(0, 38)},
                  {Token::EOI, MakeLocation(80, 80), "", "\n"},
              }));
  EXPECT_EQ(TokensToString(output.expanded_tokens), "'nullary'\n");
}

TEST_P(
    MacroExpanderParameterizedTest,
    CorrectErrorWhenSingleArgumentPassedToNullaryMacroEvenWhenExpandsToEmpty) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO empty;\n"
      "DEFINE MACRO no_args 'nullary';\n",
      macro_catalog);

  bool is_strict = GetParam();
  absl::StatusOr<ExpansionOutput> output =
      ExpandMacros("$no_args( /*comment1*/ $empty()   /*comment2*/)\n",
                   macro_catalog, is_strict);

  absl::string_view expected_diagnostic =
      "Macro invocation has too many arguments (1) while the definition only "
      "references up to 0 arguments [at top_file.sql:1:1]";

  if (is_strict) {
    EXPECT_THAT(output, StatusIs(_, HasSubstr(expected_diagnostic)));
  } else {
    EXPECT_THAT(
        output,
        IsOkAndHolds(AllOf(
            TokensEq(std::vector<TokenWithLocation>{
                {Token::STRING_LITERAL, MakeLocation(kDefsFileName, 60, 69),
                 "'nullary'", "", MakeLocation(0, 47)},
                {Token::EOI, MakeLocation(48, 48), "", "\n"},
            }),
            HasWarnings(
                ElementsAre(StatusIs(_, HasSubstr(expected_diagnostic)))))));
  }
}

TEST_P(MacroExpanderParameterizedTest,
       TokensToStringForcesSpaceToAvoidCommentOuts) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO forward_slash /;\n"
      "DEFINE MACRO star *;\n",
      macro_catalog);

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ExpansionOutput output,
                       ExpandMacros("$forward_slash()$star()", macro_catalog,
                                    /*is_strict=*/GetParam()));
  EXPECT_EQ(TokensToString(output.expanded_tokens), "/ *");
}

TEST_P(MacroExpanderParameterizedTest, RightShift) {
  MacroCatalog macro_catalog;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      ExpansionOutput output,
      ExpandMacros(">>", macro_catalog, /*is_strict=*/GetParam()));
  EXPECT_THAT(output, TokensEq(std::vector<TokenWithLocation>{
                          {Token::GT, MakeLocation(0, 1), ">", ""},
                          {Token::GT, MakeLocation(1, 2), ">", ""},
                          {Token::EOI, MakeLocation(2, 2), "", ""},
                      }));
  EXPECT_EQ(TokensToString(output.expanded_tokens), ">>");
}

TEST_P(MacroExpanderParameterizedTest, TopLevelCommentsArePreserved) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO m /* dropped_comment */ 1;", macro_catalog);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      ExpansionOutput output,
      ExpandMacros("/* preserved_comment */ $m()", macro_catalog,
                   /*is_strict=*/GetParam()));
  EXPECT_THAT(
      output,
      TokensEq(std::vector<TokenWithLocation>{
          {Token::COMMENT, MakeLocation(0, 23), "/* preserved_comment */", ""},
          {Token::DECIMAL_INTEGER_LITERAL, MakeLocation(kDefsFileName, 37, 38),
           "1", " ", MakeLocation(24, 28)},
          {Token::EOI, MakeLocation(28, 28), "", ""},
      }));
}

TEST_P(MacroExpanderParameterizedTest,
       TopLevelCommentsArePreservedExpandingDefineMacroStatements) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO m1 /*internal comment*/ 123;", macro_catalog);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      ExpansionOutput output,
      ExpandMacros(
          "select 1; /*comment 1*/ /*another comment*/ DEFINE MACRO m2 "
          "/*comment 2*/ $m1",
          macro_catalog, /*is_strict=*/GetParam()));

  EXPECT_THAT(
      output,
      TokensEq(std::vector<TokenWithLocation>{
          {Token::KW_SELECT, MakeLocation(0, 6), "select", ""},
          {Token::DECIMAL_INTEGER_LITERAL, MakeLocation(7, 8), "1", " "},
          {Token::SEMICOLON, MakeLocation(8, 9), ";", ""},
          {Token::COMMENT, MakeLocation(10, 23), "/*comment 1*/", " "},
          {Token::COMMENT, MakeLocation(24, 43), "/*another comment*/", " "},
          {Token::KW_DEFINE_FOR_MACROS, MakeLocation(44, 50), "DEFINE", " "},
          {Token::KW_MACRO, MakeLocation(51, 56), "MACRO", " "},
          {Token::IDENTIFIER, MakeLocation(57, 59), "m2", " "},
          {Token::COMMENT, MakeLocation(60, 73), "/*comment 2*/", " "},
          {Token::MACRO_INVOCATION, MakeLocation(74, 77), "$m1", " "},
          {Token::EOI, MakeLocation(77, 77), "", ""},
      }));
}

// Repro for b/415799606, where macro expansion has a cycle but in a manner that
// doesn't run out of stack space. Cycle detector should catch this case.
TEST_P(MacroExpanderParameterizedTest, FailsOnCycle) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO B $1;"
      "DEFINE MACRO A $A($B($1));",
      macro_catalog);
  EXPECT_THAT(ExpandMacros("$A($B(0));", macro_catalog,
                           /*is_strict=*/GetParam()),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Cycle detected in macro expansion")));
}

TEST_P(MacroExpanderParameterizedTest, ShouldNotDetectCycleInMacroExpansion) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO A $1;", macro_catalog);
  GOOGLESQL_EXPECT_OK(ExpandMacros("$A($A($A(0)));", macro_catalog,
                         /*is_strict=*/GetParam()));
}

INSTANTIATE_TEST_SUITE_P(MacroExpanderParameterizedTest,
                         MacroExpanderParameterizedTest, Bool());

// Tests for the stack frames.
// Note: TokensWithFrameEq is a custom matcher that compares the stack frames
// of the tokens.

TEST(MacroExpanderStackFramesTest,
     GeneratesCorrectStackFramesForMacroInvocation) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO a 1;\n"
      "DEFINE MACRO b $a();\n",
      macro_catalog);
  std::vector<std::unique_ptr<StackFrame>> stack_frame_container;

  EXPECT_THAT(
      ExpandMacros("$b() $a()", macro_catalog,
                   /*is_strict=*/false),
      IsOkAndHolds(TokensWithFrameEq(std::vector<TokenWithLocation>{
          {Token::DECIMAL_INTEGER_LITERAL, MakeLocation(kDefsFileName, 15, 16),
           "1", "", MakeLocation(0, 4),
           CreateChainedStackFrames(
               {{"macro:a", FrameType::kMacroInvocation,
                 MakeLocation(kDefsFileName, 50, 54)},
                {"macro:b", FrameType::kMacroInvocation, MakeLocation(0, 4)}},
               stack_frame_container)},
          {Token::DECIMAL_INTEGER_LITERAL, MakeLocation(kDefsFileName, 15, 16),
           "1", " ", MakeLocation(5, 9),
           CreateChainedStackFrames(
               {{"macro:a", FrameType::kMacroInvocation, MakeLocation(5, 9)}},
               stack_frame_container)},
          {Token::EOI, MakeLocation(9, 9), "", ""},
      })));
}

TEST(MacroExpanderStackFramesTest, ExpandsIdentifiersSplicedWithIntLiterals) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO identifier abc;\n"
      "DEFINE MACRO empty;\n"
      "DEFINE MACRO int 123;\n",
      macro_catalog);
  std::vector<std::unique_ptr<StackFrame>> stack_frame_container;

  EXPECT_THAT(ExpandMacros("\na$empty()$int", macro_catalog,
                           /*is_strict=*/false),
              IsOkAndHolds(TokensWithFrameEq(std::vector<TokenWithLocation>{
                  {.kind = Token::IDENTIFIER,
                   .location = MakeLocation(kDefsFileName, 114, 117),
                   .text = "a123",
                   .preceding_whitespaces = "\n",
                   .stack_frame = CreateChainedStackFrames(
                       {{"macro:int", FrameType::kMacroInvocation,
                         MakeLocation(10, 14)}},
                       stack_frame_container)},
                  {Token::EOI, MakeLocation(14, 14), "", ""}})));

  // While splicing two tokens with stack frames, always use the frame from the
  // first token.
  EXPECT_THAT(ExpandMacros("\n$identifier$empty()$int", macro_catalog,
                           /*is_strict=*/false),
              IsOkAndHolds(TokensWithFrameEq(std::vector<TokenWithLocation>{
                  {Token::IDENTIFIER, MakeLocation(kDefsFileName, 24, 27),
                   "abc123", "\n", MakeLocation(1, 12),
                   CreateChainedStackFrames(
                       {{"macro:identifier", FrameType::kMacroInvocation,
                         MakeLocation(1, 12)}},
                       stack_frame_container)},
                  {Token::EOI, MakeLocation(24, 24), "", ""}})));
}

TEST(MacroExpanderStackFramesTest, ExpandsWithQuotedIdentifiers_SingleToken) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO identifier xyz;\n"
      "DEFINE MACRO numbers 456;\n"
      "DEFINE MACRO inner_quoted_id `bq`;\n"
      "DEFINE MACRO empty     ",
      macro_catalog);
  std::vector<std::unique_ptr<StackFrame>> stack_frame_container;

  // While expanding inside a quoted identifier, select the frame from the first
  // token.
  EXPECT_THAT(
      ExpandMacros("select `ab$identifier$numbers$empty$inner_quoted_id`",
                   macro_catalog, /*is_strict=*/false),
      IsOkAndHolds(TokensWithFrameEq(std::vector<TokenWithLocation>{
          {Token::KW_SELECT, MakeLocation(0, 6), "select", ""},
          {.kind = Token::IDENTIFIER,
           .location = MakeLocation(kDefsFileName, 24, 27),
           .text = "`abxyz456bq`",
           .preceding_whitespaces = " ",
           .stack_frame = CreateChainedStackFrames(
               {{"macro:identifier", FrameType::kMacroInvocation,
                 MakeLocation(10, 21)}},
               stack_frame_container)},
          {Token::EOI, MakeLocation(52, 52), "", ""},
      })));
}

TEST(MacroExpanderStackFramesTest, GenerateCorrectStackFramesForArgs) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO m $1;", macro_catalog);
  std::vector<std::unique_ptr<StackFrame>> stack_frame_container;

  // With argument getting spliced with a token.
  EXPECT_THAT(
      ExpandMacros("$m(x)", macro_catalog,
                   /*is_strict=*/false),
      IsOkAndHolds(TokensWithFrameEq(std::vector<TokenWithLocation>{
          {Token::IDENTIFIER, MakeLocation(3, 4), "x", "", MakeLocation(0, 5),
           CreateChainedStackFrames(
               {{"arg:$1", FrameType::kMacroArg, MakeLocation(3, 4)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 15, 17)},
                {"macro:m", FrameType::kMacroInvocation, MakeLocation(0, 5)}},
               stack_frame_container)},
          {Token::EOI, MakeLocation(5, 5), "", ""},
      })));
}

TEST(MacroExpanderStackFramesTest, SplicingStackFramesWithMultipleTokens) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO m1 a b;\n"
      "DEFINE MACRO m2 c d;\n"
      "DEFINE MACRO splice $1$2;\n",
      macro_catalog);
  std::vector<std::unique_ptr<StackFrame>> stack_frame_container;
  // Expansion Output: a bc d
  // Note: bc is spliced. we are reporting chain of left side stack frames($1).
  EXPECT_THAT(
      ExpandMacros("$splice($m1,$m2)", macro_catalog, /*is_strict=*/false),
      IsOkAndHolds(TokensWithFrameEq(std::vector<TokenWithLocation>{
          {Token::IDENTIFIER, MakeLocation(kDefsFileName, 16, 17), "a", "",
           MakeLocation(0, 16),
           CreateChainedStackFrames(
               {{"macro:m1", FrameType::kMacroInvocation, MakeLocation(8, 11)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(8, 11)},
                {"$1", FrameType::kArgRef,
                 MakeLocation(kDefsFileName, 103, 105)},
                {"macro:splice", FrameType::kMacroInvocation,
                 MakeLocation(0, 16)}},
               stack_frame_container)},
          {Token::IDENTIFIER, MakeLocation(kDefsFileName, 18, 19), "bc", " ",
           MakeLocation(0, 16),
           CreateChainedStackFrames(
               {{"macro:m1", FrameType::kMacroInvocation, MakeLocation(8, 11)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(8, 11)},
                {"$1", FrameType::kArgRef,
                 MakeLocation(kDefsFileName, 103, 105)},
                {"macro:splice", FrameType::kMacroInvocation,
                 MakeLocation(0, 16)}},
               stack_frame_container)},
          {Token::IDENTIFIER, MakeLocation(kDefsFileName, 59, 60), "d", " ",
           MakeLocation(0, 16),
           CreateChainedStackFrames(
               {{"macro:m2", FrameType::kMacroInvocation, MakeLocation(12, 15)},
                {"arg:$2", FrameType::kMacroArg, MakeLocation(12, 15)},
                {"$2", FrameType::kArgRef,
                 MakeLocation(kDefsFileName, 105, 107)},
                {"macro:splice", FrameType::kMacroInvocation,
                 MakeLocation(0, 16)}},
               stack_frame_container)},
          {Token::EOI, MakeLocation(16, 16), "", ""},
      })));
}

TEST(MacroExpanderStackFramesTest,
     MultipleArgumentWithMacroInvocationAndSplicing) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO m1 $1 $2;\n"
      "DEFINE MACRO m2 $1;\n",
      macro_catalog);
  std::vector<std::unique_ptr<StackFrame>> stack_frame_container;

  EXPECT_THAT(
      ExpandMacros("$m1(a,$m2(x)y)", macro_catalog,
                   /*is_strict=*/false),
      IsOkAndHolds(TokensWithFrameEq(std::vector<TokenWithLocation>{
          {Token::IDENTIFIER, MakeLocation(4, 5), "a", "", MakeLocation(0, 14),
           CreateChainedStackFrames(
               {{"arg:$1", FrameType::kMacroArg, MakeLocation(4, 5)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 16, 18)},
                {"macro:m1", FrameType::kMacroInvocation, MakeLocation(0, 14)}},
               stack_frame_container)},
          {Token::IDENTIFIER, MakeLocation(10, 11), "xy", " ",
           MakeLocation(0, 14),
           CreateChainedStackFrames(
               {{"arg:$1", FrameType::kMacroArg, MakeLocation(10, 11)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 61, 63)},
                {"macro:m2", FrameType::kMacroInvocation, MakeLocation(6, 12)},
                {"arg:$2", FrameType::kMacroArg, MakeLocation(6, 13)},
                {"$2", FrameType::kArgRef, MakeLocation(kDefsFileName, 19, 21)},
                {"macro:m1", FrameType::kMacroInvocation, MakeLocation(0, 14)}},
               stack_frame_container)},
          {Token::EOI, MakeLocation(14, 14), "", ""},
      })));
}

TEST(MacroExpanderStackFramesTest, MultipleArgumentWithMultipleUsages) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO repeat $1, $1, $2, $2;", macro_catalog);
  std::vector<std::unique_ptr<StackFrame>> stack_frame_container;

  EXPECT_THAT(
      ExpandMacros("select $repeat(1, (2,3))", macro_catalog,
                   /*is_strict=*/true),
      IsOkAndHolds(TokensWithFrameEq(std::vector<TokenWithLocation>{
          {Token::KW_SELECT, MakeLocation(0, 6), "select", ""},
          {Token::DECIMAL_INTEGER_LITERAL, MakeLocation(15, 16), "1", " ",
           MakeLocation(7, 24),
           CreateChainedStackFrames(
               {{"arg:$1", FrameType::kMacroArg, MakeLocation(15, 16)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 20, 22)},
                {"macro:repeat", FrameType::kMacroInvocation,
                 MakeLocation(7, 24)}},
               stack_frame_container)},
          {Token::COMMA, MakeLocation(kDefsFileName, 22, 23), ",", "",
           MakeLocation(7, 24),
           CreateChainedStackFrames(
               {{"macro:repeat", FrameType::kMacroInvocation,
                 MakeLocation(7, 24)}},
               stack_frame_container)},
          {Token::DECIMAL_INTEGER_LITERAL, MakeLocation(15, 16), "1", " ",
           MakeLocation(7, 24),
           CreateChainedStackFrames(
               {{"arg:$1", FrameType::kMacroArg, MakeLocation(15, 16)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 24, 26)},
                {"macro:repeat", FrameType::kMacroInvocation,
                 MakeLocation(7, 24)}},
               stack_frame_container)},
          {Token::COMMA, MakeLocation(kDefsFileName, 26, 27), ",", "",
           MakeLocation(7, 24),
           CreateChainedStackFrames(
               {{"macro:repeat", FrameType::kMacroInvocation,
                 MakeLocation(7, 24)}},
               stack_frame_container)},
          {Token::LPAREN, MakeLocation(18, 19), "(", " ", MakeLocation(7, 24),
           CreateChainedStackFrames(
               {{"arg:$2", FrameType::kMacroArg, MakeLocation(17, 23)},
                {"$2", FrameType::kArgRef, MakeLocation(kDefsFileName, 28, 30)},
                {"macro:repeat", FrameType::kMacroInvocation,
                 MakeLocation(7, 24)}},
               stack_frame_container)},
          {Token::DECIMAL_INTEGER_LITERAL, MakeLocation(19, 20), "2", "",
           MakeLocation(7, 24),
           CreateChainedStackFrames(
               {{"arg:$2", FrameType::kMacroArg, MakeLocation(17, 23)},
                {"$2", FrameType::kArgRef, MakeLocation(kDefsFileName, 28, 30)},
                {"macro:repeat", FrameType::kMacroInvocation,
                 MakeLocation(7, 24)}},
               stack_frame_container)},
          {Token::COMMA, MakeLocation(20, 21), ",", "", MakeLocation(7, 24),
           CreateChainedStackFrames(
               {{"arg:$2", FrameType::kMacroArg, MakeLocation(17, 23)},
                {"$2", FrameType::kArgRef, MakeLocation(kDefsFileName, 28, 30)},
                {"macro:repeat", FrameType::kMacroInvocation,
                 MakeLocation(7, 24)}},
               stack_frame_container)},
          {Token::DECIMAL_INTEGER_LITERAL, MakeLocation(21, 22), "3", "",
           MakeLocation(7, 24),
           CreateChainedStackFrames(
               {{"arg:$2", FrameType::kMacroArg, MakeLocation(17, 23)},
                {"$2", FrameType::kArgRef, MakeLocation(kDefsFileName, 28, 30)},
                {"macro:repeat", FrameType::kMacroInvocation,
                 MakeLocation(7, 24)}},
               stack_frame_container)},
          {Token::RPAREN, MakeLocation(22, 23), ")", "", MakeLocation(7, 24),
           CreateChainedStackFrames(
               {{"arg:$2", FrameType::kMacroArg, MakeLocation(17, 23)},
                {"$2", FrameType::kArgRef, MakeLocation(kDefsFileName, 28, 30)},
                {"macro:repeat", FrameType::kMacroInvocation,
                 MakeLocation(7, 24)}},
               stack_frame_container)},
          {Token::COMMA, MakeLocation(kDefsFileName, 30, 31), ",", "",
           MakeLocation(7, 24),
           CreateChainedStackFrames(
               {{"macro:repeat", FrameType::kMacroInvocation,
                 MakeLocation(7, 24)}},
               stack_frame_container)},
          {Token::LPAREN, MakeLocation(18, 19), "(", " ", MakeLocation(7, 24),
           CreateChainedStackFrames(
               {{"arg:$2", FrameType::kMacroArg, MakeLocation(17, 23)},
                {"$2", FrameType::kArgRef, MakeLocation(kDefsFileName, 32, 34)},

                {"macro:repeat", FrameType::kMacroInvocation,
                 MakeLocation(7, 24)}},
               stack_frame_container)},
          {Token::DECIMAL_INTEGER_LITERAL, MakeLocation(19, 20), "2", "",
           MakeLocation(7, 24),
           CreateChainedStackFrames(
               {{"arg:$2", FrameType::kMacroArg, MakeLocation(17, 23)},
                {"$2", FrameType::kArgRef, MakeLocation(kDefsFileName, 32, 34)},
                {"macro:repeat", FrameType::kMacroInvocation,
                 MakeLocation(7, 24)}},
               stack_frame_container)},
          {Token::COMMA, MakeLocation(20, 21), ",", "", MakeLocation(7, 24),
           CreateChainedStackFrames(
               {{"arg:$2", FrameType::kMacroArg, MakeLocation(17, 23)},
                {"$2", FrameType::kArgRef, MakeLocation(kDefsFileName, 32, 34)},
                {"macro:repeat", FrameType::kMacroInvocation,
                 MakeLocation(7, 24)}},
               stack_frame_container)},
          {Token::DECIMAL_INTEGER_LITERAL, MakeLocation(21, 22), "3", "",
           MakeLocation(7, 24),
           CreateChainedStackFrames(
               {{"arg:$2", FrameType::kMacroArg, MakeLocation(17, 23)},
                {"$2", FrameType::kArgRef, MakeLocation(kDefsFileName, 32, 34)},
                {"macro:repeat", FrameType::kMacroInvocation,
                 MakeLocation(7, 24)}},
               stack_frame_container)},
          {Token::RPAREN, MakeLocation(22, 23), ")", "", MakeLocation(7, 24),
           CreateChainedStackFrames(
               {{"arg:$2", FrameType::kMacroArg, MakeLocation(17, 23)},
                {"$2", FrameType::kArgRef, MakeLocation(kDefsFileName, 32, 34)},
                {"macro:repeat", FrameType::kMacroInvocation,
                 MakeLocation(7, 24)}},
               stack_frame_container)},
          {Token::EOI, MakeLocation(24, 24), "", ""}})));
}

TEST(MacroExpanderStackFramesTest, ArgumentCanBeMacroInvocation) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO m $1;\n"
      "DEFINE MACRO b ABC;"
      "DEFINE MACRO c $b();",
      macro_catalog);
  std::vector<std::unique_ptr<StackFrame>> stack_frame_container;
  EXPECT_THAT(
      ExpandMacros("$m($c())", macro_catalog, /*is_strict=*/false),
      IsOkAndHolds(TokensWithFrameEq(std::vector<TokenWithLocation>{
          {Token::IDENTIFIER, MakeLocation(kDefsFileName, 52, 55), "ABC", "",
           MakeLocation(0, 8),
           CreateChainedStackFrames(
               {{"macro:b", FrameType::kMacroInvocation,
                 MakeLocation(kDefsFileName, 91, 95)},
                {"macro:c", FrameType::kMacroInvocation, MakeLocation(3, 7)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(3, 7)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 15, 17)},
                {"macro:m", FrameType::kMacroInvocation, MakeLocation(0, 8)}},
               stack_frame_container)},
          {Token::EOI, MakeLocation(8, 8), "", ""}})));
}

TEST(MacroExpanderStackFramesTest,
     NestedMacroInvocationInArgWithMultipleUsagesWithSplicing) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO x 1;\n"
      "DEFINE MACRO y 2;\n"
      "DEFINE MACRO repeat1 $1 $1;\n"
      "DEFINE MACRO repeat2 $1 $1 $2 $2;\n",
      macro_catalog);
  std::vector<std::unique_ptr<StackFrame>> stack_frame_container;

  EXPECT_THAT(
      ExpandMacros("select $repeat1($repeat2(\"$x$y\", (a$x, 3)))",
                   macro_catalog,
                   /*is_strict=*/false),
      IsOkAndHolds(TokensWithFrameEq(std::vector<TokenWithLocation>{
          {Token::KW_SELECT, MakeLocation(0, 6), "select", ""},
          {Token::STRING_LITERAL, MakeLocation(kDefsFileName, 15, 16), "\"12\"",
           " ", MakeLocation(7, 43),
           CreateChainedStackFrames(
               {{"macro:x", FrameType::kMacroInvocation, MakeLocation(26, 28)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(25, 31)},
                {"$1", FrameType::kArgRef,
                 MakeLocation(kDefsFileName, 148, 150)},
                {"macro:repeat2", FrameType::kMacroInvocation,
                 MakeLocation(16, 42)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(16, 42)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 92, 94)},
                {"macro:repeat1", FrameType::kMacroInvocation,
                 MakeLocation(7, 43)}},
               stack_frame_container)},
          {Token::STRING_LITERAL, MakeLocation(kDefsFileName, 15, 16), "\"12\"",
           " ", MakeLocation(7, 43),
           CreateChainedStackFrames(
               {{"macro:x", FrameType::kMacroInvocation, MakeLocation(26, 28)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(25, 31)},
                {"$1", FrameType::kArgRef,
                 MakeLocation(kDefsFileName, 151, 153)},
                {"macro:repeat2", FrameType::kMacroInvocation,
                 MakeLocation(16, 42)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(16, 42)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 92, 94)},
                {"macro:repeat1", FrameType::kMacroInvocation,
                 MakeLocation(7, 43)}},
               stack_frame_container)},
          {Token::LPAREN, MakeLocation(33, 34), "(", " ", MakeLocation(7, 43),
           CreateChainedStackFrames(
               {{"arg:$2", FrameType::kMacroArg, MakeLocation(32, 41)},
                {"$2", FrameType::kArgRef,
                 MakeLocation(kDefsFileName, 154, 156)},
                {"macro:repeat2", FrameType::kMacroInvocation,
                 MakeLocation(16, 42)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(16, 42)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 92, 94)},
                {"macro:repeat1", FrameType::kMacroInvocation,
                 MakeLocation(7, 43)}},
               stack_frame_container)},
          {Token::IDENTIFIER, MakeLocation(kDefsFileName, 15, 16), "a1", "",
           MakeLocation(7, 43),
           CreateChainedStackFrames(
               {{"macro:x", FrameType::kMacroInvocation, MakeLocation(35, 37)},
                {"arg:$2", FrameType::kMacroArg, MakeLocation(32, 41)},
                {"$2", FrameType::kArgRef,
                 MakeLocation(kDefsFileName, 154, 156)},
                {"macro:repeat2", FrameType::kMacroInvocation,
                 MakeLocation(16, 42)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(16, 42)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 92, 94)},
                {"macro:repeat1", FrameType::kMacroInvocation,
                 MakeLocation(7, 43)}},
               stack_frame_container)},
          {Token::COMMA, MakeLocation(37, 38), ",", "", MakeLocation(7, 43),
           CreateChainedStackFrames(
               {{"arg:$2", FrameType::kMacroArg, MakeLocation(32, 41)},
                {"$2", FrameType::kArgRef,
                 MakeLocation(kDefsFileName, 154, 156)},
                {"macro:repeat2", FrameType::kMacroInvocation,
                 MakeLocation(16, 42)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(16, 42)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 92, 94)},
                {"macro:repeat1", FrameType::kMacroInvocation,
                 MakeLocation(7, 43)}},
               stack_frame_container)},
          {Token::DECIMAL_INTEGER_LITERAL, MakeLocation(39, 40), "3", " ",
           MakeLocation(7, 43),
           CreateChainedStackFrames(
               {{"arg:$2", FrameType::kMacroArg, MakeLocation(32, 41)},
                {"$2", FrameType::kArgRef,
                 MakeLocation(kDefsFileName, 154, 156)},
                {"macro:repeat2", FrameType::kMacroInvocation,
                 MakeLocation(16, 42)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(16, 42)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 92, 94)},
                {"macro:repeat1", FrameType::kMacroInvocation,
                 MakeLocation(7, 43)}},
               stack_frame_container)},
          {Token::RPAREN, MakeLocation(40, 41), ")", "", MakeLocation(7, 43),
           CreateChainedStackFrames(
               {{"arg:$2", FrameType::kMacroArg, MakeLocation(32, 41)},
                {"$2", FrameType::kArgRef,
                 MakeLocation(kDefsFileName, 154, 156)},
                {"macro:repeat2", FrameType::kMacroInvocation,
                 MakeLocation(16, 42)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(16, 42)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 92, 94)},
                {"macro:repeat1", FrameType::kMacroInvocation,
                 MakeLocation(7, 43)}},
               stack_frame_container)},
          {Token::LPAREN, MakeLocation(33, 34), "(", " ", MakeLocation(7, 43),
           CreateChainedStackFrames(
               {{"arg:$2", FrameType::kMacroArg, MakeLocation(32, 41)},
                {"$2", FrameType::kArgRef,
                 MakeLocation(kDefsFileName, 157, 159)},
                {"macro:repeat2", FrameType::kMacroInvocation,
                 MakeLocation(16, 42)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(16, 42)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 92, 94)},
                {"macro:repeat1", FrameType::kMacroInvocation,
                 MakeLocation(7, 43)}},
               stack_frame_container)},

          {Token::IDENTIFIER, MakeLocation(kDefsFileName, 15, 16), "a1", "",
           MakeLocation(7, 43),
           CreateChainedStackFrames(
               {{"macro:x", FrameType::kMacroInvocation, MakeLocation(35, 37)},
                {"arg:$2", FrameType::kMacroArg, MakeLocation(32, 41)},
                {"$2", FrameType::kArgRef,
                 MakeLocation(kDefsFileName, 157, 159)},
                {"macro:repeat2", FrameType::kMacroInvocation,
                 MakeLocation(16, 42)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(16, 42)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 92, 94)},
                {"macro:repeat1", FrameType::kMacroInvocation,
                 MakeLocation(7, 43)}},
               stack_frame_container)},

          {Token::COMMA, MakeLocation(37, 38), ",", "", MakeLocation(7, 43),
           CreateChainedStackFrames(
               {{"arg:$2", FrameType::kMacroArg, MakeLocation(32, 41)},
                {"$2", FrameType::kArgRef,
                 MakeLocation(kDefsFileName, 157, 159)},
                {"macro:repeat2", FrameType::kMacroInvocation,
                 MakeLocation(16, 42)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(16, 42)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 92, 94)},
                {"macro:repeat1", FrameType::kMacroInvocation,
                 MakeLocation(7, 43)}},
               stack_frame_container)},
          {Token::DECIMAL_INTEGER_LITERAL, MakeLocation(39, 40), "3", " ",
           MakeLocation(7, 43),
           CreateChainedStackFrames(
               {{"arg:$2", FrameType::kMacroArg, MakeLocation(32, 41)},
                {"$2", FrameType::kArgRef,
                 MakeLocation(kDefsFileName, 157, 159)},
                {"macro:repeat2", FrameType::kMacroInvocation,
                 MakeLocation(16, 42)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(16, 42)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 92, 94)},
                {"macro:repeat1", FrameType::kMacroInvocation,
                 MakeLocation(7, 43)}},
               stack_frame_container)},
          {Token::RPAREN, MakeLocation(40, 41), ")", "", MakeLocation(7, 43),
           CreateChainedStackFrames(
               {{"arg:$2", FrameType::kMacroArg, MakeLocation(32, 41)},
                {"$2", FrameType::kArgRef,
                 MakeLocation(kDefsFileName, 157, 159)},
                {"macro:repeat2", FrameType::kMacroInvocation,
                 MakeLocation(16, 42)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(16, 42)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 92, 94)},
                {"macro:repeat1", FrameType::kMacroInvocation,
                 MakeLocation(7, 43)}},
               stack_frame_container)},
          {Token::STRING_LITERAL, MakeLocation(kDefsFileName, 15, 16), "\"12\"",
           " ", MakeLocation(7, 43),
           CreateChainedStackFrames(
               {{"macro:x", FrameType::kMacroInvocation, MakeLocation(26, 28)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(25, 31)},
                {"$1", FrameType::kArgRef,
                 MakeLocation(kDefsFileName, 148, 150)},
                {"macro:repeat2", FrameType::kMacroInvocation,
                 MakeLocation(16, 42)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(16, 42)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 95, 97)},
                {"macro:repeat1", FrameType::kMacroInvocation,
                 MakeLocation(7, 43)}},
               stack_frame_container)},
          {Token::STRING_LITERAL, MakeLocation(kDefsFileName, 15, 16), "\"12\"",
           " ", MakeLocation(7, 43),
           CreateChainedStackFrames(
               {{"macro:x", FrameType::kMacroInvocation, MakeLocation(26, 28)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(25, 31)},
                {"$1", FrameType::kArgRef,
                 MakeLocation(kDefsFileName, 151, 153)},
                {"macro:repeat2", FrameType::kMacroInvocation,
                 MakeLocation(16, 42)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(16, 42)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 95, 97)},
                {"macro:repeat1", FrameType::kMacroInvocation,
                 MakeLocation(7, 43)}},
               stack_frame_container)},
          {Token::LPAREN, MakeLocation(33, 34), "(", " ", MakeLocation(7, 43),
           CreateChainedStackFrames(
               {{"arg:$2", FrameType::kMacroArg, MakeLocation(32, 41)},
                {"$2", FrameType::kArgRef,
                 MakeLocation(kDefsFileName, 154, 156)},
                {"macro:repeat2", FrameType::kMacroInvocation,
                 MakeLocation(16, 42)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(16, 42)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 95, 97)},
                {"macro:repeat1", FrameType::kMacroInvocation,
                 MakeLocation(7, 43)}},
               stack_frame_container)},
          {Token::IDENTIFIER, MakeLocation(kDefsFileName, 15, 16), "a1", "",
           MakeLocation(7, 43),
           CreateChainedStackFrames(
               {{"macro:x", FrameType::kMacroInvocation, MakeLocation(35, 37)},
                {"arg:$2", FrameType::kMacroArg, MakeLocation(32, 41)},
                {"$2", FrameType::kArgRef,
                 MakeLocation(kDefsFileName, 154, 156)},
                {"macro:repeat2", FrameType::kMacroInvocation,
                 MakeLocation(16, 42)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(16, 42)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 95, 97)},
                {"macro:repeat1", FrameType::kMacroInvocation,
                 MakeLocation(7, 43)}},
               stack_frame_container)},
          {Token::COMMA, MakeLocation(37, 38), ",", "", MakeLocation(7, 43),
           CreateChainedStackFrames(
               {{"arg:$2", FrameType::kMacroArg, MakeLocation(32, 41)},
                {"$2", FrameType::kArgRef,
                 MakeLocation(kDefsFileName, 154, 156)},
                {"macro:repeat2", FrameType::kMacroInvocation,
                 MakeLocation(16, 42)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(16, 42)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 95, 97)},
                {"macro:repeat1", FrameType::kMacroInvocation,
                 MakeLocation(7, 43)}},
               stack_frame_container)},
          {Token::DECIMAL_INTEGER_LITERAL, MakeLocation(39, 40), "3", " ",
           MakeLocation(7, 43),
           CreateChainedStackFrames(
               {{"arg:$2", FrameType::kMacroArg, MakeLocation(32, 41)},
                {"$2", FrameType::kArgRef,
                 MakeLocation(kDefsFileName, 154, 156)},
                {"macro:repeat2", FrameType::kMacroInvocation,
                 MakeLocation(16, 42)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(16, 42)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 95, 97)},
                {"macro:repeat1", FrameType::kMacroInvocation,
                 MakeLocation(7, 43)}},
               stack_frame_container)},
          {Token::RPAREN, MakeLocation(40, 41), ")", "", MakeLocation(7, 43),
           CreateChainedStackFrames(
               {{"arg:$2", FrameType::kMacroArg, MakeLocation(32, 41)},
                {"$2", FrameType::kArgRef,
                 MakeLocation(kDefsFileName, 154, 156)},
                {"macro:repeat2", FrameType::kMacroInvocation,
                 MakeLocation(16, 42)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(16, 42)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 95, 97)},
                {"macro:repeat1", FrameType::kMacroInvocation,
                 MakeLocation(7, 43)}},
               stack_frame_container)},
          {Token::LPAREN, MakeLocation(33, 34), "(", " ", MakeLocation(7, 43),
           CreateChainedStackFrames(
               {{"arg:$2", FrameType::kMacroArg, MakeLocation(32, 41)},
                {"$2", FrameType::kArgRef,
                 MakeLocation(kDefsFileName, 157, 159)},
                {"macro:repeat2", FrameType::kMacroInvocation,
                 MakeLocation(16, 42)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(16, 42)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 95, 97)},
                {"macro:repeat1", FrameType::kMacroInvocation,
                 MakeLocation(7, 43)}},
               stack_frame_container)},
          {Token::IDENTIFIER, MakeLocation(kDefsFileName, 15, 16), "a1", "",
           MakeLocation(7, 43),
           CreateChainedStackFrames(
               {{"macro:x", FrameType::kMacroInvocation, MakeLocation(35, 37)},
                {"arg:$2", FrameType::kMacroArg, MakeLocation(32, 41)},
                {"$2", FrameType::kArgRef,
                 MakeLocation(kDefsFileName, 157, 159)},
                {"macro:repeat2", FrameType::kMacroInvocation,
                 MakeLocation(16, 42)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(16, 42)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 95, 97)},
                {"macro:repeat1", FrameType::kMacroInvocation,
                 MakeLocation(7, 43)}},
               stack_frame_container)},
          {Token::COMMA, MakeLocation(37, 38), ",", "", MakeLocation(7, 43),
           CreateChainedStackFrames(
               {{"arg:$2", FrameType::kMacroArg, MakeLocation(32, 41)},
                {"$2", FrameType::kArgRef,
                 MakeLocation(kDefsFileName, 157, 159)},
                {"macro:repeat2", FrameType::kMacroInvocation,
                 MakeLocation(16, 42)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(16, 42)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 95, 97)},
                {"macro:repeat1", FrameType::kMacroInvocation,
                 MakeLocation(7, 43)}},
               stack_frame_container)},
          {Token::DECIMAL_INTEGER_LITERAL, MakeLocation(39, 40), "3", " ",
           MakeLocation(7, 43),
           CreateChainedStackFrames(
               {{"arg:$2", FrameType::kMacroArg, MakeLocation(32, 41)},
                {"$2", FrameType::kArgRef,
                 MakeLocation(kDefsFileName, 157, 159)},
                {"macro:repeat2", FrameType::kMacroInvocation,
                 MakeLocation(16, 42)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(16, 42)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 95, 97)},
                {"macro:repeat1", FrameType::kMacroInvocation,
                 MakeLocation(7, 43)}},
               stack_frame_container)},
          {Token::RPAREN, MakeLocation(40, 41), ")", "", MakeLocation(7, 43),
           CreateChainedStackFrames(
               {{"arg:$2", FrameType::kMacroArg, MakeLocation(32, 41)},
                {"$2", FrameType::kArgRef,
                 MakeLocation(kDefsFileName, 157, 159)},
                {"macro:repeat2", FrameType::kMacroInvocation,
                 MakeLocation(16, 42)},
                {"arg:$1", FrameType::kMacroArg, MakeLocation(16, 42)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 95, 97)},
                {"macro:repeat1", FrameType::kMacroInvocation,
                 MakeLocation(7, 43)}},
               stack_frame_container)},
          {Token::EOI, MakeLocation(43, 43), "", ""},
      })));
}

TEST(MacroExpanderStackFramesTest, ArgPassedToNestedMacro) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO m1 $1;\n"
      "DEFINE MACRO m2 $m1($1);\n",
      macro_catalog);
  std::vector<std::unique_ptr<StackFrame>> stack_frame_container;
  EXPECT_THAT(
      ExpandMacros("$m2(ABC)", macro_catalog, /*is_strict=*/false),
      IsOkAndHolds(TokensWithFrameEq(std::vector<TokenWithLocation>{
          {Token::IDENTIFIER, MakeLocation(4, 7), "ABC", "", MakeLocation(0, 8),
           CreateChainedStackFrames(
               {{"arg:$1", FrameType::kMacroArg, MakeLocation(4, 7)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 59, 61)},
                {"arg:$1", FrameType::kMacroArg,
                 MakeLocation(kDefsFileName, 59, 61)},
                {"$1", FrameType::kArgRef, MakeLocation(kDefsFileName, 16, 18)},
                {"macro:m1", FrameType::kMacroInvocation,
                 MakeLocation(kDefsFileName, 55, 62)},
                {"macro:m2", FrameType::kMacroInvocation, MakeLocation(0, 8)}},
               stack_frame_container)},
          {Token::EOI, MakeLocation(8, 8), "", ""}})));
}

TEST(MacroExpanderStackFramesTest, TestInvocationFrame) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO m1 $1;\n"
      "DEFINE MACRO m2 $m1($1);\n",
      macro_catalog);
  std::vector<std::unique_ptr<StackFrame>> stack_frame_container;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto output, ExpandMacros("$m2(ABC)", macro_catalog,
                                                 /*is_strict=*/false));
  TokenWithLocation token = output.expanded_tokens[0];
  EXPECT_EQ(token.text, "ABC");
  EXPECT_EQ(token.stack_frame->frame_type, StackFrame::FrameType::kMacroArg);
  EXPECT_EQ(token.stack_frame->invocation_frame->name, "macro:m2");
}

TEST(MacroExpanderExpansionStateTest,
     ShouldReturnErrorIfTooManyMacroInvocations) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO m1 $1;\n"
      "DEFINE MACRO m2 $m1($m1($1));"
      "DEFINE MACRO m3 $m2($m2($1));"
      "DEFINE MACRO m4 $m3($m3($1));"
      "DEFINE MACRO m5 $m4($m4($1));",
      macro_catalog);
  // Main query : SELECT $m5(ABC)
  // The number of invocations is 63.

  // DEFINE MACRO $m1(ABC), Let's consider this as a base case I(1)
  // here One Invocation is $m1(ABC) and another invocation to expand the
  // argument ABC.
  // I(1) = 2
  // I(2) = (I(1) * 2) + 2 = 6
  // I(3) = (I(2) * 2) + 2 = 14
  // I(4) = (I(3) * 2) + 2 = 30
  // I(5) = (I(4) * 2 + 2 = 62

  // 62 invocations are needed for expanding $m5(ABC), and 1 more for the main
  // query, So total 63 invocations are needed.

  // At limit 63, it should return OK.
  MacroExpanderOptions macro_expander_options = {.max_macro_invocations = 63};
  GOOGLESQL_EXPECT_OK(ExpandMacros("$m5(ABC)", macro_catalog, macro_expander_options));

  // At limit 62, it should return error.
  macro_expander_options = {.max_macro_invocations = 62};
  EXPECT_THAT(ExpandMacros("$m5(ABC)", macro_catalog, macro_expander_options),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Too many macro invocations")));
}

TEST(MacroExpanderExpansionStateTest,
     NestedMacroInvocationShouldBeCountedAsMacroInvocation) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO m1 Hello World;\n"
      "DEFINE MACRO m2 $m1;\n",
      macro_catalog);

  // Main query : SELECT $m2()
  // The number of invocations is 3.
  // The first invocation is on main query: SELECT  $m2()
  // The second invocation is $m2
  // The third invocation is $m1
  // NOTE : Here there is invocation of m1 and m2 does not uses () that is why
  // we are not counting argument expansion of m2 and m1.

  // At limit 3, it should return OK.
  MacroExpanderOptions macro_expander_options = {.max_macro_invocations = 3};
  GOOGLESQL_EXPECT_OK(ExpandMacros("SELECT $m2", macro_catalog, macro_expander_options));

  // At limit 2, it should return error.
  macro_expander_options = {.max_macro_invocations = 2};
  EXPECT_THAT(
      ExpandMacros("SELECT $m2()", macro_catalog, macro_expander_options),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("Too many macro invocations")));
}

TEST(MacroExpanderExpansionStateTest,
     ArgumentExpansionShouldBeCountedAsMacroInvocation) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO m1 $1 $1;\n", macro_catalog);

  // Main query : SELECT $m1(ABC)
  // The number of invocations is 3.
  // The first invocation is on main query: SELECT  $m1(ABC)
  // The second invocation is $m1(ABC)
  // The third invocation is for argument expansion"ABC"
  // Multiple usage of $1 should not be counted as macro invocation.

  // At limit 3, it should return OK.
  MacroExpanderOptions macro_expander_options = {.max_macro_invocations = 3};
  GOOGLESQL_EXPECT_OK(
      ExpandMacros("SELECT $m1(ABC)", macro_catalog, macro_expander_options));

  // At limit 2, it should return error.
  macro_expander_options = {.max_macro_invocations = 2};
  EXPECT_THAT(
      ExpandMacros("SELECT $m1(ABC)", macro_catalog, macro_expander_options),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("Too many macro invocations")));
}

TEST(MacroExpanderExpansionStateTest,
     MultipleArgumentExpansionShouldBeCountedAsMacroInvocation) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO m1 $1 $2;\n", macro_catalog);

  // Main query : SELECT $m1(ABC, DEF)
  // The number of invocations is 3.
  // The first invocation is on main query: SELECT  $m1(ABC, DEF)
  // The second invocation is $m1(ABC, DEF)
  // The third invocation is for argument expansion "ABC"
  // The fourth invocation is for argument expansion "DEF"

  // At limit 4, it should return OK.
  MacroExpanderOptions macro_expander_options = {.max_macro_invocations = 4};
  GOOGLESQL_EXPECT_OK(ExpandMacros("SELECT $m1(ABC, DEF)", macro_catalog,
                         macro_expander_options));

  // At limit 3, it should return error.
  macro_expander_options = {.max_macro_invocations = 3};
  EXPECT_THAT(ExpandMacros("SELECT $m1(ABC, DEF)", macro_catalog,
                           macro_expander_options),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Too many macro invocations")));
}

TEST(MacroExpanderExpansionStateTest,
     ArgumentExpansionWithMacroInvocationShouldBeCountedAsMacroInvocation) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO m Hello world;\n"
      "DEFINE MACRO m1 $1;\n",
      macro_catalog);

  // Main query : SELECT '$m1($m)'
  // The number of invocations is 3.
  // The first invocation is on main query: SELECT  $m1($m)
  // The second invocation is $m1($m)
  // The third invocation is for argument expansion "$m"
  // The fourth invocation is for macro expansion "$m"

  // At limit 4, it should return OK.
  MacroExpanderOptions macro_expander_options = {.max_macro_invocations = 4};
  GOOGLESQL_EXPECT_OK(
      ExpandMacros("SELECT $m1($m)", macro_catalog, macro_expander_options));

  // At limit 3, it should return error.
  macro_expander_options = {.max_macro_invocations = 3};
  EXPECT_THAT(
      ExpandMacros("SELECT $m1($m)", macro_catalog, macro_expander_options),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("Too many macro invocations")));
}

TEST(MacroExpanderExpansionStateTest,
     LiteralExpansionShouldBeCountedAsMacroInvocation) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO m1 $1;\n", macro_catalog);

  // Main query : SELECT '$m1(ABC)'
  // The number of invocations is 4.
  // The first invocation is on main query: SELECT  '$m1(ABC)'
  // The second invocation is for literal expansion: '$m1(ABC)'
  // The third invocation is for m1(ABC)
  // The fourth invocation is for argument expansion"ABC"

  // At limit 4, it should return OK.
  MacroExpanderOptions macro_expander_options = {.max_macro_invocations = 4};
  GOOGLESQL_EXPECT_OK(
      ExpandMacros("SELECT '$m1(ABC)'", macro_catalog, macro_expander_options));

  // At limit 3, it should return error.
  macro_expander_options = {.max_macro_invocations = 3};
  EXPECT_THAT(
      ExpandMacros("SELECT '$m1(ABC)'", macro_catalog, macro_expander_options),
      StatusIs(absl::StatusCode::kInternal,
               HasSubstr("Too many macro invocations")));
}

void VerifyStackFrameInputAndOffsets(
    const StackFrame* stack_frame, absl::string_view expected_name,
    absl::string_view expected_invocation_string,
    absl::string_view expected_input_text,
    int expected_offset_in_original_input, int expected_input_start_line_offset,
    int expected_input_start_column_offset) {
  ASSERT_THAT(stack_frame, NotNull());
  EXPECT_EQ(stack_frame->name, expected_name);
  EXPECT_EQ(stack_frame->GetInvocationString(), expected_invocation_string);
  EXPECT_EQ(stack_frame->input_text, expected_input_text);
  EXPECT_EQ(stack_frame->offset_in_original_input,
            expected_offset_in_original_input);
  EXPECT_EQ(stack_frame->input_start_line_offset,
            expected_input_start_line_offset);
  EXPECT_EQ(stack_frame->input_start_column_offset,
            expected_input_start_column_offset);
}

TEST(MacroExpanderStackFramesTest, FramesShouldHaveCorrectInvocationString) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO one 1;\n"
      "DEFINE MACRO two 2;\n"
      "DEFINE MACRO add $1 + $2;\n"
      "DEFINE MACRO outer $add($one(), $two());  # Extra comment",
      macro_catalog);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto output, ExpandMacros("SELECT $outer", macro_catalog,
                                                 /*is_strict=*/false));
  // Expanded String : SELECT 1 + 2
  EXPECT_EQ(output.expanded_tokens.size(), 5);

  // Token : 1
  // 1 -> $one() -> arg:$1 -> ArgUsage:$1 -> outer
  // Verify the chain of frames for first token.
  std::string expected_input_text =
      "DEFINE MACRO one 1;\n"
      "DEFINE MACRO two 2;\n"
      "DEFINE MACRO add $1 + $2;\n"
      "DEFINE MACRO outer $add($one(), $two());  # Extra comment";
  StackFrame* stack_frame = output.expanded_tokens[1].stack_frame;
  VerifyStackFrameInputAndOffsets(stack_frame, "macro:one", "$one()",
                                  expected_input_text,
                                  /*expected_offset_in_original_input=*/65,
                                  /*expected_input_start_line_offset=*/1,
                                  /*expected_input_start_column_offset=*/1);

  stack_frame = stack_frame->parent;
  VerifyStackFrameInputAndOffsets(stack_frame, "arg:$1", "$one()",
                                  expected_input_text,
                                  /*expected_offset_in_original_input=*/65,
                                  /*expected_input_start_line_offset=*/1,
                                  /*expected_input_start_column_offset=*/1);

  stack_frame = stack_frame->parent;
  VerifyStackFrameInputAndOffsets(stack_frame, "$1", "$1", expected_input_text,
                                  /*expected_offset_in_original_input=*/39,
                                  /*expected_input_start_line_offset=*/1,
                                  /*expected_input_start_column_offset=*/1);

  stack_frame = stack_frame->parent;
  VerifyStackFrameInputAndOffsets(stack_frame, "macro:add",
                                  "$add($one(), $two())", expected_input_text,
                                  /*expected_offset_in_original_input=*/65,
                                  /*expected_input_start_line_offset=*/1,
                                  /*expected_input_start_column_offset=*/1);

  stack_frame = stack_frame->parent;
  VerifyStackFrameInputAndOffsets(stack_frame, "macro:outer", "$outer",
                                  "SELECT $outer",
                                  /*expected_offset_in_original_input=*/0,
                                  /*expected_input_start_line_offset=*/1,
                                  /*expected_input_start_column_offset=*/1);
}

TEST(MacroExpanderStackFramesTest,
     FrameShouldHaveCorrectInputOffsetsWithDifferentRegistrationLocations) {
  MacroCatalog macro_catalog;
  RegisterMacroAt("DEFINE MACRO one 1;", macro_catalog, /*start_offset=*/0,
                  /*original_start_line=*/1, /*original_start_column=*/1);
  RegisterMacroAt("DEFINE MACRO two 2;", macro_catalog,
                  /*start_offset=*/23, /*original_start_line=*/5,
                  /*original_start_column=*/1);
  RegisterMacroAt("DEFINE MACRO add $1 + $2;", macro_catalog,
                  /*start_offset=*/45, /*original_start_line=*/8,
                  /*original_start_column=*/1);
  RegisterMacroAt("DEFINE MACRO outer $add($one(), $two());", macro_catalog,
                  /*start_offset=*/71,
                  /*original_start_line=*/9, /*original_start_column=*/1);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(auto output, ExpandMacros("SELECT $outer", macro_catalog,
                                                 /*is_strict=*/false));
  // Expanded String : SELECT 1 + 2
  EXPECT_EQ(output.expanded_tokens.size(), 5);

  // Token : 1
  // 1 -> $one() -> arg:$1 -> ArgUsage:$1 -> outer
  // Verify the chain of frames for first token.
  StackFrame* stack_frame = output.expanded_tokens[1].stack_frame;
  VerifyStackFrameInputAndOffsets(stack_frame, "macro:one", "$one()",
                                  "DEFINE MACRO outer $add($one(), $two());",
                                  /*expected_offset_in_original_input=*/71,
                                  /*expected_input_start_line_offset=*/9,
                                  /*expected_input_start_column_offset=*/1);

  // Verify the location of $one() is on line 9, column 25.
  ParseLocationTranslator parse_location_translator(stack_frame->input_text);
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      auto line_and_column,
      parse_location_translator.GetLineAndColumnAfterTabExpansion(
          stack_frame->LocationRangeWithoutStartOffset().start()));
  EXPECT_EQ(line_and_column.first + stack_frame->input_start_line_offset - 1,
            9);
  EXPECT_EQ(line_and_column.second + stack_frame->input_start_column_offset - 1,
            25);

  stack_frame = stack_frame->parent;
  VerifyStackFrameInputAndOffsets(stack_frame, "arg:$1", "$one()",
                                  "DEFINE MACRO outer $add($one(), $two());",
                                  /*expected_offset_in_original_input=*/71,
                                  /*expected_input_start_line_offset=*/9,
                                  /*expected_input_start_column_offset=*/1);

  stack_frame = stack_frame->parent;
  VerifyStackFrameInputAndOffsets(stack_frame, "$1", "$1",
                                  "DEFINE MACRO add $1 + $2;",
                                  /*expected_offset_in_original_input=*/45,
                                  /*expected_input_start_line_offset=*/8,
                                  /*expected_input_start_column_offset=*/1);

  stack_frame = stack_frame->parent;
  VerifyStackFrameInputAndOffsets(stack_frame, "macro:add",
                                  "$add($one(), $two())",
                                  "DEFINE MACRO outer $add($one(), $two());",
                                  /*expected_offset_in_original_input=*/71,
                                  /*expected_input_start_line_offset=*/9,
                                  /*expected_input_start_column_offset=*/1);

  stack_frame = stack_frame->parent;
  VerifyStackFrameInputAndOffsets(stack_frame, "macro:outer", "$outer",
                                  "SELECT $outer",
                                  /*expected_offset_in_original_input=*/0,
                                  /*expected_input_start_line_offset=*/1,
                                  /*expected_input_start_column_offset=*/1);
}

TEST(MacroExpanderStackFramesTest, ShouldReturnErrorIfTooManyStackFrames) {
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO m1 $1;\n"
      "DEFINE MACRO m2 $m1($1);\n"
      "DEFINE MACRO m3 $m2($1);\n"
      "DEFINE MACRO m4 $m3($1);\n"
      "DEFINE MACRO m5 $m4($1);\n",
      macro_catalog);
  MacroExpanderOptions macro_expander_options = {.max_number_of_stack_frames =
                                                     5};
  EXPECT_THAT(ExpandMacros("$m5(ABC)", macro_catalog, macro_expander_options),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Too many stack frames are created")));
}

}  // namespace macros
}  // namespace parser
}  // namespace googlesql
