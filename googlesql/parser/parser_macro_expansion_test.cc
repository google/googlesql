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
#include <vector>

#include "googlesql/parser/macros/macro_catalog.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser.h"
#include "googlesql/parser/parser_mode.h"
#include "googlesql/parser/parser_runtime_info.h"
#include "googlesql/proto/internal_error_location.pb.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
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
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::TestParamInfo;
using ::testing::TestWithParam;
using ::testing::Values;
using ::testing::ValuesIn;
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
    GOOGLESQL_ASSERT_OK(macro_catalog.RegisterMacro(macros::MacroInfo(
        source, def_macro_stmt->location(), def_macro_stmt->name()->location(),
        def_macro_stmt->body()->location())));
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

TEST(ParserMacroExpansionTest, ExpandsMacrosInStringLiteralTableReference) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO m 123", macro_catalog);

  ParserOptions parser_options(GetLanguageOptions(),
                               MacroExpansionMode::kLenient, &macro_catalog);

  ParseResumeLocation resume_location =
      ParseResumeLocation::FromStringView("SELECT * FROM 'x$m'");
  bool at_end_of_input;
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseNextStatement(&resume_location, parser_options, &parser_output,
                               &at_end_of_input));
  EXPECT_TRUE(at_end_of_input);

  EXPECT_EQ(parser_output->statement()->DebugString(),
            R"(QueryStatement [0-top_file.sql:18]
  Query [0-top_file.sql:18]
    Select [0-top_file.sql:18]
      SelectList [7-8]
        SelectColumn [7-8]
          Star(*) [7-8]
      FromClause [9-top_file.sql:18]
        TablePathExpression [top_file.sql:15-18]
          StringLiteral [top_file.sql:15-18]
            StringLiteralComponent('x123') [top_file.sql:15-18]
)");
}

TEST(ParserMacroExpansionTest, MacroExpandingToStringLiteralTableReference) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO path 'a' \"b.csv\"", macro_catalog);

  ParserOptions parser_options(GetLanguageOptions(),
                               MacroExpansionMode::kStrict, &macro_catalog);

  ParseResumeLocation resume_location =
      ParseResumeLocation::FromStringView("SELECT * FROM $path() AS t");
  bool at_end_of_input;
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseNextStatement(&resume_location, parser_options, &parser_output,
                               &at_end_of_input));
  EXPECT_TRUE(at_end_of_input);

  EXPECT_EQ(parser_output->statement()->DebugString(),
            R"(QueryStatement [0-26]
  Query [0-26]
    Select [0-26]
      SelectList [7-8]
        SelectColumn [7-8]
          Star(*) [7-8]
      FromClause [9-26]
        TablePathExpression [14-26]
          StringLiteral [14-21]
            StringLiteralComponent('a') [14-21]
            StringLiteralComponent("b.csv") [14-21]
          Alias [22-26]
            Identifier(t) [25-26]
)");
}

TEST(ParserMacroExpansionTest,
     MacroExpandingToLastComponentOfStringLiteralTableReference) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO b \"b.csv\"", macro_catalog);

  ParserOptions parser_options(GetLanguageOptions(),
                               MacroExpansionMode::kStrict, &macro_catalog);

  ParseResumeLocation resume_location =
      ParseResumeLocation::FromStringView("SELECT * FROM 'a' $b() AS t");
  bool at_end_of_input;
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseNextStatement(&resume_location, parser_options, &parser_output,
                               &at_end_of_input));
  EXPECT_TRUE(at_end_of_input);

  EXPECT_EQ(parser_output->statement()->DebugString(),
            R"(QueryStatement [0-27]
  Query [0-27]
    Select [0-27]
      SelectList [7-8]
        SelectColumn [7-8]
          Star(*) [7-8]
      FromClause [9-27]
        TablePathExpression [14-27]
          StringLiteral [14-22]
            StringLiteralComponent('a') [14-17]
            StringLiteralComponent("b.csv") [18-22]
          Alias [23-27]
            Identifier(t) [26-27]
)");
}

TEST(ParserMacroExpansionTest,
     MacroExpandingToFirstComponentOfStringLiteralTableReference) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO b 'b'", macro_catalog);

  ParserOptions parser_options(GetLanguageOptions(),
                               MacroExpansionMode::kStrict, &macro_catalog);

  ParseResumeLocation resume_location =
      ParseResumeLocation::FromStringView("SELECT * FROM $b() \"a.csv\" AS t");
  bool at_end_of_input;
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseNextStatement(&resume_location, parser_options, &parser_output,
                               &at_end_of_input));
  EXPECT_TRUE(at_end_of_input);

  EXPECT_EQ(parser_output->statement()->DebugString(),
            R"(QueryStatement [0-31]
  Query [0-31]
    Select [0-31]
      SelectList [7-8]
        SelectColumn [7-8]
          Star(*) [7-8]
      FromClause [9-31]
        TablePathExpression [14-31]
          StringLiteral [14-26]
            StringLiteralComponent('b') [14-18]
            StringLiteralComponent("a.csv") [19-26]
          Alias [27-31]
            Identifier(t) [30-31]
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

TEST(ParserMacroExpansionTest, PublicPrivateMacroAllowed) {
  MacroCatalog macro_catalog;
  ParserOptions parser_options(GetLanguageOptions(),
                               MacroExpansionMode::kStrict, &macro_catalog);

  bool at_end_of_input;
  std::unique_ptr<ParserOutput> parser_output;

  auto location1 =
      ParseResumeLocation::FromStringView("DEFINE PUBLIC MACRO m1 1");
  GOOGLESQL_EXPECT_OK(ParseNextStatement(&location1, parser_options, &parser_output,
                               &at_end_of_input));
  ASSERT_NE(parser_output->statement(), nullptr);
  EXPECT_EQ(parser_output->statement()
                ->GetAsOrNull<ASTDefineMacroStatement>()
                ->visibility(),
            ASTDefineMacroStatement::PUBLIC);

  auto location2 =
      ParseResumeLocation::FromStringView("DEFINE PRIVATE MACRO m1 1");
  GOOGLESQL_EXPECT_OK(ParseNextStatement(&location2, parser_options, &parser_output,
                               &at_end_of_input));
  ASSERT_NE(parser_output->statement(), nullptr);
  EXPECT_EQ(parser_output->statement()
                ->GetAsOrNull<ASTDefineMacroStatement>()
                ->visibility(),
            ASTDefineMacroStatement::PRIVATE);
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
                       HasSubstr("Syntax error: Unexpected \"+\"")));
}

TEST(ParserMacroExpansionTest, CorrectErrorWhenMacroNameIsMissingAtEof) {
  MacroCatalog macro_catalog;
  ParserOptions parser_options(GetLanguageOptions(),
                               MacroExpansionMode::kStrict, &macro_catalog);

  ParseResumeLocation resume_location =
      ParseResumeLocation::FromStringView("DEFINE MACRO      /*nothing*/  ");
  bool at_end_of_input;
  std::unique_ptr<ParserOutput> parser_output;
  EXPECT_THAT(ParseNextStatement(&resume_location, parser_options,
                                 &parser_output, &at_end_of_input),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Syntax error: Unexpected end of statement")));
}

TEST(ParserMacroExpansionTest, CorrectErrorWhenMacroNameIsMissingAtSemicolon) {
  MacroCatalog macro_catalog;
  ParserOptions parser_options(GetLanguageOptions(),
                               MacroExpansionMode::kStrict, &macro_catalog);

  ParseResumeLocation resume_location =
      ParseResumeLocation::FromStringView("DEFINE MACRO      /*nothing*/  ;");
  bool at_end_of_input;
  std::unique_ptr<ParserOutput> parser_output;
  EXPECT_THAT(ParseNextStatement(&resume_location, parser_options,
                                 &parser_output, &at_end_of_input),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Syntax error: Unexpected \";\"")));
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

struct ParseNextResult {
  absl::Status status;
  std::unique_ptr<ParserOutput> output;
  bool has_macro_invocation = false;
};

static ParseNextResult ParseNext(ParseResumeLocation& resume_location,
                                 const ParserOptions& parser_options,
                                 bool* at_end_of_input,
                                 ParserMode mode = ParserMode::kNextStatement) {
  std::unique_ptr<ParserOutput> parser_output;
  ParserRuntimeInfo runtime_info;
  const absl::Status status =
      mode == ParserMode::kNextScriptStatement
          ? ParseNextScriptStatement(&resume_location, parser_options,
                                     &parser_output, at_end_of_input,
                                     &runtime_info)
          : ParseNextStatement(&resume_location, parser_options, &parser_output,
                               at_end_of_input, &runtime_info);
  return {.status = status,
          .output = std::move(parser_output),
          .has_macro_invocation = runtime_info.has_macro_invocation()};
}

struct HasMacroInvocationTestCase {
  std::string test_name;
  MacroExpansionMode mode = MacroExpansionMode::kStrict;
  std::string sql;
  std::string expected_error;
  bool has_macro_invocation = false;
};

using HasMacroInvocationTest = TestWithParam<HasMacroInvocationTestCase>;

TEST_P(HasMacroInvocationTest, ReportsMacroInvocation) {
  const HasMacroInvocationTestCase& test_case = GetParam();
  MacroCatalog macro_catalog;
  RegisterMacros(
      "DEFINE MACRO m 123;"
      "DEFINE MACRO empty;"
      "DEFINE MACRO id $1;"
      "DEFINE MACRO fused 1.2;"
      "DEFINE MACRO outer $m();",
      macro_catalog);
  ParserOptions parser_options(GetLanguageOptions(), test_case.mode,
                               &macro_catalog);
  ParseResumeLocation resume_location =
      ParseResumeLocation::FromStringView("query.sql", test_case.sql);
  bool at_end_of_input = false;

  const ParseNextResult result =
      ParseNext(resume_location, parser_options, &at_end_of_input);

  if (test_case.expected_error.empty()) {
    GOOGLESQL_ASSERT_OK(result.status);
    EXPECT_EQ(result.output->runtime_info().has_macro_invocation(),
              test_case.has_macro_invocation);
  } else {
    ASSERT_THAT(result.status,
                StatusIs(_, HasSubstr(test_case.expected_error)));
  }
  EXPECT_EQ(result.has_macro_invocation, test_case.has_macro_invocation);
}

INSTANTIATE_TEST_SUITE_P(
    HasMacroInvocationTest, HasMacroInvocationTest,
    ValuesIn<HasMacroInvocationTestCase>({
        {.test_name = "SimpleMacro",
         .sql = "SELECT $m();",
         .has_macro_invocation = true},
        {.test_name = "NoMacros", .sql = "SELECT 123;"},
        {.test_name = "MacroWithArguments",
         .sql = "SELECT $id(456);",
         .has_macro_invocation = true},
        {.test_name = "MacroFusedByLookahead",
         .sql = "SELECT $fused();",
         .has_macro_invocation = true},
        {.test_name = "EmptyMacro",
         .sql = "SELECT $empty() 1;",
         .has_macro_invocation = true},
        {.test_name = "NestedMacro",
         .sql = "SELECT $outer();",
         .has_macro_invocation = true},
        {.test_name = "BuiltinMacro",
         .sql = "SELECT $$IDENTIFIER(abc);",
         .has_macro_invocation = true},
        {.test_name = "DefineMacroBody", .sql = "DEFINE MACRO m2 $m() + $1;"},
        {.test_name = "MalformedDefineMacroName",
         .sql = "DEFINE MACRO $m 1;",
         .expected_error = "Syntax error"},
        {.test_name = "MacroInNextStatement", .sql = "SELECT 1; SELECT $m();"},
        {.test_name = "EmptyMacroAfterLastStatement",
         .sql = "SELECT 1; $empty()"},
        {.test_name = "LiteralInStrictMode", .sql = "SELECT 'x$m';"},
        {.test_name = "LiteralInLenientMode",
         .mode = MacroExpansionMode::kLenient,
         .sql = "SELECT 'x$m';",
         .has_macro_invocation = true},
        {.test_name = "LiteralWithEmptyMacroInLenientMode",
         .mode = MacroExpansionMode::kLenient,
         .sql = "SELECT 'x$empty';",
         .has_macro_invocation = true},
        {.test_name = "LiteralWithUnknownMacroInLenientMode",
         .mode = MacroExpansionMode::kLenient,
         .sql = "SELECT '$USD';",
         .has_macro_invocation = true},
        {.test_name = "TableLiteralInStrictMode",
         .sql = "SELECT * FROM 'x$m';"},
        {.test_name = "TableLiteralInLenientMode",
         .mode = MacroExpansionMode::kLenient,
         .sql = "SELECT * FROM 'x$m';",
         .has_macro_invocation = true},
        {.test_name = "LiteralInNoneMode",
         .mode = MacroExpansionMode::kNone,
         .sql = "SELECT '$m()';"},
        {.test_name = "UndefinedMacro",
         .sql = "SELECT $undefined;",
         .expected_error = "Macro 'undefined' not found",
         .has_macro_invocation = true},
        {.test_name = "MacroWithoutArgumentList",
         .sql = "SELECT a.$m.c;",
         .expected_error = "missing argument list",
         .has_macro_invocation = true},
        {.test_name = "TokenizerErrorAfterMacro",
         .sql = "SELECT $m() 'abc;",
         .expected_error = "Unclosed string literal",
         .has_macro_invocation = true},
        {.test_name = "SyntaxErrorAfterMacro",
         .sql = "SELECT $m() +;",
         .expected_error = "Syntax error",
         .has_macro_invocation = true},
        {.test_name = "SyntaxErrorAtTokenSplicedWithMacro",
         .sql = "SELECT 1 AS x y$empty();",
         .expected_error = "Syntax error",
         .has_macro_invocation = true},
        {.test_name = "SyntaxErrorBeforeMacro",
         .sql = "SELECT 1 2\nSELECT $m();",
         .expected_error = "Syntax error"},
        {.test_name = "SyntaxErrorAtExpandedLiteralInLenientMode",
         .mode = MacroExpansionMode::kLenient,
         // A string literal is a valid table reference in FROM, but not as a
         // DELETE target.
         .sql = "DELETE FROM 'x$m';",
         .expected_error = "Syntax error",
         .has_macro_invocation = true},
        {.test_name = "UnknownMacroInLenientMode",
         .mode = MacroExpansionMode::kLenient,
         .sql = "SELECT $undefined;",
         .expected_error = "Syntax error",
         .has_macro_invocation = true},
        {.test_name = "MacroInNoneMode",
         .mode = MacroExpansionMode::kNone,
         .sql = "SELECT $m();",
         .expected_error = "Syntax error",
         .has_macro_invocation = true},
        {.test_name = "BuiltinMacroInNoneMode",
         .mode = MacroExpansionMode::kNone,
         .sql = "SELECT $$IDENTIFIER(a);",
         .expected_error = "Syntax error",
         .has_macro_invocation = true},
        {.test_name = "MacroArgumentInNoneMode",
         .mode = MacroExpansionMode::kNone,
         .sql = "SELECT 1 AS $1;",
         .expected_error = "Syntax error",
         .has_macro_invocation = true},
        {.test_name = "SyntaxErrorBeforeMacroInNoneMode",
         .mode = MacroExpansionMode::kNone,
         .sql = "SELECT 1 2 $m();",
         .expected_error = "Syntax error"},
        {.test_name = "BareDollarSignInNoneMode",
         .mode = MacroExpansionMode::kNone,
         .sql = "SELECT 1 + $;",
         .expected_error = "Syntax error"},
    }),
    [](const TestParamInfo<HasMacroInvocationTestCase>& info) {
      return info.param.test_name;
    });

using ParserMacroExpansionModeTest = TestWithParam<MacroExpansionMode>;

TEST_P(ParserMacroExpansionModeTest,
       SemicolonDoesNotConsumeLookaheadMacroInNextStatement) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO next_stmt SELECT 123;", macro_catalog);
  ParserOptions parser_options(GetLanguageOptions(), GetParam(),
                               &macro_catalog);
  ParseResumeLocation resume_location =
      ParseResumeLocation::FromStringView("query.sql", "SELECT 1;$next_stmt()");
  bool at_end_of_input = false;

  const ParseNextResult first =
      ParseNext(resume_location, parser_options, &at_end_of_input);
  GOOGLESQL_ASSERT_OK(first.status);
  EXPECT_FALSE(first.has_macro_invocation);

  const ParseNextResult second =
      ParseNext(resume_location, parser_options, &at_end_of_input);
  EXPECT_EQ(second.status.ok(), GetParam() != MacroExpansionMode::kNone);
  EXPECT_TRUE(second.has_macro_invocation);
}

TEST_P(ParserMacroExpansionModeTest,
       SemicolonDoesNotConsumeLookaheadMacroInNextScriptStatement) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO next_stmt SELECT 123;", macro_catalog);
  ParserOptions parser_options(GetLanguageOptions(), GetParam(),
                               &macro_catalog);
  ParseResumeLocation resume_location = ParseResumeLocation::FromStringView(
      "query.sql", "IMPORT MODULE a.b;$next_stmt()");
  bool at_end_of_input = false;

  const ParseNextResult first =
      ParseNext(resume_location, parser_options, &at_end_of_input,
                ParserMode::kNextScriptStatement);
  GOOGLESQL_ASSERT_OK(first.status);
  EXPECT_FALSE(first.has_macro_invocation);

  const ParseNextResult second =
      ParseNext(resume_location, parser_options, &at_end_of_input,
                ParserMode::kNextScriptStatement);
  EXPECT_EQ(second.status.ok(), GetParam() != MacroExpansionMode::kNone);
  EXPECT_TRUE(second.has_macro_invocation);
}

TEST_P(ParserMacroExpansionModeTest,
       SyntaxErrorBeforeLookaheadMacroDoesNotReportInvocation) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO m 123;", macro_catalog);
  ParserOptions parser_options(GetLanguageOptions(), GetParam(),
                               &macro_catalog);
  ParseResumeLocation resume_location =
      ParseResumeLocation::FromStringView("query.sql", "SELECT 1 )$m();");
  bool at_end_of_input = false;

  const ParseNextResult result =
      ParseNext(resume_location, parser_options, &at_end_of_input);

  ASSERT_THAT(result.status, StatusIs(_, HasSubstr("Syntax error")));
  EXPECT_FALSE(result.has_macro_invocation);
}

INSTANTIATE_TEST_SUITE_P(AllMacroExpansionModes, ParserMacroExpansionModeTest,
                         Values(MacroExpansionMode::kNone,
                                MacroExpansionMode::kLenient,
                                MacroExpansionMode::kStrict));

TEST(ParserMacroExpansionTest, SyntaxErrorInsideExpandedMacro) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO two_ints 1 2;", macro_catalog);
  ParserOptions parser_options(GetLanguageOptions(),
                               MacroExpansionMode::kStrict, &macro_catalog);
  ParseResumeLocation resume_location = ParseResumeLocation::FromStringView(
      "query.sql", "SELECT 'the macro starts after offset 24', $two_ints();");
  bool at_end_of_input = false;

  const ParseNextResult result =
      ParseNext(resume_location, parser_options, &at_end_of_input);

  ASSERT_THAT(result.status, StatusIs(_, HasSubstr("Syntax error")));
  EXPECT_TRUE(result.has_macro_invocation);
}

TEST(ParserMacroExpansionTest, MacroExpandingToSemicolonReportsInvocation) {
  constexpr absl::string_view kDefinition = "DEFINE MACRO end_stmt 1;";
  MacroCatalog macro_catalog;
  GOOGLESQL_ASSERT_OK(macro_catalog.RegisterMacro(macros::MacroInfo(
      kDefinition,
      ParseLocationRange(ParseLocationPoint::FromByteOffset(kTopFileName, 0),
                         ParseLocationPoint::FromByteOffset(kTopFileName, 24)),
      ParseLocationRange(ParseLocationPoint::FromByteOffset(kTopFileName, 13),
                         ParseLocationPoint::FromByteOffset(kTopFileName, 21)),
      ParseLocationRange(
          ParseLocationPoint::FromByteOffset(kTopFileName, 22),
          ParseLocationPoint::FromByteOffset(kTopFileName, 24)))));
  ParserOptions parser_options(GetLanguageOptions(),
                               MacroExpansionMode::kStrict, &macro_catalog);
  ParseResumeLocation resume_location = ParseResumeLocation::FromStringView(
      "query.sql", "SELECT $end_stmt() SELECT 2");
  bool at_end_of_input = false;

  const ParseNextResult result =
      ParseNext(resume_location, parser_options, &at_end_of_input);

  GOOGLESQL_ASSERT_OK(result.status);
  ASSERT_EQ(resume_location.byte_position(), 18);
  EXPECT_TRUE(result.has_macro_invocation);
}

TEST(ParserMacroExpansionTest, RuntimeInfoIsPerStatementAcrossParseNextCalls) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO m 123;", macro_catalog);
  ParserOptions parser_options(GetLanguageOptions(),
                               MacroExpansionMode::kStrict, &macro_catalog);
  ParseResumeLocation resume_location = ParseResumeLocation::FromStringView(
      "query.sql", "SELECT 1; SELECT $m(); SELECT 2; SELECT $m()");
  bool at_end_of_input = false;

  std::vector<bool> has_macro_invocation;
  while (!at_end_of_input) {
    const ParseNextResult result =
        ParseNext(resume_location, parser_options, &at_end_of_input);
    GOOGLESQL_ASSERT_OK(result.status);
    has_macro_invocation.push_back(result.has_macro_invocation);
  }

  EXPECT_THAT(has_macro_invocation, ElementsAre(false, true, false, true));
}

TEST(ParserMacroExpansionTest, ParseStatementReportsMacroInvocation) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO m 123;", macro_catalog);
  ParserOptions parser_options(GetLanguageOptions(),
                               MacroExpansionMode::kStrict, &macro_catalog);
  std::unique_ptr<ParserOutput> parser_output;

  GOOGLESQL_ASSERT_OK(ParseStatement("SELECT $m()", parser_options, &parser_output));

  EXPECT_TRUE(parser_output->runtime_info().has_macro_invocation());
}

TEST(ParserMacroExpansionTest, ParseScriptReportsMacroInvocation) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO m 123;", macro_catalog);
  ParserOptions parser_options(GetLanguageOptions(),
                               MacroExpansionMode::kStrict, &macro_catalog);
  std::unique_ptr<ParserOutput> parser_output;

  GOOGLESQL_ASSERT_OK(ParseScript("SELECT 1; SELECT $m();", parser_options,
                        ErrorMessageOptions{}, &parser_output));

  EXPECT_TRUE(parser_output->runtime_info().has_macro_invocation());
}

TEST(ParserMacroExpansionTest, ParseExpressionReportsMacroInvocation) {
  MacroCatalog macro_catalog;
  RegisterMacros("DEFINE MACRO m 123;", macro_catalog);
  ParserOptions parser_options(GetLanguageOptions(),
                               MacroExpansionMode::kStrict, &macro_catalog);
  std::unique_ptr<ParserOutput> parser_output;

  GOOGLESQL_ASSERT_OK(ParseExpression("1 + $m()", parser_options, &parser_output));

  EXPECT_TRUE(parser_output->runtime_info().has_macro_invocation());
}

TEST(ParserMacroExpansionTest, MatchRecognizeEndAnchorIsNotMacroInvocation) {
  LanguageOptions language_options = GetLanguageOptions();
  GOOGLESQL_ASSERT_OK(language_options.EnableReservableKeyword("MATCH_RECOGNIZE"));
  ParserOptions parser_options(language_options, MacroExpansionMode::kNone);
  ParseResumeLocation resume_location = ParseResumeLocation::FromStringView(
      "query.sql",
      "SELECT * FROM KeyValue MATCH_RECOGNIZE (ORDER BY key MEASURES "
      "max(length(value)) AS m PATTERN (a $) DEFINE a AS length(value) < 10)");
  bool at_end_of_input = false;

  const ParseNextResult result =
      ParseNext(resume_location, parser_options, &at_end_of_input);

  GOOGLESQL_ASSERT_OK(result.status);
  EXPECT_FALSE(result.has_macro_invocation);
}

TEST(ParserMacroExpansionTest, AccumulateAllPropagatesHasMacroInvocation) {
  ParserRuntimeInfo info1;
  ParserRuntimeInfo info2;
  info2.set_has_macro_invocation(true);

  info1.AccumulateAll(info2);

  EXPECT_TRUE(info1.has_macro_invocation());
}

}  // namespace parser
}  // namespace googlesql
