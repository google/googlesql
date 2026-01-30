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

#include "googlesql/public/sql_formatter.h"

#include <string>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace googlesql {

using testing::_;
using testing::HasSubstr;
using absl_testing::StatusIs;

namespace {

TEST(SqlFormatterTest, ValidSingleStatement) {
  std::string formatted_sql;

  // Without semicolon.
  GOOGLESQL_ASSERT_OK(FormatSql("select a", &formatted_sql));
  EXPECT_EQ("SELECT\n"
            "  a;",
            formatted_sql);

  // With semicolon and trailing whitespaces.
  GOOGLESQL_ASSERT_OK(FormatSql(" select a ; \t ", &formatted_sql));
  EXPECT_EQ("SELECT\n"
            "  a;",
            formatted_sql);

  // With semicolon and trailing comment.
  GOOGLESQL_ASSERT_OK(FormatSql(" select a ; # foo", &formatted_sql));
  EXPECT_EQ("SELECT\n"
            "  a;",
            formatted_sql);
}

TEST(SqlFormatterTest, InvalidSingleStatement) {
  // SQLs in this test are returning errors, but we still expect that the
  // formatted_sql is always filled in.
  std::string formatted_sql;

  constexpr absl::string_view kExpectedError =
      "Syntax error: Expected \";\" or end of input but got keyword HAVING "
      "[at 1:36]";

  // Without semicolon.
  EXPECT_THAT(FormatSql("select f1 as a from T having a > 5 having a > 5",
                        &formatted_sql),
              StatusIs(_, HasSubstr(kExpectedError)));
  EXPECT_EQ("select f1 as a from T having a > 5 having a > 5;",
            formatted_sql);

  // With semicolon as the last char.
  EXPECT_THAT(FormatSql("select f1 as a from T having a > 5 having a > 5;",
                        &formatted_sql),
              StatusIs(_, HasSubstr(kExpectedError)));
  EXPECT_EQ("select f1 as a from T having a > 5 having a > 5;",
            formatted_sql);

  // With semicolon and trailing whitespaces.
  EXPECT_THAT(FormatSql("select f1 as a from T having a > 5 having a > 5;    ",
                        &formatted_sql),
              StatusIs(_, HasSubstr(kExpectedError)));
  EXPECT_EQ("select f1 as a from T having a > 5 having a > 5;",
            formatted_sql);

  // With semicolon and trailing comment.
  EXPECT_THAT(
      FormatSql("select f1 as a from T having a > 5 having a > 5; # foo",
                &formatted_sql),
      StatusIs(_,
               HasSubstr(
                   "Syntax error: Expected \";\" or end of input but got "
                   "keyword HAVING [at 1:36]\n"
                   "select f1 as a from T having a > 5 having a > 5; # foo\n"
                   "                                   ^\n"
                   "Syntax error: Unexpected end of statement [at 1:55]\n"
                   "select f1 as a from T having a > 5 having a > 5; # foo\n"
                   "                                                      ^")));
  EXPECT_EQ("select f1 as a from T having a > 5 having a > 5;",
            formatted_sql);

  // Empty statement.
  EXPECT_THAT(
      FormatSql(";", &formatted_sql),
      StatusIs(_, HasSubstr("Syntax error: Unexpected \";\" [at 1:1]")));
  EXPECT_EQ(";", formatted_sql);

  // Semicolon in string.
  EXPECT_THAT(FormatSql("select ' ; ' as a as b;", &formatted_sql),
              StatusIs(_, HasSubstr("Syntax error: Expected \";\" or end of "
                                    "input but got keyword AS [at 1:19]")));
  EXPECT_EQ("select ' ; ' as a as b;", formatted_sql);

  EXPECT_THAT(
      FormatSql("select a group by 1 where a < 'xxx;yyy';", &formatted_sql),
      StatusIs(_, HasSubstr("Syntax error: Expected \";\" or end of "
                            "input but got keyword WHERE [at 1:21]")));
  EXPECT_EQ("select a group by 1 where a < 'xxx;yyy';", formatted_sql);
}

TEST(SqlFormatterTest, ValidMultipleStatements) {
  std::string formatted_sql;

  GOOGLESQL_ASSERT_OK(FormatSql(" define table t1 (a=1,b=\"a\",c=1.4,d=true) ; "
                      "select a from t1; ", &formatted_sql));
  EXPECT_EQ("DEFINE TABLE t1(a = 1, b = \"a\", c = 1.4, d = true);\n"
            "SELECT\n"
            "  a\n"
            "FROM\n"
            "  t1;",
            formatted_sql);

  GOOGLESQL_ASSERT_OK(FormatSql("select 1;\n"
                      "select 2", &formatted_sql));
  EXPECT_EQ("SELECT\n"
            "  1;\n"
            "SELECT\n"
            "  2;",
            formatted_sql);
}

TEST(SqlFormatterTest, InvalidMultipleStatements) {
  // SQLs in this test are returning errors, but we still expect that the
  // formatted_sql is always filled in.
  std::string formatted_sql;

  // The second and last statements are formatted, but the other two are passed
  // through as-is since they do not parse successfully.
  EXPECT_THAT(
      FormatSql(
          " drop foo.bar;  define table t1 (a=1,b=\"a\",c=1.4,d=true) ;\n"
          " select sum(f1) as a from T having a > 5 having a > 5;select 1",
          &formatted_sql),
      StatusIs(
          _,
          HasSubstr(
              "foo is not a supported object type [at 1:7]\n"
              " drop foo.bar;  define table t1 (a=1,b=\"a\",c=1.4,d=true) ;\n"
              "      ^\n"
              "Syntax error: Expected \";\" or end of input but got keyword "
              "HAVING [at 2:42]\n"
              " select sum(f1) as a from T having a > 5 having a > 5;select 1\n"
              "                                         ^")));
  EXPECT_EQ("drop foo.bar;\n"
            "DEFINE TABLE t1(a = 1, b = \"a\", c = 1.4, d = true);\n"
            "select sum(f1) as a from T having a > 5 having a > 5;\n"
            "SELECT\n"
            "  1;",
            formatted_sql);

  // The second statement is an invalid empty statement.
  EXPECT_THAT(
      FormatSql("select 1;  ;", &formatted_sql),
      StatusIs(_, HasSubstr("Syntax error: Unexpected \";\" [at 1:12]")));
  EXPECT_EQ("SELECT\n"
            "  1;\n"
            ";",
            formatted_sql);

  // The second statement contains invalid input character '`', which makes
  // GetParseTokens fail. Original sql is returned in this case even if the
  // first statement can be formatted.
  EXPECT_THAT(FormatSql("select 1;  select ` ;", &formatted_sql),
              StatusIs(_, HasSubstr("Unclosed identifier")));
  EXPECT_EQ("select 1;  select ` ;", formatted_sql);
}

TEST(SqlFormatterTest, MissingWhitespaceBetweenLiteralsAndIdentifier) {
  // Floating point literals ending with dot are allowed to be followed by
  // identifiers directly.
  {
    std::string formatted_sql;
    GOOGLESQL_EXPECT_OK(FormatSql("SELECT 1.m", &formatted_sql));
    EXPECT_EQ(formatted_sql, "SELECT\n  1.AS m;");
  }

  std::vector<std::string> test_cases = {
      // Floating point literals starting with dot.
      "SELECT .1e",
      "SELECT .1E10m",
      "SELECT .1E+10m",
      "SELECT .1E-10m",
      // Floating point literals dot in middle.
      "SELECT 1.1e",
      "SELECT 1.1E10m",
      "SELECT 1.1E+10m",
      "SELECT 1.1E-10m",
      // Floating point literals no dot.
      "SELECT 1E10m",
      "SELECT 1E+10m",
      "SELECT 1E-10m",
      // Integer literals.
      "SELECT 1m",
      "SELECT 0x1m",
  };

  for (const auto& test_case : test_cases) {
    std::string formatted_sql;
    EXPECT_THAT(
        FormatSql(test_case, &formatted_sql),
        StatusIs(
            absl::StatusCode::kInvalidArgument,
            HasSubstr(
                "Syntax error: Missing whitespace between literal and alias")));
  }
}

TEST(SqlFormatterTest, FloatingPointLiteral) {
  std::string formatted_sql;
  // Floating point literals that start with dot.
  {
    GOOGLESQL_ASSERT_OK(FormatSql("select .123", &formatted_sql));
    EXPECT_EQ(
        "SELECT\n"
        "  .123;",
        formatted_sql);
  }
  {
    GOOGLESQL_ASSERT_OK(FormatSql("select .123E2", &formatted_sql));
    EXPECT_EQ(
        "SELECT\n"
        "  .123E2;",
        formatted_sql);
  }
  {
    GOOGLESQL_ASSERT_OK(FormatSql("select .123E+2", &formatted_sql));
    EXPECT_EQ(
        "SELECT\n"
        "  .123E+2;",
        formatted_sql);
  }
  {
    GOOGLESQL_ASSERT_OK(FormatSql("select .123E-2", &formatted_sql));
    EXPECT_EQ(
        "SELECT\n"
        "  .123E-2;",
        formatted_sql);
  }
  // Floating point literals that end with dot.
  {
    GOOGLESQL_ASSERT_OK(FormatSql("select 1.", &formatted_sql));
    EXPECT_EQ(
        "SELECT\n"
        "  1.;",
        formatted_sql);
  }
  // Floating point literals that have dot in middle.
  {
    GOOGLESQL_ASSERT_OK(FormatSql("select 1.1", &formatted_sql));
    EXPECT_EQ(
        "SELECT\n"
        "  1.1;",
        formatted_sql);
  }
  {
    GOOGLESQL_ASSERT_OK(FormatSql("select 1.1E2", &formatted_sql));
    EXPECT_EQ(
        "SELECT\n"
        "  1.1E2;",
        formatted_sql);
  }
  {
    GOOGLESQL_ASSERT_OK(FormatSql("select 1.1E+2", &formatted_sql));
    EXPECT_EQ(
        "SELECT\n"
        "  1.1E+2;",
        formatted_sql);
  }
  {
    GOOGLESQL_ASSERT_OK(FormatSql("select 1.1E-2", &formatted_sql));
    EXPECT_EQ(
        "SELECT\n"
        "  1.1E-2;",
        formatted_sql);
  }
  // Floating point literals without dot.
  {
    GOOGLESQL_ASSERT_OK(FormatSql("select 1E10", &formatted_sql));
    EXPECT_EQ(
        "SELECT\n"
        "  1E10;",
        formatted_sql);
  }
  {
    GOOGLESQL_ASSERT_OK(FormatSql("select 1E+10", &formatted_sql));
    EXPECT_EQ(
        "SELECT\n"
        "  1E+10;",
        formatted_sql);
  }
  {
    GOOGLESQL_ASSERT_OK(FormatSql("select 1E-10", &formatted_sql));
    EXPECT_EQ(
        "SELECT\n"
        "  1E-10;",
        formatted_sql);
  }
}

}  // namespace
}  // namespace googlesql
