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

#include "googlesql/parser/parse_tree_errors.h"

#include <functional>
#include <memory>
#include <string>

#include "googlesql/common/status_payload_utils.h"
#include "googlesql/common/testing/proto_matchers.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/common/testing/status_payload_matchers.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser.h"
#include "googlesql/proto/internal_fix_suggestion.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/source_location.h"
#include "googlesql/base/status.h"
#include "googlesql/base/status_builder.h"

using ::googlesql::testing::EqualsProto;
using ::googlesql::testing::StatusHasPayload;
using testing::HasSubstr;
using absl_testing::StatusIs;

namespace googlesql {

static absl::Status NoError() { return absl::OkStatus(); }

static absl::Status ErrorWithoutLocation() {
  return MakeSqlError() << "No location";
}

static absl::Status ErrorWithLocation() {
  return MakeSqlErrorAtPoint(ParseLocationPoint::FromByteOffset(10))
      << "With location";
}

static absl::Status ErrorNonSQL() {
  return absl::NotFoundError("Non-SQL error");
}

static absl::Status RetCheckError() { GOOGLESQL_RET_CHECK_FAIL() << "ret_check_error"; }

TEST(GetErrorLocationPoint, Basic) {
  FakeASTNode ast_location;
  GOOGLESQL_ASSERT_OK(ast_location.InitFields());
  ParseLocationPoint expected =
      ParseLocationPoint::FromByteOffset("fake_filename", 7);

  EXPECT_THAT(
      GetErrorLocationPoint(&ast_location, false).ToInternalErrorLocation(),
      EqualsProto(expected.ToInternalErrorLocation()));
}

TEST(GetErrorLocationPoint, AtEnd) {
  FakeASTNode ast_location;
  GOOGLESQL_ASSERT_OK(ast_location.InitFields());
  ParseLocationPoint expected =
      ParseLocationPoint::FromByteOffset("fake_filename", 10);

  EXPECT_THAT(
      GetErrorLocationPoint(&ast_location, /*include_leftmost_child=*/false,
                            /*use_end_location=*/true)
          .ToInternalErrorLocation(),
      EqualsProto(expected.ToInternalErrorLocation()));
}

TEST(GetErrorLocationPoint, LeftMost_ButNoChildren) {
  FakeASTNode ast_location;
  GOOGLESQL_ASSERT_OK(ast_location.InitFields());
  ParseLocationPoint expected =
      ParseLocationPoint::FromByteOffset("fake_filename", 7);

  EXPECT_THAT(
      GetErrorLocationPoint(&ast_location, true).ToInternalErrorLocation(),
      EqualsProto(expected.ToInternalErrorLocation()));
}

TEST(GetErrorLocationPoint, LeftMost) {
  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_EXPECT_OK(ParseExpression("( a, b)", ParserOptions(), &parser_output));
  const ASTExpression *expr = parser_output->expression();
  EXPECT_EQ(0, GetErrorLocationPoint(expr, true).GetByteOffset());
  // "a"
  EXPECT_EQ(2, GetErrorLocationPoint(expr->child(0), true).GetByteOffset());
  // "b"
  EXPECT_EQ(5, GetErrorLocationPoint(expr->child(1), true).GetByteOffset());
}

TEST(Errors, LocationOverride) {
  // Dummy query that can be used to resolve all lines and columns in
  // InternalErrorLocations in this test.
  const std::string query = "1\n2\n3\n42345\n";

  auto ReturnWithLocationOverride =
      [](std::function<absl::Status()> make_error) -> absl::Status {
    FakeASTNode ast_location;
    GOOGLESQL_RETURN_IF_ERROR(ast_location.InitFields());
    GOOGLESQL_RETURN_IF_ERROR(make_error()).With(LocationOverride(&ast_location));
    return absl::OkStatus();
  };

  absl::Status test_status = ConvertInternalErrorPayloadsToExternal(
      ReturnWithLocationOverride(RetCheckError), query);
  EXPECT_THAT(test_status, StatusIs(absl::StatusCode::kInternal));
  EXPECT_THAT(
      FormatError(test_status),
      HasSubstr("ret_check_error [googlesql.ErrorLocation] { line: 4 "
                "column: 2 filename: \"fake_filename\" "
                "input_start_line_offset: 0 input_start_column_offset: 0 }"));

  GOOGLESQL_EXPECT_OK(ConvertInternalErrorPayloadsToExternal(
      ReturnWithLocationOverride(&NoError), query));

  test_status = ConvertInternalErrorPayloadsToExternal(
      ReturnWithLocationOverride(&ErrorWithoutLocation), query);
  EXPECT_THAT(test_status, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(FormatError(test_status),
              HasSubstr("No location [at fake_filename:4:2]"));

  test_status = ConvertInternalErrorPayloadsToExternal(
      ReturnWithLocationOverride(&ErrorWithLocation), query);
  EXPECT_THAT(test_status, StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(FormatError(test_status),
              HasSubstr("With location [at fake_filename:4:2]"));

  // LocationOverride only overrides location, never error code. We took the
  // philosophy that any overriding of specific error cods should be explicit
  // at the place where they need to be overridden.
  test_status = ConvertInternalErrorPayloadsToExternal(
      ReturnWithLocationOverride(&ErrorNonSQL), query);
  EXPECT_THAT(test_status, StatusIs(absl::StatusCode::kNotFound));
  EXPECT_THAT(
      FormatError(test_status),
      HasSubstr("generic::not_found: Non-SQL error [googlesql.ErrorLocation] { "
                "line: 4 column: 2 filename: \"fake_filename\" "
                "input_start_line_offset: 0 input_start_column_offset: 0 }"));
}

static absl::Status TestOnNode(const ASTNode* node) {
  GOOGLESQL_RETURN_IF_ERROR(MakeSqlErrorIfPresent(node)) << "Node not supported here";
  return absl::OkStatus();
}

TEST(Errors, MakeSqlErrorIfPresent) {
  const ASTNode* node = nullptr;
  GOOGLESQL_EXPECT_OK(TestOnNode(node));

  FakeASTNode ast_location;
  node = &ast_location;
  EXPECT_THAT(TestOnNode(node), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(TestOnNode(node).ToString(),
              HasSubstr("Node not supported here"));
}

TEST(WrapNestedErrorStatus, PropagatesFixPayload) {
  absl::Status status = absl::InvalidArgumentError("Invalid query");
  InternalErrorFixSuggestions fixes;
  fixes.add_fix_suggestions()->set_title("Fix title");
  internal::AttachPayload(&status, fixes);
  FakeASTNode ast_location;

  absl::Status new_status = WrapNestedErrorStatus(
      &ast_location, "Nested error", status, ERROR_MESSAGE_WITH_PAYLOAD);

  EXPECT_THAT(new_status, StatusHasPayload<InternalErrorFixSuggestions>(
                              EqualsProto(fixes)));
}

TEST(AddFixSuggestionToStatus, AddsFirstFixSuggestion) {
  absl::Status status = absl::InvalidArgumentError("Invalid query");

  EXPECT_THAT(AddFixSuggestionToStatus(
                  status, "Fix title",
                  ParseLocationPoint::FromByteOffset("filename", 2),
                  ParseLocationPoint::FromByteOffset(4), "replacement_fix"),
              StatusHasPayload<InternalErrorFixSuggestions>(EqualsProto(R"pb(
                fix_suggestions {
                  title: "Fix title"
                  edits {
                    text_edits {
                      range {
                        start { byte_offset: 2 filename: "filename" }
                        length: 2
                      }
                      new_text: "replacement_fix"
                    }
                  }
                }
              )pb")));
}

TEST(AddFixSuggestionToStatus, AddsMultipleFixSuggestions) {
  absl::Status status = absl::InvalidArgumentError("Invalid query");

  AddFixSuggestionToStatus(
      status, "Fix title 1", ParseLocationPoint::FromByteOffset("filename", 11),
      ParseLocationPoint::FromByteOffset(14), "replacement_1");
  AddFixSuggestionToStatus(status, "Fix title 2",
                           ParseLocationPoint::FromByteOffset("filename_2", 22),
                           ParseLocationPoint::FromByteOffset(24),
                           "replacement_2");

  EXPECT_THAT(status,
              StatusHasPayload<InternalErrorFixSuggestions>(EqualsProto(R"pb(
                fix_suggestions {
                  title: "Fix title 1"
                  edits {
                    text_edits {
                      range {
                        start { byte_offset: 11 filename: "filename" }
                        length: 3
                      }
                      new_text: "replacement_1"
                    }
                  }
                },
                fix_suggestions {
                  title: "Fix title 2"
                  edits {
                    text_edits {
                      range {
                        start { byte_offset: 22 filename: "filename_2" }
                        length: 2
                      }
                      new_text: "replacement_2"
                    }
                  }
                }
              )pb")));
}

}  // namespace googlesql
