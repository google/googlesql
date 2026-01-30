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

#include "googlesql/parser/macros/macro_catalog.h"

#include <memory>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/parser/parse_tree.h"
#include "googlesql/parser/parser.h"
#include "googlesql/parser/parser_mode.h"
#include "googlesql/public/language_options.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {
namespace parser {
namespace macros {

using ::absl::StatusCode;
using ::testing::HasSubstr;
using ::testing::Optional;
using ::absl_testing::StatusIs;

absl::StatusOr<MacroInfo> CreateMacroInfo(absl::string_view macro_definition) {
  std::unique_ptr<ParserOutput> parser_output;
  ParserOptions parser_options(LanguageOptions(), MacroExpansionMode::kStrict,
                               /*macro_catalog=*/nullptr);

  GOOGLESQL_RETURN_IF_ERROR(
      ParseStatement(macro_definition, parser_options, &parser_output));
  if (parser_output->statement() == nullptr ||
      !parser_output->statement()->Is<ASTDefineMacroStatement>()) {
    return absl::InvalidArgumentError(
        "Macro definition supplied in not a valid DEFINE MACRO statement");
  }

  const ASTDefineMacroStatement* define_macro_ast =
      parser_output->statement()->GetAsOrDie<ASTDefineMacroStatement>();
  return MacroInfo{
      .source_text = macro_definition,
      .location = define_macro_ast->location(),
      .name_location = define_macro_ast->name()->location(),
      .body_location = define_macro_ast->body()->location(),
  };
}

TEST(MacroCatalogTest, MacroInfoNameAndBody) {
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const MacroInfo& macro,
                       CreateMacroInfo("DEFINE MACRO macro_name macro_body;"));

  EXPECT_EQ(macro.name(), "macro_name");
  EXPECT_EQ(macro.body(), "macro_body");
}

TEST(MacroCatalogTest, RegisterMacroWithOverwritesDisabled) {
  MacroCatalog macro_catalog({.allow_overwrite = false});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const MacroInfo& old_macro,
                       CreateMacroInfo("DEFINE MACRO macro_name old_body;"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const MacroInfo& new_macro,
                       CreateMacroInfo("DEFINE MACRO macro_name new_body;"));
  GOOGLESQL_ASSERT_OK(macro_catalog.RegisterMacro(old_macro));

  EXPECT_THAT(macro_catalog.RegisterMacro(new_macro),
              StatusIs(StatusCode::kAlreadyExists, HasSubstr("macro_name")));
  EXPECT_THAT(macro_catalog.Find("macro_name"), Optional(old_macro));
}

TEST(MacroCatalogTest, RegisterMacroWithOverwritesEnabled) {
  MacroCatalog macro_catalog({.allow_overwrite = true});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const MacroInfo& old_macro,
                       CreateMacroInfo("DEFINE MACRO macro_name old_body;"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const MacroInfo& new_macro,
                       CreateMacroInfo("DEFINE MACRO macro_name new_body;"));
  GOOGLESQL_ASSERT_OK(macro_catalog.RegisterMacro(old_macro));

  GOOGLESQL_EXPECT_OK(macro_catalog.RegisterMacro(new_macro));
  EXPECT_THAT(macro_catalog.Find("macro_name"), Optional(new_macro));
}

TEST(MacroCatalogTest, MacroVersioningUponRedefinition) {
  MacroCatalog macro_catalog_1({.allow_overwrite = true});
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const MacroInfo& old_macro,
                       CreateMacroInfo("DEFINE MACRO macro_name old_body;"));
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(
      const MacroInfo& unchanged_macro,
      CreateMacroInfo("DEFINE MACRO unchanged unchanged_body;"));
  GOOGLESQL_ASSERT_OK(macro_catalog_1.RegisterMacro(old_macro));
  GOOGLESQL_ASSERT_OK(macro_catalog_1.RegisterMacro(unchanged_macro));
  EXPECT_THAT(macro_catalog_1.Find("macro_name"), Optional(old_macro));
  EXPECT_THAT(macro_catalog_1.Find("unchanged"), Optional(unchanged_macro));

  GOOGLESQL_ASSERT_OK_AND_ASSIGN(const MacroInfo& new_macro,
                       CreateMacroInfo("DEFINE MACRO macro_name new_body;"));
  std::unique_ptr<MacroCatalog> macro_catalog_2 = macro_catalog_1.NewVersion();
  GOOGLESQL_ASSERT_OK(macro_catalog_2->RegisterMacro(new_macro));
  EXPECT_THAT(macro_catalog_2->Find("macro_name"), Optional(new_macro));
  EXPECT_THAT(macro_catalog_2->Find("unchanged"), Optional(unchanged_macro));
}

}  // namespace macros
}  // namespace parser
}  // namespace googlesql
