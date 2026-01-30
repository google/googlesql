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

#include "googlesql/common/errors.h"

#include <string>
#include <vector>

#include "googlesql/common/status_payload_utils.h"
#include "googlesql/common/testing/proto_matchers.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/proto/internal_error_location.pb.h"
#include "googlesql/public/deprecation_warning.pb.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/testdata/test_schema.pb.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "googlesql/base/status_builder.h"

namespace googlesql {

using ::googlesql::testing::EqualsProto;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;

static absl::Status NoError() { return absl::OkStatus(); }

static absl::Status ErrorWithoutLocation() {
  return MakeSqlError() << "No location";
}

static absl::Status ErrorWithLocation() {
  return MakeSqlErrorAtPoint(ParseLocationPoint::FromByteOffset(10))
      << "With location";
}

static absl::Status ErrorNonSQL() {
  return ::googlesql_base::NotFoundErrorBuilder() << "Non-SQL error";
}

TEST(Errors, ReturnIf) {
  // Dummy query that can be used to resolve all lines and columns in
  // InternalErrorLocations in this test.
  const std::string query = "1\n2\n3\n42345\n";

  EXPECT_EQ("OK", FormatError(NoError()));
  EXPECT_EQ("No location", FormatError(ErrorWithoutLocation()));
  EXPECT_EQ("With location [at 4:5]",
            FormatError(ConvertInternalErrorPayloadsToExternal(
                ErrorWithLocation(), query)));
  EXPECT_EQ("generic::not_found: Non-SQL error", FormatError(ErrorNonSQL()));
}

static FreestandingDeprecationWarning CreateDeprecationWarning() {
  FreestandingDeprecationWarning warning;
  warning.set_message("foo");
  warning.mutable_deprecation_warning()->set_kind(
      DeprecationWarning::PROTO3_FIELD_PRESENCE);
  ErrorLocation* location = warning.mutable_error_location();
  location->set_line(1);
  location->set_column(5);

  return warning;
}

TEST(DeprecationWarnings, ToDebugString) {
  std::vector<FreestandingDeprecationWarning> warnings;
  EXPECT_THAT(DeprecationWarningsToDebugString(warnings), IsEmpty());

  FreestandingDeprecationWarning warning = CreateDeprecationWarning();

  warnings.push_back(warning);
  EXPECT_EQ(DeprecationWarningsToDebugString(warnings),
            "(1 deprecation warning)");

  warning.set_message(absl::StrCat(warning.message(), "bar"));
  ErrorLocation* location = warning.mutable_error_location();
  location->set_line(location->line() + 1);
  location->set_column(location->column() + 2);
  warnings.push_back(warning);
  EXPECT_EQ(DeprecationWarningsToDebugString(warnings),
            "(2 deprecation warnings)");
}

TEST(DeprecationWarnings, StatusToDeprecationWarning) {
  const std::string sql = "some sql statement";

  const absl::Status uninitialized_status;
  EXPECT_THAT(
      StatusToDeprecationWarning(uninitialized_status, sql),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("INVALID_ARGUMENT")));

  FreestandingDeprecationWarning freestanding_deprecation_warning =
      CreateDeprecationWarning();

  InternalErrorLocation internal_error_location;
  internal_error_location.set_byte_offset(6);

  const absl::Status no_payload_status(
      absl::StatusCode::kInvalidArgument,
      freestanding_deprecation_warning.message());
  EXPECT_THAT(
      StatusToDeprecationWarning(no_payload_status, sql),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("must have payloads")));

  absl::Status missing_deprecation_warning = no_payload_status;
  internal::AttachPayload(&missing_deprecation_warning,
                          freestanding_deprecation_warning.error_location());
  EXPECT_THAT(StatusToDeprecationWarning(missing_deprecation_warning, sql),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("DeprecationWarning payloads")));

  absl::Status missing_error_location = no_payload_status;
  internal::AttachPayload(
      &missing_error_location,
      freestanding_deprecation_warning.deprecation_warning());
  EXPECT_THAT(StatusToDeprecationWarning(missing_error_location, sql),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("ErrorLocation payloads")));

  absl::Status internal_error_location_instead_of_error_location =
      missing_error_location;
  internal::AttachPayload(&internal_error_location_instead_of_error_location,
                          internal_error_location);
  EXPECT_THAT(StatusToDeprecationWarning(
                  internal_error_location_instead_of_error_location, sql),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("InternalErrorLocation payloads")));

  absl::Status internal_error_location_and_error_location =
      missing_error_location;
  internal::AttachPayload(&internal_error_location_and_error_location,
                          freestanding_deprecation_warning.error_location());
  internal::AttachPayload(&internal_error_location_and_error_location,
                          internal_error_location);
  EXPECT_THAT(StatusToDeprecationWarning(
                  internal_error_location_and_error_location, sql),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("InternalErrorLocation payloads")));

  FreestandingDeprecationWarning expected_warning =
      freestanding_deprecation_warning;
  expected_warning.set_caret_string("some sql statement\n    ^");

  absl::Status correct_deprecation_status = missing_error_location;
  internal::AttachPayload(&correct_deprecation_status,
                          freestanding_deprecation_warning.error_location());
  EXPECT_THAT(StatusToDeprecationWarning(correct_deprecation_status, sql),
              IsOkAndHolds(EqualsProto(expected_warning)));

  googlesql_test::TestStatusPayload extra_payload;
  extra_payload.set_value("extra");

  absl::Status extra_payload_status = correct_deprecation_status;
  internal::AttachPayload(&extra_payload_status, extra_payload);
  EXPECT_THAT(
      StatusToDeprecationWarning(extra_payload_status, sql),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("extra payload")));
}

TEST(Errors, FirstCharLower) {
  EXPECT_EQ("", FirstCharLower(""));
  EXPECT_EQ("abc", FirstCharLower("abc"));
  EXPECT_EQ("abc", FirstCharLower("Abc"));
  // This looks like a keyword, so don't lowercase part of it.
  EXPECT_EQ("ABC", FirstCharLower("ABC"));
}

}  // namespace googlesql
