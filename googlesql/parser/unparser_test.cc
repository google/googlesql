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

#include "googlesql/parser/unparser.h"

#include <memory>
#include <string>
#include <string_view>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/parser/parser.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/string_view.h"

namespace googlesql {

using testing::NotNull;

static void CompareParseTrees(const ASTNode* expected_tree,
                              const ASTNode* unparsed_tree,
                              absl::string_view expected_string,
                              absl::string_view unparsed_string) {
  std::string expected = expected_tree->DebugString();
  std::string from_unparsed = unparsed_tree->DebugString();
  EXPECT_EQ(expected, from_unparsed)
      << "Different trees:\n"
      << "\nfor unparsed vs. original tree.\nOriginal query:\n"
      << expected_string << "\nUnparsed query:\n"
      << unparsed_string << "\nExpected Tree:\n"
      << expected << "\nTree for unparsed sql:\n"
      << from_unparsed;
}

class UnparserQueryTest : public testing::TestWithParam<absl::string_view> {};

INSTANTIATE_TEST_SUITE_P(
    UnparserQueryTestSuite, UnparserQueryTest,
    testing::Values(
        // These testcases must as be formatted as from the unparser
        "SELECT\n"
        "  *\n"
        "FROM\n"
        "  foo\n",
        "SELECT\n"
        "  (0).`0`\n",
        "SELECT\n"
        "  (7).*\n",
        "SELECT\n"
        "  (1.5).* EXCEPT (a)\n",
        "SELECT\n"
        "  (2).* REPLACE (1 AS b)\n"));

TEST_P(UnparserQueryTest, QueryStatementParsesThenUnparses) {
  absl::string_view query_string = GetParam();
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_EXPECT_OK(ParseStatement(query_string, ParserOptions(), &parser_output));
  ASSERT_THAT(parser_output->statement(), NotNull());
  std::string unparsed_string = Unparse(parser_output->node());

  std::unique_ptr<ParserOutput> unparsed_query_parser_output;
  GOOGLESQL_EXPECT_OK(ParseStatement(unparsed_string, ParserOptions(),
                           &unparsed_query_parser_output));
  // This checks that the input and output are effectively the same, or least
  // all tokens have the exact same source locations.
  CompareParseTrees(parser_output->node(), unparsed_query_parser_output->node(),
                    query_string, unparsed_string);
}

TEST(TestUnparser, ExpressionTest) {
  std::string expression_string(
      "CASE\n"
      "  WHEN a = 5 THEN true\n"
      "END\n");
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_EXPECT_OK(
      ParseExpression(expression_string, ParserOptions(), &parser_output));
  ASSERT_THAT(parser_output.get(), NotNull());
  ASSERT_THAT(parser_output->expression(), NotNull());
  std::string unparsed_expression_string = Unparse(parser_output->expression());
  // Cannot generally do string equality because of capitalization and white
  // space issues, so we will reparse and also compare the parse trees.
  EXPECT_EQ(expression_string, unparsed_expression_string);
  std::unique_ptr<ParserOutput> unparsed_expression_parser_output;
  GOOGLESQL_EXPECT_OK(ParseExpression(unparsed_expression_string, ParserOptions(),
                            &unparsed_expression_parser_output));
  CompareParseTrees(parser_output->expression(),
                    unparsed_expression_parser_output->expression(),
                    expression_string, unparsed_expression_string);
}

TEST(TestUnparser, Graph) {
  const std::string fuzzed_create_graph_statement = R"SQL(
CREATE TEMP
  PROPERTY
      GRAPH
MusicGraph NODE    TABLES
 (
 Singers AS Singer OPTIONS (description = "A singer") LABEL
MUSIC_CREATOR PROPERTIES (
  CONCAT(FirstName, " ", LastName) AS name,
     SingerInfo.nationality AS country_origin)
LABEL SINGER
PROPERTIES  (SingerId AS id, CONCAT(FirstName, " ", LastName) AS singer_name,
BirthDate AS birthday), ProductionCompanies AS Company
LABEL MUSIC_CREATOR  PROPERTIES (CompanyName AS name, LocationCountry
AS country_origin) LABEL MUSIC_COMPANY PROPERTIES
(CompanyName AS name, FoundedYear AS
founded_year), Albums AS Album LABEL ALBUM PROPERTIES ALL COLUMNS)
EDGE   TABLES (
Albums AS SINGER_CREATES_ALBUM SOURCE KEY(SingerId) REFERENCES Singer
  DESTINATION KEY(AlbumId)
REFERENCES Album(AlbumId)
LABEL CREATES_MUSIC PROPERTIES
( ReleaseDate AS release_date,
AlbumId AS album_id ),

SingerContracts
AS SINGER_SIGNED_BY_COMPANY SOURCE KEY(SingerId) REFERENCES Singer
DESTINATION KEY(CompanyId)
REFERENCES Company LABEL SIGNED_BY,
  SingerFriends
AS SINGER_HAS_FRIEND
    SOURCE KEY(SingerId)   REFERENCES Singer
    DESTINATION
KEY(FriendId)   REFERENCES Singer
    LABEL KNOWS,

Albums AS COMPANY_PRODUCES_ALBUM KEY(
  CompanyId, SingerId, AlbumId) SOURCE KEY (CompanyId) REFERENCES Company
DESTINATION
KEY(SingerId, AlbumId) REFERENCES Album
    DEFAULT LABEL OPTIONS (description = "Music production") PROPERTIES(ReleaseDate AS release_date, AlbumId AS album_id),
)
)SQL";

  const std::string expected_unparsed_create_graph_statement =
      R"SQL(CREATE TEMP PROPERTY GRAPH MusicGraph
  NODE TABLES(
    Singers AS Singer
      OPTIONS(description = "A singer")
      LABEL MUSIC_CREATOR PROPERTIES(
        CONCAT(FirstName, " ", LastName) AS name,
        SingerInfo.nationality AS country_origin)
      LABEL SINGER PROPERTIES(
        SingerId AS id,
        CONCAT(FirstName, " ", LastName) AS singer_name,
        BirthDate AS birthday),

    ProductionCompanies AS Company
      LABEL MUSIC_CREATOR PROPERTIES(
        CompanyName AS name,
        LocationCountry AS country_origin)
      LABEL MUSIC_COMPANY PROPERTIES(
        CompanyName AS name,
        FoundedYear AS founded_year),

    Albums AS Album
      LABEL ALBUM PROPERTIES ALL COLUMNS
  )
  EDGE TABLES(
    Albums AS SINGER_CREATES_ALBUM
      SOURCE KEY(SingerId) REFERENCES Singer
      DESTINATION KEY(AlbumId) REFERENCES Album(AlbumId)
      LABEL CREATES_MUSIC PROPERTIES(
        ReleaseDate AS release_date,
        AlbumId AS album_id),

    SingerContracts AS SINGER_SIGNED_BY_COMPANY
      SOURCE KEY(SingerId) REFERENCES Singer
      DESTINATION KEY(CompanyId) REFERENCES Company
      LABEL SIGNED_BY PROPERTIES ALL COLUMNS,

    SingerFriends AS SINGER_HAS_FRIEND
      SOURCE KEY(SingerId) REFERENCES Singer
      DESTINATION KEY(FriendId) REFERENCES Singer
      LABEL KNOWS PROPERTIES ALL COLUMNS,

    Albums AS COMPANY_PRODUCES_ALBUM
      KEY(CompanyId, SingerId, AlbumId)
      SOURCE KEY(CompanyId) REFERENCES Company
      DESTINATION KEY(SingerId, AlbumId) REFERENCES Album
      DEFAULT LABEL OPTIONS(description = "Music production") PROPERTIES(
        ReleaseDate AS release_date,
        AlbumId AS album_id)
  )
)SQL";

  std::unique_ptr<googlesql::ParserOutput> output;
  GOOGLESQL_ASSERT_OK(googlesql::ParseStatement(fuzzed_create_graph_statement,
                                      ParserOptions(), &output));
  const std::string result = googlesql::Unparse(output->statement());
  EXPECT_EQ(result, expected_unparsed_create_graph_statement);
}

}  // namespace googlesql
