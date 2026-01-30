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

#include "googlesql/common/resolution_scope.h"

#include <memory>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/parser/parser.h"
#include "googlesql/public/language_options.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"

using absl::StatusCode::kInvalidArgument;
using absl_testing::IsOkAndHolds;
using absl_testing::StatusIs;

namespace googlesql {

class ResolutionScopeTest : public ::testing::Test {
 protected:
  std::unique_ptr<ParserOutput> parser_output_;
  const ASTStatement* stmt_;

  void ParseTestStatement(absl::string_view statement) {
    LanguageOptions language_options;
    language_options.EnableMaximumLanguageFeatures();
    GOOGLESQL_ASSERT_OK(ParseStatement(statement, ParserOptions(language_options),
                             &parser_output_));
    stmt_ = parser_output_->statement();
  }
};

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionNoOptions) {
  ParseTestStatement("CREATE FUNCTION f() AS (1)");
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kBuiltin),
              IsOkAndHolds(ResolutionScope::kBuiltin));
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kGlobal),
              IsOkAndHolds(ResolutionScope::kGlobal));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionTvfNoOptions) {
  ParseTestStatement("CREATE TABLE FUNCTION f() AS (SELECT 1)");
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kBuiltin),
              IsOkAndHolds(ResolutionScope::kBuiltin));
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kGlobal),
              IsOkAndHolds(ResolutionScope::kGlobal));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionViewNoOptions) {
  ParseTestStatement("CREATE VIEW v AS (SELECT 1)");
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kBuiltin),
              IsOkAndHolds(ResolutionScope::kBuiltin));
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kGlobal),
              IsOkAndHolds(ResolutionScope::kGlobal));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionProcedureNoOptions) {
  ParseTestStatement("CREATE PROCEDURE p() BEGIN END");
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kBuiltin),
              IsOkAndHolds(ResolutionScope::kBuiltin));
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kGlobal),
              IsOkAndHolds(ResolutionScope::kGlobal));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionUnrelatedOption) {
  ParseTestStatement(
      "CREATE FUNCTION f() OPTIONS (unrelated_opt = 'builtin') AS (1)");
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kBuiltin),
              IsOkAndHolds(ResolutionScope::kBuiltin));
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kGlobal),
              IsOkAndHolds(ResolutionScope::kGlobal));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionTvfUnrelatedOption) {
  ParseTestStatement(
      "CREATE TABLE FUNCTION f() OPTIONS (unrelated_opt = 'builtin') AS "
      "(SELECT 1)");
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kBuiltin),
              IsOkAndHolds(ResolutionScope::kBuiltin));
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kGlobal),
              IsOkAndHolds(ResolutionScope::kGlobal));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionViewUnrelatedOption) {
  ParseTestStatement(
      "CREATE VIEW v OPTIONS (unrelated_opt = 'builtin') AS (SELECT 1)");
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kBuiltin),
              IsOkAndHolds(ResolutionScope::kBuiltin));
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kGlobal),
              IsOkAndHolds(ResolutionScope::kGlobal));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionProcedureUnrelatedOption) {
  ParseTestStatement(
      "CREATE PROCEDURE p() OPTIONS (unrelated_opt = 'builtin') BEGIN END");
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kBuiltin),
              IsOkAndHolds(ResolutionScope::kBuiltin));
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kGlobal),
              IsOkAndHolds(ResolutionScope::kGlobal));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionBuiltin) {
  ParseTestStatement(
      "CREATE FUNCTION f() OPTIONS (allowed_references = 'builtin') AS (1)");
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kGlobal),
              IsOkAndHolds(ResolutionScope::kBuiltin));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionTvfBuiltin) {
  ParseTestStatement(
      "CREATE TABLE FUNCTION f() OPTIONS (allowed_references = 'builtin') AS "
      "(SELECT 1)");
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kGlobal),
              IsOkAndHolds(ResolutionScope::kBuiltin));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionViewBuiltin) {
  ParseTestStatement(
      "CREATE VIEW v OPTIONS (allowed_references = 'builtin') AS (SELECT 1)");
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kGlobal),
              IsOkAndHolds(ResolutionScope::kBuiltin));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionProcedureBuiltin) {
  ParseTestStatement(
      "CREATE PROCEDURE p() OPTIONS (allowed_references = 'builtin') BEGIN "
      "END");
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kGlobal),
              IsOkAndHolds(ResolutionScope::kBuiltin));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionGlobal) {
  ParseTestStatement(
      "CREATE FUNCTION f() OPTIONS (allowed_references = 'global') AS (1)");
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kBuiltin),
              IsOkAndHolds(ResolutionScope::kGlobal));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionTvfGlobal) {
  ParseTestStatement(
      "CREATE TABLE FUNCTION f() OPTIONS (allowed_references = 'global') AS "
      "(SELECT 1)");
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kBuiltin),
              IsOkAndHolds(ResolutionScope::kGlobal));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionViewGlobal) {
  ParseTestStatement(
      "CREATE VIEW v OPTIONS (allowed_references = 'global') AS (SELECT 1)");
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kBuiltin),
              IsOkAndHolds(ResolutionScope::kGlobal));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionProcedureGlobal) {
  ParseTestStatement(
      "CREATE PROCEDURE p() OPTIONS (allowed_references = 'global') BEGIN "
      "END");
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kBuiltin),
              IsOkAndHolds(ResolutionScope::kGlobal));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionMixedCase) {
  ParseTestStatement(
      "CREATE FUNCTION f() OPTIONS (aLLoweD_reFereNceS = 'gLoBaL') AS (1)");
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kBuiltin),
              IsOkAndHolds(ResolutionScope::kGlobal));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionTvfMixedCase) {
  ParseTestStatement(
      "CREATE TABLE FUNCTION f() OPTIONS (aLLoweD_reFereNceS = 'gLoBaL') AS "
      "(SELECT 1)");
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kBuiltin),
              IsOkAndHolds(ResolutionScope::kGlobal));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionViewMixedCase) {
  ParseTestStatement(
      "CREATE VIEW v OPTIONS (aLLoweD_reFereNceS = 'gLoBaL') AS (SELECT 1)");
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kBuiltin),
              IsOkAndHolds(ResolutionScope::kGlobal));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionProcedureMixedCase) {
  ParseTestStatement(
      "CREATE PROCEDURE p() OPTIONS (aLLoweD_reFereNceS = 'gLoBaL') BEGIN "
      "END");
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kBuiltin),
              IsOkAndHolds(ResolutionScope::kGlobal));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionDuplicate) {
  ParseTestStatement(
      "CREATE FUNCTION f() OPTIONS (allowed_references = 'global', "
      "allowed_references = 'global') AS (1)");
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kBuiltin),
              StatusIs(kInvalidArgument,
                       "Option allowed_references can only occur once"));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionViewDuplicate) {
  ParseTestStatement(
      "CREATE VIEW v OPTIONS (allowed_references = 'global', "
      "allowed_references = 'global') AS (SELECT 1)");
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kBuiltin),
              StatusIs(kInvalidArgument,
                       "Option allowed_references can only occur once"));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionProcedureDuplicate) {
  ParseTestStatement(
      "CREATE PROCEDURE p() OPTIONS (allowed_references = 'global', "
      "allowed_references = 'global') BEGIN END");
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kBuiltin),
              StatusIs(kInvalidArgument,
                       "Option allowed_references can only occur once"));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionNotString) {
  ParseTestStatement(
      "CREATE FUNCTION f() OPTIONS (allowed_references = 3) AS (1)");
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kBuiltin),
              StatusIs(kInvalidArgument,
                       "Option allowed_references must be a string literal"));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionViewNotString) {
  ParseTestStatement(
      "CREATE VIEW v OPTIONS (allowed_references = 3) AS (SELECT 1)");
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kBuiltin),
              StatusIs(kInvalidArgument,
                       "Option allowed_references must be a string literal"));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionProcedureNotString) {
  ParseTestStatement(
      "CREATE PROCEDURE p() OPTIONS (allowed_references = 3) BEGIN END");
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kBuiltin),
              StatusIs(kInvalidArgument,
                       "Option allowed_references must be a string literal"));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionNotLiteral) {
  ParseTestStatement(
      "CREATE FUNCTION f() OPTIONS (allowed_references = CONCAT('glob', 'al')) "
      "AS (1)");
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kBuiltin),
              StatusIs(kInvalidArgument,
                       "Option allowed_references must be a string literal"));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionViewNotLiteral) {
  ParseTestStatement(
      "CREATE VIEW v OPTIONS (allowed_references = CONCAT('glob', 'al')) "
      "AS (SELECT 1)");
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kBuiltin),
              StatusIs(kInvalidArgument,
                       "Option allowed_references must be a string literal"));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionProcedureNotLiteral) {
  ParseTestStatement(
      "CREATE PROCEDURE p() OPTIONS (allowed_references = CONCAT('glob', "
      "'al')) "
      "BEGIN END");
  EXPECT_THAT(GetResolutionScopeOption(stmt_, ResolutionScope::kBuiltin),
              StatusIs(kInvalidArgument,
                       "Option allowed_references must be a string literal"));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionInvalid) {
  ParseTestStatement(
      "CREATE FUNCTION f() OPTIONS (allowed_references = 'notvalid') "
      "AS (1)");
  EXPECT_THAT(
      GetResolutionScopeOption(stmt_, ResolutionScope::kBuiltin),
      StatusIs(
          kInvalidArgument,
          "Option allowed_references must be one of 'builtin' or 'global'"));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionViewInvalid) {
  ParseTestStatement(
      "CREATE VIEW v OPTIONS (allowed_references = 'notvalid') "
      "AS (SELECT 1)");
  EXPECT_THAT(
      GetResolutionScopeOption(stmt_, ResolutionScope::kBuiltin),
      StatusIs(
          kInvalidArgument,
          "Option allowed_references must be one of 'builtin' or 'global'"));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionProcedureInvalid) {
  ParseTestStatement(
      "CREATE PROCEDURE p() OPTIONS (allowed_references = 'notvalid') BEGIN "
      "END");
  EXPECT_THAT(
      GetResolutionScopeOption(stmt_, ResolutionScope::kBuiltin),
      StatusIs(
          kInvalidArgument,
          "Option allowed_references must be one of 'builtin' or 'global'"));
}

TEST_F(ResolutionScopeTest, GetResolutionScopeOptionUnsupportedStatementType) {
  ParseTestStatement("CREATE MATERIALIZED VIEW v AS (SELECT 1)");
  EXPECT_THAT(
      GetResolutionScopeOption(stmt_, ResolutionScope::kBuiltin),
      StatusIs(absl::StatusCode::kInternal,
               testing::HasSubstr(
                   "Unsupported node kind: CreateMaterializedViewStatement")));
}

}  // namespace googlesql
