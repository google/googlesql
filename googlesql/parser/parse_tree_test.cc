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

#include "googlesql/parser/parse_tree.h"

#include <any>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "googlesql/common/errors.h"
#include "googlesql/common/status_payload_utils.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/parser/ast_node_kind.h"
#include "googlesql/parser/parse_tree_visitor.h"
#include "googlesql/parser/parser.h"
#include "googlesql/proto/internal_error_location.pb.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/error_location.pb.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "googlesql/base/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status_builder.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace testing {
namespace {

TEST(StatusWithInternalErrorLocation, ByteOffset) {
  const ParseLocationPoint point =
      ParseLocationPoint::FromByteOffset("filename_7", 7);

  const absl::Status status = StatusWithInternalErrorLocation(
      ::googlesql_base::InternalErrorBuilder() << "Foo bar baz", point);
  const InternalErrorLocation location =
      internal::GetPayload<InternalErrorLocation>(status);
  EXPECT_EQ(7, location.byte_offset());
  EXPECT_EQ("filename_7", location.filename());
}

TEST(ConvertInternalErrorLocationToExternal, ByteOffset) {
  // A query that has the byte offset referenced in the error location.
  const std::string dummy_query =
      "1234567890123456789\n"
      "1234567890123456789\n";

  const ParseLocationPoint point =
      ParseLocationPoint::FromByteOffset("filename_23", 23);

  const absl::Status status = StatusWithInternalErrorLocation(
      ::googlesql_base::InternalErrorBuilder() << "Foo bar baz", point);
  const absl::Status status2 =
      ConvertInternalErrorPayloadsToExternal(status, dummy_query);
  EXPECT_FALSE(internal::HasPayloadWithType<InternalErrorLocation>(status2));
  const ErrorLocation external_location =
      internal::GetPayload<ErrorLocation>(status2);
  EXPECT_EQ(2, external_location.line());
  EXPECT_EQ(4, external_location.column());
  EXPECT_TRUE(external_location.has_filename());
  EXPECT_EQ("filename_23", external_location.filename());
}


TEST(ConvertInternalErrorLocationToExternal, Idempotent) {
  // A query that has the offset referenced in the error location.
  const std::string dummy_query =
      "1234567890123456789\n"
      "1234567890123456789\n";

  const ParseLocationPoint point =
      ParseLocationPoint::FromByteOffset("filename_22", 22);

  const absl::Status status = StatusWithInternalErrorLocation(
      ::googlesql_base::InternalErrorBuilder() << "Foo bar baz", point);
  const absl::Status status2 =
      ConvertInternalErrorPayloadsToExternal(status, dummy_query);
  const absl::Status status3 =
      ConvertInternalErrorPayloadsToExternal(status2, dummy_query);

  EXPECT_EQ(status2, status3);
}

TEST(ConvertInternalErrorLocationToExternal, NoPayload) {
  const absl::Status status = ::googlesql_base::InternalErrorBuilder() << "Foo bar baz";

  const absl::Status status2 =
      ConvertInternalErrorPayloadsToExternal(status, "abc" /* dummy query */);
  EXPECT_FALSE(internal::HasPayload(status2));
}

TEST(ConvertInternalErrorLocationToExternal, NoLocationWithExtraPayload) {
  googlesql_test::TestStatusPayload extra_extension;
  extra_extension.set_value("abc");

  const absl::Status status =
      ::googlesql_base::InternalErrorBuilder().AttachPayload(extra_extension)
      << "Foo bar baz";

  const absl::Status status2 =
      ConvertInternalErrorPayloadsToExternal(status, "abc" /* dummy query */);
  EXPECT_FALSE(internal::HasPayloadWithType<InternalErrorLocation>(status2));
  EXPECT_FALSE(internal::HasPayloadWithType<ErrorLocation>(status2));
  EXPECT_TRUE(
      internal::HasPayloadWithType<googlesql_test::TestStatusPayload>(status2));
}

TEST(ConvertInternalErrorLocationToExternal, LocationWithExtraPayload) {
  // A query that has the line numbers referenced in the error location.
  const std::string dummy_query =
      "1234567890123456789\n"
      "1234567890123456789\n";

  googlesql_test::TestStatusPayload extra_extension;
  extra_extension.set_value("abc");

  const ParseLocationPoint point =
      ParseLocationPoint::FromByteOffset("filename_21", 21);

  const absl::Status status = StatusWithInternalErrorLocation(
      ::googlesql_base::InternalErrorBuilder().AttachPayload(extra_extension)
          << "Foo bar baz",
      point);

  const absl::Status status2 =
      ConvertInternalErrorPayloadsToExternal(status, dummy_query);
  EXPECT_FALSE(internal::HasPayloadWithType<InternalErrorLocation>(status2));
  EXPECT_TRUE(internal::HasPayloadWithType<ErrorLocation>(status2));
  EXPECT_TRUE(
      internal::HasPayloadWithType<googlesql_test::TestStatusPayload>(status2));
}

TEST(ASTNodeTest, GetAndSetLocation) {
  // Test is just exercising the trivial getters and setters.
  ParseLocationPoint start1 = ParseLocationPoint::FromByteOffset("f1", 1);
  ParseLocationPoint end1 = ParseLocationPoint::FromByteOffset("f1", 2);
  ParseLocationRange range1(start1, end1);
  ParseLocationPoint start2 = ParseLocationPoint::FromByteOffset("f2", 3);
  ParseLocationPoint end2 = ParseLocationPoint::FromByteOffset("f2", 4);
  ParseLocationRange range2(start2, end2);
  ParseLocationPoint start3 = ParseLocationPoint::FromByteOffset("f3", 5);
  ParseLocationPoint end3 = ParseLocationPoint::FromByteOffset("f3", 6);
  ParseLocationRange range3(start3, end3);

  FakeASTNode node;
  node.set_location(range1);
  EXPECT_EQ(node.location(), range1);
  EXPECT_EQ(node.start_location(), start1);
  EXPECT_EQ(node.end_location(), end1);
  node.set_location(range2);
  EXPECT_EQ(node.location(), range2);
  EXPECT_EQ(node.start_location(), start2);
  EXPECT_EQ(node.end_location(), end2);
  node.set_location(range3)->set_start_location(start1)->set_end_location(end1);
  EXPECT_EQ(node.start_location(), start1);
  EXPECT_EQ(node.location().start(), start1);
  EXPECT_EQ(node.end_location(), end1);
  EXPECT_EQ(node.location().end(), end1);
}

// Return a string with counts of node kinds that looks like
// "Expression:3 Select:1", with names in sorted order.
static std::string CountNodeKinds(const std::vector<const ASTNode*>& nodes) {
  std::map<std::string, int> counts;
  for (const ASTNode* node : nodes) {
    ++counts[node->GetNodeKindString()];
  }
  std::string ret;
  for (const auto& item : counts) {
    if (!ret.empty()) ret += " ";
    absl::StrAppend(&ret, item.first, ":", item.second);
  }
  return ret;
}

TEST(ParseTreeTest, NodeKindCategories_IfStatement) {
  const std::string sql = "if true then select 5; end if";

  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseScript(sql, ParserOptions(),
                        {.mode = ERROR_MESSAGE_WITH_PAYLOAD,
                         .attach_error_location_payload = true,
                         .stability = GetDefaultErrorMessageStability()},
                        &parser_output));
  auto statement_list = parser_output->script()->statement_list();
  ASSERT_EQ(statement_list.size(), 1);
  const ASTStatement* statement = statement_list[0];

  EXPECT_TRUE(statement->IsStatement());
  EXPECT_TRUE(statement->IsScriptStatement());
  EXPECT_FALSE(statement->IsLoopStatement());
  EXPECT_FALSE(statement->IsSqlStatement());
  EXPECT_FALSE(statement->IsType());
  EXPECT_FALSE(statement->IsExpression());
  EXPECT_FALSE(statement->IsQueryExpression());
  EXPECT_FALSE(statement->IsTableExpression());
}

TEST(ParseTreeTest, NodeKindCategories_DdlStatement_IsCreateStatement) {
  const std::string sql = "create table t as select 1 x";

  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseScript(sql, ParserOptions(),
                        {.mode = ERROR_MESSAGE_WITH_PAYLOAD,
                         .attach_error_location_payload = true,
                         .stability = GetDefaultErrorMessageStability()},
                        &parser_output));
  auto statement_list = parser_output->script()->statement_list();
  ASSERT_EQ(statement_list.size(), 1);
  const ASTDdlStatement* statement =
      statement_list[0]->GetAs<ASTDdlStatement>();

  EXPECT_TRUE(statement->IsDdlStatement());
  EXPECT_TRUE(statement->IsCreateStatement());
  EXPECT_FALSE(statement->IsAlterStatement());
}

TEST(ParseTreeTest, NodeKindCategories_DdlStatement_IsAlterStatement) {
  const std::string sql = "alter table t set options()";

  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseScript(sql, ParserOptions(),
                        {.mode = ERROR_MESSAGE_WITH_PAYLOAD,
                         .attach_error_location_payload =
                             (ERROR_MESSAGE_WITH_PAYLOAD ==
                              ErrorMessageMode::ERROR_MESSAGE_WITH_PAYLOAD),
                         .stability = GetDefaultErrorMessageStability()},
                        &parser_output));
  auto statement_list = parser_output->script()->statement_list();
  ASSERT_EQ(statement_list.size(), 1);
  const ASTDdlStatement* statement =
      statement_list[0]->GetAs<ASTDdlStatement>();

  EXPECT_TRUE(statement->IsDdlStatement());
  EXPECT_TRUE(statement->IsAlterStatement());
  EXPECT_FALSE(statement->IsCreateStatement());
}

TEST(ParseTreeTest, NodeKindCategories_LoopStatement) {
  const std::string sql = "LOOP END LOOP;";

  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseScript(sql, ParserOptions(),
                        {.mode = ERROR_MESSAGE_WITH_PAYLOAD,
                         .attach_error_location_payload = true,
                         .stability = GetDefaultErrorMessageStability()},
                        &parser_output));
  ASSERT_EQ(parser_output->script()->statement_list().size(), 1);
  const ASTStatement* statement = parser_output->script()->statement_list()[0];

  EXPECT_TRUE(statement->IsStatement());
  EXPECT_TRUE(statement->IsScriptStatement());
  EXPECT_TRUE(statement->IsLoopStatement());
  EXPECT_FALSE(statement->IsSqlStatement());
  EXPECT_FALSE(statement->IsType());
  EXPECT_FALSE(statement->IsExpression());
  EXPECT_FALSE(statement->IsQueryExpression());
  EXPECT_FALSE(statement->IsTableExpression());
}

TEST(ParseTreeTest, NodeKindCategories_WhileStatement) {
  const std::string sql = "WHILE TRUE DO END WHILE;";

  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseScript(sql, ParserOptions(),
                        {.mode = ERROR_MESSAGE_WITH_PAYLOAD,
                         .attach_error_location_payload = true,
                         .stability = GetDefaultErrorMessageStability()},
                        &parser_output));
  ASSERT_EQ(parser_output->script()->statement_list().size(), 1);
  const ASTStatement* statement = parser_output->script()->statement_list()[0];

  EXPECT_TRUE(statement->IsStatement());
  EXPECT_TRUE(statement->IsScriptStatement());
  EXPECT_TRUE(statement->IsLoopStatement());
  EXPECT_FALSE(statement->IsSqlStatement());
  EXPECT_FALSE(statement->IsType());
  EXPECT_FALSE(statement->IsExpression());
  EXPECT_FALSE(statement->IsQueryExpression());
  EXPECT_FALSE(statement->IsTableExpression());
}

TEST(ParseTreeTest, NodeKindCategories_QueryStatement) {
  const std::string sql = "SELECT 5";

  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseStatement(sql, ParserOptions(), &parser_output));
  const ASTStatement* statement = parser_output->statement();

  EXPECT_TRUE(statement->IsStatement());
  EXPECT_FALSE(statement->IsScriptStatement());
  EXPECT_TRUE(statement->IsSqlStatement());
  EXPECT_FALSE(statement->IsType());
  EXPECT_FALSE(statement->IsExpression());
  EXPECT_FALSE(statement->IsQueryExpression());
  EXPECT_FALSE(statement->IsTableExpression());
}

TEST(ParseTreeTest, NodeKindCategories_LiteralExpression) {
  const std::string sql = "5";

  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseExpression(sql, ParserOptions(), &parser_output));
  const ASTExpression* expr = parser_output->expression();

  EXPECT_FALSE(expr->IsStatement());
  EXPECT_FALSE(expr->IsScriptStatement());
  EXPECT_FALSE(expr->IsSqlStatement());
  EXPECT_FALSE(expr->IsType());
  EXPECT_TRUE(expr->IsExpression());
  EXPECT_FALSE(expr->IsQueryExpression());
  EXPECT_FALSE(expr->IsTableExpression());
}

TEST(ParseTreeTest, GetDescendantsWithKinds) {
  const std::string sql =
      "select * from (select 1+0x2, x+y), "
      "  (select x union all select f(x+y)+z), "
      "  (select 5+(select 6+7))";

  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseStatement(sql, ParserOptions(), &parser_output));
  const ASTStatement* statement = parser_output->statement();

  ABSL_LOG(INFO) << "Parse tree:\n" << statement->DebugString();

  std::vector<const ASTNode*> found_nodes;
  // Look up empty set of node kinds.
  statement->GetDescendantSubtreesWithKinds({}, &found_nodes);
  EXPECT_EQ("", CountNodeKinds(found_nodes));
  statement->GetDescendantsWithKinds({}, &found_nodes);
  EXPECT_EQ("", CountNodeKinds(found_nodes));

  // Look up a node kind that doesn't occur.
  statement->GetDescendantSubtreesWithKinds({AST_OR_EXPR}, &found_nodes);
  EXPECT_EQ("", CountNodeKinds(found_nodes));
  statement->GetDescendantsWithKinds({AST_OR_EXPR}, &found_nodes);
  EXPECT_EQ("", CountNodeKinds(found_nodes));

  // Look up the root node.
  statement->GetDescendantSubtreesWithKinds({AST_QUERY_STATEMENT},
                                            &found_nodes);
  EXPECT_EQ("QueryStatement:1", CountNodeKinds(found_nodes));
  EXPECT_EQ(AST_QUERY_STATEMENT, statement->node_kind());
  EXPECT_TRUE(statement->Is<ASTQueryStatement>());
  EXPECT_FALSE(statement->Is<ASTCreateTableStatement>());
  statement->GetDescendantsWithKinds({AST_QUERY_STATEMENT}, &found_nodes);
  EXPECT_EQ("QueryStatement:1", CountNodeKinds(found_nodes));

  // Look up the outmost query node.
  statement->GetDescendantSubtreesWithKinds({AST_QUERY}, &found_nodes);
  EXPECT_EQ("Query:1", CountNodeKinds(found_nodes));
  statement->GetDescendantsWithKinds({AST_QUERY}, &found_nodes);
  EXPECT_EQ("Query:5", CountNodeKinds(found_nodes));

  // Look up one node that does occur.
  statement->GetDescendantSubtreesWithKinds({AST_SET_OPERATION},
                                            &found_nodes);
  EXPECT_EQ("SetOperation:1", CountNodeKinds(found_nodes));
  statement->GetDescendantsWithKinds({AST_SET_OPERATION}, &found_nodes);
  EXPECT_EQ("SetOperation:1", CountNodeKinds(found_nodes));

  // Look up a node that occurs multiple times.
  statement->GetDescendantSubtreesWithKinds({AST_INT_LITERAL},
                                            &found_nodes);
  EXPECT_EQ("IntLiteral:5", CountNodeKinds(found_nodes));
  statement->GetDescendantsWithKinds({AST_INT_LITERAL}, &found_nodes);
  EXPECT_EQ("IntLiteral:5", CountNodeKinds(found_nodes));

  // We find the two binary expressions in the first subquery, the outermost
  // one in the third subquery, and none under the union all.
  statement->GetDescendantSubtreesWithKinds(
      {AST_BINARY_EXPRESSION, AST_SET_OPERATION}, &found_nodes);
  EXPECT_EQ("BinaryExpression:3 SetOperation:1", CountNodeKinds(found_nodes));
  statement->GetDescendantsWithKinds(
      {AST_BINARY_EXPRESSION, AST_SET_OPERATION}, &found_nodes);
  EXPECT_EQ("BinaryExpression:6 SetOperation:1", CountNodeKinds(found_nodes));
}

class TestVisitor : public NonRecursiveParseTreeVisitor {
 public:
  explicit TestVisitor(bool post_visit) : post_visit_(post_visit) {}

  absl::StatusOr<VisitResult> defaultVisit(const ASTNode* node) override {
    return VisitInternal(node, "default");
  }

  // Choose an arbitrary node type to provide a specific visitor for.
  // This allows the test to verify that specific visit() methods are invoked
  // when provided, rather than the default.
  absl::StatusOr<VisitResult> visitASTPathExpression(
      const ASTPathExpression* node) override {
    return VisitInternal(node, "path_expression");
  }

  const std::string& log() const { return log_; }

  int pre_visit_count() const { return pre_visit_count_; }
  int post_visit_count() const { return post_visit_count_; }

 protected:
  // Invoked while traversing a node, before traversing its children.
  // Returns a custom VisitResult to return after pre-visit, or
  // this->VisitChildren() to proceed to visiting children and optionally
  // perform post-visit.
  virtual absl::StatusOr<VisitResult> OnDonePreVisit(const ASTNode* node,
                                                     const std::string& label) {
    return VisitChildren(node, label);
  }

  // Invoked while traversing a node, after traversing its children.
  // Returns OK to continue traversal or a failed status to abort.
  virtual absl::Status OnDonePostVisit(const ASTNode* node) {
    return absl::OkStatus();
  }

  absl::StatusOr<VisitResult> VisitChildren(const ASTNode* node,
                                            absl::string_view label) {
    if (post_visit_) {
      auto continuation = [node, this,
                           label = std::string(label)]() -> absl::Status {
        ++post_visit_count_;
        absl::StrAppend(&log_, "postVisit(", label,
                        "): ", node->SingleNodeDebugString(), "\n");

        GOOGLESQL_RETURN_IF_ERROR(OnDonePostVisit(node));
        return absl::OkStatus();
      };
      return VisitResult::VisitChildren(node, continuation);
    } else {
      return VisitResult::VisitChildren(node);
    }
  }

 private:
  absl::StatusOr<VisitResult> VisitInternal(const ASTNode* node,
                                            const std::string& label) {
    ++pre_visit_count_;
    absl::StrAppend(&log_, "preVisit(", label,
                    "): ", node->SingleNodeDebugString(), "\n");

    return OnDonePreVisit(node, label);
  }

  int pre_visit_count_ = 0;
  int post_visit_count_ = 0;
  std::string log_ = "\n";
  bool post_visit_;
};

// TestVisitor which aborts in the PreVisit stage when the node kind matches a
// particular value.
class AbortInPreVisitTestVisitor : public TestVisitor {
 public:
  AbortInPreVisitTestVisitor(bool post_visit, const ASTNodeKind abort_on,
                             absl::StatusOr<VisitResult> abort_result)
      : TestVisitor(post_visit),
        abort_on_(abort_on),
        abort_result_(abort_result) {}

 protected:
  absl::StatusOr<VisitResult> OnDonePreVisit(
      const ASTNode* node, const std::string& label) override {
    if (node->node_kind() == abort_on_) {
      return abort_result_;
    }
    return VisitChildren(node, label);
  }

 private:
  const ASTNodeKind abort_on_;
  absl::StatusOr<VisitResult> abort_result_;
};

// TestVisitor which aborts in the PostVisit stage when the node kind matches a
// particular value.
class AbortInPostVisitTestVisitor : public TestVisitor {
 public:
  explicit AbortInPostVisitTestVisitor(const ASTNodeKind abort_on)
      : TestVisitor(/*post_visit=*/true), abort_on_(abort_on) {}

 protected:
  absl::Status OnDonePostVisit(const ASTNode* node) override {
    if (node->node_kind() == abort_on_) {
      return absl::InternalError("Traverse aborted in PostVisit stage");
    }
    return absl::OkStatus();
  }

 private:
  const ASTNodeKind abort_on_;
};

TEST(ParseTreeTest, NonRecursiveVisitors_WithPostVisit) {
  const std::string sql = "select (1+x) from t;";

  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseStatement(sql, ParserOptions(), &parser_output));

  TestVisitor visitor(/*post_visit=*/true);
  GOOGLESQL_ASSERT_OK(parser_output->statement()->TraverseNonRecursive(&visitor));
  std::string expected_log = R"(
preVisit(default): QueryStatement
preVisit(default): Query
preVisit(default): Select
preVisit(default): SelectList
preVisit(default): SelectColumn
preVisit(default): BinaryExpression(+)
preVisit(default): IntLiteral(1)
postVisit(default): IntLiteral(1)
preVisit(path_expression): PathExpression
preVisit(default): Identifier(x)
postVisit(default): Identifier(x)
postVisit(path_expression): PathExpression
postVisit(default): BinaryExpression(+)
postVisit(default): SelectColumn
postVisit(default): SelectList
preVisit(default): FromClause
preVisit(default): TablePathExpression
preVisit(path_expression): PathExpression
preVisit(default): Identifier(t)
postVisit(default): Identifier(t)
postVisit(path_expression): PathExpression
postVisit(default): TablePathExpression
postVisit(default): FromClause
postVisit(default): Select
postVisit(default): Query
postVisit(default): QueryStatement
)";
  ASSERT_THAT(visitor.log(), expected_log);
  ASSERT_EQ(visitor.pre_visit_count(), visitor.post_visit_count());
  ASSERT_GT(visitor.pre_visit_count(), 0);
}

TEST(ParseTreeTest, NonRecursiveVisitors_NoPostVisit) {
  const std::string sql = "select (1+x) from t;";

  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseStatement(sql, ParserOptions(), &parser_output));

  TestVisitor visitor(/*post_visit=*/false);
  GOOGLESQL_ASSERT_OK(parser_output->statement()->TraverseNonRecursive(&visitor));
  std::string expected_log = R"(
preVisit(default): QueryStatement
preVisit(default): Query
preVisit(default): Select
preVisit(default): SelectList
preVisit(default): SelectColumn
preVisit(default): BinaryExpression(+)
preVisit(default): IntLiteral(1)
preVisit(path_expression): PathExpression
preVisit(default): Identifier(x)
preVisit(default): FromClause
preVisit(default): TablePathExpression
preVisit(path_expression): PathExpression
preVisit(default): Identifier(t)
)";
  ASSERT_THAT(visitor.log(), expected_log);
  ASSERT_GT(visitor.pre_visit_count(), 0);
  ASSERT_EQ(visitor.post_visit_count(), 0);
}

class AbortNonRecursiveVisitorPreVisitTest
    : public ::testing::Test,
      public ::testing::WithParamInterface<absl::StatusOr<VisitResult>> {};

INSTANTIATE_TEST_CASE_P(
    NonRecursivePreVisit, AbortNonRecursiveVisitorPreVisitTest,
    ::testing::Values(absl::InternalError("Traverse aborted in PreVisit stage"),
                      VisitResult::Terminate()));

TEST_P(AbortNonRecursiveVisitorPreVisitTest, PreVisit) {
  const std::string sql = "select y, (1+x) from t;";

  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseStatement(sql, ParserOptions(), &parser_output));

  AbortInPreVisitTestVisitor visitor(
      /*post_visit=*/true,
      /*abort_on=*/AST_INT_LITERAL, GetParam());
  absl::Status status =
      parser_output->statement()->TraverseNonRecursive(&visitor);
  ASSERT_EQ(status, GetParam().status());
  std::string expected_log = R"(
preVisit(default): QueryStatement
preVisit(default): Query
preVisit(default): Select
preVisit(default): SelectList
preVisit(default): SelectColumn
preVisit(path_expression): PathExpression
preVisit(default): Identifier(y)
postVisit(default): Identifier(y)
postVisit(path_expression): PathExpression
postVisit(default): SelectColumn
preVisit(default): SelectColumn
preVisit(default): BinaryExpression(+)
preVisit(default): IntLiteral(1)
)";
  ASSERT_THAT(visitor.log(), expected_log);
}

TEST(ParseTreeTest, NonRecursiveVisitors_AbortInPostVisit) {
  const std::string sql = "select y, (1+x) from t;";

  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseStatement(sql, ParserOptions(), &parser_output));

  AbortInPostVisitTestVisitor visitor(/*abort_on=*/AST_INT_LITERAL);
  ASSERT_THAT(parser_output->statement()->TraverseNonRecursive(&visitor),
              ::absl_testing::StatusIs(
                  absl::StatusCode::kInternal,
                  ::testing::Eq("Traverse aborted in PostVisit stage")));
  std::string expected_log = R"(
preVisit(default): QueryStatement
preVisit(default): Query
preVisit(default): Select
preVisit(default): SelectList
preVisit(default): SelectColumn
preVisit(path_expression): PathExpression
preVisit(default): Identifier(y)
postVisit(default): Identifier(y)
postVisit(path_expression): PathExpression
postVisit(default): SelectColumn
preVisit(default): SelectColumn
preVisit(default): BinaryExpression(+)
preVisit(default): IntLiteral(1)
postVisit(default): IntLiteral(1)
)";
  ASSERT_THAT(visitor.log(), expected_log);
}

// Returns a copy of <s> repeated <count> times.
std::string Repeat(absl::string_view s, int count) {
  ABSL_CHECK_GT(count, 0);
  return absl::StrJoin(std::vector<absl::string_view>(count, s), "");
}

class TestStatusVisitor : public DefaultParseTreeStatusVisitor {
 public:
  explicit TestStatusVisitor() = default;
  using OutputType = const ASTBinaryExpression*;

  absl::Status VisitASTBinaryExpression(const ASTBinaryExpression* node,
                                        std::any& output) override {
    // Overwrite whatever is there. The final output will be the last visited.
    output = node;
    return absl::OkStatus();
  }

  absl::Status VisitASTFunctionCall(const ASTFunctionCall* node,
                                    std::any& output) override {
    // Just fail to test status propagation.
    GOOGLESQL_RET_CHECK_FAIL() << "FunctionCall should not be visited";
  }
};

TEST(TestStatusVisitor, TestOutputValue) {
  constexpr absl::string_view kSql = "select y, (1+x) from t;";

  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseStatement(
      kSql, ParserOptions(LanguageOptions::MaximumFeatures()), &parser_output));

  TestStatusVisitor visitor;
  std::any output;
  GOOGLESQL_ASSERT_OK(parser_output->statement()->Accept(visitor, output));
  EXPECT_TRUE(output.has_value());
  ASSERT_THAT(std::string(output.type().name()),
              ::testing::HasSubstr("ASTBinaryExpression"));
  EXPECT_EQ(std::any_cast<TestStatusVisitor::OutputType>(output)->node_kind(),
            AST_BINARY_EXPRESSION);
};

TEST(TestStatusVisitor, StatusPropagation) {
  constexpr absl::string_view kSql = "select foo(x, y) from t;";

  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseStatement(
      kSql, ParserOptions(LanguageOptions::MaximumFeatures()), &parser_output));

  TestStatusVisitor visitor;
  std::any output;
  EXPECT_THAT(parser_output->statement()->Accept(visitor, output),
              ::absl_testing::StatusIs(absl::StatusCode::kInternal));
};

}  // namespace
}  // namespace testing
}  // namespace googlesql
