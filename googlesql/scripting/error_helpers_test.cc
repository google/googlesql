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

#include "googlesql/scripting/error_helpers.h"

#include <memory>
#include <string>

#include "googlesql/common/errors.h"
#include "googlesql/common/status_payload_utils.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser.h"
#include "googlesql/proto/internal_error_location.pb.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/scripting/script_segment.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace {

using ::testing::AllOf;
using ::testing::HasSubstr;
using ::testing::Test;
using ::absl_testing::StatusIs;

// Matcher to verify that a absl::Status object does not have a payload of the
// given type.
template<class T>
::testing::Matcher<const absl::Status&> HasNoPayloadWithType() {
  return ::testing::ResultOf(
      [](const absl::Status& status) {
        return internal::HasPayloadWithType<T>(status);
      },
      false);
}

absl::Status ConvertStatus(const absl::Status& status,
                           const ScriptSegment& segment) {
  GOOGLESQL_RETURN_IF_ERROR(status).With(ConvertLocalErrorToScriptError(segment));
  return status;
}

void TestConvertErrorWithSource(
    ErrorMessageMode script_executor_error_message_mode,
    ErrorMessageMode inner_error_message_mode,
    absl::string_view expected_error_message) {
  const std::string sql =
      "SELECT 3;\n  SELECT outer_error_location, inner_error_location";
  const std::string error_stmt_text =
      "SELECT outer_error_location, inner_error_location";
  absl::Status inner_status = ConvertInternalErrorPayloadsToExternal(
      StatusWithInternalErrorLocation(
          absl::InternalError("Inner error"),
          ParseLocationPoint::FromByteOffset(
              static_cast<int>(error_stmt_text.find("inner_error_location")))),
      error_stmt_text);

  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseScript(sql, ParserOptions(),
                        {.mode = ERROR_MESSAGE_WITH_PAYLOAD,
                         .attach_error_location_payload = true,
                         .stability = GetDefaultErrorMessageStability()},
                        &parser_output));
  ASSERT_EQ(parser_output->script()->statement_list().size(), 2);
  const ASTStatement* error_stmt =
      parser_output->script()->statement_list().at(1);
  ScriptSegment error_stmt_segment =
      ScriptSegment::FromASTNode(sql, error_stmt);
  ASSERT_EQ(error_stmt_segment.GetSegmentText(), error_stmt_text);

  InternalErrorLocation outer_location;
  outer_location.set_byte_offset(
      static_cast<int>(error_stmt_text.find("outer_error_location")));

  absl::Status outer_status = ConvertInternalErrorPayloadsToExternal(
      MakeSqlError().AttachPayload(
          SetErrorSourcesFromStatus(outer_location, inner_status,
                                    inner_error_message_mode, error_stmt_text))
          << "outer error",
      error_stmt_text);

  EXPECT_THAT(
      MaybeUpdateErrorFromPayload(
          script_executor_error_message_mode,
          /*keep_error_location_payload=*/
          script_executor_error_message_mode == ERROR_MESSAGE_WITH_PAYLOAD, sql,
          ConvertStatus(outer_status, error_stmt_segment)),
      StatusIs(absl::StatusCode::kInvalidArgument, expected_error_message));
}

TEST(ConvertLocalErrorToScriptError, WithSourceAndOneLine) {
  TestConvertErrorWithSource(ERROR_MESSAGE_ONE_LINE, ERROR_MESSAGE_WITH_PAYLOAD,
                             "outer error [at 2:10]; Inner error [at 2:32]");
}

TEST(ConvertLocalErrorToScriptError, WithSourceAndCaret) {
  TestConvertErrorWithSource(ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
                             ERROR_MESSAGE_WITH_PAYLOAD,
                             R"(outer error [at 2:10]
  SELECT outer_error_location, inner_error_location
         ^
Inner error [at 2:32])");
}

TEST(ConvertLocalErrorToScriptError,
     WithSourceAndCaret_InnerErrorAlsoWithCaret) {
  TestConvertErrorWithSource(ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
                             ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
                             R"(outer error [at 2:10]
  SELECT outer_error_location, inner_error_location
         ^
Inner error [at 2:32]
SELECT outer_error_location, inner_error_location
                             ^)");
}

TEST(ConvertLocalErrorToScriptError, InvalidErrorLocation) {
  const std::string sql = "SELECT 3;\n  SELECT error_location";
  const std::string error_stmt_text = "SELECT error_location";

  std::unique_ptr<ParserOutput> parser_output;
  GOOGLESQL_ASSERT_OK(ParseScript(sql, ParserOptions(),
                        {.mode = ERROR_MESSAGE_WITH_PAYLOAD,
                         .attach_error_location_payload = true,
                         .stability = GetDefaultErrorMessageStability()},
                        &parser_output));
  ASSERT_EQ(parser_output->script()->statement_list().size(), 2);
  const ASTStatement* error_stmt =
      parser_output->script()->statement_list().at(1);
  ScriptSegment error_stmt_segment =
      ScriptSegment::FromASTNode(sql, error_stmt);
  ASSERT_EQ(error_stmt_segment.GetSegmentText(), error_stmt_text);

  // Line 2, column 1 exists in the overall script, but not in
  // <error_stmt_text>.
  ErrorLocation error_location;
  error_location.set_line(2);
  error_location.set_column(1);

  absl::Status invalid_status = MakeSqlError().AttachPayload(error_location)
                                << "Test error message";

  EXPECT_THAT(
      ConvertStatus(invalid_status, error_stmt_segment),
      AllOf(StatusIs(absl::StatusCode::kInternal,
                     HasSubstr("Query had 1 lines but line 2 was requested")),
            HasNoPayloadWithType<ErrorLocation>()));
}

}  // namespace
}  // namespace googlesql
