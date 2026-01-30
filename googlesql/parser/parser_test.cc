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

#include <cstdint>
#include <memory>
#include <string>

#include "googlesql/base/logging.h"
#include "googlesql/base/helpers.h"
#include "googlesql/base/options.h"
#include "parsers/sql/sql_parser_test_helpers.h"
#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/parser/parser_runtime_info.h"
#include "googlesql/proto/internal_error_location.pb.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/public/proto/logging.pb.h"
#include "benchmark/benchmark.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status.h"

ABSL_FLAG(std::string, parser_benchmark_query,
          "SELECT Column FROM Table WHERE OtherColumn = 123",
          "Test sql to run in BM_ParseQueryFromFlag benchmark");

ABSL_FLAG(std::string, parser_benchmark_query_file, "",
          "File containing sql to run in BM_ParseQueryFromFile benchmark");

ABSL_DECLARE_FLAG(bool, googlesql_parser_strip_errors);
ABSL_DECLARE_FLAG(bool, googlesql_redact_error_messages_for_tests);

namespace googlesql {

using ::absl::StatusCode::kInvalidArgument;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::ExplainMatchResult;
using ::testing::HasSubstr;
using ::testing::Not;
using ::absl_testing::StatusIs;

MATCHER_P(StatusHasByteOffset, byte_offset, "") {
  const InternalErrorLocation& location =
      ::util::GetPayload<InternalErrorLocation>(arg);
  return ExplainMatchResult(Eq(byte_offset), location.byte_offset(),
                            result_listener);
}

MATCHER_P2(WarningHasSubstrAndByteOffset, substr, byte_offset, "") {
  return ExplainMatchResult(
      AllOf(StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr(substr)),
            StatusHasByteOffset(byte_offset)),
      arg, result_listener);
}

TEST(ParserTest, IssuesWarningOnUsageOfGraphTable) {
  std::unique_ptr<ParserOutput> parser_output;
  LanguageOptions options_with_graphs;
  options_with_graphs.EnableLanguageFeature(FEATURE_SQL_GRAPH);

  // Unambiguous places produce no warning. Same for quoted identifiers.
  // Note: the tokenizer doesn't recognize keywords as keywords when prefixed.
  //       It tags them as identifiers instead.
  GOOGLESQL_ASSERT_OK(
      ParseStatement("select * FROM t.GRAPH_TABLE() INNER JOIN `GRAPH_TABLE`",
                     ParserOptions(options_with_graphs), &parser_output));

  EXPECT_TRUE(parser_output->warnings().empty());

  GOOGLESQL_ASSERT_OK(ParseStatement(
      "select graph_table(1), graph_table AS gRaph_tAblE FROM GRAPH_TABLE()",
      ParserOptions(options_with_graphs), &parser_output));

  constexpr absl::string_view kGraphTableWarningText =
      "GRAPH_TABLE is used as an identifier. GRAPH_TABLE may become a reserved "
      "word in the future. To make this statement robust, add backticks around "
      "GRAPH_TABLE to make the identifier unambiguous";
  EXPECT_THAT(
      parser_output->warnings(),
      ElementsAre(WarningHasSubstrAndByteOffset(kGraphTableWarningText, 7),
                  WarningHasSubstrAndByteOffset(kGraphTableWarningText, 23),
                  WarningHasSubstrAndByteOffset(kGraphTableWarningText, 38),
                  WarningHasSubstrAndByteOffset(kGraphTableWarningText, 55)));
}

TEST(ParserTest, IssuesWarningWhenQualifyIsUsedAsAnIdentifier) {
  std::unique_ptr<ParserOutput> parser_output;
  LanguageOptions options_with_qualify_feature;
  options_with_qualify_feature.EnableLanguageFeature(FEATURE_QUALIFY);
  GOOGLESQL_ASSERT_OK(
      options_with_qualify_feature.EnableReservableKeyword("QUALIFY",
                                                           /*reserved=*/false));

  // Unambiguous places produce no warning. Same for quoted identifiers.
  // Note: the tokenizer doesn't recognize keywords as keywords when prefixed.
  //       It tags them as identifiers instead.
  GOOGLESQL_ASSERT_OK(ParseStatement(
      "select `qualify`.qualify FROM `QUALIFY` WHERE a = 1 "
      "QUALIFY `qualify` = 1",
      ParserOptions(options_with_qualify_feature), &parser_output));

  EXPECT_TRUE(parser_output->warnings().empty());

  const absl::string_view sql_causing_warnings =
      "select qualify(1), qualify AS qUaLifY FROM qualify(), qualify AS "
      "qualify WHERE qualify = 1 QUALIFY qualify";
  GOOGLESQL_ASSERT_OK(ParseStatement(sql_causing_warnings,
                           ParserOptions(options_with_qualify_feature),
                           &parser_output));

  // Note the offsets: everything here causes a warning, except the legitimate
  // one, which is fully capitalized in this example.
  const absl::string_view kQualifyWarning =
      "QUALIFY is used as an identifier. QUALIFY may become a reserved word in "
      "the future. To make this statement robust, add backticks around QUALIFY "
      "to make the identifier unambiguous";
  EXPECT_THAT(parser_output->warnings(),
              ElementsAre(WarningHasSubstrAndByteOffset(kQualifyWarning, 7),
                          WarningHasSubstrAndByteOffset(kQualifyWarning, 19),
                          WarningHasSubstrAndByteOffset(kQualifyWarning, 30),
                          WarningHasSubstrAndByteOffset(kQualifyWarning, 43),
                          WarningHasSubstrAndByteOffset(kQualifyWarning, 54),
                          WarningHasSubstrAndByteOffset(kQualifyWarning, 65),
                          WarningHasSubstrAndByteOffset(kQualifyWarning, 79),
                          WarningHasSubstrAndByteOffset(kQualifyWarning, 99)));

  // Verify that the legitimate use (which we capitalized above) does not
  // generate a warning
  const int legitimate_usage_start_offset = 91;
  EXPECT_EQ(sql_causing_warnings.substr(legitimate_usage_start_offset,
                                        std::string("QUALIFY").length()),
            "QUALIFY");
  EXPECT_THAT(parser_output->warnings(),
              Not(Contains(WarningHasSubstrAndByteOffset(
                  kQualifyWarning, legitimate_usage_start_offset))));
}

TEST(ParserTest, IssuesWarningWhenFunctionOptionsPlacedAfterBody) {
  std::unique_ptr<ParserOutput> parser_output;

  GOOGLESQL_ASSERT_OK(ParseStatement(
      R"sql(CREATE FUNCTION fn(s STRING) RETURNS STRING LANGUAGE testlang
                OPTIONS ( a=b, bruce=springsteen ) AS "return 'a'";)sql",
      ParserOptions(), &parser_output));
  EXPECT_TRUE(parser_output->warnings().empty());

  GOOGLESQL_ASSERT_OK(ParseStatement(
      R"sql(CREATE FUNCTION fn(s STRING) RETURNS STRING LANGUAGE testlang
                AS "return 'a'" OPTIONS ( a=b, bruce=springsteen );)sql",
      ParserOptions(), &parser_output));
  EXPECT_THAT(parser_output->warnings(),
              ElementsAre(WarningHasSubstrAndByteOffset(
                  "The preferred style places the OPTIONS clause before the "
                  "function body",
                  94)));
}

TEST(ParserTest, ErrorWhenConcatenatedLiteralsAreNotSeparate) {
  std::unique_ptr<ParserOutput> parser_output;
  LanguageOptions language_options;
  EXPECT_THAT(
      ParseStatement("select 'x''y'", ParserOptions(language_options),
                     &parser_output),
      StatusIs(kInvalidArgument,
               HasSubstr("Syntax error: concatenated string literals must be "
                         "separated by whitespace or comments")));

  EXPECT_THAT(
      ParseStatement("select \"x\"r'y'", ParserOptions(language_options),
                     &parser_output),
      StatusIs(kInvalidArgument,
               HasSubstr("Syntax error: concatenated string literals must be "
                         "separated by whitespace or comments")));

  EXPECT_THAT(
      ParseStatement("select b'x'b'y'", ParserOptions(language_options),
                     &parser_output),
      StatusIs(kInvalidArgument,
               HasSubstr("Syntax error: concatenated bytes literals must be "
                         "separated by whitespace or comments")));

  EXPECT_THAT(
      ParseStatement("select b\"x\"rb'y'", ParserOptions(language_options),
                     &parser_output),
      StatusIs(kInvalidArgument,
               HasSubstr("Syntax error: concatenated bytes literals must be "
                         "separated by whitespace or comments")));
}

class DeepStackQueriesTest
    : public parsers_sql::LargeExpressionAtParserLimitTest {
  void TryToParseExpression(
      const int piece_repeat_count,
      const parsers_sql::ExpressionTestSpec &test_spec,
      bool* expression_parsed) override {
    std::string query = "";
    AppendTestExpression(piece_repeat_count, test_spec, &query);

    std::unique_ptr<ParserOutput> parser_output;

    // There should be no stack based limitations because Bison is not a
    // recursive descent parser.
    GOOGLESQL_EXPECT_OK(ParseExpression(query, ParserOptions(), &parser_output));
  }
};

PARSE_LIMIT_POINT_TEST(DeepStackQueriesTest, true);

// Test that comments at the end of queries do not cause errors.
// File-based tests cannot be used since they would add a newline to the end
// of the query.
class ParseQueryEndingWithCommentTest : public ::testing::Test {
 public:
  absl::Status RunStatementTest(
      absl::string_view test_case_input,
      std::unique_ptr<ParserOutput>* parser_output) {
    return ParseStatement(
        test_case_input, ParserOptions(), parser_output);
  }

  absl::Status RunNextStatementTest(
      absl::string_view test_case_input,
      std::unique_ptr<ParserOutput>* parser_output) {
    bool at_end_of_input;
    ParseResumeLocation resume_location =
        ParseResumeLocation::FromStringView(test_case_input);
    absl::Status status = ParseNextStatement(
        &resume_location, ParserOptions(), parser_output, &at_end_of_input);
    return status;
  }

  absl::Status RunNextScriptStatementTest(
      absl::string_view test_case_input,
      std::unique_ptr<ParserOutput>* parser_output,
      const LanguageOptions& language_options = {}) {
    bool at_end_of_input;
    ParseResumeLocation resume_location =
        ParseResumeLocation::FromStringView(test_case_input);
    absl::Status status = ParseNextScriptStatement(
        &resume_location, ParserOptions(language_options), parser_output,
        &at_end_of_input);
    return status;
  }

  ASTNodeKind RunStatementKindTest(
      absl::string_view test_case_input) {
    bool is_ctas;
    LanguageOptions language_options;
    return ParseStatementKind(
        test_case_input, language_options, &is_ctas);
  }

  ASTNodeKind RunNextStatementKindTest(
      absl::string_view test_case_input,
      const LanguageOptions& language_options = {}) {
    bool is_ctas;
    ParseResumeLocation resume_location =
        ParseResumeLocation::FromStringView(test_case_input);
    return ParseNextStatementKind(resume_location, language_options, &is_ctas);
  }

  absl::Status RunScriptTest(
      absl::string_view test_case_input,
      std::unique_ptr<ParserOutput>* parser_output) {
    return ParseScript(
        test_case_input, ParserOptions(), ERROR_MESSAGE_WITH_PAYLOAD,
        parser_output);
  }
};

TEST_F(ParseQueryEndingWithCommentTest, DashComment) {
  absl::string_view query = "select 0;--comment";
  const int kStmtLength = 8;
  std::unique_ptr<ParserOutput> parser_output;

  GOOGLESQL_EXPECT_OK(RunStatementTest(query, &parser_output));
  ASSERT_NE(parser_output->statement(), nullptr);
  EXPECT_EQ(parser_output->statement()->location().start(),
            ParseLocationPoint::FromByteOffset(0));
  EXPECT_EQ(parser_output->statement()->location().end(),
            ParseLocationPoint::FromByteOffset(kStmtLength));

  GOOGLESQL_EXPECT_OK(RunNextStatementTest(query, &parser_output));
  ASSERT_NE(parser_output->statement(), nullptr);
  EXPECT_EQ(parser_output->statement()->location().start(),
            ParseLocationPoint::FromByteOffset(0));
  EXPECT_EQ(parser_output->statement()->location().end(),
            ParseLocationPoint::FromByteOffset(kStmtLength));

  GOOGLESQL_EXPECT_OK(RunNextScriptStatementTest(query, &parser_output));
  ASSERT_NE(parser_output->statement(), nullptr);
  EXPECT_EQ(parser_output->statement()->location().start(),
            ParseLocationPoint::FromByteOffset(0));
  EXPECT_EQ(parser_output->statement()->location().end(),
            ParseLocationPoint::FromByteOffset(kStmtLength));

  ASTNodeKind node_kind = RunStatementKindTest(query);
  ASSERT_EQ(node_kind, AST_QUERY_STATEMENT);

  GOOGLESQL_EXPECT_OK(RunScriptTest(query, &parser_output));
  ASSERT_NE(parser_output->script(), nullptr);
  EXPECT_EQ(parser_output->script()->location().start(),
            ParseLocationPoint::FromByteOffset(0));
  EXPECT_EQ(parser_output->script()->location().end().GetByteOffset(), 9);
}

TEST_F(ParseQueryEndingWithCommentTest, PoundComment) {
  absl::string_view query = "select 0;#comment";
  const int kStmtLength = 8;
  std::unique_ptr<ParserOutput> parser_output;

  GOOGLESQL_EXPECT_OK(RunStatementTest(query, &parser_output));
  ASSERT_NE(parser_output->statement(), nullptr);
  EXPECT_EQ(parser_output->statement()->location().start(),
            ParseLocationPoint::FromByteOffset(0));
  EXPECT_EQ(parser_output->statement()->location().end(),
            ParseLocationPoint::FromByteOffset(kStmtLength));

  GOOGLESQL_EXPECT_OK(RunNextStatementTest(query, &parser_output));
  ASSERT_NE(parser_output->statement(), nullptr);
  EXPECT_EQ(parser_output->statement()->location().start(),
            ParseLocationPoint::FromByteOffset(0));
  EXPECT_EQ(parser_output->statement()->location().end(),
            ParseLocationPoint::FromByteOffset(kStmtLength));

  GOOGLESQL_EXPECT_OK(RunNextScriptStatementTest(query, &parser_output));
  ASSERT_NE(parser_output->statement(), nullptr);
  EXPECT_EQ(parser_output->statement()->location().start(),
            ParseLocationPoint::FromByteOffset(0));
  EXPECT_EQ(parser_output->statement()->location().end(),
            ParseLocationPoint::FromByteOffset(kStmtLength));

  ASTNodeKind node_kind = RunStatementKindTest(query);
  ASSERT_EQ(node_kind, AST_QUERY_STATEMENT);

  GOOGLESQL_EXPECT_OK(RunScriptTest(query, &parser_output));
  ASSERT_NE(parser_output->script(), nullptr);
  EXPECT_EQ(parser_output->script()->location().start(),
            ParseLocationPoint::FromByteOffset(0));
  EXPECT_EQ(parser_output->script()->location().end().GetByteOffset(), 9);
}

TEST_F(ParseQueryEndingWithCommentTest, CommentEndingWithWhitespace) {
  absl::string_view query = "select 0;#comment ";
  const int kStmtLength = 8;
  std::unique_ptr<ParserOutput> parser_output;

  GOOGLESQL_EXPECT_OK(RunStatementTest(query, &parser_output));
  ASSERT_NE(parser_output->statement(), nullptr);
  EXPECT_EQ(parser_output->statement()->location().start(),
            ParseLocationPoint::FromByteOffset(0));
  EXPECT_EQ(parser_output->statement()->location().end(),
            ParseLocationPoint::FromByteOffset(kStmtLength));

  GOOGLESQL_EXPECT_OK(RunNextStatementTest(query, &parser_output));
  ASSERT_NE(parser_output->statement(), nullptr);
  EXPECT_EQ(parser_output->statement()->location().start(),
            ParseLocationPoint::FromByteOffset(0));
  EXPECT_EQ(parser_output->statement()->location().end(),
            ParseLocationPoint::FromByteOffset(kStmtLength));

  GOOGLESQL_EXPECT_OK(RunNextScriptStatementTest(query, &parser_output));
  ASSERT_NE(parser_output->statement(), nullptr);
  EXPECT_EQ(parser_output->statement()->location().start(),
            ParseLocationPoint::FromByteOffset(0));
  EXPECT_EQ(parser_output->statement()->location().end(),
            ParseLocationPoint::FromByteOffset(kStmtLength));

  ASTNodeKind node_kind = RunStatementKindTest(query);
  ASSERT_EQ(node_kind, AST_QUERY_STATEMENT);

  GOOGLESQL_EXPECT_OK(RunScriptTest(query, &parser_output));
  ASSERT_NE(parser_output->script(), nullptr);
  EXPECT_EQ(parser_output->script()->location().start(),
            ParseLocationPoint::FromByteOffset(0));
  EXPECT_EQ(parser_output->script()->location().end().GetByteOffset(), 9);
}

TEST_F(ParseQueryEndingWithCommentTest, EmptyComment) {
  absl::string_view query = "select 0;--";
  const int kStmtLength = 8;
  std::unique_ptr<ParserOutput> parser_output;

  GOOGLESQL_EXPECT_OK(RunStatementTest(query, &parser_output));
  ASSERT_NE(parser_output->statement(), nullptr);
  EXPECT_EQ(parser_output->statement()->location().start(),
            ParseLocationPoint::FromByteOffset(0));
  EXPECT_EQ(parser_output->statement()->location().end(),
            ParseLocationPoint::FromByteOffset(kStmtLength));

  GOOGLESQL_EXPECT_OK(RunNextStatementTest(query, &parser_output));
  ASSERT_NE(parser_output->statement(), nullptr);
  EXPECT_EQ(parser_output->statement()->location().start(),
            ParseLocationPoint::FromByteOffset(0));
  EXPECT_EQ(parser_output->statement()->location().end(),
            ParseLocationPoint::FromByteOffset(kStmtLength));

  GOOGLESQL_EXPECT_OK(RunNextScriptStatementTest(query, &parser_output));
  ASSERT_NE(parser_output->statement(), nullptr);
  EXPECT_EQ(parser_output->statement()->location().start(),
            ParseLocationPoint::FromByteOffset(0));
  EXPECT_EQ(parser_output->statement()->location().end(),
            ParseLocationPoint::FromByteOffset(kStmtLength));

  ASTNodeKind node_kind = RunStatementKindTest(query);
  ASSERT_EQ(node_kind, AST_QUERY_STATEMENT);

  GOOGLESQL_EXPECT_OK(RunScriptTest(query, &parser_output));
  ASSERT_NE(parser_output->script(), nullptr);
  EXPECT_EQ(parser_output->script()->location().start(),
            ParseLocationPoint::FromByteOffset(0));
  EXPECT_EQ(parser_output->script()->location().end().GetByteOffset(), 9);
}

TEST_F(ParseQueryEndingWithCommentTest, JustComment) {
  absl::string_view query = "--comment";
  std::unique_ptr<ParserOutput> parser_output;
  EXPECT_THAT(
      RunStatementTest(query, &parser_output),
      absl_testing::StatusIs(
          testing::_,
          testing::HasSubstr("Syntax error: Unexpected end of statement")));

  EXPECT_THAT(
      RunNextStatementTest(query, &parser_output),
      absl_testing::StatusIs(
          testing::_,
          testing::HasSubstr("Syntax error: Unexpected end of statement")));

  EXPECT_THAT(
      RunNextScriptStatementTest(query, &parser_output),
      absl_testing::StatusIs(
          testing::_,
          testing::HasSubstr("Syntax error: Unexpected end of statement")));

  ASTNodeKind node_kind = RunStatementKindTest(query);
  ASSERT_EQ(node_kind, kUnknownASTNodeKind);

  GOOGLESQL_EXPECT_OK(RunScriptTest(query, &parser_output));
  ASSERT_NE(parser_output->script(), nullptr);
  EXPECT_EQ(parser_output->script()->location().start(),
            ParseLocationPoint::FromByteOffset(9));
  EXPECT_EQ(parser_output->script()->location().end().GetByteOffset(), 9);
}

TEST_F(ParseQueryEndingWithCommentTest, JustEmptyComment) {
  absl::string_view query = "--";
  std::unique_ptr<ParserOutput> parser_output;
  EXPECT_THAT(
      RunStatementTest(query, &parser_output),
      absl_testing::StatusIs(
          testing::_,
          testing::HasSubstr("Syntax error: Unexpected end of statement")));

  EXPECT_THAT(
      RunNextStatementTest(query, &parser_output),
      absl_testing::StatusIs(
          testing::_,
          testing::HasSubstr("Syntax error: Unexpected end of statement")));

  EXPECT_THAT(
      RunNextScriptStatementTest(query, &parser_output),
      absl_testing::StatusIs(
          testing::_,
          testing::HasSubstr("Syntax error: Unexpected end of statement")));

  ASTNodeKind node_kind = RunStatementKindTest(query);
  ASSERT_EQ(node_kind, kUnknownASTNodeKind);

  GOOGLESQL_EXPECT_OK(RunScriptTest(query, &parser_output));
  ASSERT_NE(parser_output->script(), nullptr);
  EXPECT_EQ(parser_output->script()->location().start(),
            ParseLocationPoint::FromByteOffset(2));
  EXPECT_EQ(parser_output->script()->location().end().GetByteOffset(), 2);
}

TEST_F(ParseQueryEndingWithCommentTest, IncompleteStmt) {
  absl::string_view query = "SELECT --comment";
  std::unique_ptr<ParserOutput> parser_output;
  EXPECT_THAT(
      RunStatementTest(query, &parser_output),
      absl_testing::StatusIs(
          testing::_,
          testing::HasSubstr("Syntax error: Unexpected end of statement")));

  EXPECT_THAT(
      RunNextStatementTest(query, &parser_output),
      absl_testing::StatusIs(
          testing::_,
          testing::HasSubstr("Syntax error: Unexpected end of statement")));

  EXPECT_THAT(
      RunNextScriptStatementTest(query, &parser_output),
      absl_testing::StatusIs(
          testing::_,
          testing::HasSubstr("Syntax error: Unexpected end of statement")));

  ASTNodeKind node_kind = RunStatementKindTest(query);
  ASSERT_EQ(node_kind, AST_QUERY_STATEMENT);

  EXPECT_THAT(
      RunScriptTest(query, &parser_output),
      absl_testing::StatusIs(
          testing::_,
          testing::HasSubstr("Syntax error: Unexpected end of script")));
}

TEST_F(ParseQueryEndingWithCommentTest, ScriptLabelInNextScriptStatement) {
  std::unique_ptr<ParserOutput> parser_output;
  LanguageOptions options;
  options.EnableLanguageFeature(FEATURE_SCRIPT_LABEL);
  {
    absl::string_view query = "L1 : BEGIN END;";
    EXPECT_THAT(RunNextScriptStatementTest(query, &parser_output, options),
                absl_testing::IsOk());
  }
  {
    absl::string_view query = "FULL : BEGIN END;";
    EXPECT_THAT(
        RunNextScriptStatementTest(query, &parser_output, options),
        absl_testing::StatusIs(
            absl::StatusCode::kInvalidArgument,
            testing::HasSubstr("Reserved keyword 'FULL' may not be used as a "
                               "label name without backticks")));
  }
}

TEST_F(ParseQueryEndingWithCommentTest, ScriptLabelInNextStatementKind) {
  LanguageOptions options;
  options.EnableLanguageFeature(FEATURE_SCRIPT_LABEL);
  {
    absl::string_view query = "@{a=1} L1 : BEGIN END;";
    EXPECT_EQ(RunNextStatementKindTest(query, options),
              ASTNodeKind::AST_BEGIN_STATEMENT);
  }
  {
    absl::string_view query = "@{a=1} FULL : BEGIN END;";
    EXPECT_EQ(RunNextStatementKindTest(query, options),
              ASTNodeKind::kUnknownASTNodeKind);
  }
  {
    absl::string_view query = "@1 L1 : BEGIN END;";
    EXPECT_EQ(RunNextStatementKindTest(query, options),
              ASTNodeKind::AST_BEGIN_STATEMENT);
  }
  {
    absl::string_view query = "@1 FULL : BEGIN END;";
    EXPECT_EQ(RunNextStatementKindTest(query, options),
              ASTNodeKind::kUnknownASTNodeKind);
  }
}

TEST_F(ParseQueryEndingWithCommentTest, ParseType) {
  absl::string_view type = "int64--comment";
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_EXPECT_OK(ParseType(type, ParserOptions(), &parser_output));
  ASSERT_NE(parser_output->type(),  nullptr);
  EXPECT_EQ(parser_output->type()->location().start(),
            ParseLocationPoint::FromByteOffset(0));
  EXPECT_EQ(parser_output->type()->location().end(),
            ParseLocationPoint::FromByteOffset(5));
}

TEST_F(ParseQueryEndingWithCommentTest, ParseExpression) {
  absl::string_view expression = "x + 1--comment ";
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_EXPECT_OK(ParseExpression(expression, ParserOptions(), &parser_output));
  ASSERT_NE(parser_output->expression(), nullptr);
  EXPECT_EQ(parser_output->expression()->location().start(),
            ParseLocationPoint::FromByteOffset(0));
  EXPECT_EQ(parser_output->expression()->location().end(),
            ParseLocationPoint::FromByteOffset(5));
}

TEST_F(ParseQueryEndingWithCommentTest, ScriptDashComment) {
  absl::string_view script = "select 0;select 1;--comment";
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_EXPECT_OK(RunScriptTest(script, &parser_output));
  ASSERT_NE(parser_output->script(), nullptr);
  EXPECT_EQ(parser_output->script()->location().start(),
            ParseLocationPoint::FromByteOffset(0));
  EXPECT_EQ(parser_output->script()->location().end().GetByteOffset(), 18);
}

TEST_F(ParseQueryEndingWithCommentTest, ScriptPoundCommentWithWhitespace) {
  absl::string_view script = "select 0;select 1;#comment ";
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_EXPECT_OK(RunScriptTest(script, &parser_output));
  ASSERT_NE(parser_output->script(), nullptr);
  EXPECT_EQ(parser_output->script()->location().start(),
            ParseLocationPoint::FromByteOffset(0));
  EXPECT_EQ(parser_output->script()->location().end().GetByteOffset(), 18);
}

static int64_t TotalNanos(const google::protobuf::Duration duration) {
  return 1000 * duration.seconds() + duration.nanos();
}

TEST_F(ParseQueryEndingWithCommentTest, StableErrorsForParseScript) {
  constexpr absl::string_view query = "select `";
  std::unique_ptr<ParserOutput> parser_output;
  EXPECT_THAT(
      RunScriptTest(query, &parser_output),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("Unclosed")));
  absl::SetFlag(&FLAGS_googlesql_redact_error_messages_for_tests, true);
  EXPECT_THAT(
      RunScriptTest(query, &parser_output),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("SQL ERROR")));

  absl::SetFlag(&FLAGS_googlesql_redact_error_messages_for_tests, false);
}

TEST(ParserTest, ValidQueryReturnsNonZeroCpuTime) {
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseStatement("select a + f(b) FROM T",
                           ParserOptions(LanguageOptions()), &parser_output));
  ASSERT_NE(parser_output->statement(), nullptr);
  EXPECT_GT(TotalNanos(parser_output->runtime_info()
                           .parser_timed_value()
                           .ToExecutionStatsProto()
                           .cpu_time()),
            0);
}

TEST(ParserTest, ValidateErrorStripping) {
  absl::SetFlag(&FLAGS_googlesql_parser_strip_errors, false);
  std::unique_ptr<ParserOutput> parser_output;

  absl::Status error = ParseStatement(
      "select f())", ParserOptions(LanguageOptions()), &parser_output);
  EXPECT_THAT(error, StatusIs(absl::StatusCode::kInvalidArgument,
                              Not(Eq("Syntax error"))));
  EXPECT_THAT(error.ToString(), HasSubstr("Expected end of input but got"));

  absl::SetFlag(&FLAGS_googlesql_parser_strip_errors, true);

  absl::Status stripped_error = ParseStatement(
      "select f())", ParserOptions(LanguageOptions()), &parser_output);
  EXPECT_THAT(stripped_error,
              StatusIs(absl::StatusCode::kInvalidArgument, Eq("Syntax error")));
  EXPECT_EQ(stripped_error.ToString(), "INVALID_ARGUMENT: Syntax error");
}

TEST(ParserTest, ValidateStrippedErrorIsSimpleWithTextmapper) {
  absl::SetFlag(&FLAGS_googlesql_parser_strip_errors, true);
  std::unique_ptr<ParserOutput> parser_output;
  LanguageOptions options;
  absl::Status error =
      ParseStatement("select f())", ParserOptions(options), &parser_output);
  EXPECT_THAT(error,
              StatusIs(absl::StatusCode::kInvalidArgument, Eq("Syntax error")));
  EXPECT_EQ(error.ToString(), "INVALID_ARGUMENT: Syntax error");
}

class LanguageOptionsMigrationTest : public ::testing::Test {};

TEST_F(LanguageOptionsMigrationTest, LanguageOptionsTest) {
  // For this test case, we use FEATURE_ALLOW_SLASH_PATHS, which is
  // disabled by default, as a testbed to ensure language options are
  // properly propagated and respected, regardless of how ParserOptions
  // are constructed/copied/moved, etc.
  constexpr absl::string_view query = "select 0 from /a/b";
  std::unique_ptr<ParserOutput> parser_output;
  // By default, this should not parse, because FEATURE_ALLOW_SLASH_PATHS
  // is disabled.
  EXPECT_THAT(
      ParseStatement(query, ParserOptions{LanguageOptions{}}, &parser_output),
      absl_testing::StatusIs(absl::StatusCode::kInvalidArgument));

  EXPECT_THAT(
      ParseStatement(query, ParserOptions{nullptr, nullptr, LanguageOptions{}},
                     &parser_output),
      absl_testing::StatusIs(absl::StatusCode::kInvalidArgument));

  // ... Also during migration, this is also disabled by default
  EXPECT_THAT(ParseStatement(query, ParserOptions{}, &parser_output),
              absl_testing::StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(ParseStatement(query, ParserOptions{nullptr, nullptr, nullptr},
                             &parser_output),
              absl_testing::StatusIs(absl::StatusCode::kInvalidArgument));

  // If we provide default language options explicitly, it should behave
  // the same way.
  LanguageOptions explicit_language_options;
  EXPECT_THAT(ParseStatement(query, ParserOptions{explicit_language_options},
                             &parser_output),
              absl_testing::StatusIs(absl::StatusCode::kInvalidArgument));

  EXPECT_THAT(
      ParseStatement(query,
                     ParserOptions{nullptr, nullptr, explicit_language_options},
                     &parser_output),
      absl_testing::StatusIs(absl::StatusCode::kInvalidArgument));

  EXPECT_THAT(
      ParseStatement(
          query, ParserOptions{nullptr, nullptr, &explicit_language_options},
          &parser_output),
      absl_testing::StatusIs(absl::StatusCode::kInvalidArgument));

  {
    // Weird corner case during migration, clarify that we make a copy, and
    // will ignore mutations to mutable_language_options after construction.
    LanguageOptions mutable_language_options;

    ParserOptions options{nullptr, nullptr, &mutable_language_options};
    EXPECT_THAT(ParseStatement(query, options, &parser_output),
                absl_testing::StatusIs(absl::StatusCode::kInvalidArgument));
    mutable_language_options.EnableLanguageFeature(FEATURE_ALLOW_SLASH_PATHS);
    EXPECT_THAT(ParseStatement(query, options, &parser_output),
                absl_testing::StatusIs(absl::StatusCode::kInvalidArgument));
  }
}

static void BM_ParseQuery(absl::string_view query, benchmark::State& state) {
  for (auto s : state) {
    std::unique_ptr<ParserOutput> parser_output;
    absl::Status status =
        ParseStatement(query, ParserOptions(), &parser_output);
    GOOGLESQL_EXPECT_OK(status);

    // Don't measure the destruction times. The AST is passed into the
    // AnalyzerOutput and can be destroyed outside the critical path if needed.
    state.PauseTiming();
    parser_output.reset();
    state.ResumeTiming();
  }
}

// Generate expressions like "select 1+1+1+1+..." with <size> additions.
// This is a baseline that doesn't demonstrate bad runtime.
static void BM_ParseAdd(benchmark::State& state) {
  const int size = state.range(0);

  std::string query = "select 1";
  for (int i = 0; i < size; ++i) {
    query += "+1";
  }
  BM_ParseQuery(query, state);
}
BENCHMARK(BM_ParseAdd)->Range(1, 2048);

// Generate expressions like "(((((1)))))" with <size> parentheses.
// This demonstrates the bad quadratic performance of lookahead on parentheses
// in expressions.
static void BM_ParseParensExpr(benchmark::State& state) {
  const int size = state.range(0);

  const std::string query = absl::StrCat("SELECT ", std::string(size, '('), "1",
                                         std::string(size, ')'));
  BM_ParseQuery(query, state);
}
BENCHMARK(BM_ParseParensExpr)->Range(1, 512);

// Generate expressions like "(((((select 1)))))" with <size> parentheses.
// This demonstrates the bad quadratic performance of lookahead on parentheses
// in queries.
static void BM_ParseParensQuery(benchmark::State& state) {
  const int size = state.range(0);

  const std::string query =
      absl::StrCat(std::string(size, '('), "select 1", std::string(size, ')'));
  BM_ParseQuery(query, state);
}
BENCHMARK(BM_ParseParensQuery)->Range(1, 2048);

// Generate expressions like "select 1 from (((((select 1)))))"
// with <size> parentheses.  This demonstrates the bad quadratic performance of
// lookahead on parentheses in FROM clauses (looking for joins).
static void BM_ParseParensJoin(benchmark::State& state) {
  const int size = state.range(0);

  const std::string query =
      absl::StrCat("select 1 from ", std::string(size, '('), "select 1",
                   std::string(size, ')'));
  BM_ParseQuery(query, state);
}
BENCHMARK(BM_ParseParensJoin)->Range(1, 2048);

static void BM_ParseQueryTrivial(benchmark::State& state) {
  BM_ParseQuery("SELECT 1", state);
}
BENCHMARK(BM_ParseQueryTrivial);

static void BM_ParseQueryFromFlag(benchmark::State& state) {
  BM_ParseQuery(absl::GetFlag(FLAGS_parser_benchmark_query), state);
}
BENCHMARK(BM_ParseQueryFromFlag);

static void BM_ParseQueryFromFile(benchmark::State& state) {
  if (absl::GetFlag(FLAGS_parser_benchmark_query_file).empty()) return;
  std::string query;
  GOOGLESQL_QCHECK_OK(googlesql_base::GetContents(absl::GetFlag(FLAGS_parser_benchmark_query_file),
                              &query, ::googlesql_base::Defaults()));
  BM_ParseQuery(query, state);
}
BENCHMARK(BM_ParseQueryFromFile);

}  // namespace googlesql
