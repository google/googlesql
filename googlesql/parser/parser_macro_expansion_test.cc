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

#include "googlesql/parser/macros/macro_catalog.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser.h"
#include "googlesql/parser/parser_mode.h"
#include "googlesql/proto/internal_error_location.pb.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/public/proto/logging.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace parser {

using MacroCatalog = macros::MacroCatalog;

using ::testing::_;
using ::testing::HasSubstr;
using ::absl_testing::StatusIs;

static LanguageOptions GetLanguageOptions() {
  LanguageOptions language_options;
  language_options.EnableMaximumLanguageFeatures();
  return language_options;
}

static constexpr absl::string_view kTopFileName = "top_file.sql";

static void RegisterMacros(absl::string_view source,
                           MacroCatalog& macro_catalog) {
  ParseResumeLocation location =
      ParseResumeLocation::FromStringView(kTopFileName, source);
  bool at_end_of_input = false;
  while (!at_end_of_input) {
    std::unique_ptr<ParserOutput> output;
    GOOGLESQL_ASSERT_OK(ParseNextStatement(
        &location,
        ParserOptions(GetLanguageOptions(), MacroExpansionMode::kStrict),
        &output, &at_end_of_input));
    ASSERT_TRUE(output->statement() != nullptr);
    auto def_macro_stmt =
        output->statement()->GetAsOrNull<ASTDefineMacroStatement>();
    ASSERT_TRUE(def_macro_stmt != nullptr);
    GOOGLESQL_ASSERT_OK(macro_catalog.RegisterMacro(
        {.source_text = source,
         .location = def_macro_stmt->location(),
         .name_location = def_macro_stmt->name()->location(),
         .body_location = def_macro_stmt->body()->location()}));
  }
}

TEST(ParserMacroExpansionTest, ExpandsMacros) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO m $1 b", macro_catalog);

  ParserOptions parser_options(GetLanguageOptions(),
                               MacroExpansionMode::kLenient, &macro_catalog);

  ParseResumeLocation resume_location =
      ParseResumeLocation::FromStringView("SELECT a$m(x)2");
  bool at_end_of_input;
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseNextStatement(&resume_location, parser_options, &parser_output,
                               &at_end_of_input));
  EXPECT_TRUE(at_end_of_input);
  EXPECT_EQ(parser_output->runtime_info().num_lexical_tokens(), 8);

  EXPECT_EQ(parser_output->statement()->DebugString(),
            R"(QueryStatement [0-13]
  Query [0-13]
    Select [0-13]
      SelectList [11-13]
        SelectColumn [11-13]
          PathExpression [11-12]
            Identifier(ax) [11-12]
          Alias [8-13]
            Identifier(b2) [8-13]
)");
}

TEST(ParserMacroExpansionTest, RecognizesOnlyOriginalDefineMacroStatements) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO def DEFINE MACRO x 1", macro_catalog);

  ParserOptions parser_options(GetLanguageOptions(),
                               MacroExpansionMode::kLenient, &macro_catalog);

  ParseResumeLocation resume_location =
      ParseResumeLocation::FromStringView("DEFINE MACRO m 1; $def");
  bool at_end_of_input;
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseNextStatement(&resume_location, parser_options, &parser_output,
                               &at_end_of_input));
  EXPECT_FALSE(at_end_of_input);
  EXPECT_EQ(parser_output->runtime_info().num_lexical_tokens(), 6);

  EXPECT_EQ(parser_output->statement()->DebugString(),
            R"(DefineMacroStatement [0-16]
  Identifier(m) [13-14]
  MacroBody(1) [15-16]
)");

  EXPECT_THAT(
      ParseNextStatement(&resume_location, parser_options, &parser_output,
                         &at_end_of_input),
      StatusIs(_, HasSubstr("Syntax error: DEFINE MACRO statements cannot be "
                            "composed from other expansions")));
}

TEST(ParserMacroExpansionTest,
     CorrectErrorOnPartiallyGeneratedDefineMacroStatement) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO def define", macro_catalog);

  ParserOptions parser_options(GetLanguageOptions(),
                               MacroExpansionMode::kLenient, &macro_catalog);

  ParseResumeLocation resume_location =
      ParseResumeLocation::FromStringView("$def MACRO m 1");
  bool at_end_of_input;
  std::unique_ptr<ParserOutput> parser_output;
  EXPECT_THAT(
      ParseNextStatement(&resume_location, parser_options, &parser_output,
                         &at_end_of_input),
      StatusIs(_, HasSubstr("Syntax error: DEFINE MACRO statements cannot be "
                            "composed from other expansions")));
}

TEST(ParserMacroExpansionTest, DefineEmptyMacroNoSemiColon) {
  MacroCatalog macro_catalog;
  ParserOptions parser_options(GetLanguageOptions(),
                               MacroExpansionMode::kStrict, &macro_catalog);

  ParseResumeLocation resume_location =
      ParseResumeLocation::FromStringView("DEFINE MACRO empty");
  bool at_end_of_input;
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseNextStatement(&resume_location, parser_options, &parser_output,
                               &at_end_of_input));
  EXPECT_TRUE(at_end_of_input);
  EXPECT_EQ(parser_output->runtime_info().num_lexical_tokens(), 4);

  EXPECT_EQ(parser_output->statement()->DebugString(),
            R"(DefineMacroStatement [0-18]
  Identifier(empty) [13-18]
  MacroBody() [18-18]
)");
}

TEST(ParserMacroExpansionTest, DefineEmptyMacroWithSemiColon) {
  MacroCatalog macro_catalog;
  ParserOptions parser_options(GetLanguageOptions(),
                               MacroExpansionMode::kStrict, &macro_catalog);

  ParseResumeLocation resume_location =
      ParseResumeLocation::FromStringView("DEFINE MACRO empty;");
  bool at_end_of_input;
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseNextStatement(&resume_location, parser_options, &parser_output,
                               &at_end_of_input));
  EXPECT_TRUE(at_end_of_input);
  EXPECT_EQ(parser_output->runtime_info().num_lexical_tokens(), 5);

  EXPECT_EQ(parser_output->statement()->DebugString(),
            R"(DefineMacroStatement [0-18]
  Identifier(empty) [13-18]
  MacroBody() [18-18]
)");
}

TEST(ParserMacroExpansionTest, DefineMacroWithKeywordAsName) {
  MacroCatalog macro_catalog;
  ParserOptions parser_options(GetLanguageOptions(),
                               MacroExpansionMode::kStrict, &macro_catalog);

  ParseResumeLocation resume_location =
      ParseResumeLocation::FromStringView("DEFINE MACRO limit 1");
  bool at_end_of_input;
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseNextStatement(&resume_location, parser_options, &parser_output,
                               &at_end_of_input));
  EXPECT_TRUE(at_end_of_input);
  EXPECT_EQ(parser_output->runtime_info().num_lexical_tokens(), 5);

  EXPECT_EQ(parser_output->statement()->DebugString(),
            R"(DefineMacroStatement [0-20]
  Identifier(`limit`) [13-18]
  MacroBody(1) [19-20]
)");
}

TEST(ParserMacroExpansionTest, MacroNameCanBeQuoted) {
  MacroCatalog macro_catalog;
  ParserOptions parser_options(GetLanguageOptions(),
                               MacroExpansionMode::kStrict, &macro_catalog);

  ParseResumeLocation resume_location =
      ParseResumeLocation::FromStringView("DEFINE MACRO `limit` 1");
  bool at_end_of_input;
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseNextStatement(&resume_location, parser_options, &parser_output,
                               &at_end_of_input));
  EXPECT_TRUE(at_end_of_input);
  EXPECT_EQ(parser_output->runtime_info().num_lexical_tokens(), 5);

  EXPECT_EQ(parser_output->statement()->DebugString(),
            R"(DefineMacroStatement [0-22]
  Identifier(`limit`) [13-20]
  MacroBody(1) [21-22]
)");
}

TEST(ParserMacroExpansionTest, CorrectErrorWhenMacroNameIsASymbol) {
  MacroCatalog macro_catalog;
  ParserOptions parser_options(GetLanguageOptions(),
                               MacroExpansionMode::kStrict, &macro_catalog);

  ParseResumeLocation resume_location =
      ParseResumeLocation::FromStringView("DEFINE MACRO + 1");
  bool at_end_of_input;
  std::unique_ptr<ParserOutput> parser_output;
  EXPECT_THAT(ParseNextStatement(&resume_location, parser_options,
                                 &parser_output, &at_end_of_input),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Syntax error: Expected macro name")));
}

TEST(ParserMacroExpansionTest, CorrectErrorWhenMacroNameIsMissingAtEof) {
  MacroCatalog macro_catalog;
  ParserOptions parser_options(GetLanguageOptions(),
                               MacroExpansionMode::kStrict, &macro_catalog);

  ParseResumeLocation resume_location =
      ParseResumeLocation::FromStringView("DEFINE MACRO      /*nothing*/  ");
  bool at_end_of_input;
  std::unique_ptr<ParserOutput> parser_output;
  EXPECT_THAT(
      ParseNextStatement(&resume_location, parser_options, &parser_output,
                         &at_end_of_input),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr(
              "Syntax error: Expected macro name but got end of statement")));
}

TEST(ParserMacroExpansionTest, CorrectErrorWhenMacroNameIsMissingAtSemicolon) {
  MacroCatalog macro_catalog;
  ParserOptions parser_options(GetLanguageOptions(),
                               MacroExpansionMode::kStrict, &macro_catalog);

  ParseResumeLocation resume_location =
      ParseResumeLocation::FromStringView("DEFINE MACRO      /*nothing*/  ;");
  bool at_end_of_input;
  std::unique_ptr<ParserOutput> parser_output;
  EXPECT_THAT(
      ParseNextStatement(&resume_location, parser_options, &parser_output,
                         &at_end_of_input),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Syntax error: Expected macro name but got \";\"")));
}

TEST(ParserMacroExpansionTest, TopLevelCommentsArePreserved) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO m /* dropped_comment */ 1;", macro_catalog);
  ParserOptions parser_options(GetLanguageOptions(),
                               MacroExpansionMode::kStrict, &macro_catalog);
  ParseResumeLocation resume_location = ParseResumeLocation::FromStringView(
      "/* preserved_comment */ SELECT $m()");
  bool at_end_of_input;
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseNextStatement(&resume_location, parser_options, &parser_output,
                               &at_end_of_input));
  EXPECT_TRUE(at_end_of_input);
  EXPECT_EQ(parser_output->statement()->DebugString(),
            R"(QueryStatement [24-35]
  Query [24-35]
    Select [24-35]
      SelectList [31-35]
        SelectColumn [31-35]
          IntLiteral(1) [31-35]
)");
}

TEST(ParserMacroExpansionTest,
     TopLevelCommentsArePreservedExpandingDefineMacroStatements) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO m1 /*internal comment*/ 123;", macro_catalog);
  ParserOptions parser_options(GetLanguageOptions(),
                               MacroExpansionMode::kStrict, &macro_catalog);
  ParseResumeLocation resume_location = ParseResumeLocation::FromStringView(
      "select 1; /*comment 1*/ DEFINE MACRO m2 /*comment 2*/ $m1; SELECT "
      "$m1()");
  bool at_end_of_input;
  std::unique_ptr<ParserOutput> parser_output;

  // "select 1;" does not have comments.
  GOOGLESQL_ASSERT_OK(ParseNextStatement(&resume_location, parser_options, &parser_output,
                               &at_end_of_input));
  EXPECT_FALSE(at_end_of_input);
  EXPECT_EQ(parser_output->statement()->DebugString(),
            R"(QueryStatement [0-8]
  Query [0-8]
    Select [0-8]
      SelectList [7-8]
        SelectColumn [7-8]
          IntLiteral(1) [7-8]
)");

  // "/*comment 1*/ DEFINE MACRO m2 /*comment 2*/ $m1;" has both the comments
  // preserved, and "$m1" is not expanded.
  GOOGLESQL_ASSERT_OK(ParseNextStatement(&resume_location, parser_options, &parser_output,
                               &at_end_of_input));
  EXPECT_FALSE(at_end_of_input);
  EXPECT_EQ(parser_output->statement()->DebugString(),
            R"(DefineMacroStatement [24-57]
  Identifier(m2) [37-39]
  MacroBody($m1) [54-57]
)");

  // The last statement "SELECT $m1()" has "$m1" expanded, and the comment
  // "/*internal comment*/" is not dropped, i.e. the statement that the parser
  // sees is: SELECT 123
  GOOGLESQL_ASSERT_OK(ParseNextStatement(&resume_location, parser_options, &parser_output,
                               &at_end_of_input));
  EXPECT_TRUE(at_end_of_input);
  EXPECT_EQ(parser_output->statement()->DebugString(),
            R"(QueryStatement [59-71]
  Query [59-71]
    Select [59-71]
      SelectList [66-71]
        SelectColumn [66-71]
          IntLiteral(123) [66-71]
)");
}

}  // namespace parser
}  // namespace googlesql
