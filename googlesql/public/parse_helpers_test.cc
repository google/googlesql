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

#include "googlesql/public/parse_helpers.h"

#include <string>
#include <vector>

#include "googlesql/base/path.h"
#include "googlesql/common/status_payload_utils.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/parse_resume_location.h"
#include "googlesql/public/strings.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "file_based_test_driver/file_based_test_driver.h"
#include "file_based_test_driver/run_test_case_result.h"

using ::testing::ContainerEq;
using ::testing::HasSubstr;
using ::absl_testing::StatusIs;

namespace googlesql {
namespace {

// Test cases that are valid for both IsValidStatementSyntax() and
// IsValidNextStatementSyntax().
struct ValidTestCase {
  // The SQL string to test
  std::string sql;

  // The following are only relevant for IsValidNextStatementSyntax() calls,
  // and indicate the expected return values for <at_end_of_input> and
  // <parse_resume_location>.byte_position().
  bool at_end_of_input;
  int byte_position;

  // Only relevant for GetNextStatementKind() calls.
  ResolvedNodeKind statement_kind;
};

std::vector<ValidTestCase> GetValidTestCases() {
  return {
      {"SELECT * FROM T", true, 15, RESOLVED_QUERY_STMT},
      {"SELECT * FROM T;", true, 16, RESOLVED_QUERY_STMT},

      // whitespace after the ';'
      {"SELECT * FROM T;   ", true, 19, RESOLVED_QUERY_STMT},

      // comments after the ';'
      {"SELECT * FROM T;  -- comments ", true, 30, RESOLVED_QUERY_STMT},
      {"SELECT * FROM T;  -- comments ;", true, 31, RESOLVED_QUERY_STMT},

      {"WITH T AS (SELECT 1) SELECT * FROM T", true, 36, RESOLVED_QUERY_STMT},
      {"CREATE TABLE FOO AS SELECT * FROM T", true, 35,
       RESOLVED_CREATE_TABLE_AS_SELECT_STMT},

      // Tests with newlines
      {"SELECT\n*\nFROM\nT\n -- comment;   ", true, 31, RESOLVED_QUERY_STMT},
      {"SELECT\n*\nFROM\nT\nblah", true, 20, RESOLVED_QUERY_STMT},

      // Test with comments and newline
      {"-- comment\nSELECT * FROM T; --comment\n--comment\n--comment", true, 57,
       RESOLVED_QUERY_STMT},
  };
}

// Test cases that are invalid for both IsValidStatementSyntax() and
// IsValidNextStatementSyntax().
struct ErrorTestCase {
  std::string sql;
  std::string expected_error_substring;
  ResolvedNodeKind statement_kind;
};

std::vector<ErrorTestCase> GetInvalidSyntaxTestCases() {
  return {
      // Normal Syntax errors.
      {"", "Syntax error: Unexpected end of statement", RESOLVED_LITERAL},
      {";", "Syntax error", RESOLVED_LITERAL},
      {"-- bah bah bah COMMENTS!", "Syntax error", RESOLVED_LITERAL},
      {"-- bah bah bah COMMENTS!;", "Syntax error", RESOLVED_LITERAL},
      {"blah", "Syntax error", RESOLVED_LITERAL},
      {"SELECT * FROM T ORDER BY bar with crap on the end", "Syntax error",
       RESOLVED_QUERY_STMT},
      {"SELECT\n*\nFROM\nT\nORDER\nBY\nT.foo\nblah", "Syntax error",
       RESOLVED_QUERY_STMT},
      // An error produced by the parser that is not a 'syntax error'.
      {"SELECT x FROM UNNEST(SELECT y+z) AS k WITH OFFSET pos",
       "The argument to UNNEST is an expression", RESOLVED_QUERY_STMT},
      // parse_helpers APIs do not support script statements.
      {"IF TRUE THEN select 3; end if", "Syntax error", RESOLVED_LITERAL},
  };
}

// Test cases that valid for IsValidNextStatementSyntax(), but invalid
// for IsValidStatementSyntax().  The difference is that
// IsValidStatementSyntax() produces an error if there is trailing garbage
// after the semicolon that is not a comment or whitespace.  In contrast,
// IsValidNextStatementSyntax does not produce an error, but rather returns
// that the next statement has valid syntax and resets the ParseResumeLocation
// to point at the trailing garbage.
//
// For simplicity, all of these test cases are "Syntax error" test cases
// for IsValidStatementSyntax(), and all of these test cases return
// <at_end_of_input> false for IsValidNextStatementSyntax().
struct OtherTestCase {
  // The SQL string to test
  std::string sql;

  // The <byte_position> is only relevant for IsValidNextStatementSyntax()
  // calls, and indicates the expected return value for
  // <parse_resume_location>.byte_position().
  int byte_position;

  // The type of the first statement in the query string.  Only relevant
  // for tests which call GetNextStatementKind().
  ResolvedNodeKind statement_kind;
};

std::vector<OtherTestCase> GetOtherTestCases() {
  return {
      {"SELECT * FROM T;  -- comments \n;", 16, RESOLVED_QUERY_STMT},
      {"SELECT * FROM T;  -- comments\n SELECT * FROM T;", 16,
       RESOLVED_QUERY_STMT},
      {"SELECT * FROM T;  SELECT * FROM T", 16, RESOLVED_QUERY_STMT},
      {"SELECT * FROM T;  SELECT * FROM T;", 16, RESOLVED_QUERY_STMT},
      {"SELECT * FROM T;  WITH crap on the end ", 16, RESOLVED_QUERY_STMT},
      {"SELECT * FROM T;  more crap ", 16, RESOLVED_QUERY_STMT},

      // Test with comments and newline
      {"--- comment\nSELECT * FROM T; blah", 28, RESOLVED_QUERY_STMT},
      {"--- comment\nSELECT * FROM T; --comment\nblah", 28,
       RESOLVED_QUERY_STMT},
  };
}

TEST(IsValidStatementSyntaxTest, BasicStatements) {
  for (const ValidTestCase& valid_test_case : GetValidTestCases()) {
    GOOGLESQL_EXPECT_OK(
        IsValidStatementSyntax(valid_test_case.sql, ERROR_MESSAGE_WITH_PAYLOAD))
        << valid_test_case.sql;
  }
  for (const ErrorTestCase& invalid_test_case : GetInvalidSyntaxTestCases()) {
    const absl::Status status = IsValidStatementSyntax(
        invalid_test_case.sql, ERROR_MESSAGE_WITH_PAYLOAD);
    EXPECT_THAT(
        status,
        StatusIs(absl::StatusCode::kInvalidArgument,
                 HasSubstr(invalid_test_case.expected_error_substring)));
  }
  for (const OtherTestCase& other_test_case : GetOtherTestCases()) {
    const absl::Status status =
        IsValidStatementSyntax(other_test_case.sql, ERROR_MESSAGE_WITH_PAYLOAD);
    EXPECT_THAT(status, StatusIs(absl::StatusCode::kInvalidArgument,
                                 HasSubstr("Syntax error")));
  }

  // Test that we get an error location payload from the error Status.
  absl::Status status = IsValidStatementSyntax(
      "SELECT * FROM oops I did it again", ERROR_MESSAGE_WITH_PAYLOAD);
  // The syntax error location is at the 'did', since 'oops' is interpreted
  // as a table name and 'I' is interpreted as the alias.
  EXPECT_EQ(
      internal::StatusToString(status),
      "generic::invalid_argument: Syntax error: Expected end of input but "
      "got identifier \"did\" [googlesql.ErrorLocation] { line: 1 column: 22 "
      "input_start_line_offset: 0 input_start_column_offset: 0 }");

  status = IsValidStatementSyntax("SELECT * FROM oops I did it again",
                                  ERROR_MESSAGE_ONE_LINE);
  EXPECT_EQ(
      internal::StatusToString(status),
      "generic::invalid_argument: Syntax error: Expected end of input but "
      "got identifier "
      "\"did\" [at 1:22]");

  status = IsValidStatementSyntax("SELECT * FROM oops I did it again",
                                  ERROR_MESSAGE_MULTI_LINE_WITH_CARET);
  EXPECT_EQ(
      internal::StatusToString(status),
      "generic::invalid_argument: Syntax error: Expected end of input but "
      "got identifier "
      "\"did\" [at 1:22]\n"
      "SELECT * FROM oops I did it again\n"
      "                     ^");
}

TEST(IsValidNextStatementSyntaxTest, BasicStatements) {
  for (const ValidTestCase& valid_test_case : GetValidTestCases()) {
    ParseResumeLocation parse_resume_location =
        ParseResumeLocation::FromString(valid_test_case.sql);
    bool at_end_of_input;
    GOOGLESQL_EXPECT_OK(IsValidNextStatementSyntax(
        &parse_resume_location, ERROR_MESSAGE_WITH_PAYLOAD, &at_end_of_input))
        << valid_test_case.sql;
    // Validate that the updated ParseResumeLocation byte location is what
    // we expect.
    EXPECT_EQ(parse_resume_location.byte_position(),
              valid_test_case.byte_position)
        << valid_test_case.sql;
    EXPECT_EQ(at_end_of_input, valid_test_case.at_end_of_input)
        << valid_test_case.sql;
  }
  for (const ErrorTestCase& invalid_test_case : GetInvalidSyntaxTestCases()) {
    ParseResumeLocation parse_resume_location =
        ParseResumeLocation::FromString(invalid_test_case.sql);
    bool at_end_of_input;
    const absl::Status status = IsValidNextStatementSyntax(
        &parse_resume_location, ERROR_MESSAGE_WITH_PAYLOAD, &at_end_of_input);
    EXPECT_THAT(status,
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr(invalid_test_case.expected_error_substring)))
        << invalid_test_case.sql;
  }
  for (const OtherTestCase& other_test_case : GetOtherTestCases()) {
    ParseResumeLocation parse_resume_location =
        ParseResumeLocation::FromString(other_test_case.sql);
    bool at_end_of_input;
    GOOGLESQL_EXPECT_OK(IsValidNextStatementSyntax(
        &parse_resume_location, ERROR_MESSAGE_WITH_PAYLOAD, &at_end_of_input))
        << other_test_case.sql;
    // Validate that the updated ParseResumeLocation byte location is what
    // we expect.
    EXPECT_EQ(parse_resume_location.byte_position(),
              other_test_case.byte_position)
        << other_test_case.sql;
    EXPECT_FALSE(at_end_of_input) << other_test_case.sql;
  }

  // Test that we get an error location payload from the error Status.
  ParseResumeLocation parse_resume_location =
      ParseResumeLocation::FromString("SELECT * FROM oops I did it again");
  bool at_end_of_input;
  absl::Status status = IsValidNextStatementSyntax(
      &parse_resume_location, ERROR_MESSAGE_WITH_PAYLOAD, &at_end_of_input);
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kInvalidArgument,
                               HasSubstr("Syntax error")));
  EXPECT_THAT(
      internal::StatusToString(status),
      HasSubstr("[googlesql.ErrorLocation] { line: 1 column: 22 "
                "input_start_line_offset: 0 input_start_column_offset: 0 }"));

  // Test where the ParseResumeLocation is in the middle of a string.
  parse_resume_location = ParseResumeLocation::FromString(
      "some invalid stuff... SELECT * FROM T;  some more stuff");
  parse_resume_location.set_byte_position(22);
  status = IsValidNextStatementSyntax(
      &parse_resume_location, ERROR_MESSAGE_WITH_PAYLOAD, &at_end_of_input);
  GOOGLESQL_EXPECT_OK(status);
  EXPECT_EQ(parse_resume_location.byte_position(), 38);
  EXPECT_FALSE(at_end_of_input);
}

TEST(IsValidNextStatementSyntaxTest, MultiStatementsTest) {
  // Test that loops through all the statements in a multi-statement string
  // to check syntax.
  ParseResumeLocation parse_resume_location = ParseResumeLocation::FromString(
      "SELECT * FROM T; SELECT * FROM U; SELECT * FROM V; CREATE TABLE T AS \n"
      "SELECT * FROM T1;  SELECT * FROM T");
  bool at_end_of_input = false;
  int statement_count = 0;
  while (!at_end_of_input) {
    GOOGLESQL_EXPECT_OK(IsValidNextStatementSyntax(
        &parse_resume_location, ERROR_MESSAGE_WITH_PAYLOAD, &at_end_of_input));
    statement_count++;
  }
  EXPECT_EQ(statement_count, 5);

  // Similar to the previous test, but the third statement has a syntax error.
  parse_resume_location = ParseResumeLocation::FromString(
      "SELECT * FROM T; SELECT * FROM U; SELECTS FROMM V; CREATE TABLE T AS \n"
      "SELECT * FROM T1;  SELECT * FROM T");
  at_end_of_input = false;
  statement_count = 0;
  absl::Status status;
  while (status.ok() && !at_end_of_input) {
    status = IsValidNextStatementSyntax(&parse_resume_location,
                                        ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
                                        &at_end_of_input);
    statement_count++;
  }
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(internal::StatusToString(status), HasSubstr("[at 1:35]\nSELECT"))
      << status;
  EXPECT_EQ(statement_count, 3);
}

TEST(GetNextStatementKindAndPropertiesTest, BasicStatements) {
  for (const ValidTestCase& valid_test_case : GetValidTestCases()) {
    ParseResumeLocation parse_resume_location =
        ParseResumeLocation::FromString(valid_test_case.sql);
    ResolvedNodeKind kind = GetNextStatementKind(parse_resume_location);
    EXPECT_EQ(ResolvedNodeKind_Name(kind),
              ResolvedNodeKind_Name(valid_test_case.statement_kind))
        << valid_test_case.sql;
    StatementProperties statement_properties;
    GOOGLESQL_ASSERT_OK(GetNextStatementProperties(
        parse_resume_location, LanguageOptions(), &statement_properties))
        << valid_test_case.sql;
    ASSERT_EQ(ResolvedNodeKind_Name(kind),
              ResolvedNodeKind_Name(statement_properties.node_kind))
        << valid_test_case.sql;
  }
  for (const ErrorTestCase& invalid_test_case : GetInvalidSyntaxTestCases()) {
    ParseResumeLocation parse_resume_location =
        ParseResumeLocation::FromString(invalid_test_case.sql);
    ResolvedNodeKind kind = GetNextStatementKind(parse_resume_location);
    EXPECT_EQ(ResolvedNodeKind_Name(kind),
              ResolvedNodeKind_Name(invalid_test_case.statement_kind))
        << invalid_test_case.sql;
    StatementProperties statement_properties;
    GOOGLESQL_ASSERT_OK(GetNextStatementProperties(
        parse_resume_location, LanguageOptions(), &statement_properties))
        << invalid_test_case.sql;
    ASSERT_EQ(ResolvedNodeKind_Name(kind),
              ResolvedNodeKind_Name(statement_properties.node_kind))
        << invalid_test_case.sql;
  }
  for (const OtherTestCase& other_test_case : GetOtherTestCases()) {
    ParseResumeLocation parse_resume_location =
        ParseResumeLocation::FromString(other_test_case.sql);
    ResolvedNodeKind kind = GetNextStatementKind(parse_resume_location);
    EXPECT_EQ(ResolvedNodeKind_Name(kind),
              ResolvedNodeKind_Name(other_test_case.statement_kind))
        << other_test_case.sql;
    StatementProperties statement_properties;
    GOOGLESQL_ASSERT_OK(GetNextStatementProperties(
        parse_resume_location, LanguageOptions(), &statement_properties))
        << other_test_case.sql;
    ASSERT_EQ(ResolvedNodeKind_Name(kind),
              ResolvedNodeKind_Name(statement_properties.node_kind))
        << other_test_case.sql;
  }
}

struct StatementPropertiesTestCase {
  // The SQL string to test
  std::string sql;

  // The expected properties of <sql>.
  ResolvedNodeKind statement_kind;  // The statement's kind
  StatementProperties::StatementCategory statement_category;  // DDL, DML, etc.
  bool is_create_temp_object;  // CREATE TEMP TABLE, etc.
  absl::btree_map<std::string, std::string> hint_map;  // Statement level hints.
};

std::vector<StatementPropertiesTestCase> GetStatementPropertiesTestCases() {
  return {
      {
          "SELECT * FROM T",
          RESOLVED_QUERY_STMT,
          StatementProperties::SELECT,
          false,
          {},
      },
      {
          "CREATE TABLE T AS SELECT 1",
          RESOLVED_CREATE_TABLE_AS_SELECT_STMT,
          StatementProperties::DDL,
          false,
          {},
      },
      {
          "CREATE TEMP TABLE T (A INT64);",
          RESOLVED_CREATE_TABLE_STMT,
          StatementProperties::DDL,
          true,
          {},
      },
      {
          "CREATE LOCALITY GROUP foo",
          RESOLVED_LITERAL,
          StatementProperties::DDL,
          false,
          {},
      },
      {
          "INSERT INTO FOO VALUES (1,2,3)",
          RESOLVED_INSERT_STMT,
          StatementProperties::DML,
          false,
          {},
      },
      {
          "EXPORT DATA OPTIONS",
          RESOLVED_EXPORT_DATA_STMT,
          StatementProperties::OTHER,
          false,
          {},
      },
      {
          "@{a = 4, b = 1 +2} SELECT * FROM T",
          RESOLVED_QUERY_STMT,
          StatementProperties::SELECT,
          false,
          {{"a", "4"}, {"b", "1 +2"}},
      },
      {
          "@{b = 5, a = 1 +2} SELECT * FROM T",
          RESOLVED_QUERY_STMT,
          StatementProperties::SELECT,
          false,
          {{"a", "1 +2"}, {"b", "5"}},
      },
      {
          "@2 SELECT * FROM T",
          RESOLVED_QUERY_STMT,
          StatementProperties::SELECT,
          false,
          {},
      },
      {
          "@{b = 9} CREATE TABLE T AS SELECT 1",
          RESOLVED_CREATE_TABLE_AS_SELECT_STMT,
          StatementProperties::DDL,
          false,
          {{"b", "9"}},
      },
      {
          "  /**/ @{b = 9} /**/ CREATE TABLE T AS SELECT 1",
          RESOLVED_CREATE_TABLE_AS_SELECT_STMT,
          StatementProperties::DDL,
          false,
          {{"b", "9"}},
      },
      {"RETURN 'foo'", RESOLVED_LITERAL, StatementProperties::OTHER, false, {}},
      {R"(IF TRUE THEN
            SELECT 5;
          END IF;)",
       RESOLVED_LITERAL,
       StatementProperties::OTHER,
       false,
       {}},
      {R"(LOOP
          END LOOP;)",
       RESOLVED_LITERAL,
       StatementProperties::OTHER,
       false,
       {}},
      {R"(SET (a, b) = (1, 2))",
       RESOLVED_LITERAL,
       StatementProperties::OTHER,
       false,
       {}},
      {R"(L1: REPEAT UNTIL TRUE
          END REPEAT;)",
       RESOLVED_LITERAL,
       StatementProperties::OTHER,
       false,
       {}},
      {R"(CASE
         WHEN TRUE THEN
         END CASE;)",
       RESOLVED_LITERAL,
       StatementProperties::OTHER,
       false,
       {}}};
}

TEST(GetNextStatementPropertiesTest, BasicStatements) {
  for (const StatementPropertiesTestCase& test_case :
       GetStatementPropertiesTestCases()) {
    ParseResumeLocation parse_resume_location =
        ParseResumeLocation::FromString(test_case.sql);

    StatementProperties statement_properties;
    GOOGLESQL_ASSERT_OK(GetNextStatementProperties(
        parse_resume_location, LanguageOptions(), &statement_properties))
        << test_case.sql;

    EXPECT_EQ(ResolvedNodeKind_Name(test_case.statement_kind),
              ResolvedNodeKind_Name(statement_properties.node_kind))
        << test_case.sql;
    EXPECT_EQ(test_case.statement_category,
              statement_properties.statement_category)
        << test_case.sql;
    EXPECT_EQ(test_case.is_create_temp_object,
              statement_properties.is_create_temporary_object)
        << test_case.sql;

    // Create an ordered map for comparison.
    absl::btree_map<std::string, std::string> statement_properties_hint_map(
        statement_properties.statement_level_hints.begin(),
        statement_properties.statement_level_hints.end());

    EXPECT_EQ(test_case.hint_map.size(), statement_properties_hint_map.size())
        << test_case.sql;

    std::string expected_hints;
    for (const auto& map_entry : test_case.hint_map) {
      absl::StrAppend(&expected_hints, map_entry.first, "=", map_entry.second,
                      ";");
    }
    std::string fetched_hints;
    for (const auto& map_entry : statement_properties_hint_map) {
      absl::StrAppend(&fetched_hints, map_entry.first, "=", map_entry.second,
                      ";");
    }
    EXPECT_EQ(expected_hints, fetched_hints) << test_case.sql;

    // Ensure that GetNextStatementKind returns the same kind.
    ResolvedNodeKind kind = GetNextStatementKind(parse_resume_location);
    EXPECT_EQ(ResolvedNodeKind_Name(kind),
              ResolvedNodeKind_Name(statement_properties.node_kind))
        << test_case.sql;
  }
}

// Tests the fix for the regression in b/266192857
TEST(GetNextStatementKindTest, DefineMacroStmt) {
  // We do not yet have ResolvedDefineMacroStatemment
  ParseResumeLocation parse_resume_location =
      ParseResumeLocation::FromString("DEFINE MACRO m 1");

  LanguageOptions language_options;
  StatementProperties statement_properties;
  GOOGLESQL_EXPECT_OK(GetNextStatementProperties(parse_resume_location, language_options,
                                       &statement_properties));
}

struct GetTopLevelTableNameFromDDLStatementTestCase {
  // The SQL string to test
  std::string sql;

  // The expected top level table name to be extracted from <sql> as an
  // identifier path string. This is only set if the test case is expected to
  // succeed.
  std::string table_name;

  // A substring of the expected error message. This is only set if the test
  // case is expected to fail.
  std::string error_message;
};

TEST(GetTopLevelTableNameFromDDLStatementTest, BasicStatements) {
  // Success test cases
  std::vector<GetTopLevelTableNameFromDDLStatementTestCase> success_test_cases =
      {
          {"CREATE TABLE apple AS SELECT 1", "apple", ""},
          {"CREATE TEMP TABLE BANANA (A INT64);", "BANANA", ""},
          {"CREATE TABLE pear AS SELECT 1", "pear", ""},
          {"CREATE TEMP TABLE Mango.Peach (A INT64);", "Mango.Peach", ""},
          {"CREATE TABLE `🍝`.foo.bar AS SELECT 1;", "`🍝`.foo.bar", ""},
      };
  for (const GetTopLevelTableNameFromDDLStatementTestCase& test_case :
       success_test_cases) {
    ParseResumeLocation parse_resume_location =
        ParseResumeLocation::FromString(test_case.sql);
    GOOGLESQL_ASSERT_OK_AND_ASSIGN(
        std::vector<std::string> table_name,
        GetTopLevelTableNameFromDDLStatement(test_case.sql, LanguageOptions()));
    EXPECT_EQ(IdentifierPathToString(table_name), test_case.table_name);
  }

  // Failure test cases
  std::vector<GetTopLevelTableNameFromDDLStatementTestCase> failure_test_cases =
      {
          {"CREATE TEMP FUNCTION foo() RETURNS INT64 AS (1);", "",
           "Unsupported AST node type"},
          {"SELECT * FROM FRUITS;", "", "Unsupported AST node type"},
          {"CREATE TABLE CREATE;", "", "Syntax error"},
          {"CREATE TABLE my.table.path AS SELECT SELECT SELECT", "",
           "Syntax error"},
      };
  for (const GetTopLevelTableNameFromDDLStatementTestCase& test_case :
       failure_test_cases) {
    EXPECT_THAT(
        GetTopLevelTableNameFromDDLStatement(test_case.sql, LanguageOptions())
            .status()
            .message(),
        HasSubstr(test_case.error_message));
  }
}

TEST(GetTopLevelTableNameFromDDLStatementTest, MultiStatementsTest) {
  {
    // Test that loops through all the statements in a multi-statement string
    // to collect all the top level DDL table names.
    LanguageOptions language_options;
    std::string sql =
        "CREATE TABLE Foo AS (SELECT 1 AS number UNION ALL SELECT 2 AS "
        "number);CREATE TABLE Bar AS (SELECT number * 2 AS doubled_number FROM "
        "Foo);CREATE TABLE Baz AS (SELECT Foo.number, Bar.doubled_number, "
        "Foo.number * Bar.doubled_number AS product FROM Foo INNER JOIN Bar);";
    ParseResumeLocation parse_resume_location =
        ParseResumeLocation::FromString(sql);
    bool at_end_of_input = false;
    int statement_count = 0;
    std::vector<std::string> table_names;
    while (!at_end_of_input) {
      GOOGLESQL_ASSERT_OK_AND_ASSIGN(
          std::vector<std::string> table_name,
          GetTopLevelTableNameFromNextDDLStatement(
              sql, parse_resume_location, &at_end_of_input, language_options));
      table_names.push_back(IdentifierPathToString(table_name));
      statement_count++;
    }
    EXPECT_EQ(statement_count, 3);
    EXPECT_THAT(table_names,
                ContainerEq(std::vector<std::string>({"Foo", "Bar", "Baz"})));
  }
  {
    // Error: Only CREATE TABLE is supported, but 2nd statement is a query.
    LanguageOptions language_options;
    std::string sql =
        "CREATE TABLE Foo AS (SELECT 1 AS number UNION ALL SELECT 2 AS "
        "number);SELECT * FROM Foo;CREATE TABLE Baz AS (SELECT Foo.number, "
        "Bar.doubled_number, "
        "Foo.number * Bar.doubled_number AS product FROM Foo INNER JOIN Bar);";
    ParseResumeLocation parse_resume_location =
        ParseResumeLocation::FromString(sql);
    bool at_end_of_input = false;
    int statement_count = 0;
    std::vector<std::string> table_names;
    absl::StatusOr<std::vector<std::string>> table_name_or_status;
    while (!at_end_of_input) {
      table_name_or_status = GetTopLevelTableNameFromNextDDLStatement(
          sql, parse_resume_location, &at_end_of_input, language_options);
      if (table_name_or_status.ok()) {
        table_names.push_back(
            IdentifierPathToString(table_name_or_status.value()));
        statement_count++;
      } else {
        at_end_of_input = true;
      }
    }
    EXPECT_EQ(statement_count, 1);
    EXPECT_THAT(table_names, ContainerEq(std::vector<std::string>({"Foo"})));
    EXPECT_FALSE(table_name_or_status.ok());
    EXPECT_THAT(table_name_or_status.status(),
                StatusIs(absl::StatusCode::kUnimplemented,
                         HasSubstr("Unsupported AST node type")));
  }
  {
    // Error: Final statement has a syntax error.
    LanguageOptions language_options;
    std::string sql =
        "CREATE TABLE Foo AS (SELECT 1 AS number UNION ALL SELECT 2 AS "
        "number);CREATE TABLE Bar AS (SELECT number * 2 AS doubled_number FROM "
        "Foo);CREATE TABLE CREATE TABLE CREATE TABLE";
    ParseResumeLocation parse_resume_location =
        ParseResumeLocation::FromString(sql);
    bool at_end_of_input = false;
    int statement_count = 0;
    std::vector<std::string> table_names;
    absl::StatusOr<std::vector<std::string>> table_name_or_status;
    while (!at_end_of_input) {
      table_name_or_status = GetTopLevelTableNameFromNextDDLStatement(
          sql, parse_resume_location, &at_end_of_input, language_options);
      if (table_name_or_status.ok()) {
        table_names.push_back(
            IdentifierPathToString(table_name_or_status.value()));
        statement_count++;
      } else {
        at_end_of_input = true;
      }
    }
    EXPECT_EQ(statement_count, 2);
    EXPECT_THAT(table_names,
                ContainerEq(std::vector<std::string>({"Foo", "Bar"})));
    EXPECT_FALSE(table_name_or_status.ok());
    EXPECT_THAT(table_name_or_status.status(),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("Syntax error")));
  }
}

struct SkipNextStatementTestCase {
  std::string test_case;
  std::vector<std::string> stmts;
  bool expect_to_fail = false;
  bool expect_at_eoi = true;
  // Expected byte position after SkipNextStatement. If -1, defaults to
  // sql.size() if expect_at_eoi is true, or stmts[0].size() otherwise.
  int expected_byte_position = -1;
};

class SkipNextStatementTest
    : public ::testing::TestWithParam<SkipNextStatementTestCase> {};

TEST_P(SkipNextStatementTest, SingleValidStatement) {
  const std::string sql = absl::StrJoin(GetParam().stmts, "");
  ParseResumeLocation resume = ParseResumeLocation::FromStringView(sql);
  EXPECT_EQ(resume.byte_position(), 0);

  bool at_eoi;
  if (GetParam().expect_to_fail) {
    EXPECT_THAT(SkipNextStatement(&resume, &at_eoi),
                StatusIs(absl::StatusCode::kInvalidArgument));
  } else {
    GOOGLESQL_ASSERT_OK(SkipNextStatement(&resume, &at_eoi));
    EXPECT_EQ(at_eoi, GetParam().expect_at_eoi) << GetParam().test_case;
    int expected_pos = GetParam().expected_byte_position;
    if (expected_pos == -1) {
      expected_pos = GetParam().expect_at_eoi
                         ? static_cast<int>(sql.size())
                         : static_cast<int>(GetParam().stmts[0].size());
    }
    EXPECT_EQ(resume.byte_position(), expected_pos) << GetParam().test_case;
  }
}

INSTANTIATE_TEST_SUITE_P(
    SkipNextStatementTestParams, SkipNextStatementTest,
    ::testing::Values(
        SkipNextStatementTestCase{"single_statement_with_semicolon",
                                  {"SELECT 1, 2, 3;"}},
        SkipNextStatementTestCase{"single_statement_no_semicolon",
                                  {"SELECT 1, 2, 3"}},
        SkipNextStatementTestCase{"single_statement_with_whitespace_at_end",
                                  {"SELECT 1, 2, 3;   "}},
        SkipNextStatementTestCase{"single_statement_with_double_dash_comment",
                                  {"SELECT 1; -- :)"}},
        SkipNextStatementTestCase{"single_statement_with_pound_comment",
                                  {"SELECT 1 # ####"}},
        SkipNextStatementTestCase{"multiple_valid_statements",
                                  {"SELECT 1, 2, 34;", " /*hello*/ SELECT 2;"},
                                  /*expect_to_fail=*/false,
                                  /*expect_at_eoi=*/false},
        SkipNextStatementTestCase{
            "multiple_valid_statements_with_inner_comment",
            {"SELECT /* ? */1;", " SET @a = 2;"},
            /*expect_to_fail=*/false,
            /*expect_at_eoi=*/false},
        SkipNextStatementTestCase{
            "single_invalid_tokenizable_statement_with_semicolon",
            {"SELECT FROM ??;"}},
        SkipNextStatementTestCase{
            "single_invalid_tokenizable_statement_no_semicolon",
            {"alkdjlas1!!!asf"}},
        SkipNextStatementTestCase{
            "single_invalid_tokenizable_statement_with_comment_at_end",
            {"123456789012; #"},
            /*expect_to_fail=*/false,
            /*expect_at_eoi=*/true},
        SkipNextStatementTestCase{"first_statement_invalid",
                                  {"129381092380198;", " SHOW TABLES"},
                                  /*expect_to_fail=*/false,
                                  /*expect_at_eoi=*/false},
        SkipNextStatementTestCase{"second_statement_invalid",
                                  {"SELECT 1, 2, 34;", " 371983792173912831;"},
                                  /*expect_to_fail=*/false,
                                  /*expect_at_eoi=*/false},
        SkipNextStatementTestCase{"first_statement_has_macro_call",
                                  {"SELECT $FOO(12);", " HELLO WORLD"},
                                  /*expect_to_fail=*/false,
                                  /*expect_at_eoi=*/false},
        SkipNextStatementTestCase{"valid_with_internal_semicolons",
                                  {"SELECT ';' /*;*/;", " SELECT 1, 2, 3;"},
                                  /*expect_to_fail=*/false,
                                  /*expect_at_eoi=*/false},
        SkipNextStatementTestCase{"invalid_with_internal_semicolons",
                                  {"HELLO OUT THERE!;", ";"},
                                  /*expect_to_fail=*/false,
                                  /*expect_at_eoi=*/false},
        SkipNextStatementTestCase{"semicolons_inside_comment",
                                  {"/*;;;;;;;;;;;;*/;", " ?"},
                                  /*expect_to_fail=*/false,
                                  /*expect_at_eoi=*/false},
        SkipNextStatementTestCase{"unclosed_quote",
                                  {"SELECT '"},
                                  /*expect_to_fail=*/true},
        SkipNextStatementTestCase{"unclosed_block_comment",
                                  {"SELECT 1, 2 /* ..."},
                                  /*expect_to_fail=*/true},
        SkipNextStatementTestCase{"invalid_character",
                                  {"SELECT 🍝"},
                                  /*expect_to_fail=*/true},
        SkipNextStatementTestCase{"procedural_statement_with_semicolon_inside",
                                  {"BEGIN SELECT 1; END;"},
                                  /*expect_to_fail=*/false,
                                  /*expect_at_eoi=*/false,
                                  /*expected_byte_position=*/15}));

static void RunFileBasedTest(
    absl::string_view test_case_input,
    file_based_test_driver::RunTestCaseResult* test_result) {
  LanguageOptions language_options;
  language_options.EnableMaximumLanguageFeatures();

  absl::StatusOr<std::vector<absl::string_view>> result =
      ListSelectColumnExpressionsFromFinalSelectClause(test_case_input,
                                                       language_options);

  if (result.ok()) {
    std::vector<std::string> indexed_expressions;
    indexed_expressions.reserve(result->size());
    for (int i = 0; i < result->size(); ++i) {
      indexed_expressions.push_back(absl::StrCat(i, ": ", (*result)[i]));
    }
    test_result->AddTestOutput(absl::StrJoin(indexed_expressions, "\n"));
  } else {
    // Format the error message to match the expected output format.
    test_result->AddTestOutput(
        absl::StrCat(absl::StatusCodeToString(result.status().code()), ": ",
                     result.status().message()));
  }
}

TEST(ListSelectColumnExpressionsFromFinalSelectClauseFileBased, FileBasedTest) {
  const std::string filename = googlesql_base::JoinPath(
      ::testing::SrcDir(),
      "_main/googlesql/public/list_select_expressions.test");

  EXPECT_TRUE(file_based_test_driver::RunTestCasesFromFiles(filename,
                                                            &RunFileBasedTest));
}

TEST(ParseHelpersTest, ListReferencedFunctions) {
  const absl::string_view sql =
      "SELECT "
      "  foo(t.a), "
      "  bar.baz(1, t.b), "
      "  FN.QUX(2, t.c), "
      "  FLATTEN(x), "
      "  Z.FLATTEN(x), "
      "  FILTER_FIELDS(y, z), "
      "  ('a').CHAINED_FOO().(CHAINED_BAR.CHAINED_BAZ)() "
      "FROM t";
  LanguageOptions language_options;
  absl::StatusOr<absl::flat_hash_set<std::vector<std::string>>>
      referenced_functions =
          ListReferencedFunctionsInStatement(sql, language_options);
  GOOGLESQL_ASSERT_OK(referenced_functions);
  EXPECT_THAT(*referenced_functions,
              testing::UnorderedElementsAre(
                  std::vector<std::string>{"foo"},
                  std::vector<std::string>{"bar", "baz"},
                  std::vector<std::string>{"fn", "qux"},
                  std::vector<std::string>{"z", "flatten"},
                  std::vector<std::string>{"chained_foo"},
                  std::vector<std::string>{"chained_bar", "chained_baz"}));
}
}  // namespace
}  // namespace googlesql
