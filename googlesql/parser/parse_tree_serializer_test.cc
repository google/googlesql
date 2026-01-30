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

#include "googlesql/parser/parse_tree_serializer.h"

#include <iostream>
#include <memory>
#include <string>

#include "googlesql/base/path.h"
#include "googlesql/parser/ast_node.h"
#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/functional/bind_front.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "file_based_test_driver/file_based_test_driver.h"
#include "file_based_test_driver/run_test_case_result.h"
#include "file_based_test_driver/test_case_options.h"

ABSL_FLAG(std::string, test_file, "parse_tree_serializer.test",
          "name of test data file.");

namespace googlesql {
class ParseTreeSerializerTest : public ::testing::Test {
 public:
  ParseTreeSerializerTest() = default;
  void RunTest(absl::string_view test_case_input,
               file_based_test_driver::RunTestCaseResult* test_result) {
    // Read the SQL from the .test file. Parse it and extract the
    // parsed ASTNode, Serialize() the ASTNode and compare the
    // proto to the expected value.
    std::string sql = std::string(test_case_input);
    auto language_options = std::make_unique<LanguageOptions>();
    ParserOptions parser_options = ParserOptions(
        /*id_string_pool=*/nullptr, /*arena=*/nullptr,
        language_options ? *language_options : LanguageOptions());
    std::unique_ptr<ParserOutput> parser_output;
    absl::Status status;
    bool is_expression = absl::StartsWith(sql, "expression: ");
    if (is_expression) {
      status = ParseExpression(sql.substr(12), parser_options, &parser_output);
    } else {
      status = ParseStatement(sql, parser_options, &parser_output);
    }
    ASSERT_TRUE(status.ok());

    ParserOptions deserialize_parser_options =
        ParserOptions(/*id_string_pool=*/nullptr,
                      /*arena=*/nullptr);
    if (is_expression) {
      const googlesql::ASTExpression* expression =
          status.ok() ? parser_output->expression() : nullptr;

      googlesql::AnyASTExpressionProto proto;
      status = ParseTreeSerializer::Serialize(expression, &proto);
      test_result->AddTestOutput(proto.DebugString());

      absl::StatusOr<std::unique_ptr<ParserOutput>> deserialized_parser_output =
          ParseTreeSerializer::Deserialize(proto, deserialize_parser_options);
      GOOGLESQL_EXPECT_OK(deserialized_parser_output);
      const googlesql::ASTExpression* deserialized_expression =
          deserialized_parser_output.value()->expression();

      // This is only a partially reliable test for node equality
      // because not all attributes of a node are necessarily represented in
      // the DebugString. Still need to generate a deep equals() method for
      // better comparison.
      EXPECT_EQ(expression->DebugString(),
                deserialized_expression->DebugString());
      EXPECT_EQ(Unparse(expression), Unparse(deserialized_expression));
    } else {
      const googlesql::ASTStatement* statement =
          status.ok() ? parser_output->statement() : nullptr;

      googlesql::AnyASTStatementProto proto;
      status = ParseTreeSerializer::Serialize(statement, &proto);
      test_result->AddTestOutput(proto.DebugString());

      ParserOptions deserialize_parser_options =
          ParserOptions(/*id_string_pool=*/nullptr,
                        /*arena=*/nullptr, /* language_options=*/nullptr);

      absl::StatusOr<std::unique_ptr<ParserOutput>> deserialized_parser_output =
          ParseTreeSerializer::Deserialize(proto, deserialize_parser_options);
      GOOGLESQL_EXPECT_OK(deserialized_parser_output);
      const googlesql::ASTStatement* deserialized_statement =
          deserialized_parser_output.value()->statement();

      // This is only a partially reliable test for node equality
      // because not all attributes of a node are necessarily represented in
      // the DebugString. Still need to generate a deep equals() method for
      // better comparison.
      EXPECT_EQ(statement->DebugString(),
                deserialized_statement->DebugString());
      EXPECT_EQ(Unparse(statement), Unparse(deserialized_statement));
    }
  }
};

TEST_F(ParseTreeSerializerTest, SerializeStatements) {
  EXPECT_TRUE(file_based_test_driver::RunTestCasesFromFiles(
      absl::GetFlag(FLAGS_test_file),
      absl::bind_front(&ParseTreeSerializerTest::RunTest, this)));
}

}  // namespace googlesql
