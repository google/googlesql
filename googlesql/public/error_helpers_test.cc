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

#include "googlesql/public/error_helpers.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/base/enum_utils.h"
#include "googlesql/common/errors.h"
#include "googlesql/common/status_payload_utils.h"
#include "googlesql/common/testing/proto_matchers.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/common/testing/status_payload_matchers.h"
#include "googlesql/proto/internal_error_location.pb.h"
#include "googlesql/proto/internal_fix_suggestion.pb.h"
#include "googlesql/proto/script_exception.pb.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/deprecation_warning.pb.h"
#include "googlesql/public/error_location.pb.h"
#include "googlesql/public/fix_suggestion.pb.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/status_builder.h"

ABSL_DECLARE_FLAG(std::string, googlesql_minimized_error_message_tag);

namespace googlesql {

using ::googlesql::testing::EqualsProto;
using ::googlesql::testing::StatusHasGenericPayload;
using ::googlesql::testing::StatusHasPayload;
using ::testing::_;
using ::testing::AllOf;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::ExplainMatchResult;
using ::testing::HasSubstr;
using ::testing::Not;
using ::absl_testing::StatusIs;

TEST(ErrorHelpersTest, FormatErrorLocation) {
  ErrorLocation location;
  EXPECT_EQ("1:1", FormatErrorLocation(location));

  location.set_line(4);
  location.set_column(15);
  EXPECT_EQ("4:15", FormatErrorLocation(location));

  location.set_filename("filename");
  EXPECT_EQ("filename:4:15", FormatErrorLocation(location));

  location.set_line(0);
  location.set_column(-4);
  EXPECT_EQ("filename:0:-4", FormatErrorLocation(location));
}

TEST(ErrorHelpersTest, FormatError) {
  // Dummy query for use in these tests. The important part is that the query
  // has the offsets referenced in the ParseLocationPoints.
  const std::string dummy_query =
      "1234567890123456789_1\n"
      "1234567890123456789_2\n"
      "1234567890123456789_3\n"
      "1234567890123456789_4\n";

  const absl::Status ok;
  EXPECT_EQ("OK", FormatError(ok));
  const absl::Status status1 = MakeSqlError() << "Message1";
  EXPECT_EQ("generic::invalid_argument: Message1",
            internal::StatusToString(status1));
  EXPECT_EQ("Message1", FormatError(status1));

  absl::Status status2 =
      MakeSqlErrorAtPoint(ParseLocationPoint::FromByteOffset(74)) << "Message2";
  status2 = ConvertInternalErrorPayloadsToExternal(status2, dummy_query);
  EXPECT_EQ(
      "generic::invalid_argument: Message2 "
      "[googlesql.ErrorLocation] { line: 4 column: 9 input_start_line_offset: "
      "0 input_start_column_offset: 0 }",
      internal::StatusToString(status2));
  EXPECT_EQ("Message2 [at 4:9]", FormatError(status2));

  // Error with code other than INVALID_ARGUMENT.
  const absl::Status status3 = ::googlesql_base::UnknownErrorBuilder() << "Message3";
  EXPECT_EQ("generic::unknown: Message3", internal::StatusToString(status3));
  EXPECT_EQ("generic::unknown: Message3", FormatError(status3));

  // Error with a googlesql payload but the wrong code.
  absl::Status status4 =
      ::googlesql_base::UnknownErrorBuilder().AttachPayload(
          ParseLocationPoint::FromByteOffset(1).ToInternalErrorLocation())
      << "Message4";
  status4 = ConvertInternalErrorPayloadsToExternal(status4, dummy_query);
  EXPECT_EQ(
      "generic::unknown: Message4 "
      "[googlesql.ErrorLocation] { line: 1 column: 2 input_start_line_offset: "
      "0 input_start_column_offset: 0 }",
      internal::StatusToString(status4));
  EXPECT_EQ(internal::StatusToString(status4), FormatError(status4));

  googlesql_test::TestStatusPayload extra_extension;
  extra_extension.set_value("abc");

  // Error with a non-ErrorLocation payload.
  absl::Status status5 =
      ::googlesql_base::UnknownErrorBuilder().AttachPayload(extra_extension)
      << "Message5";
  EXPECT_EQ(
      "generic::unknown: Message5 "
      "[googlesql_test.TestStatusPayload] { value: \"abc\" }",
      internal::StatusToString(status5));
  EXPECT_EQ(internal::StatusToString(status5), FormatError(status5));

  // Error with both ErrorLocation and non-ErrorLocation payloads.
  // The ErrorLocation gets stripped by FormatError, but the other payload
  // still prints.
  absl::Status status6 =
      MakeSqlErrorAtPoint(ParseLocationPoint::FromByteOffset(43))
          .AttachPayload(extra_extension)
      << "Message6";
  status6 = ConvertInternalErrorPayloadsToExternal(status6, dummy_query);
  EXPECT_THAT(
      internal::StatusToString(status6),
      AllOf(
          HasSubstr("generic::invalid_argument: Message6"),
          HasSubstr(
              "[googlesql.ErrorLocation] { line: 2 column: 22 "
              "input_start_line_offset: 0 input_start_column_offset: 0 }"),
          HasSubstr("[googlesql_test.TestStatusPayload] { value: \"abc\" }")));
  EXPECT_EQ(
      "Message6 [at 2:22] "
      "[googlesql_test.TestStatusPayload] { value: \"abc\" }",
      FormatError(status6));
}

TEST(ErrorHelpersTest, ErrorLocationHelpers) {
  ErrorLocation location;

  absl::Status status1 = MakeSqlError() << "Message1";
  EXPECT_EQ("generic::invalid_argument: Message1",
            internal::StatusToString(status1));
  EXPECT_FALSE(HasErrorLocation(status1));
  EXPECT_FALSE(GetErrorLocation(status1, &location));
  ClearErrorLocation(&status1);
  EXPECT_EQ("generic::invalid_argument: Message1",
            internal::StatusToString(status1));

  absl::Status status2 =
      ::googlesql_base::UnknownErrorBuilder().AttachPayload(
          ParseLocationPoint::FromByteOffset(1).ToInternalErrorLocation())
      << "Message2";
  status2 = ConvertInternalErrorPayloadsToExternal(status2, "123\n456");
  EXPECT_EQ(
      "generic::unknown: Message2 "
      "[googlesql.ErrorLocation] { line: 1 column: 2 input_start_line_offset: "
      "0 input_start_column_offset: 0 }",
      internal::StatusToString(status2));
  EXPECT_TRUE(HasErrorLocation(status2));
  EXPECT_TRUE(GetErrorLocation(status2, &location));
  EXPECT_THAT(location,
              EqualsProto("line: 1 column: 2 input_start_line_offset: 0 "
                          "input_start_column_offset: 0"));
  ClearErrorLocation(&status2);
  EXPECT_EQ("generic::unknown: Message2", internal::StatusToString(status2));
  // No payload, not an empty payload.
  EXPECT_FALSE(internal::HasPayload(status2));
  EXPECT_FALSE(HasErrorLocation(status2));
  ClearErrorLocation(&status2);

  googlesql_test::TestStatusPayload extra_extension;

  extra_extension.set_value("abc");

  // For a status with both an ErrorLocation and another payload, Clear
  // will just remove the ErrorLocation.
  absl::Status status3 =
      MakeSqlErrorAtPoint(ParseLocationPoint::FromByteOffset(11))
          .AttachPayload(extra_extension)
      << "Message3";
  status3 = ConvertInternalErrorPayloadsToExternal(status3, "123\n456\n78901");
  std::string status3_str = internal::StatusToString(status3);

  EXPECT_THAT(
      internal::StatusToString(status3),
      AllOf(
          HasSubstr("generic::invalid_argument: Message3"),
          HasSubstr(
              "[googlesql.ErrorLocation] { line: 3 column: 4 "
              "input_start_line_offset: 0 input_start_column_offset: 0 }"),
          HasSubstr("[googlesql_test.TestStatusPayload] { value: \"abc\" }")));

  EXPECT_TRUE(HasErrorLocation(status3));
  EXPECT_TRUE(GetErrorLocation(status3, &location));
  EXPECT_THAT(location,
              EqualsProto("line: 3 column: 4 input_start_line_offset: 0 "
                          "input_start_column_offset: 0"));
  ClearErrorLocation(&status3);
  EXPECT_EQ(
      "generic::invalid_argument: Message3 "
      "[googlesql_test.TestStatusPayload] { value: \"abc\" }",
      internal::StatusToString(status3));
  EXPECT_FALSE(HasErrorLocation(status3));
  ClearErrorLocation(&status3);
}

static void TestGetCaret(absl::string_view query, const ErrorLocation& location,
                         absl::string_view expected_output) {
  EXPECT_EQ(expected_output, GetErrorStringWithCaret(query, location));
}

static ErrorLocation MakeErrorLocation(int line, int column) {
  ABSL_DCHECK_GE(line, 1) << "with line = " << line << ", column = " << column;
  ABSL_DCHECK_GE(column, 1) << "with line = " << line << ", column = " << column;

  ErrorLocation location;
  location.set_line(line);
  location.set_column(column);
  return location;
}

TEST(ErrorHelpersTest, GetErrorStringWithCaret) {
  const std::string str1 =
      "abc\n"
      "def\n"
      "ghi";
  TestGetCaret(str1, MakeErrorLocation(1, 1),
               "abc\n"
               "^");
  TestGetCaret(str1, MakeErrorLocation(1, 3),
               "abc\n"
               "  ^");
  TestGetCaret(str1, MakeErrorLocation(2, 2),
               "def\n"
               " ^");
  TestGetCaret(str1, MakeErrorLocation(2, 4),  // One off end of line is okay.
               "def\n"
               "   ^");
  TestGetCaret(str1, MakeErrorLocation(3, 3),
               "ghi\n"
               "  ^");
  TestGetCaret(str1, MakeErrorLocation(3, 4),  // One off end of string is okay.
               "ghi\n"
               "   ^");

  // Test with tabs.  Tabs are expanded to eight spaces.
  TestGetCaret("abc\ndef\tghi\njkl", MakeErrorLocation(2, 10),
               "def     ghi\n"
               "         ^");
  TestGetCaret("1234567\txxx", MakeErrorLocation(1, 10),
               "1234567 xxx\n"
               "         ^");
  TestGetCaret("12345678\txxx", MakeErrorLocation(1, 18),
               "12345678        xxx\n"
               "                 ^");
  TestGetCaret("\t\txxx\tyyy", MakeErrorLocation(1, 18),
               "                xxx     yyy\n"
               "                 ^");

  // Test with \r in the string.
  TestGetCaret("ab\rcd\ref", MakeErrorLocation(2, 1),
               "cd\n"
               "^");
  // Test with \r\n in the string.
  TestGetCaret("ab\r\ncd\r\nef", MakeErrorLocation(2, 1),
               "cd\n"
               "^");
  // Test with \n\r in the string.  This does not count as a single newline,
  // so it ends up counting as two.
  TestGetCaret("ab\n\rcd\n\ref", MakeErrorLocation(2, 1),
               "\n"
               "^");
  TestGetCaret("ab\n\rcd\n\ref", MakeErrorLocation(3, 1),
               "cd\n"
               "^");

  // Out of bounds locations will hit DCHECKs.
  EXPECT_DEBUG_DEATH(
      GetErrorStringWithCaret(str1, MakeErrorLocation(0, 1)),
      "Check failed: line >= 1");
  EXPECT_DEBUG_DEATH(
      GetErrorStringWithCaret(str1, MakeErrorLocation(1, 0)),
      "Check failed: column >= 1");
  EXPECT_DEBUG_DEATH(GetErrorStringWithCaret(str1, MakeErrorLocation(4, 1)),
                     "Query had .* lines but line .* was requested");
  EXPECT_DEBUG_DEATH(
      GetErrorStringWithCaret(str1, MakeErrorLocation(1, 5)),
      "Check failed: location.column.. <= truncated_input->size");

  // Now try some where we have to prune a long line.
  // Construct a line with multiple words, where each word starts with
  // its character position (e.g. "45_45678" at column 45).
  std::string line;
  for (int block_size = 5; line.size() < 160; block_size += 3) {
    const std::string block_background = "12345678901234567890";
    std::string block = absl::StrCat(line.size() + 1, "_");
    absl::StrAppend(&block, block_background.substr(block.size()));
    absl::StrAppend(&line, block.substr(0, block_size - 1), " ");
    block_size += 3;
  }
  ABSL_LOG(INFO) << "Made line '" << line << "' with length " << line.length();
  const int kMaxWidth = 56;
  std::vector<std::string> outputs;
  for (int i = 1; i < line.length() - 1; i += 7) {
    outputs.push_back(
        GetErrorStringWithCaret(line, MakeErrorLocation(1, i), kMaxWidth));
    ABSL_LOG(INFO) << "Error at column " << i << "\n" << outputs.back();

    // Should be less than max_width chars before the newline.
    EXPECT_LE(outputs.back().find('\n'), kMaxWidth);
  }
  outputs.push_back(
      GetErrorStringWithCaret(line,
                              MakeErrorLocation(1, line.length() - 1),
                              kMaxWidth));
  outputs.push_back(
      GetErrorStringWithCaret(line,
                              MakeErrorLocation(1, line.length()), kMaxWidth));

  // These are example outputs from GetErrorStringWithCaret at various
  // positions.  These look like substring of the full string of
  // length <= kMaxWidth that try to start at word boundary.
  // Some don't find an acceptable word boundary and just point at the middle.
  EXPECT_THAT(outputs, ElementsAreArray({
      "1_34 6_34567890 17_4567890123456 34_45678901234567890...\n"
      "^",
      "1_34 6_34567890 17_4567890123456 34_45678901234567890...\n"
      "       ^",
      "1_34 6_34567890 17_4567890123456 34_45678901234567890...\n"
      "              ^",
      "1_34 6_34567890 17_4567890123456 34_45678901234567890...\n"
      "                     ^",
      "1_34 6_34567890 17_4567890123456 34_45678901234567890...\n"
      "                            ^",
      "1_34 6_34567890 17_4567890123456 34_45678901234567890...\n"
      "                                   ^",
      "...17_4567890123456 34_45678901234567890 55_456789012...\n"
      "                             ^",
      "...17_4567890123456 34_45678901234567890 55_456789012...\n"
      "                                    ^",
      "...34_45678901234567890 55_45678901234567890 76_45678...\n"
      "                          ^",
      "...34_45678901234567890 55_45678901234567890 76_45678...\n"
      "                                 ^",
      "...01234567890 55_45678901234567890 76_45678901234567...\n"
      "                               ^",
      "...55_45678901234567890 76_45678901234567890 97_45678...\n"
      "                          ^",
      "...55_45678901234567890 76_45678901234567890 97_45678...\n"
      "                                 ^",
      "...01234567890 76_45678901234567890 97_45678901234567...\n"
      "                               ^",
      "...76_45678901234567890 97_45678901234567890 118_5678...\n"
      "                          ^",
      "...76_45678901234567890 97_45678901234567890 118_5678...\n"
      "                                 ^",
      "...01234567890 97_45678901234567890 118_5678901234567...\n"
      "                               ^",
      "...97_45678901234567890 118_5678901234567890 139_5678...\n"
      "                          ^",
      "...97_45678901234567890 118_5678901234567890 139_5678...\n"
      "                                 ^",
      "...01234567890 118_5678901234567890 139_5678901234567...\n"
      "                               ^",
      "...118_5678901234567890 139_5678901234567890 160_5678...\n"
      "                          ^",
      "...118_5678901234567890 139_5678901234567890 160_5678...\n"
      "                                 ^",
      "...01234567890 139_5678901234567890 160_5678901234567...\n"
      "                               ^",
      "...139_5678901234567890 160_5678901234567890 \n"
      "                          ^",
      "...139_5678901234567890 160_5678901234567890 \n"
      "                                 ^",
      "...01234567890 160_5678901234567890 \n"
      "                               ^",
      "...160_5678901234567890 \n"
      "                      ^",
      "...160_5678901234567890 \n"
      "                       ^"
  }));

  // Here's one where we don't have any word boundaries, so we just
  // point at the middle.
  EXPECT_EQ(
      "...901234567890123456789012...\n"
      "                  ^",
      GetErrorStringWithCaret(
          "01234567890123456789012345678901234567890123456789"
          "01234567890123456789012345678901234567890123456789",
          MakeErrorLocation(1, 35), 30 /* max_width */));

  // Here's one where we avoid slicing a UTF8 codepoint when we do max width
  // truncation. In this example we choose to print a 29 byte error string
  // rather than slicing the 2-byte UTF8 codepoint in half.
  EXPECT_EQ("01234567890123456789012345...\n^",
            GetErrorStringWithCaret("01234567890123456789012345"
                                    "\xc3\xb0"
                                    "67890",
                                    MakeErrorLocation(1, 1),
                                    /*max_width_in=*/30));
  // Repeat the above test but this time give enough room for the whole
  // codepoint.
  EXPECT_EQ("01234567890123456789012345\xc3\xb0...\n^",
            GetErrorStringWithCaret("01234567890123456789012345"
                                    "\xc3\xb0"
                                    "67890",
                                    MakeErrorLocation(1, 1),
                                    /*max_width_in=*/31));
}

TEST(ErrorHelpersTest, GetErrorStringWithCaret_WeirdData) {
  // 30 characters of garbage data.
  std::string test_string1 =
      "\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80"
      "\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80"
      "\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80";
  EXPECT_EQ(30, test_string1.size());
  EXPECT_EQ(
      "\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80"
      "\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80"
      "\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80"
      "\n^",
      GetErrorStringWithCaret(test_string1, MakeErrorLocation(1, 1),
                              /*max_width_in=*/31));

  // 32 characters of garbage data.
  std::string test_string2 =
      "\xfe\xff\x80\x80\x80\x80\x80\x80\x80\x80"
      "\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80"
      "\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80"
      "\x80\x80";
  EXPECT_EQ(32, test_string2.size());
  EXPECT_EQ(
      "\xfe\xff\x80\x80\x80\x80\x80\x80\x80\x80"
      "\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80"
      "\x80\x80\x80\x80\x80\x80\x80\x80"
      "...\n^",
      GetErrorStringWithCaret(test_string2, MakeErrorLocation(1, 1),
                              /*max_width_in=*/31));
  // 30 characters of garbage data, then half a code point.
  std::string test_string3 =
      "\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80"
      "\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80"
      "\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80"
      "\xc3\xb0";
  EXPECT_EQ(test_string3.size(), 32);
  EXPECT_EQ(
      "\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80"
      "\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80"
      "\x80\x80\x80\x80\x80\x80\x80\x80"
      "...\n^",
      GetErrorStringWithCaret(test_string3, MakeErrorLocation(1, 1),
                              /*max_width_in=*/31));

  // 30 characters of garbage data, then half a code point, then lots more
  // garbage.
  std::string test_string4 =
      "\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80"
      "\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80"
      "\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80"
      "\xc3\xb0"
      "\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80";
  EXPECT_EQ(test_string4.size(), 42);
  EXPECT_EQ(
      "\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80"
      "\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80"
      "\x80\x80\x80\x80\x80\x80\x80\x80"
      "...\n^",
      GetErrorStringWithCaret(test_string3, MakeErrorLocation(1, 1),
                              /*max_width_in=*/31));
}

struct UpdateErrorFromPayloadTestCase {
 public:
  UpdateErrorFromPayloadTestCase(
      const std::string& query_in, const absl::Status& status_in,
      const std::map<ErrorMessageMode, std::string>& expected_results_in)
      : query(query_in),
        status(status_in),
        expected_results(expected_results_in) {}
  ~UpdateErrorFromPayloadTestCase() = default;

  std::string query;
  absl::Status status;
  std::map<ErrorMessageMode, std::string> expected_results;
};

// Helper method that runs a list of test cases.  Each test case indicates
// a source query string, a Status, and a map of <ErrorMessageMode, string>.
// Each entry in the map identifies the expected result of calling
// MaybeUpdateErrorFromPayload() for the given mode, followed by FormatError()
// to get a string for the updated Status.
static void RunTests(
    const std::vector<UpdateErrorFromPayloadTestCase> test_cases) {
  for (const UpdateErrorFromPayloadTestCase& test_case : test_cases) {
    for (const auto& map_entry : test_case.expected_results) {
      const ErrorMessageMode mode = map_entry.first;
      const std::string& expected_error_string = map_entry.second;
      absl::Status adjusted_status =
          MaybeUpdateErrorFromPayload(mode, /*keep_error_location_payload=*/
                                      mode == ERROR_MESSAGE_WITH_PAYLOAD,
                                      test_case.query, test_case.status);
      const std::string test_string = absl::StrCat(
          "mode: ", ErrorMessageMode_Name(mode),
          "\ninput status: ", internal::StatusToString(test_case.status),
          "\nexpected_error_string: ", expected_error_string,
          "\ninternal::StatusToString(nadjusted_status): ",
          internal::StatusToString(adjusted_status),
          "\nFormatError(adjusted_status): ", FormatError(adjusted_status));

      // For this test, we do not care about the error message mode payload.
      adjusted_status.ErasePayload(kErrorMessageModeUrl);

      // The adjusted status should match the expected status string.
      EXPECT_EQ(FormatError(adjusted_status), expected_error_string)
          << test_string;

      if (!internal::HasPayload(test_case.status)) {
        EXPECT_FALSE(internal::HasPayload(adjusted_status)) << test_string;
      } else {
        // If the original status has a payload and the mode is
        // ERROR_MESSAGE_WITH_PAYLOAD, so the adjusted status will
        // have also have similar payloads.
        if (mode == ERROR_MESSAGE_WITH_PAYLOAD) {
          ASSERT_TRUE(internal::HasPayload(adjusted_status));
          EXPECT_EQ(
              internal::HasPayloadWithType<ErrorLocation>(test_case.status),
              internal::HasPayloadWithType<ErrorLocation>(adjusted_status))
              << test_string;
        } else {
          // The original status had an ErrorLocation payload and/or another
          // payload.
          if (!internal::HasPayload(adjusted_status)) {
            continue;
          }
          // The mode is not ERROR_MESSAGE_WITH_PAYLOAD, so the adjusted
          // status must not have an ErrorLocation.
          EXPECT_FALSE(
              internal::HasPayloadWithType<ErrorLocation>(adjusted_status));
        }
      }
    }
  }
}

// Tests for MaybeUpdateErrorFromPayload, for an ErrorLocation payload
TEST(ErrorHelpersTest, UpdateErrorFromErrorLocationPayloadTests) {
  // Dummy query for use in these tests. The important bit is that the query
  // has the offsets referenced in the ParseLocationPoints.
  const std::string dummy_query =
      "1234567890123456789_1\n"
      "1234567890123456789_2\n"
      "1234567890123456789_3\n"
      "1234567890123456789_4\n";

  const absl::Status ok;
  for (const ErrorMessageMode mode :
           googlesql_base::EnumerateEnumValues<ErrorMessageMode>()) {
    GOOGLESQL_EXPECT_OK(MaybeUpdateErrorFromPayload(
        static_cast<ErrorMessageMode>(mode), /*keep_error_location_payload=*/
        static_cast<ErrorMessageMode>(mode) == ERROR_MESSAGE_WITH_PAYLOAD,
        dummy_query, ok));
  }
  std::vector<UpdateErrorFromPayloadTestCase> test_cases;

  // Basic error status, with no ErrorLocation.
  // MaybeUpdateErrorFromPayload() has no effect.
  const absl::Status status1 = MakeSqlError() << "Message1";
  const std::string expected_string1 = "Message1";
  EXPECT_EQ(expected_string1, FormatError(status1));
  std::map<ErrorMessageMode, std::string> expected_result;
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_WITH_PAYLOAD,
                          expected_string1);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_ONE_LINE,
                          expected_string1);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
                          expected_string1);
  test_cases.emplace_back(dummy_query, status1, expected_result);

  // Status with InternalErrorLocation is not allowed in this API, resulting
  // in GOOGLESQL_RET_CHECK.
  absl::Status status_with_internal_error_location =
      MakeSqlErrorAtPoint(ParseLocationPoint::FromByteOffset(74)) << "Message2";
  EXPECT_EQ("Message2 [googlesql.InternalErrorLocation] { byte_offset: 74 }",
            FormatError(status_with_internal_error_location));

  // Convert the status InternalErrorLocation to ErrorLocation for this test.
  absl::Status status_with_error_location =
      ConvertInternalErrorPayloadsToExternal(
          status_with_internal_error_location, dummy_query);
  EXPECT_EQ("Message2 [at 4:9]", FormatError(status_with_error_location));
  const std::string expected_string2 = "Message2 [at 4:9]";
  const std::string expected_caret_string2 =
      "Message2 [at 4:9]\n1234567890123456789_4\n        ^";
  expected_result.clear();
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_WITH_PAYLOAD,
                          expected_string2);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_ONE_LINE,
                          expected_string2);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
                          expected_caret_string2);
  test_cases.emplace_back(dummy_query, status_with_error_location,
                          expected_result);

  test_cases.emplace_back(
      dummy_query, status_with_internal_error_location,
      std::map<ErrorMessageMode, std::string>{
          // For internal error locations, the payload is shown as is.
          {ERROR_MESSAGE_WITH_PAYLOAD,
           "Message2 [googlesql.InternalErrorLocation] { byte_offset: 74 }"},
          // For the non-payload applications, internal error locations are
          // first converted to error locations, then formatted, so we expect
          // the same output as when we manually convert.
          {ERROR_MESSAGE_ONE_LINE, expected_string2},
          {ERROR_MESSAGE_MULTI_LINE_WITH_CARET, expected_caret_string2},
      });

  // Error with a googlesql payload but different/invalid code.
  // MaybeUpdateErrorFromPayload() doesn't consider the
  // error code.
  absl::Status status3 =
      ::googlesql_base::UnknownErrorBuilder().AttachPayload(
          ParseLocationPoint::FromByteOffset(3).ToInternalErrorLocation())
      << "Message3";
  EXPECT_EQ("generic::unknown: Message3 "
            "[googlesql.InternalErrorLocation] { byte_offset: 3 }",
            FormatError(status3));
  status3 = ConvertInternalErrorPayloadsToExternal(status3, dummy_query);
  EXPECT_EQ(
      "generic::unknown: Message3 [googlesql.ErrorLocation] "
      "{ line: 1 column: 4 input_start_line_offset: 0 "
      "input_start_column_offset: 0 }",
      FormatError(status3));

  // The byte_offset is 0-based and the column is 1-based, so byte_offset 3
  // is line 1, column 4.
  const std::string expected_payload_string3 =
      "generic::unknown: Message3 [googlesql.ErrorLocation] "
      "{ line: 1 column: 4 input_start_line_offset: 0 "
      "input_start_column_offset: 0 }";
  const std::string expected_oneline_string3 =
      "generic::unknown: Message3 [at 1:4]";
  const std::string expected_caret_string3 =
      "generic::unknown: Message3 [at 1:4]\n1234567890123456789_1\n   ^";
  expected_result.clear();
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_WITH_PAYLOAD,
                          expected_payload_string3);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_ONE_LINE,
                          expected_oneline_string3);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
                          expected_caret_string3);
  test_cases.emplace_back(dummy_query, status3, expected_result);

  // Error with a non-ErrorLocation payload.  Since no location,
  // MaybeUpdateErrorFromPayload() has no affect.
  googlesql_test::TestStatusPayload extra_extension;
  extra_extension.set_value("abc");
  const absl::Status status4 =
      ::googlesql_base::UnknownErrorBuilder().AttachPayload(extra_extension)
      << "Message4";
  const std::string expected_string4 = FormatError(status4);
  expected_result.clear();
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_WITH_PAYLOAD,
                          expected_string4);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_ONE_LINE,
                          expected_string4);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
                          expected_string4);
  test_cases.emplace_back(dummy_query, status4, expected_result);

  // Status with both an ErrorLocation and another payload.
  absl::Status status5 =
      MakeSqlErrorAtPoint(ParseLocationPoint::FromByteOffset(43))
          .AttachPayload(extra_extension)
      << "Message5";
  EXPECT_THAT(
      FormatError(status5),
      AllOf(
          HasSubstr("Message5"),
          HasSubstr("[googlesql.InternalErrorLocation] { byte_offset: 43 }"),
          HasSubstr("[googlesql_test.TestStatusPayload] { value: \"abc\" }")));

  status5 = ConvertInternalErrorPayloadsToExternal(status5, dummy_query);
  EXPECT_EQ(
      "Message5 [at 2:22] "
      "[googlesql_test.TestStatusPayload] { value: \"abc\" }",
      FormatError(status5));

  const std::string expected_payload_string5 =
      "Message5 [at 2:22] [googlesql_test.TestStatusPayload] "
      "{ value: \"abc\" }";
  const std::string expected_oneline_string5 =
      "Message5 [at 2:22] [googlesql_test.TestStatusPayload] "
      "{ value: \"abc\" }";
  const std::string expected_caret_string5 =
      "Message5 [at 2:22]\n1234567890123456789_2\n                     ^\n"
      "[googlesql_test.TestStatusPayload] { value: \"abc\" }";
  expected_result.clear();
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_WITH_PAYLOAD,
                          expected_payload_string5);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_ONE_LINE,
                          expected_oneline_string5);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
                          expected_caret_string5);
  test_cases.emplace_back(dummy_query, status5, expected_result);

  // A status that already has an external error location.
  // MaybeUpdateErrorFromPayload() does not update the location.
  absl::Status status6 =
      MakeSqlErrorAtPoint(ParseLocationPoint::FromByteOffset(43))
          .AttachPayload(extra_extension)
      << "Message6";
  status6 = ConvertInternalErrorPayloadsToExternal(status6, dummy_query);
  const std::string expected_payload_string6 =
      "Message6 [at 2:22] [googlesql_test.TestStatusPayload] "
      "{ value: \"abc\" }";
  const std::string expected_oneline_string6 =
      "Message6 [at 2:22] [googlesql_test.TestStatusPayload] "
      "{ value: \"abc\" }";
  const std::string expected_caret_string6 =
      "Message6 [at 2:22]\n1234567890123456789_2\n                     ^\n"
      "[googlesql_test.TestStatusPayload] { value: \"abc\" }";
  expected_result.clear();
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_WITH_PAYLOAD,
                          expected_payload_string6);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_ONE_LINE,
                          expected_oneline_string6);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
                          expected_caret_string6);
  test_cases.emplace_back(dummy_query, status6, expected_result);

  RunTests(test_cases);
}

TEST(ErrorHelpersTest, ConvertsInternalErrorFixSuggestionsToExternal) {
  const std::string dummy_query =
      "0123\n"
      "456\n"
      "789\n";
  absl::Status status_with_internal_fix_suggestions =
      MakeSqlErrorAtPoint(ParseLocationPoint::FromByteOffset(2))
      << "Error message";
  InternalErrorFixSuggestions internal_fix_suggestions;

  InternalFix* valid_fix = internal_fix_suggestions.add_fix_suggestions();
  valid_fix->set_title("Valid fix");
  InternalTextEdit* edit_1 = valid_fix->mutable_edits()->add_text_edits();
  edit_1->set_new_text("111");
  edit_1->mutable_range()->mutable_start()->set_filename("file_1.sql");
  edit_1->mutable_range()->mutable_start()->set_byte_offset(3);
  edit_1->mutable_range()->set_length(2);
  InternalTextEdit* edit_2 = valid_fix->mutable_edits()->add_text_edits();
  edit_2->set_new_text("222");
  edit_2->mutable_range()->mutable_start()->set_filename("file_2.sql");
  edit_2->mutable_range()->mutable_start()->set_byte_offset(8);
  edit_2->mutable_range()->set_length(1);

  InternalFix* fix_with_missing_edit_range =
      internal_fix_suggestions.add_fix_suggestions();
  fix_with_missing_edit_range->set_title("Fix with missing edit range");
  InternalTextEdit* edit_3 =
      fix_with_missing_edit_range->mutable_edits()->add_text_edits();
  edit_3->set_new_text("333");
  InternalTextEdit* edit_4 =
      fix_with_missing_edit_range->mutable_edits()->add_text_edits();
  edit_4->set_new_text("444");
  edit_4->mutable_range()->mutable_start()->set_filename("file_4.sql");
  edit_4->mutable_range()->mutable_start()->set_byte_offset(0);
  edit_4->mutable_range()->set_length(3);

  InternalFix* fix_with_missing_text_edits =
      internal_fix_suggestions.add_fix_suggestions();
  fix_with_missing_text_edits->set_title("Fix with missing text edits");

  internal::AttachPayload(&status_with_internal_fix_suggestions,
                          internal_fix_suggestions);

  absl::Status status_with_external_fix_suggestions =
      ConvertInternalErrorPayloadsToExternal(
          status_with_internal_fix_suggestions, dummy_query);

  EXPECT_FALSE(internal::HasPayloadWithType<InternalErrorFixSuggestions>(
      status_with_external_fix_suggestions));
  ASSERT_TRUE(internal::HasPayloadWithType<ErrorFixSuggestions>(
      status_with_external_fix_suggestions));
  // Fixes with missing or invalid edit locations are skipped.
  EXPECT_THAT(internal::GetPayload<ErrorFixSuggestions>(
                  status_with_external_fix_suggestions),
              EqualsProto(R"pb(
                fix_suggestions {
                  title: "Valid fix"
                  edits {
                    text_edits {
                      range {
                        start {
                          filename: "file_1.sql"
                          line: 1
                          column: 4
                          input_start_line_offset: 0
                          input_start_column_offset: 0
                        }
                        length: 2
                      }
                      new_text: "111"
                    }
                    text_edits {
                      range {
                        start {
                          filename: "file_2.sql"
                          line: 2
                          column: 4
                          input_start_line_offset: 0
                          input_start_column_offset: 0
                        }
                        length: 1
                      }
                      new_text: "222"
                    }
                  }
                }
              )pb"));
}

TEST(ErrorHelpersTest, BasicErrorSourcePayloadTests) {
  // Basic tests for a Status that does not include an ErrorSource payload.
  ErrorLocation location;
  location.set_filename("location_file");
  location.set_line(2);
  location.set_column(2);

  const std::string source_error_message = "source_error_message";
  const std::string source_caret_string = "caret string 2\n      ^";
  ErrorLocation source_error_location;
  source_error_location.set_filename("source_file");
  source_error_location.set_line(1);
  source_error_location.set_column(7);
  const std::string source_location_string = "[at source_file:1:7]";

  // Basic tests for a Status that includes an ErrorSource payload.
  ErrorSource error_source;
  error_source.set_error_message(source_error_message);
  error_source.set_error_message_caret_string(source_caret_string);
  *error_source.mutable_error_location() = source_error_location;
  *location.add_error_source() = error_source;
  absl::Status status = MakeSqlError().AttachPayload(location) << "Message2";

  error_source.Clear();
  EXPECT_FALSE(error_source.has_error_message());
  EXPECT_FALSE(error_source.has_error_message_caret_string());
  EXPECT_FALSE(error_source.has_error_location());

  ErrorLocation error_location;
  EXPECT_TRUE(HasErrorLocation(status));
  EXPECT_TRUE(GetErrorLocation(status, &error_location));

  ASSERT_EQ(1, error_location.error_source_size());
  error_source = error_location.error_source(0);

  ASSERT_TRUE(error_source.has_error_message());
  EXPECT_EQ(error_source.error_message(), source_error_message);
  ASSERT_TRUE(error_source.has_error_message_caret_string());
  EXPECT_EQ(error_source.error_message_caret_string(), source_caret_string);
  ASSERT_TRUE(error_source.has_error_location());
  EXPECT_EQ(FormatErrorLocation(error_source.error_location()),
            FormatErrorLocation(source_error_location));
}

// Tests for MaybeUpdateErrorFromPayload, for an ErrorSource payload
TEST(ErrorHelpersTest, UpdateErrorFromErrorSourcePayloadTests) {
  const std::string dummy_query =
      "1234567890123456789_1\n"
      "1234567890123456789_2\n"
      "1234567890123456789_3\n"
      "1234567890123456789_4\n";
  const std::string source_dummy_query = "abcdefghijklmnopqrs";

  std::vector<UpdateErrorFromPayloadTestCase> test_cases;

  // Basic error status, with no ErrorSource.
  // MaybeUpdateErrorFromPayload() has no effect.
  absl::Status status = MakeSqlError() << "Message1";
  std::string expected_oneline_string = "Message1";
  // These tests use FormatError() to compare the expected string to
  // the updated status.  If the mode is ERROR_MESSAGE_MODE_WITH_PAYLOAD,
  // the result of FormatError() for a Status with payload is the
  // same as FormatError() on a Status updated for ERROR_MESSAGE_MODE_ONE_LINE
  // mode.
  std::string expected_payload_string = expected_oneline_string;
  std::string expected_caret_string = "Message1";
  EXPECT_EQ(expected_payload_string, FormatError(status));

  std::map<ErrorMessageMode, std::string> expected_result;
  expected_result.clear();
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_WITH_PAYLOAD,
                          expected_payload_string);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_ONE_LINE,
                          expected_oneline_string);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
                          expected_caret_string);
  test_cases.emplace_back(dummy_query, status, expected_result);

  // Basic error status with ErrorLocation, but no ErrorSource in ErrorLocation
  ErrorLocation location;
  location.set_filename("location_file");
  location.set_line(2);
  location.set_column(2);
  status = MakeSqlError().AttachPayload(location) << "Message1b";
  expected_oneline_string = "Message1b [at location_file:2:2]";
  expected_payload_string = expected_oneline_string;
  expected_caret_string =
      "Message1b [at location_file:2:2]\n"
      "1234567890123456789_2\n"
      " ^";
  EXPECT_EQ(expected_oneline_string, FormatError(status));

  expected_result.clear();
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_WITH_PAYLOAD,
                          expected_payload_string);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_ONE_LINE,
                          expected_oneline_string);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
                          expected_caret_string);
  test_cases.emplace_back(dummy_query, status, expected_result);

  // Status with simple ErrorSource and only <error_message>.
  ErrorSource error_source;
  error_source.set_error_message("error_source_message");
  location.clear_error_source();
  *location.add_error_source() = error_source;
  status = MakeSqlError().AttachPayload(location) << "Message2";
  expected_oneline_string =
      "Message2 [at location_file:2:2]; error_source_message";
  expected_payload_string = expected_oneline_string;
  expected_caret_string =
      "Message2 [at location_file:2:2]\n"
      "1234567890123456789_2\n"
      " ^\n"
      "error_source_message";
  EXPECT_EQ(expected_oneline_string, FormatError(status));

  expected_result.clear();
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_WITH_PAYLOAD,
                          expected_payload_string);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_ONE_LINE,
                          expected_oneline_string);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
                          expected_caret_string);
  test_cases.emplace_back(dummy_query, status, expected_result);

  // Status with simple ErrorSource and only <error_message_caret_string>.
  error_source.Clear();
  error_source.set_error_message_caret_string("caret_string");
  location.clear_error_source();
  *location.add_error_source() = error_source;
  status = MakeSqlError().AttachPayload(location) << "Message3";
  expected_oneline_string = "Message3 [at location_file:2:2]";
  expected_payload_string = expected_oneline_string;
  expected_caret_string =
      "Message3 [at location_file:2:2]\n"
      "1234567890123456789_2\n"
      " ^\n"
      "caret_string";
  EXPECT_EQ(expected_oneline_string, FormatError(status));

  expected_result.clear();
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_WITH_PAYLOAD,
                          expected_payload_string);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_ONE_LINE,
                          expected_oneline_string);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
                          expected_caret_string);
  test_cases.emplace_back(dummy_query, status, expected_result);

  // Status with simple ErrorSource and only <error_location>.  Normally, an
  // <error_message> and <error_message_caret_string> would also be present.
  // he <error_location> is not included if there is no error message, since
  // since it only makes sense in the context of an error message.
  error_source.Clear();
  ErrorLocation error_location;
  error_location.set_line(1);
  error_location.set_column(7);
  *error_source.mutable_error_location() = error_location;
  location.clear_error_source();
  *location.add_error_source() = error_source;
  status = MakeSqlError().AttachPayload(location) << "Message4";
  expected_oneline_string = "Message4 [at location_file:2:2]";
  expected_payload_string = expected_oneline_string;
  expected_caret_string =
      "Message4 [at location_file:2:2]\n"
      "1234567890123456789_2\n"
      " ^";
  EXPECT_EQ(expected_oneline_string, FormatError(status));

  expected_result.clear();
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_WITH_PAYLOAD,
                          expected_payload_string);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_ONE_LINE,
                          expected_oneline_string);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
                          expected_caret_string);
  test_cases.emplace_back(dummy_query, status, expected_result);

  // Status with simple ErrorSource, with <error_message_caret_string> and
  // <error_location>.  For ONE_LINE mode, <error_message_caret_string> and
  // <error_location> are ignored.  For CARET mode, the <error_location>
  // is ignored.
  error_location.set_filename("error_filename");
  error_location.set_line(1);
  error_location.set_column(7);
  *error_source.mutable_error_location() = error_location;
  error_source.set_error_message_caret_string("abcdefghijklmnopqrs\n      ^");
  location.clear_error_source();
  *location.add_error_source() = error_source;
  status = MakeSqlError().AttachPayload(location) << "Message5";
  expected_oneline_string = "Message5 [at location_file:2:2]";
  expected_payload_string = expected_oneline_string;
  expected_caret_string =
      "Message5 [at location_file:2:2]\n"
      "1234567890123456789_2\n"
      " ^\n"
      "abcdefghijklmnopqrs\n"
      "      ^";
  EXPECT_EQ(expected_oneline_string, FormatError(status));

  expected_result.clear();
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_WITH_PAYLOAD,
                          expected_payload_string);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_ONE_LINE,
                          expected_oneline_string);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
                          expected_caret_string);
  test_cases.emplace_back(dummy_query, status, expected_result);

  // This is the normal/expected case, where the Status has a simple
  // ErrorSource with all of <error_message>, <error_message_with_caret_string>,
  // and <error_location>.
  //
  // 1) ERROR_MESSAGE_WITH_PAYLOAD - The error payload is left alone on the
  //    status.
  // 2) ERROR_MESSAGE_ONE_LINE - For a status with an ErrorLocation payload,
  //    the error location gets appended to the error message, i.e.,
  //    "<error message> [at 1:4]".  The location is relative to the
  //    query/statement string that provided for resolution.  Nested errors
  //    are appended with their own error location.
  // 3) ERROR_MESSAGE_MULTI_LINE_WITH_CARET - The nested error message is
  //    provided along with the source statement and caret line indicating
  //    the error location.
  error_source.set_error_message("Nested message 6");
  location.clear_error_source();
  *location.add_error_source() = error_source;
  status = MakeSqlError().AttachPayload(location) << "Message6";
  expected_oneline_string =
      "Message6 [at location_file:2:2]; "
      "Nested message 6 [at error_filename:1:7]";
  expected_payload_string = expected_oneline_string;
  expected_caret_string =
      "Message6 [at location_file:2:2]\n"
      "1234567890123456789_2\n"
      " ^\n"
      "Nested message 6 [at error_filename:1:7]\n"
      "abcdefghijklmnopqrs\n"
      "      ^";
  EXPECT_EQ(expected_oneline_string, FormatError(status));

  expected_result.clear();
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_WITH_PAYLOAD,
                          expected_payload_string);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_ONE_LINE,
                          expected_oneline_string);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
                          expected_caret_string);
  test_cases.emplace_back(dummy_query, status, expected_result);

  // Status with an ErrorLocation with an ErrorSource, and another payload.
  error_source.set_error_message("Nested message 7");
  googlesql_test::TestStatusPayload extra_extension;
  extra_extension.set_value("abc");
  location.clear_error_source();
  *location.add_error_source() = error_source;
  status = MakeSqlError().AttachPayload(location).AttachPayload(extra_extension)
           << "Message7";

  expected_oneline_string =
      "Message7 [at location_file:2:2]; "
      "Nested message 7 [at error_filename:1:7] "
      "[googlesql_test.TestStatusPayload] { value: \"abc\" }";

  expected_payload_string = expected_oneline_string;

  expected_caret_string = R"(Message7 [at location_file:2:2]
1234567890123456789_2
 ^
Nested message 7 [at error_filename:1:7]
abcdefghijklmnopqrs
      ^
[googlesql_test.TestStatusPayload] { value: "abc" })";

  EXPECT_EQ(expected_oneline_string, FormatError(status));

  expected_result.clear();
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_WITH_PAYLOAD,
                          expected_payload_string);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_ONE_LINE,
                          expected_oneline_string);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
                          expected_caret_string);
  test_cases.emplace_back(dummy_query, status, expected_result);

  RunTests(test_cases);
}

// Tests for MaybeUpdateErrorFromPayload, for a nested ErrorSource payload
TEST(ErrorHelpersTest, UpdateErrorFromNestedErrorSourcePayloadTests) {
  const std::string dummy_query =
      "1234567890123456789_1\n"
      "1234567890123456789_2\n"
      "1234567890123456789_3\n"
      "1234567890123456789_4\n";
  const std::string caret_string = "abcdefghijklmnopqrs\n  ^";
  const std::string nested_caret_string = "123456\n    ^";

  std::vector<UpdateErrorFromPayloadTestCase> test_cases;

  ErrorSource error_source;
  ErrorLocation error_location;
  error_location.set_filename("filename");
  error_location.set_line(1);
  error_location.set_column(3);
  ErrorSource nested_error_source;
  ErrorLocation nested_error_location;
  nested_error_location.set_filename("nested_filename");
  nested_error_location.set_line(1);
  nested_error_location.set_column(5);

  // Status with an ErrorSource, an ErrorLocation, and another payload.
  nested_error_source.set_error_message("nested_source_error_message");
  nested_error_source.set_error_message_caret_string(nested_caret_string);
  *nested_error_source.mutable_error_location() = nested_error_location;

  error_source.set_error_message("source_error_message");
  error_source.set_error_message_caret_string(caret_string);
  *error_source.mutable_error_location() = error_location;

  InternalErrorLocation location;
  location.set_byte_offset(43);
  // Nested errors are first in the ErrorSource repeated list.
  *location.add_error_source() = nested_error_source;
  *location.add_error_source() = error_source;

  googlesql_test::TestStatusPayload extra_extension;
  extra_extension.set_value("abc");

  absl::Status status =
      MakeSqlError().AttachPayload(location).AttachPayload(extra_extension)
      << "ErrorMessage";
  std::string error_str = FormatError(status);
  EXPECT_THAT(
      FormatError(status),
      AllOf(
          HasSubstr("ErrorMessage"),
          HasSubstr("[googlesql_test.TestStatusPayload] { value: \"abc\" }"),
          HasSubstr("[googlesql.InternalErrorLocation] { "
                    "byte_offset: 43 "
                    "error_source { "
                    "error_message: \"nested_source_error_message\" "
                    "error_message_caret_string: \"123456\\n    ^\" "
                    "error_location { "
                    "line: 1 column: 5 filename: \"nested_filename\" } } "
                    "error_source { "
                    "error_message: \"source_error_message\" "
                    "error_message_caret_string: \"abcdefghijklmnopqrs\\n  ^\" "
                    "error_location { "
                    "line: 1 column: 3 filename: \"filename\" } } }")));
  status = ConvertInternalErrorPayloadsToExternal(status, dummy_query);

  const std::string expected_oneline_string =
      "ErrorMessage [at 2:22]; "
      "source_error_message [at filename:1:3]; "
      "nested_source_error_message [at nested_filename:1:5] "
      "[googlesql_test.TestStatusPayload] { value: \"abc\" }";

  const std::string expected_payload_string = expected_oneline_string;

  const std::string expected_caret_string = R"(ErrorMessage [at 2:22]
1234567890123456789_2
                     ^
source_error_message [at filename:1:3]
abcdefghijklmnopqrs
  ^
nested_source_error_message [at nested_filename:1:5]
123456
    ^
[googlesql_test.TestStatusPayload] { value: "abc" })";

  EXPECT_EQ(expected_oneline_string, FormatError(status));

  std::map<ErrorMessageMode, std::string> expected_result;
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_WITH_PAYLOAD,
                          expected_payload_string);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_ONE_LINE,
                          expected_oneline_string);
  googlesql_base::InsertIfNotPresent(&expected_result, ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
                          expected_caret_string);
  test_cases.emplace_back(dummy_query, status, expected_result);

  RunTests(test_cases);
}

TEST(ErrorHelpersTest, UpdateErrorLocationPayloadWithFilenameIfNotPresentTests)
{
  const std::string dummy_query =
      "1234567890123456789_1\n"
      "1234567890123456789_2\n"
      "1234567890123456789_3\n"
      "1234567890123456789_4\n";
  const std::string caret_string = "abcdefghijklmnopqrs\n  ^";
  const std::string nested_caret_string = "123456\n    ^";

  ErrorLocation error_location;
  error_location.set_filename("filename");
  error_location.set_line(1);
  error_location.set_column(3);

  absl::Status status = MakeSqlError().AttachPayload(error_location)
                        << "ErrorMessage";
  absl::Status updated_status =
      UpdateErrorLocationPayloadWithFilenameIfNotPresent(status,
                                                         "new_filename");
  EXPECT_TRUE(internal::HasPayloadWithType<ErrorLocation>(updated_status));

  ErrorLocation updated_status_location =
      internal::GetPayload<ErrorLocation>(updated_status);

  EXPECT_EQ(error_location.filename(), updated_status_location.filename());

  error_location.clear_filename();
  error_location.set_line(2);
  error_location.set_column(4);

  status = MakeSqlError().AttachPayload(error_location) << "ErrorMessage";
  updated_status =
      UpdateErrorLocationPayloadWithFilenameIfNotPresent(status,
                                                         "new_filename");
  EXPECT_TRUE(internal::HasPayloadWithType<ErrorLocation>(updated_status));

  updated_status_location = internal::GetPayload<ErrorLocation>(updated_status);
  EXPECT_EQ("new_filename", updated_status_location.filename());
}

TEST(ErrorHelpersTest, CanKeepThePayloadAfterUpdatingTheStatus) {
  const std::string dummy_query =
      "1234567890123456789_1\n"
      "1234567890123456789_2\n"
      "1234567890123456789_3\n"
      "1234567890123456789_4\n";

  ErrorLocation error_location;
  error_location.set_filename("filename");
  error_location.set_line(1);
  error_location.set_column(3);

  absl::Status status = MakeSqlError().AttachPayload(error_location)
                        << "ErrorMessage";
  {
    absl::Status updated_status_without_payload = MaybeUpdateErrorFromPayload(
        ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
        /*keep_error_location_payload=*/false, dummy_query, status);
    EXPECT_FALSE(internal::HasPayloadWithType<ErrorLocation>(
        updated_status_without_payload));
    EXPECT_THAT(updated_status_without_payload,
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("ErrorMessage [at filename:1:3]\n"
                                   "1234567890123456789_1\n"
                                   "  ^")));
  }
  {
    absl::Status updated_status_with_payload = MaybeUpdateErrorFromPayload(
        ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
        /*keep_error_location_payload=*/true, dummy_query, status);
    EXPECT_TRUE(internal::HasPayloadWithType<ErrorLocation>(
        updated_status_with_payload));
    EXPECT_THAT(updated_status_with_payload,
                StatusIs(absl::StatusCode::kInvalidArgument,
                         HasSubstr("ErrorMessage [at filename:1:3]\n"
                                   "1234567890123456789_1\n"
                                   "  ^")));
  }
}

TEST(ErrorHelpersTest, UpdateFromPayloadIsIdempotent) {
  const std::string dummy_query =
      "1234567890123456789_1\n"
      "1234567890123456789_2\n"
      "1234567890123456789_3\n"
      "1234567890123456789_4\n";

  ErrorLocation error_location;
  error_location.set_filename("filename");
  error_location.set_line(1);
  error_location.set_column(3);

  absl::Status status = MakeSqlError().AttachPayload(error_location)
                        << "ErrorMessage";
  ASSERT_TRUE(internal::HasPayloadWithType<ErrorLocation>(status));
  ASSERT_FALSE(status.GetPayload(kErrorMessageModeUrl).has_value());

  absl::Status updated_status_with_payload = MaybeUpdateErrorFromPayload(
      ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
      /*keep_error_location_payload=*/true, dummy_query, status);

  EXPECT_THAT(updated_status_with_payload,
              StatusIs(absl::StatusCode::kInvalidArgument,
                       Eq("ErrorMessage [at filename:1:3]\n"
                          "1234567890123456789_1\n"
                          "  ^")));
  EXPECT_TRUE(
      updated_status_with_payload.GetPayload(kErrorMessageModeUrl).has_value());

  EXPECT_THAT(
      MaybeUpdateErrorFromPayload(ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
                                  /*keep_error_location_payload=*/true,
                                  dummy_query, updated_status_with_payload),
      StatusIs(absl::StatusCode::kInvalidArgument,
               Eq("ErrorMessage [at filename:1:3]\n"
                  "1234567890123456789_1\n"
                  "  ^")));

  // Payload mode detects conflict
  EXPECT_THAT(
      MaybeUpdateErrorFromPayload(ERROR_MESSAGE_WITH_PAYLOAD,
                                  /*keep_error_location_payload=*/true,
                                  dummy_query, updated_status_with_payload),
      StatusIs(
          absl::StatusCode::kInternal,
          AllOf(HasSubstr("RET_CHECK failure"),
                HasSubstr("mode_already_applied.mode() == mode (2 vs. 0)"))));

  // Also try with the other caret mode. This is a conflict.
  EXPECT_THAT(
      MaybeUpdateErrorFromPayload(ERROR_MESSAGE_ONE_LINE,
                                  /*keep_error_location_payload=*/true,
                                  dummy_query, updated_status_with_payload),
      StatusIs(
          absl::StatusCode::kInternal,
          AllOf(HasSubstr("RET_CHECK failure"),
                HasSubstr("mode_already_applied.mode() == mode (2 vs. 1)"))));
}

static absl::Status NoError() { return absl::OkStatus(); }

static absl::Status ErrorWithoutLocation() {
  return MakeSqlError() << "No location";
}

static absl::Status ErrorWithLocation() {
  return MakeSqlErrorAtPoint(ParseLocationPoint::FromByteOffset(10))
         << "With location";
}

static absl::Status UpdateWithRedaction(
    const absl::Status& status, absl::string_view query,
    ErrorMessageStability stability = ERROR_MESSAGE_STABILITY_TEST_REDACTED) {
  return MaybeUpdateErrorFromPayload(
      ErrorMessageOptions{
          .mode = ErrorMessageMode::ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
          .attach_error_location_payload = true,
          .stability = stability},
      query, status);
}

static absl::Status UpdateWithEnhancedRedaction(
    const absl::Status& status, absl::string_view query,
    ErrorMessageStability stability = ERROR_MESSAGE_STABILITY_TEST_MINIMIZED) {
  return MaybeUpdateErrorFromPayload(
      ErrorMessageOptions{
          .mode = ErrorMessageMode::ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
          .attach_error_location_payload = true,
          .stability = stability},
      query, status);
}

TEST(ErrorHelpersTest, RedactedModeReturnsRedactedMessages) {
  // Dummy query that can be used to resolve all lines and columns in
  // InternalErrorLocations in this test.
  const std::string query = "1\n2\n3\n42345\n";

  // Error locations also get removed. They are not stable, as we change
  // locations to improve some messages.
  GOOGLESQL_EXPECT_OK(UpdateWithRedaction(NoError(), query));
  EXPECT_THAT(
      UpdateWithRedaction(ErrorWithoutLocation(), query),
      AllOf(StatusIs(absl::StatusCode::kInvalidArgument, Eq("SQL ERROR")),
            Not(StatusHasPayload<InternalErrorLocation>())));
  absl::Status error_with_location = ErrorWithLocation();

  ASSERT_THAT(error_with_location, StatusHasPayload<InternalErrorLocation>());
  EXPECT_THAT(
      UpdateWithRedaction(error_with_location, query),
      AllOf(StatusIs(absl::StatusCode::kInvalidArgument, Eq("SQL ERROR")),
            Not(StatusHasPayload<InternalErrorLocation>())));

  EXPECT_THAT(
      UpdateWithRedaction(
          absl::Status(absl::StatusCode::kNotFound, "Some message"), query),
      AllOf(StatusIs(absl::StatusCode::kNotFound, Eq("SQL ERROR")),
            Not(StatusHasPayload<InternalErrorLocation>())));
}

TEST(Errors, RedactedModeDoesNotHideSystemErrors) {
  // Dummy query that can be used to resolve all lines and columns in
  // InternalErrorLocations in this test.
  const std::string query = "1\n2\n3\n42345\n";

  absl::Status unimplemented_error =
      ::googlesql_base::UnimplementedErrorBuilder().AttachPayload(
          ParseLocationPoint::FromByteOffset(1).ToInternalErrorLocation())
      << "Unimplemented";

  ASSERT_THAT(unimplemented_error, StatusHasPayload<InternalErrorLocation>());
  EXPECT_THAT(UpdateWithRedaction(unimplemented_error, query),
              AllOf(StatusIs(absl::StatusCode::kUnimplemented,
                             Eq("Unimplemented [at 1:2]\n1\n ^")),
                    StatusHasPayload<ErrorLocation>()));

  absl::Status internal_error =
      ::googlesql_base::InternalErrorBuilder().AttachPayload(
          ParseLocationPoint::FromByteOffset(1).ToInternalErrorLocation())
      << "Internal";

  ASSERT_THAT(internal_error, StatusHasPayload<InternalErrorLocation>());
  EXPECT_THAT(UpdateWithRedaction(internal_error, query),
              AllOf(StatusIs(absl::StatusCode::kInternal,
                             Eq("Internal [at 1:2]\n1\n ^")),
                    StatusHasPayload<ErrorLocation>()));
}

static void AttachAllRedactablePaylaods(absl::Status& status) {
  ErrorMessageModeForPayload mode_wrapper;
  mode_wrapper.set_mode(ERROR_MESSAGE_MULTI_LINE_WITH_CARET);
  status.SetPayload(kErrorMessageModeUrl, mode_wrapper.SerializeAsCord());

  ErrorLocation error_location;
  error_location.set_line(1);
  error_location.set_column(2);
  internal::AttachPayload(&status, error_location);

  DeprecationWarning deprecation_warning;
  deprecation_warning.set_kind(DeprecationWarning::DEPRECATED_FUNCTION);
  internal::AttachPayload(&status, deprecation_warning);

  ScriptException script_exception;
  script_exception.set_message("script exception");
  internal::AttachPayload(&status, script_exception);

  ErrorFixSuggestions error_fix_suggestions;
  error_fix_suggestions.add_fix_suggestions()->set_title("fix");
  internal::AttachPayload(&status, error_fix_suggestions);
}

static void AttachExtraPayload(absl::Status& status) {
  googlesql_test::TestStatusPayload extra_payload;
  extra_payload.set_value("abc");
  internal::AttachPayload(&status, extra_payload);
}

TEST(ErrorHelpersTest,
     RedactionRemovesOnlyAndAllRelevantPayloads_NoOtherPayloadsPresent) {
  absl::Status status = MakeSqlError() << "Some error";

  AttachAllRedactablePaylaods(status);

  EXPECT_THAT(
      UpdateWithRedaction(status, /*query=*/""),
      // Note: the message is redacted in the absence of other payloads.
      AllOf(StatusIs(absl::StatusCode::kInvalidArgument, Eq("SQL ERROR")),
            Not(StatusHasPayload<googlesql_test::TestStatusPayload>()),
            Not(StatusHasGenericPayload(kErrorMessageModeUrl)),
            Not(StatusHasPayload<ErrorLocation>()),
            Not(StatusHasPayload<DeprecationWarning>()),
            Not(StatusHasPayload<ScriptException>()),
            Not(StatusHasPayload<ErrorFixSuggestions>())));
}

TEST(ErrorHelpersTest,
     RedactionRemovesOnlyAndAllRelevantPayloads_OtherPayloadsPresent) {
  absl::Status status = MakeSqlError() << "Some error";

  AttachAllRedactablePaylaods(status);
  AttachExtraPayload(status);
  EXPECT_THAT(
      UpdateWithRedaction(status, /*query=*/""),
      // Note: the message is *NOT* redacted because of the extra payload.
      AllOf(StatusIs(absl::StatusCode::kInvalidArgument, Eq("Some error")),
            StatusHasPayload<googlesql_test::TestStatusPayload>(),
            Not(StatusHasGenericPayload(kErrorMessageModeUrl)),
            Not(StatusHasPayload<ErrorLocation>()),
            Not(StatusHasPayload<DeprecationWarning>()),
            Not(StatusHasPayload<ScriptException>()),
            Not(StatusHasPayload<ErrorFixSuggestions>())));
}

TEST(ErrorHelpersTest, RedecatMessageKeepPayload_InternalError) {
  absl::Status status = absl::InternalError("Some error");

  EXPECT_THAT(
      UpdateWithRedaction(status, /*query=*/"",
                          ERROR_MESSAGE_STABILITY_TEST_REDACTED_WITH_PAYLOADS),
      AllOf(StatusIs(absl::StatusCode::kInternal, Eq("Some error"))));
}

TEST(ErrorHelpersTest, RedecatMessageKeepPayload_WithGoogleSQLPayloads) {
  absl::Status status = MakeSqlError() << "Some error";

  AttachAllRedactablePaylaods(status);

  EXPECT_THAT(
      UpdateWithRedaction(status, /*query=*/"",
                          ERROR_MESSAGE_STABILITY_TEST_REDACTED_WITH_PAYLOADS),
      AllOf(StatusIs(absl::StatusCode::kInvalidArgument, Eq("SQL ERROR")),
            StatusHasGenericPayload(kErrorMessageModeUrl),
            StatusHasPayload<ErrorLocation>(),
            StatusHasPayload<DeprecationWarning>(),
            StatusHasPayload<ScriptException>(),
            StatusHasPayload<ErrorFixSuggestions>()));
}

TEST(ErrorHelpersTest, RedecatMessageKeepPayload_WithExtraPayloads) {
  absl::Status status = MakeSqlError() << "Some error";

  AttachAllRedactablePaylaods(status);
  AttachExtraPayload(status);

  EXPECT_THAT(
      UpdateWithRedaction(status, /*query=*/"",
                          ERROR_MESSAGE_STABILITY_TEST_REDACTED_WITH_PAYLOADS),
      // Note: the message is redacted in the absence of external payloads.
      AllOf(StatusIs(absl::StatusCode::kInvalidArgument, Eq("Some error")),
            StatusHasPayload<googlesql_test::TestStatusPayload>(),
            StatusHasGenericPayload(kErrorMessageModeUrl),
            StatusHasPayload<ErrorLocation>(),
            StatusHasPayload<DeprecationWarning>(),
            StatusHasPayload<ScriptException>(),
            StatusHasPayload<ErrorFixSuggestions>()));
}

MATCHER_P2(HasErrorLocationPayload, line, column, "") {
  if (!internal::HasPayloadWithType<ErrorLocation>(arg)) {
    *result_listener << "No ErrorLocation payload";
    return false;
  }

  auto loc = internal::GetPayload<ErrorLocation>(arg);
  return ExplainMatchResult(Eq(""), loc.filename(), result_listener) &&
         ExplainMatchResult(Eq(line), loc.line(), result_listener) &&
         ExplainMatchResult(Eq(column), loc.column(), result_listener);
}

struct CustomOffsetsTestCase {
  int error_byte_offset;
  ErrorMessageMode mode;
  int expected_original_line;
  int expected_original_column;
  absl::string_view expected_message;
};

class ErrorHelpersTestParameterized
    : public ::testing::TestWithParam<CustomOffsetsTestCase> {};

TEST_P(ErrorHelpersTestParameterized,
       ErrorLocationWithCustomLineColumnOffsets) {
  const auto& [error_byte_offset, mode, expected_original_line,
               expected_original_column, expected_message] = GetParam();

  int line_offset = 1000;
  int column_offset = 80;

  const std::string query =
      "abc\n"
      "def\n"
      "ghi";

  absl::Status status =
      MakeSqlErrorAtPoint(ParseLocationPoint::FromByteOffset(error_byte_offset))
      << "Error: some failure";

  status = ConvertInternalErrorPayloadsToExternal(status, query, line_offset,
                                                  column_offset, 0);

  EXPECT_THAT(MaybeUpdateErrorFromPayload(
                  ErrorMessageOptions{.mode = mode,
                                      .attach_error_location_payload = true},
                  query, status),
              AllOf(StatusIs(_, Eq(expected_message)),
                    HasErrorLocationPayload(expected_original_line,
                                            expected_original_column)));
}

INSTANTIATE_TEST_SUITE_P(
    ErrorHelpersTestParameterized, ErrorHelpersTestParameterized,
    ::testing::Values(
        // Error occurs on the first line.
        CustomOffsetsTestCase{
            .error_byte_offset = 2,
            .mode = ErrorMessageMode::ERROR_MESSAGE_WITH_PAYLOAD,
            .expected_original_line = 1,
            .expected_original_column = 3,
            .expected_message = "Error: some failure"},

        // Because the error is on the first line, the column offset is taken
        // into account.
        CustomOffsetsTestCase{
            .error_byte_offset = 2,
            .mode = ErrorMessageMode::ERROR_MESSAGE_ONE_LINE,
            .expected_original_line = 1,
            .expected_original_column = 3,
            .expected_message = "Error: some failure [at 1001:83]"},

        // Because the error is on the first line, the column offset is taken
        // into account. The column offset is 80, already taking up the
        // max_width, forcing the addition of the '...' prefix.
        CustomOffsetsTestCase{
            .error_byte_offset = 2,
            .mode = ErrorMessageMode::ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
            .expected_original_line = 1,
            .expected_original_column = 3,
            .expected_message = "Error: some failure [at 1001:83]\n"
                                "...abc\n"
                                "     ^"},

        // Error occurs on a line that is not the first, so the column offset
        // does not take effect.
        CustomOffsetsTestCase{
            .error_byte_offset = 9,
            .mode = ErrorMessageMode::ERROR_MESSAGE_WITH_PAYLOAD,
            .expected_original_line = 3,
            .expected_original_column = 2,
            .expected_message = "Error: some failure"},
        CustomOffsetsTestCase{
            .error_byte_offset = 9,
            .mode = ErrorMessageMode::ERROR_MESSAGE_ONE_LINE,
            .expected_original_line = 3,
            .expected_original_column = 2,
            .expected_message = "Error: some failure [at 1003:2]"},
        CustomOffsetsTestCase{
            .error_byte_offset = 9,
            .mode = ErrorMessageMode::ERROR_MESSAGE_MULTI_LINE_WITH_CARET,
            .expected_original_line = 3,
            .expected_original_column = 2,
            .expected_message = "Error: some failure [at 1003:2]\n"
                                "ghi\n"
                                " ^"}));

}  // namespace googlesql
