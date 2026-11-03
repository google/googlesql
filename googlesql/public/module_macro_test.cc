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

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/function.h"
#include "googlesql/public/module_factory.h"
#include "googlesql/public/modules.h"
#include "googlesql/public/parse_location.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/sql_function.h"
#include "googlesql/public/sql_tvf.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/types/type_factory.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/testing/test_module_contents_fetcher.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::TestParamInfo;
using ::testing::ValuesIn;
using ::testing::WithParamInterface;
using ::absl_testing::StatusIs;

class ModuleMacroTest : public ::testing::Test {
 protected:
  ModuleMacroTest() : builtin_catalog_("builtin", &type_factory_) {
    analyzer_options_.mutable_language()->SetSupportsAllStatementKinds();
    analyzer_options_.mutable_language()->EnableLanguageFeature(
        FEATURE_EXPERIMENTAL_MODULES);
    analyzer_options_.mutable_language()->EnableLanguageFeature(
        FEATURE_CREATE_TABLE_FUNCTION);
  }

  absl::StatusOr<ModuleCatalog*> LoadModule(absl::string_view module_name,
                                            absl::string_view module_contents,
                                            ModuleFactoryOptions options) {
    auto fetcher_ptr = std::make_unique<testing::TestModuleContentsFetcher>(
        /*descriptor_pool=*/nullptr, /*source_directory=*/"");
    if (absl::Status s = fetcher_ptr->AddInMemoryModule(
            {std::string(module_name)}, module_contents);
        !s.ok()) {
      return s;
    }
    factory_ = std::make_unique<ModuleFactory>(
        analyzer_options_, options, std::move(fetcher_ptr), &builtin_catalog_,
        &type_factory_);
    ModuleCatalog* catalog = nullptr;
    if (absl::Status s = factory_->CreateOrReturnModuleCatalog(
            {std::string(module_name)}, &catalog);
        !s.ok()) {
      return s;
    }
    return catalog;
  }

  TypeFactory type_factory_;
  AnalyzerOptions analyzer_options_;
  SimpleCatalog builtin_catalog_;
  std::unique_ptr<ModuleFactory> factory_;
};

TEST_F(ModuleMacroTest, AllowMacrosInModuleFlagTrue) {
  std::string module_contents =
      "MODULE m1;\n"
      "DEFINE PUBLIC MACRO foo '1';\n";

  ModuleFactoryOptions options;
  options.allow_macros_in_module = true;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ModuleCatalog * catalog,
                       LoadModule("m1", module_contents, options));

  EXPECT_TRUE(catalog->module_errors().empty());
  const Macro* macro = nullptr;
  GOOGLESQL_ASSERT_OK(catalog->GetMacro("foo", &macro));
  ASSERT_NE(macro, nullptr);
  EXPECT_EQ(macro->source_text(), module_contents);
  EXPECT_EQ(macro->location(),
            ParseLocationRange(ParseLocationPoint::FromByteOffset("m1", 11),
                               ParseLocationPoint::FromByteOffset("m1", 38)));
  EXPECT_EQ(macro->name_location(),
            ParseLocationRange(ParseLocationPoint::FromByteOffset("m1", 31),
                               ParseLocationPoint::FromByteOffset("m1", 34)));
  EXPECT_EQ(macro->body_location(),
            ParseLocationRange(ParseLocationPoint::FromByteOffset("m1", 35),
                               ParseLocationPoint::FromByteOffset("m1", 38)));
  EXPECT_EQ(macro->definition_start_offset(), 11);
  EXPECT_EQ(macro->definition_start_line(), 1);
  EXPECT_EQ(macro->definition_start_column(), 1);
}

TEST_F(ModuleMacroTest, AllowMacrosInModuleFlagFalse) {
  std::string module_contents =
      "MODULE m1;\n"
      "DEFINE PUBLIC MACRO foo '1';\n";

  ModuleFactoryOptions options;
  options.allow_macros_in_module = false;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ModuleCatalog * catalog,
                       LoadModule("m1", module_contents, options));

  // It should have an error about unsupported statement kind for DEFINE MACRO
  EXPECT_THAT(catalog->module_errors(),
              ::testing::Contains(StatusIs(
                  absl::StatusCode::kInvalidArgument,
                  HasSubstr("DEFINE MACRO statements are not supported"))));
  const Macro* macro = nullptr;
  GOOGLESQL_ASSERT_OK(catalog->GetMacro("foo", &macro));
  EXPECT_EQ(macro, nullptr);
}

TEST_F(ModuleMacroTest, MacrosUsableInFunctions) {
  std::string module_contents =
      "MODULE m1;\n"
      "DEFINE PUBLIC MACRO foo 1;\n"
      "DEFINE PRIVATE MACRO bar 2;\n"
      "CREATE PUBLIC FUNCTION function1() AS ($foo());\n"
      "CREATE PUBLIC FUNCTION function2() AS ($bar());\n";

  ModuleFactoryOptions options;
  options.allow_macros_in_module = true;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ModuleCatalog * catalog,
                       LoadModule("m1", module_contents, options));

  EXPECT_TRUE(catalog->module_errors().empty());
  const Macro* macro_foo = nullptr;
  GOOGLESQL_ASSERT_OK(catalog->GetMacro("foo", &macro_foo));
  EXPECT_NE(macro_foo, nullptr);
  const Macro* macro_bar = nullptr;
  GOOGLESQL_ASSERT_OK(catalog->GetMacro("bar", &macro_bar));
  EXPECT_EQ(macro_bar, nullptr);

  const Function* function1 = nullptr;
  GOOGLESQL_ASSERT_OK(catalog->FindFunction({"function1"}, &function1));
  ASSERT_NE(function1, nullptr);
  const Function* function2 = nullptr;
  GOOGLESQL_ASSERT_OK(catalog->FindFunction({"function2"}, &function2));
  ASSERT_NE(function2, nullptr);

  ASSERT_TRUE(function1->Is<SQLFunction>());
  const SQLFunction* sql_function1 = function1->GetAs<SQLFunction>();
  ASSERT_NE(sql_function1->FunctionExpression(), nullptr);
  ASSERT_EQ(sql_function1->FunctionExpression()->node_kind(), RESOLVED_LITERAL);
  EXPECT_EQ(
      sql_function1->FunctionExpression()->GetAs<ResolvedLiteral>()->value(),
      Value::Int64(1));

  ASSERT_TRUE(function2->Is<SQLFunction>());
  const SQLFunction* sql_function2 = function2->GetAs<SQLFunction>();
  ASSERT_NE(sql_function2->FunctionExpression(), nullptr);
  ASSERT_EQ(sql_function2->FunctionExpression()->node_kind(), RESOLVED_LITERAL);
  EXPECT_EQ(
      sql_function2->FunctionExpression()->GetAs<ResolvedLiteral>()->value(),
      Value::Int64(2));
}

TEST_F(ModuleMacroTest, UnspecifiedVisibilityError) {
  std::string module_contents =
      "MODULE m1;\n"
      "DEFINE MACRO foo '1';\n";

  ModuleFactoryOptions options;
  options.allow_macros_in_module = true;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ModuleCatalog * catalog,
                       LoadModule("m1", module_contents, options));

  EXPECT_THAT(
      catalog->module_errors(),
      ::testing::Contains(StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("DEFINE MACRO statements inside modules require an "
                    "explicit PUBLIC or PRIVATE visibility modifier"))));
  const Macro* macro = nullptr;
  GOOGLESQL_ASSERT_OK(catalog->GetMacro("foo", &macro));
  EXPECT_EQ(macro, nullptr);
}

TEST_F(ModuleMacroTest, DuplicateMacroDefinitionError) {
  std::string module_contents =
      "MODULE m1;\n"
      "DEFINE PUBLIC MACRO foo 1;\n"
      "DEFINE PRIVATE MACRO foo 2;\n";

  ModuleFactoryOptions options;
  options.allow_macros_in_module = true;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ModuleCatalog * catalog,
                       LoadModule("m1", module_contents, options));

  EXPECT_THAT(
      catalog->module_errors(),
      ::testing::Contains(StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr(
              "Macros must have unique names, but found duplicate name foo"))));
  const Macro* macro = nullptr;
  GOOGLESQL_ASSERT_OK(catalog->GetMacro("foo", &macro));
  ASSERT_NE(macro, nullptr);
  EXPECT_EQ(macro->body(), "1");
}

TEST_F(ModuleMacroTest, ChainedMacroExpansionWithinModule) {
  std::string module_contents =
      "MODULE m1;\n"
      "DEFINE PUBLIC MACRO a 1;\n"
      "DEFINE PRIVATE MACRO b $a();\n"
      "CREATE PUBLIC FUNCTION fn() AS ($b());\n";

  ModuleFactoryOptions options;
  options.allow_macros_in_module = true;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ModuleCatalog * catalog,
                       LoadModule("m1", module_contents, options));

  EXPECT_TRUE(catalog->module_errors().empty());
  const Function* fn = nullptr;
  GOOGLESQL_ASSERT_OK(catalog->FindFunction({"fn"}, &fn));
  ASSERT_NE(fn, nullptr);
  ASSERT_TRUE(fn->Is<SQLFunction>());
  const SQLFunction* sql_fn = fn->GetAs<SQLFunction>();
  ASSERT_NE(sql_fn->FunctionExpression(), nullptr);
  ASSERT_EQ(sql_fn->FunctionExpression()->node_kind(), RESOLVED_LITERAL);
  EXPECT_EQ(sql_fn->FunctionExpression()->GetAs<ResolvedLiteral>()->value(),
            Value::Int64(1));
}

TEST_F(ModuleMacroTest, MacroForwardReferenceFailsAtParseTime) {
  std::string module_contents =
      "MODULE m1;\n"
      "CREATE PUBLIC FUNCTION fn() AS ($foo());\n"
      "DEFINE PUBLIC MACRO foo 1;\n";

  ModuleFactoryOptions options;
  options.allow_macros_in_module = true;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ModuleCatalog * catalog,
                       LoadModule("m1", module_contents, options));

  EXPECT_THAT(
      catalog->module_errors(),
      ::testing::Contains(StatusIs(absl::StatusCode::kInvalidArgument,
                                   HasSubstr("Macro 'foo' not found"))));
  const Function* fn = nullptr;
  EXPECT_THAT(catalog->FindFunction({"fn"}, &fn),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_EQ(fn, nullptr);
}

TEST_F(ModuleMacroTest, MacrosUsableInTableFunctions) {
  std::string module_contents =
      "MODULE m1;\n"
      "DEFINE PUBLIC MACRO col_val 42;\n"
      "CREATE PUBLIC TABLE FUNCTION tvf() AS (SELECT $col_val() AS c);\n";

  ModuleFactoryOptions options;
  options.allow_macros_in_module = true;
  GOOGLESQL_ASSERT_OK_AND_ASSIGN(ModuleCatalog * catalog,
                       LoadModule("m1", module_contents, options));

  EXPECT_TRUE(catalog->module_errors().empty());
  const TableValuedFunction* tvf = nullptr;
  GOOGLESQL_ASSERT_OK(catalog->FindTableValuedFunction({"tvf"}, &tvf));
  ASSERT_NE(tvf, nullptr);
  ASSERT_TRUE(tvf->Is<SQLTableValuedFunction>());
  const SQLTableValuedFunction* sql_tvf = tvf->GetAs<SQLTableValuedFunction>();
  ASSERT_NE(sql_tvf->ResolvedStatement(), nullptr);
  ASSERT_EQ(sql_tvf->ResolvedStatement()->output_column_list_size(), 1);
}

class ModuleCatalogTestHelper : public ModuleCatalog {
 public:
  using ModuleCatalog::GetCatalog;
};

TEST_F(ModuleMacroTest, CrossModuleMacroCatalogLookup) {
  auto fetcher_ptr = std::make_unique<testing::TestModuleContentsFetcher>(
      /*descriptor_pool=*/nullptr, /*source_directory=*/"");
  GOOGLESQL_ASSERT_OK(
      fetcher_ptr->AddInMemoryModule({"m1"},
                                     "MODULE m1;\n"
                                     "DEFINE PUBLIC MACRO pub_macro 10;\n"
                                     "DEFINE PRIVATE MACRO priv_macro 20;\n"));
  GOOGLESQL_ASSERT_OK(
      fetcher_ptr->AddInMemoryModule({"m2"},
                                     "MODULE m2;\n"
                                     "IMPORT MODULE m1 AS imported_m1;\n"));

  ModuleFactoryOptions options;
  options.allow_macros_in_module = true;
  factory_ = std::make_unique<ModuleFactory>(analyzer_options_, options,
                                             std::move(fetcher_ptr),
                                             &builtin_catalog_, &type_factory_);
  ModuleCatalog* m2_catalog = nullptr;
  GOOGLESQL_ASSERT_OK(factory_->CreateOrReturnModuleCatalog({"m2"}, &m2_catalog));
  ASSERT_NE(m2_catalog, nullptr);
  EXPECT_TRUE(m2_catalog->module_errors().empty());

  Catalog* imported_m1_catalog = nullptr;
  GOOGLESQL_ASSERT_OK(static_cast<ModuleCatalogTestHelper*>(m2_catalog)
                ->GetCatalog("imported_m1", &imported_m1_catalog));
  ASSERT_NE(imported_m1_catalog, nullptr);

  const Macro* pub_macro = nullptr;
  GOOGLESQL_ASSERT_OK(imported_m1_catalog->GetMacro("pub_macro", &pub_macro));
  ASSERT_NE(pub_macro, nullptr);
  EXPECT_EQ(pub_macro->body(), "10");

  const Macro* priv_macro = nullptr;
  GOOGLESQL_ASSERT_OK(imported_m1_catalog->GetMacro("priv_macro", &priv_macro));
  EXPECT_EQ(priv_macro, nullptr);
}

constexpr absl::string_view kImportMacroError =
    "IMPORT statements cannot be produced via macro expansion or contain "
    "macro invocations";

struct ImportMacroTestCase {
  std::string test_name;
  std::string module_contents;
  absl::string_view expected_error;
  bool allow_macros_in_module = true;
};

class ModuleImportMacroTest : public ModuleMacroTest,
                              public WithParamInterface<ImportMacroTestCase> {};

TEST_P(ModuleImportMacroTest, ValidatesMacroUsageInImports) {
  const ImportMacroTestCase& test_case = GetParam();
  auto fetcher = std::make_unique<testing::TestModuleContentsFetcher>(
      /*descriptor_pool=*/nullptr, /*source_directory=*/"");
  GOOGLESQL_ASSERT_OK(fetcher->AddInMemoryModule({"m1"}, test_case.module_contents));
  GOOGLESQL_ASSERT_OK(fetcher->AddInMemoryModule({"a", "b", "c"}, "MODULE a.b.c;\n"));

  ModuleFactoryOptions options;
  options.allow_macros_in_module = test_case.allow_macros_in_module;
  factory_ = std::make_unique<ModuleFactory>(analyzer_options_, options,
                                             std::move(fetcher),
                                             &builtin_catalog_, &type_factory_);
  ModuleCatalog* catalog = nullptr;
  GOOGLESQL_ASSERT_OK(factory_->CreateOrReturnModuleCatalog({"m1"}, &catalog));

  if (test_case.expected_error.empty()) {
    EXPECT_THAT(catalog->module_errors(), IsEmpty());
  } else {
    EXPECT_THAT(catalog->module_errors(),
                ElementsAre(StatusIs(absl::StatusCode::kInvalidArgument,
                                     HasSubstr(test_case.expected_error))));
  }
}

INSTANTIATE_TEST_SUITE_P(
    ModuleImportMacroTests, ModuleImportMacroTest,
    ValuesIn(std::vector<ImportMacroTestCase>{
        {.test_name = "MacroInImportTarget",
         .module_contents = "MODULE m1;\n"
                            "DEFINE PUBLIC MACRO macro_name foo.bar;\n"
                            "IMPORT MODULE $macro_name();\n",
         .expected_error = kImportMacroError},
        {.test_name = "ImportProducedByMacro",
         .module_contents =
             "MODULE m1;\n"
             "DEFINE PUBLIC MACRO make_import IMPORT MODULE a.b.c;\n"
             "$make_import();\n",
         .expected_error = kImportMacroError},
        {.test_name = "BuiltinMacroInImport",
         .module_contents = "MODULE m1;\n"
                            "IMPORT MODULE $$IDENTIFIER(a);\n",
         .expected_error = kImportMacroError},
        {.test_name = "FusedMacroTokenInImportOptions",
         .module_contents = "MODULE m1;\n"
                            "DEFINE PUBLIC MACRO m 1.2;\n"
                            "IMPORT MODULE a.b.c OPTIONS(x = $m());\n",
         .expected_error = kImportMacroError},
        {.test_name = "EmptyMacroInImport",
         .module_contents = "MODULE m1;\n"
                            "DEFINE PUBLIC MACRO empty;\n"
                            "IMPORT MODULE a.b.c $empty();\n",
         .expected_error = kImportMacroError},
        {.test_name = "CommentOnlyMacroInImport",
         .module_contents =
             "MODULE m1;\n"
             "DEFINE PUBLIC MACRO comment_only /* comment */   ;\n"
             "IMPORT MODULE a.b.c $comment_only();\n",
         .expected_error = kImportMacroError},
        {.test_name = "UndefinedMacroInImport",
         .module_contents = "MODULE m1;\n"
                            "IMPORT MODULE a.$undefined.c;\n",
         .expected_error = kImportMacroError},
        {.test_name = "MacroWithoutArgsInImport",
         .module_contents = "MODULE m1;\n"
                            "DEFINE PUBLIC MACRO b b;\n"
                            "IMPORT MODULE a.$b.c;\n",
         .expected_error = kImportMacroError},
        {.test_name = "SyntaxErrorAfterMacroInImport",
         .module_contents = "MODULE m1;\n"
                            "DEFINE PUBLIC MACRO b b;\n"
                            "IMPORT MODULE a.$b() +;\n",
         .expected_error = kImportMacroError},
        {.test_name = "SyntaxErrorInExpandedMacroTokens",
         .module_contents = "MODULE m1;\n"
                            "DEFINE PUBLIC MACRO b c +;\n"
                            "IMPORT MODULE a.$b();\n",
         .expected_error = kImportMacroError},
        {.test_name = "TokenizerErrorAfterMacroInImport",
         .module_contents = "MODULE m1;\n"
                            "DEFINE PUBLIC MACRO b b;\n"
                            "IMPORT MODULE a.$b() 'unclosed;\n",
         .expected_error = kImportMacroError},
        {.test_name = "MacroInImportWithMacrosDisabled",
         .module_contents = "MODULE m1;\n"
                            "IMPORT MODULE $p();\n",
         .expected_error = kImportMacroError,
         .allow_macros_in_module = false},
        {.test_name = "DollarSignInImportStringLiteral",
         .module_contents =
             "MODULE m1;\n"
             "IMPORT MODULE a.b.c OPTIONS(description = \"Price $USD\");\n"},
        {.test_name = "TrailingCommentAfterImport",
         .module_contents = "MODULE m1;\n"
                            "IMPORT MODULE a.b.c OPTIONS(description=\"Price "
                            "$USD\") /* comment */"},
        {.test_name = "NonReservedKeywordsInImport",
         .module_contents = "MODULE m1;\n"
                            "IMPORT MODULE foo.bar.module.baz OPTIONS(module = "
                            "1, export = true);\n",
         .expected_error = "not found"},
        {.test_name = "ValidImportBeforeStatementWithMacro",
         .module_contents = "MODULE m1;\n"
                            "DEFINE PUBLIC MACRO m 123;\n"
                            "IMPORT MODULE a.b.c;\n"
                            "CREATE PUBLIC FUNCTION fn() AS ($m());\n"},
        {.test_name = "ValidImportBeforeAdjacentEmptyMacro",
         .module_contents = "MODULE m1;\n"
                            "DEFINE PUBLIC MACRO empty;\n"
                            "IMPORT MODULE a.b.c;$empty() CREATE PUBLIC "
                            "FUNCTION fn() AS (1);\n"},
        {.test_name = "ValidImportBeforeAdjacentMacroStatement",
         .module_contents = "MODULE m1;\n"
                            "DEFINE PUBLIC MACRO make_fn_header CREATE PUBLIC "
                            "FUNCTION fn() AS;\n"
                            "IMPORT MODULE a.b.c;$make_fn_header() (1);\n"},
        {.test_name = "ValidImportBeforeEmptyMacroAtEof",
         .module_contents = "MODULE m1;\n"
                            "DEFINE PUBLIC MACRO empty;\n"
                            "IMPORT MODULE a.b.c;\n"
                            "$empty()\n"},
        {.test_name = "ValidImportBeforeUndefinedMacroInNextStatement",
         .module_contents = "MODULE m1;\n"
                            "IMPORT MODULE a.b.c;\n"
                            "$undefined;\n",
         .expected_error = "Macro 'undefined' not found"},
        {.test_name = "ValidImportBeforeMacroWithMacrosDisabled",
         .module_contents = "MODULE m1;\n"
                            "IMPORT MODULE a.b.c;\n"
                            "CREATE PUBLIC FUNCTION fn() AS ($p());\n",
         .expected_error = "Syntax error: Unexpected \"$p\"",
         .allow_macros_in_module = false},
        {.test_name = "MissingSemicolonBeforeDefineMacro",
         .module_contents = "MODULE m1;\n"
                            "IMPORT MODULE a.b.c \n DEFINE MACRO x y \n "
                            "SELECT $local_macro;\n",
         .expected_error = "got keyword DEFINE"},
        {.test_name = "UnclosedStringLiteralWithDollarSign",
         .module_contents =
             "MODULE m1;\n"
             "IMPORT MODULE a.b.c OPTIONS(description = \"Price $USD);\n",
         .expected_error = "Unclosed string literal"},
        {.test_name = "SyntaxErrorBeforeMacroInImport",
         .module_contents = "MODULE m1;\n"
                            "DEFINE PUBLIC MACRO m 123;\n"
                            "IMPORT MODULE a.b.c ,$m();\n",
         .expected_error =
             "Syntax error: Expected \";\" or end of input but got \",\""},
        {.test_name = "SyntaxErrorBeforeUndefinedMacroInImport",
         .module_contents = "MODULE m1;\n"
                            "IMPORT MODULE a.b.c ) $undefined;\n",
         .expected_error =
             "Syntax error: Expected \";\" or end of input but got \")\""},
    }),
    [](const TestParamInfo<ImportMacroTestCase>& info) {
      return info.param.test_name;
    });

}  // namespace
}  // namespace googlesql
