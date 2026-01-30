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

#include "googlesql/public/modules.h"

#include <stddef.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/base/path.h"
#include "googlesql/common/status_payload_utils.h"
#include "googlesql/base/testing/status_matchers.h"  
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/builtin_function.h"
#include "googlesql/public/builtin_function_options.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/constant.h"
#include "googlesql/public/constant_evaluator.h"
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/error_location.pb.h"
#include "googlesql/public/function.h"
#include "googlesql/public/function_signature.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/module_contents_fetcher.h"
#include "googlesql/public/module_details.h"
#include "googlesql/public/module_factory.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/prepared_expression_constant_evaluator.h"
#include "googlesql/public/procedure.h"
#include "googlesql/public/remote_tvf_factory.h"
#include "googlesql/public/simple_catalog.h"
#include "googlesql/public/sql_constant.h"
#include "googlesql/public/sql_function.h"
#include "googlesql/public/sql_tvf.h"
#include "googlesql/public/sql_view.h"
#include "googlesql/public/table_valued_function.h"
#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/resolved_ast/resolved_node_kind.pb.h"
#include "googlesql/testing/test_module_contents_fetcher.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_map.h"
#include "googlesql/base/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "googlesql/base/file_util.h"

namespace googlesql {
namespace {

using ::testing::_;
using ::testing::AllOf;
using ::testing::Contains;
using ::testing::ContainsRegex;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::UnorderedElementsAreArray;
using ::absl_testing::StatusIs;

class FakeRemoteTvf : public TableValuedFunction {
 public:
  FakeRemoteTvf(const ResolvedCreateTableFunctionStmt& resolved_stmt,
                ModuleDetails module_details)
      : TableValuedFunction(resolved_stmt.name_path(),
                            resolved_stmt.signature()) {}

  absl::Status Resolve(
      const AnalyzerOptions* analyzer_options,
      const std::vector<TVFInputArgumentType>& actual_arguments,
      const FunctionSignature& concrete_signature, Catalog* catalog,
      TypeFactory* type_factory,
      std::shared_ptr<TVFSignature>* output_tvf_signature) const final {
    return absl::OkStatus();
  }
};

class FakeRemoteTvfFactory : public RemoteTvfFactory {
 public:
  absl::StatusOr<std::unique_ptr<googlesql::TableValuedFunction>>
  CreateRemoteTVF(const ResolvedCreateTableFunctionStmt& stmt,
                  const ModuleDetails& details,
                  googlesql::Catalog* module_resolution_catalog) override {
    return std::make_unique<FakeRemoteTvf>(stmt, details);
  }
};

}  // namespace

class ModuleTest : public ::testing::Test {
 protected:
  ModuleTest() = default;
  ModuleTest(const ModuleTest&) = delete;
  ModuleTest& operator=(const ModuleTest&) = delete;

  void SetUp() override {
    // We must enable CREATE FUNCTION statements to run these tests.
    analyzer_options_.mutable_language()->SetSupportsAllStatementKinds();

    {
      // Note that enabling maximum language features for development is
      // brittle; it would be better to enable only completed features and a
      // select list of in-development features instead.
      LanguageOptions language_options;
      language_options.EnableMaximumLanguageFeaturesForDevelopment();
      // TODO: b/277365877 - Enable this feature in unit testing once constant
      // evaluator in analyzer options and module factory is fully supported.
      language_options.DisableLanguageFeature(
          FEATURE_ANALYSIS_CONSTANT_FUNCTION_ARGUMENT);
      LanguageOptions::LanguageFeatureSet language_features =
          language_options.GetEnabledLanguageFeatures();
      analyzer_options_.mutable_language()->SetEnabledLanguageFeatures(
          language_features);
    }

    // TODO: Switch back to ERROR_MESSAGE_MULTI_LINE_WITH_CARET once
    // the error location is correct for modules.
    // This makes it easier to verify that the error locations indicated
    // in the message line up with the statements in the module string/file.
    analyzer_options_.set_error_message_mode(ERROR_MESSAGE_WITH_PAYLOAD);
    absl::flat_hash_map<std::string, std::unique_ptr<Function>> functions;
    absl::flat_hash_map<std::string, const Type*> types_ignored;
    absl::Status status = GetBuiltinFunctionsAndTypes(
        BuiltinFunctionOptions(analyzer_options_.language()), type_factory_,
        functions, types_ignored);
    GOOGLESQL_DCHECK_OK(status);
    builtin_function_catalog_ = std::make_unique<SimpleCatalog>(
        "builtin_function_catalog", &type_factory_);
    for (auto& [name, function] : functions) {
      builtin_function_catalog_->AddOwnedFunction(name, std::move(function));
    }

    auto column = std::make_unique<googlesql::SimpleColumn>(
        "test_table", "col", type_factory_.get_int64());
    auto table = std::make_unique<SimpleTable>("test_table");
    GOOGLESQL_ASSERT_OK(table->AddColumn(column.release(), /*is_owned=*/true));
    global_catalog_ =
        std::make_unique<SimpleCatalog>("global_catalog", &type_factory_);
    global_catalog_->AddOwnedTable(table.release());
  }

  ModuleFactory* module_factory() { return module_factory_.get(); }

  // Gets a ModuleContentsFetcher. If `module_contents` is non-NULL
  // then TestModuleContentsFetcher fetches the contents from that string.
  // Otherwise, it fetches the contents from test inputs on disk.
  std::unique_ptr<ModuleContentsFetcher> GetModuleContentsFetcher(
      const std::vector<std::string>& module_name,
      const std::string* module_contents) {
    const std::string source_directory =
        googlesql_base::JoinPath(::testing::SrcDir(), "_main");
    auto fetcher = std::make_unique<testing::TestModuleContentsFetcher>(
        /*descriptor_pool=*/nullptr, source_directory);
    if (module_contents != nullptr) {
      GOOGLESQL_CHECK_OK(fetcher->AddInMemoryModule(module_name, *module_contents));
    }
    return std::move(fetcher);
  }

  void InitModuleFactory(
      std::unique_ptr<ModuleContentsFetcher> module_contents_fetcher,
      ModuleFactoryOptions factory_options = {}) {
    module_factory_ = std::make_unique<ModuleFactory>(
        analyzer_options(), factory_options, std::move(module_contents_fetcher),
        builtin_function_catalog(), global_catalog(), type_factory());
  }

  absl::Status CreateModuleCatalog(const std::vector<std::string>& module_name,
                                   absl::string_view module_alias,
                                   const std::string* module_contents,
                                   ModuleFactoryOptions factory_options = {}) {
    std::vector<std::string> complete_module_name = module_name;
    if (module_contents == nullptr) {
      // We will use the TestModuleContentsFetcher to look up the module. We
      // have to update the module_name to include the full relative path so
      // it can find the file.
      complete_module_name = {"googlesql", "testdata", "modules"};
      complete_module_name.insert(complete_module_name.end(),
                                  module_name.begin(), module_name.end());
    }
    InitModuleFactory(
        GetModuleContentsFetcher(complete_module_name, module_contents),
        factory_options);
    module_catalog_ = nullptr;
    return module_factory_->CreateOrReturnModuleCatalog(complete_module_name,
                                                        &module_catalog_);
  }

  ModuleCatalog* module_catalog() { return module_catalog_; }

  absl::Status GetModuleContents(absl::string_view module_name,
                                 std::string* module_contents) const {
    const std::string directory = "_main/googlesql/testdata/modules";
    const std::string extension = ".sqlm";
    return internal::GetContents(
        googlesql_base::JoinPath(::testing::SrcDir(), directory,
                       absl::StrCat(module_name, extension)),
        module_contents);
  }

  const AnalyzerOptions& analyzer_options() const { return analyzer_options_; }

  AnalyzerOptions* mutable_analyzer_options() { return &analyzer_options_; }

  TypeFactory* type_factory() { return &type_factory_; }

  SimpleCatalog* builtin_function_catalog() const {
    return builtin_function_catalog_.get();
  }

  SimpleCatalog* global_catalog() const { return global_catalog_.get(); }

  void DisableLanguageFeature(LanguageFeature feature) {
    analyzer_options_.mutable_language()->DisableLanguageFeature(feature);
  }

 private:
  // TypeFactory to use for the test.
  TypeFactory type_factory_;

  ModuleCatalog* module_catalog_ = nullptr;  // Not owned

  std::unique_ptr<ModuleFactory> module_factory_;

  // Analyzer options to use for the test.
  AnalyzerOptions analyzer_options_;

  // A Catalog for builtin functions that can be referenced by module
  // statements.
  std::unique_ptr<SimpleCatalog> builtin_function_catalog_;

  std::unique_ptr<SimpleCatalog> global_catalog_;
};

TEST_F(ModuleTest, factory_without_fetcher_test) {
  InitModuleFactory(/*module_contents_fetcher=*/nullptr);
  ModuleCatalog* module_catalog = nullptr;
  EXPECT_FALSE(module_factory()
                   ->CreateOrReturnModuleCatalog({"a"}, &module_catalog)
                   .ok());
}

TEST_F(ModuleTest, empty_module_test) {
  std::vector<std::string> empty_modules;
  empty_modules.push_back("");
  empty_modules.push_back("   ");

  for (const std::string& empty_module : empty_modules) {
    absl::Status create_status =
        CreateModuleCatalog({"a", "B", "cc"}, "module_alias", &empty_module);
    GOOGLESQL_EXPECT_OK(create_status);
    ASSERT_NE(nullptr, module_catalog());
    EXPECT_EQ("<unnamed module>", module_catalog()->FullName());
    ASSERT_EQ(2, module_catalog()->module_errors().size())
        << "Module status errors:\n"
        << module_catalog()->ModuleErrorsDebugString(
               /*include_nested_module_errors=*/false,
               /*include_catalog_object_errors=*/false);
    EXPECT_THAT(module_catalog()->module_errors()[0],
                StatusIs(absl::StatusCode::kInvalidArgument,
                         ::testing::HasSubstr("Syntax error: Unexpected end of "
                                              "statement")));
    EXPECT_THAT(module_catalog()->module_errors()[1],
                StatusIs(absl::StatusCode::kInvalidArgument,
                         ::testing::HasSubstr("A module must contain exactly "
                                              "one MODULE statement")));
  }
}

// TODO: b/277365877 - Remove this test once
// `ModuleFactoryOptions::constant_evaluator` is deprecated and removed.
TEST_F(ModuleTest, validate_constant_evaluator_tests) {
  PreparedExpressionConstantEvaluator constant_evaluator_1(/*options=*/{});
  PreparedExpressionConstantEvaluator constant_evaluator_2(/*options=*/{});
  std::string simple_module = "module a";
  AnalyzerOptions constant_evaluator_1_analyzer_options = analyzer_options();
  constant_evaluator_1_analyzer_options.set_constant_evaluator(
      &constant_evaluator_1);
  AnalyzerOptions constant_evaluator_2_analyzer_options = analyzer_options();
  constant_evaluator_2_analyzer_options.set_constant_evaluator(
      &constant_evaluator_2);

  // No ConstantEvaluator is set.
  GOOGLESQL_EXPECT_OK(CreateModuleCatalog({"a", "B", "cc"}, "module_alias",
                                &simple_module, {}));
  // Only ModuleFactoryOptions::constant_evaluator is set.
  GOOGLESQL_EXPECT_OK(CreateModuleCatalog({"a", "B", "cc"}, "module_alias",
                                &simple_module,
                                {.constant_evaluator = &constant_evaluator_1}));
  // Only AnalyzerOptions::constant_evaluator is set.
  mutable_analyzer_options()->set_constant_evaluator(&constant_evaluator_1);
  GOOGLESQL_EXPECT_OK(CreateModuleCatalog({"a", "B", "cc"}, "module_alias",
                                &simple_module, {}));
  // ModuleFactoryOptions::constant_evaluator and
  // AnalyzerOptions::constant_evaluator are set to the same object.
  GOOGLESQL_EXPECT_OK(CreateModuleCatalog({"a", "B", "cc"}, "module_alias",
                                &simple_module,
                                {.constant_evaluator = &constant_evaluator_1}));
  // ModuleFactoryOptions::constant_evaluator and
  // AnalyzerOptions::constant_evaluator are set to different objects.
  mutable_analyzer_options()->set_constant_evaluator(&constant_evaluator_2);
  EXPECT_THAT(
      CreateModuleCatalog({"a", "B", "cc"}, "module_alias", &simple_module,
                          {.constant_evaluator = &constant_evaluator_1}),
      StatusIs(absl::StatusCode::kInternal,
               ContainsRegex("constant_evaluator.*same object")));
}

TEST_F(ModuleTest, single_module_statement_tests) {
  PreparedExpressionConstantEvaluator constant_evaluator(/*options=*/{});

  // For all of these tests, the module is valid but effectively empty.
  std::string simple_module = "module a";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"a", "B", "cc"}, "module_alias",
                                &simple_module,
                                {.constant_evaluator = &constant_evaluator}));
  EXPECT_EQ("a", module_catalog()->FullName());
  EXPECT_EQ("a.B.cc",
            absl::StrJoin(module_catalog()->module_name_from_import(), "."));

  simple_module = "module b.c.d";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"a", "B", "cc"}, "module_alias",
                                &simple_module,
                                {.constant_evaluator = &constant_evaluator}));
  EXPECT_EQ("b.c.d", module_catalog()->FullName());
  EXPECT_EQ("a.B.cc",
            absl::StrJoin(module_catalog()->module_name_from_import(), "."));

  simple_module = "module `b.c.d`";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"a", "B", "cc"}, "module_alias",
                                &simple_module,
                                {.constant_evaluator = &constant_evaluator}));
  EXPECT_EQ("b.c.d", module_catalog()->FullName());
  EXPECT_EQ("a.B.cc",
            absl::StrJoin(module_catalog()->module_name_from_import(), "."));

  simple_module =
      "module b.c.d options(stub_module_type='udf_server_catalog', "
      "udf_server_address='string', udf_scaling_factor=1.5)";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"a", "B", "cc"}, "module_alias",
                                &simple_module,
                                {.constant_evaluator = &constant_evaluator}));
  EXPECT_EQ("b.c.d", module_catalog()->FullName());
  EXPECT_EQ("a.B.cc",
            absl::StrJoin(module_catalog()->module_name_from_import(), "."));

  // Options can use builtin functions.
  simple_module =
      "module b.c.d options(stub_module_type='udf_server_catalog', "
      "udf_server_address=concat('a', 'b'))";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"a", "B", "cc"}, "module_alias",
                                &simple_module,
                                {.constant_evaluator = &constant_evaluator}));
  EXPECT_EQ("b.c.d", module_catalog()->FullName());
  EXPECT_EQ("a.B.cc",
            absl::StrJoin(module_catalog()->module_name_from_import(), "."));

  // Module with a name that is just a space.  This works, but probably
  // is not very useful.
  simple_module = "module ` `";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"a", "B", "cc"}, "module_alias",
                                &simple_module,
                                {.constant_evaluator = &constant_evaluator}));
  EXPECT_EQ(" ", module_catalog()->FullName());
  EXPECT_EQ("a.B.cc",
            absl::StrJoin(module_catalog()->module_name_from_import(), "."));
}

TEST_F(ModuleTest, single_module_statement_parser_failure_test) {
  std::vector<std::string> simple_modules;
  simple_modules.push_back("module");
  simple_modules.push_back("module 1");
  simple_modules.push_back("module a AS");
  simple_modules.push_back("module a b");
  simple_modules.push_back("module a AS b");
  simple_modules.push_back("module a 1");
  simple_modules.push_back("module a AS 1");
  simple_modules.push_back("module ``");

  for (const std::string& simple_module : simple_modules) {
    absl::Status create_status =
        CreateModuleCatalog({"a", "B", "cc"}, "module_alias", &simple_module);
    GOOGLESQL_EXPECT_OK(create_status);
    ASSERT_EQ(2, module_catalog()->module_errors().size())
        << "Module status errors:\n"
        << module_catalog()->ModuleErrorsDebugString(
               /*include_nested_module_errors=*/false,
               /*include_catalog_object_errors=*/false);
    EXPECT_THAT(module_catalog()->module_errors()[0],
                StatusIs(absl::StatusCode::kInvalidArgument,
                         ::testing::HasSubstr("Syntax error")));
    EXPECT_THAT(module_catalog()->module_errors()[1],
                StatusIs(absl::StatusCode::kInvalidArgument,
                         ::testing::HasSubstr("A module must contain exactly "
                                              "one MODULE statement")));
    EXPECT_EQ("a.B.cc",
              absl::StrJoin(module_catalog()->module_name_from_import(), "."));
  }
}

TEST_F(ModuleTest, single_module_statement_resolver_failure_test) {
  const std::string simple_module = "module b.c.d options(a=1.+'b')";
  const absl::Status create_status =
      CreateModuleCatalog({"a", "B", "cc"}, "module_alias", &simple_module);
  GOOGLESQL_EXPECT_OK(create_status);
  ASSERT_EQ(2, module_catalog()->module_errors().size())
      << "Module status errors:\n"
      << module_catalog()->ModuleErrorsDebugString(
             /*include_nested_module_errors=*/false,
             /*include_catalog_object_errors=*/false);
  EXPECT_THAT(module_catalog()->module_errors()[0],
              StatusIs(absl::StatusCode::kInvalidArgument,
                       AllOf(HasSubstr("The MODULE statement in module b.c.d "
                                       "failed resolution with error:"),
                             HasSubstr("No matching signature for operator +"),
                             HasSubstr("rgument types: DOUBLE, STRING"))));
  EXPECT_THAT(
      module_catalog()->module_errors()[1],
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("A module must contain exactly one MODULE statement")));
}

TEST_F(ModuleTest, single_module_statement_duplicate_options_failure_test) {
  const std::string simple_module =
      "module b.c.d options(stub_module_type=1, stub_module_type='b')";
  const absl::Status create_status =
      CreateModuleCatalog({"a", "B", "cc"}, "module_alias", &simple_module);
  EXPECT_THAT(
      create_status,
      StatusIs(absl::StatusCode::kInvalidArgument,
               ::testing::HasSubstr(
                   "Duplicate option stub_module_type in module b.c.d")));
  EXPECT_EQ(nullptr, module_catalog());
}

TEST_F(ModuleTest, two_module_statements_failure_test) {
  const std::string simple_module = "module a; module b";
  const absl::Status create_status =
      CreateModuleCatalog({"a", "B", "cc"}, "module_alias", &simple_module);
  GOOGLESQL_EXPECT_OK(create_status);
  ASSERT_EQ(1, module_catalog()->module_errors().size())
      << "Module status errors:\n"
      << module_catalog()->ModuleErrorsDebugString(
             /*include_nested_module_errors=*/false,
             /*include_catalog_object_errors=*/false);

  EXPECT_THAT(module_catalog()->module_errors()[0],
              StatusIs(absl::StatusCode::kInvalidArgument,
                       ::testing::HasSubstr("Found more than one MODULE "
                                            "statement")));
}

TEST_F(ModuleTest, single_statement_module_parse_failure_test) {
  // This CREATE FUNCTION statement invalid, because the 'a+1' expression
  // is expected to be surrounded by parens (like in the above test).
  const std::string simple_module =
      "module a;"
      "create public function foo(a int64) as a+1";
  GOOGLESQL_ASSERT_OK(
      CreateModuleCatalog({"a", "B", "cc"}, "module_alias", &simple_module));

  const Table* table;
  const TableValuedFunction* tvf;
  const Procedure* procedure;
  const Type* type;
  const Function* function;
  const Constant* constant;

  // Initialization finished despite the parse error, so FindXXX() functions
  // return a NOT_FOUND error.
  EXPECT_THAT(
      module_catalog()->FindTable({"a"}, &table),
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("not found in catalog")));
  EXPECT_THAT(
      module_catalog()->FindTableValuedFunction({"a"}, &tvf),
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("not found in catalog")));
  EXPECT_THAT(
      module_catalog()->FindProcedure({"a"}, &procedure),
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("not found in catalog")));
  EXPECT_THAT(
      module_catalog()->FindType({"int64"}, &type),
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("not found in catalog")));
  EXPECT_THAT(
      module_catalog()->FindFunction({"a"}, &function),
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("not found in catalog")));
  EXPECT_THAT(
      module_catalog()->FindFunction({"concat"}, &function),
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("not found in catalog")));
  EXPECT_THAT(
      module_catalog()->FindFunction({"foo"}, &function),
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("not found in catalog")));
  EXPECT_THAT(
      module_catalog()->FindConstant({"foo"}, &constant),
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("not found in catalog")));
}

TEST_F(ModuleTest, missing_module_statement_module_test) {
  // This would be a valid module, but is missing the module statement.
  const std::string simple_module =
      "create public function foo(a int64) as (a+1)";
  const absl::Status create_status =
      CreateModuleCatalog({"a", "B", "cc"}, "module_alias", &simple_module);
  EXPECT_THAT(create_status,
              StatusIs(absl::StatusCode::kInvalidArgument,
                       ::testing::HasSubstr("The first statement in a module "
                                            "must be a MODULE statement")));
  EXPECT_EQ(nullptr, module_catalog());
}

TEST_F(ModuleTest, single_statement_module_test) {
  const std::string simple_module =
      "module simple_module;    "
      "create public function foo(a int64) as (a+1)";
  GOOGLESQL_ASSERT_OK(
      CreateModuleCatalog({"a", "B", "cc"}, "module_alias", &simple_module));

  const Table* table;
  const TableValuedFunction* tvf;
  const Procedure* procedure;
  const Type* type;
  const Function* function;
  const Constant* constant;

  // Initialization succeeds, so FindXXX() functions return a NOT_FOUND
  // error if the object does not exist.
  // In this module catalog, only function 'foo' exists, but no tables, etc.
  EXPECT_THAT(
      module_catalog()->FindTable({"a"}, &table),
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("not found in catalog")));
  EXPECT_THAT(
      module_catalog()->FindTableValuedFunction({"a"}, &tvf),
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("not found in catalog")));
  EXPECT_THAT(
      module_catalog()->FindProcedure({"a"}, &procedure),
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("not found in catalog")));
  EXPECT_THAT(
      module_catalog()->FindType({"int64"}, &type),
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("not found in catalog")));
  EXPECT_THAT(
      module_catalog()->FindFunction({"a"}, &function),
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("not found in catalog")));
  EXPECT_THAT(
      module_catalog()->FindConstant({"a"}, &constant),
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("not found in catalog")));

  // Function <foo> was successfully created.
  GOOGLESQL_ASSERT_OK(module_catalog()->FindFunction({"foo"}, &function));
  EXPECT_EQ("Lazy_resolution_function:foo",
            function->DebugString(/*verbose=*/false));
  EXPECT_EQ(
      "Lazy_resolution_function:foo\n  (INT64 a) -> INT64 "
      "rejects_collation=TRUE",
      function->DebugString(/*verbose=*/true));
  EXPECT_THAT(function->function_options().module_name_from_import,
              ElementsAre("a", "B", "cc"));
}

TEST_F(ModuleTest, non_sql_function_statement_module_test) {
  const std::string simple_module =
      R"(MODULE a; CREATE PUBLIC FUNCTION f(t INT64) RETURNS INT64
           LANGUAGE JS AS """
             return 0;
           """;)";
  GOOGLESQL_EXPECT_OK(CreateModuleCatalog({"a"}, "module_alias", &simple_module,
                                ModuleFactoryOptions()));
  const Function* function;
  // Function <foo> was successfully created.
  GOOGLESQL_ASSERT_OK(module_catalog()->FindFunction({"f"}, &function));
  EXPECT_EQ("Non_sql_function:f", function->DebugString(/*verbose=*/false));
  EXPECT_EQ("Non_sql_function:f\n  (INT64 t) -> INT64 rejects_collation=TRUE",
            function->DebugString(/*verbose=*/true));
}

TEST_F(ModuleTest, non_sql_templated_function_statement_module_test) {
  const std::string simple_module =
      R"(MODULE a; CREATE PUBLIC FUNCTION f(t ANY TYPE) RETURNS INT64
           LANGUAGE JS AS """
             return 0;
           """;)";

  GOOGLESQL_EXPECT_OK(CreateModuleCatalog({"a"}, "module_alias", &simple_module,
                                ModuleFactoryOptions()));
  const Function* function;
  // Function <foo> was successfully created.
  GOOGLESQL_ASSERT_OK(module_catalog()->FindFunction({"f"}, &function));
  EXPECT_EQ("Non_sql_function:f", function->DebugString(/*verbose=*/false));
  EXPECT_EQ(
      "Non_sql_function:f\n  (ANY TYPE t) -> INT64 rejects_collation=TRUE",
      function->DebugString(/*verbose=*/true));
}

TEST_F(ModuleTest, unsupported_non_sql_table_function_statement_module_test) {
  const std::string simple_module =
      R"(MODULE a; CREATE PUBLIC TABLE FUNCTION f(t INT64)
           LANGUAGE JS AS """
             return 0;
           """;)";

  GOOGLESQL_EXPECT_OK(CreateModuleCatalog({"a"}, "module_alias", &simple_module));
  const TableValuedFunction* table_function;
  const absl::Status find_status =
      module_catalog()->FindTableValuedFunction({"f"}, &table_function);
  EXPECT_THAT(find_status,
              StatusIs(absl::StatusCode::kInvalidArgument,
                       ::testing::HasSubstr("Table function f is invalid")));
  EXPECT_THAT(internal::StatusToString(find_status),
              ::testing::HasSubstr(
                  "Unsupported language type for SQL CREATE TABLE FUNCTION "
                  "statements in modules, supported types are [SQL, REMOTE]"));
}

TEST_F(ModuleTest,
       unsupported_non_sql_templated_table_function_statement_module_test) {
  const std::string simple_module =
      R"(MODULE a; CREATE PUBLIC TABLE FUNCTION f(t ANY TYPE)
           LANGUAGE JS AS """
             return 0;
           """;)";

  GOOGLESQL_EXPECT_OK(CreateModuleCatalog({"a"}, "module_alias", &simple_module));
  const TableValuedFunction* table_function;
  const absl::Status find_status =
      module_catalog()->FindTableValuedFunction({"f"}, &table_function);
  EXPECT_THAT(find_status,
              StatusIs(absl::StatusCode::kInvalidArgument,
                       ::testing::HasSubstr("Table function f is invalid")));
  EXPECT_THAT(internal::StatusToString(find_status),
              ::testing::HasSubstr(
                  "Unsupported language type for SQL CREATE TABLE FUNCTION "
                  "statements in modules, supported types are [SQL, REMOTE]"));
}

TEST_F(ModuleTest, remote_table_function_statement_module_test) {
  const std::string simple_module =
      R"(MODULE a; CREATE PUBLIC TABLE FUNCTION f(t INT64)
      RETURNS TABLE <DOUBLE> LANGUAGE REMOTE;)";

  FakeRemoteTvfFactory tvf_factory;

  GOOGLESQL_EXPECT_OK(CreateModuleCatalog({"a"}, "module_alias", &simple_module,
                                ModuleFactoryOptions{
                                    .remote_tvf_factory = &tvf_factory,
                                }));
  const TableValuedFunction* table_function;
  GOOGLESQL_EXPECT_OK(module_catalog()->FindTableValuedFunction({"f"}, &table_function));
  EXPECT_TRUE(table_function->Is<FakeRemoteTvf>());
  EXPECT_THAT(table_function->Name(), "f");
  EXPECT_THAT(table_function->NumSignatures(), 1);
  const FunctionSignature* signature = table_function->GetSignature(0);
  EXPECT_THAT(signature->DebugString(), "(INT64 t) -> TABLE<DOUBLE>");
}

TEST_F(ModuleTest, disabled_remote_table_function_statement_module_test) {
  const std::string simple_module =
      R"(MODULE a; CREATE PUBLIC TABLE FUNCTION f(t INT64)
      RETURNS TABLE <DOUBLE> LANGUAGE REMOTE;)";

  FakeRemoteTvfFactory tvf_factory;

  GOOGLESQL_EXPECT_OK(CreateModuleCatalog({"a"}, "module_alias", &simple_module));
  const TableValuedFunction* table_function;
  const absl::Status find_status =
      module_catalog()->FindTableValuedFunction({"f"}, &table_function);
  EXPECT_THAT(find_status,
              StatusIs(absl::StatusCode::kInvalidArgument,
                       ::testing::HasSubstr("Table function f is invalid")));
  EXPECT_THAT(
      internal::StatusToString(find_status),
      ::testing::HasSubstr("REMOTE table function is disabled, please set "
                           "remote_tvf_factory in ModuleFactoryOptions"));
}

TEST_F(ModuleTest, remote_any_value_table_function_statement_module_test) {
  const std::string simple_module =
      R"(MODULE a; CREATE PUBLIC TABLE FUNCTION f(t ANY TYPE)
      RETURNS TABLE <DOUBLE> LANGUAGE REMOTE;)";

  FakeRemoteTvfFactory tvf_factory;

  GOOGLESQL_EXPECT_OK(CreateModuleCatalog({"a"}, "module_alias", &simple_module,
                                ModuleFactoryOptions{
                                    .remote_tvf_factory = &tvf_factory,
                                }));
  const TableValuedFunction* table_function;
  GOOGLESQL_EXPECT_OK(module_catalog()->FindTableValuedFunction({"f"}, &table_function));
  EXPECT_TRUE(table_function->Is<FakeRemoteTvf>());
  EXPECT_THAT(table_function->Name(), "f");
  EXPECT_THAT(table_function->NumSignatures(), 1);
  const FunctionSignature* signature = table_function->GetSignature(0);
  EXPECT_THAT(signature->DebugString(), "(ANY TYPE t) -> TABLE<DOUBLE>");
}

TEST_F(ModuleTest, remote_templated_table_function_statement_module_test) {
  const std::string simple_module =
      R"(MODULE a; CREATE PUBLIC TABLE FUNCTION f(t ANY TABLE)
      RETURNS TABLE <DOUBLE> LANGUAGE REMOTE;)";

  FakeRemoteTvfFactory tvf_factory;

  GOOGLESQL_EXPECT_OK(CreateModuleCatalog({"a"}, "module_alias", &simple_module,
                                ModuleFactoryOptions{
                                    .remote_tvf_factory = &tvf_factory,
                                }));
  const TableValuedFunction* table_function;
  GOOGLESQL_EXPECT_OK(module_catalog()->FindTableValuedFunction({"f"}, &table_function));
  EXPECT_TRUE(table_function->Is<FakeRemoteTvf>());
  EXPECT_THAT(table_function->Name(), "f");
  EXPECT_THAT(table_function->NumSignatures(), 1);
  const FunctionSignature* signature = table_function->GetSignature(0);
  EXPECT_THAT(signature->DebugString(), "(ANY TABLE t) -> TABLE<DOUBLE>");
}

TEST_F(ModuleTest, sql_function_with_error) {
  const std::string simple_module =
      "MODULE a; CREATE PUBLIC FUNCTION f() AS (a);";

  GOOGLESQL_EXPECT_OK(CreateModuleCatalog({"a"}, "module_alias", &simple_module));
  const Function* function;
  const absl::Status find_status =
      module_catalog()->FindFunction({"f"}, &function);
  EXPECT_THAT(find_status,
              StatusIs(absl::StatusCode::kInvalidArgument,
                       ::testing::HasSubstr("Function f is invalid")));
  EXPECT_THAT(internal::StatusToString(find_status),
              ::testing::HasSubstr("Unrecognized name: a"));
}

TEST_F(ModuleTest, template_udf_module_name_from_import_test) {
  const std::string simple_module =
      "MODULE a.B.cc; CREATE PUBLIC FUNCTION template_udf (t ANY TYPE) AS (t);";

  GOOGLESQL_EXPECT_OK(
      CreateModuleCatalog({"a", "B", "cc"}, "module_alias", &simple_module));
  const Function* function;
  GOOGLESQL_ASSERT_OK(module_catalog()->FindFunction({"template_udf"}, &function));
  EXPECT_THAT(function->function_options().module_name_from_import,
              ElementsAre("a", "B", "cc"));
}

TEST_F(ModuleTest, multi_statement_module_test) {
  std::vector<std::string> simple_modules;
  // These three modules are all logically the same, but have different
  // newlines and whitespace at different locations.
  std::string module_contents;
  GOOGLESQL_ASSERT_OK(GetModuleContents("multistatement_x_1", &module_contents));
  simple_modules.push_back(module_contents);
  GOOGLESQL_ASSERT_OK(GetModuleContents("multistatement_x_2", &module_contents));
  simple_modules.push_back(module_contents);
  GOOGLESQL_ASSERT_OK(GetModuleContents("multistatement_x_3", &module_contents));
  simple_modules.push_back(module_contents);

  for (const std::string& simple_module : simple_modules) {
    GOOGLESQL_ASSERT_OK(
        CreateModuleCatalog({"a", "B", "cc"}, "module_alias", &simple_module));

    const Function* function;
    // Function <foo> was successfully created.
    GOOGLESQL_ASSERT_OK(module_catalog()->FindFunction({"foo"}, &function));
    EXPECT_EQ(
        "Lazy_resolution_function:foo\n  (INT64 a) -> INT64 "
        "rejects_collation=TRUE",
        function->DebugString(/*verbose=*/true));

    ASSERT_TRUE(function->Is<SQLFunction>());
    const SQLFunction* simple_function = function->GetAs<SQLFunction>();

    std::string expected_full_debug_string = R"(Lazy_resolution_function:foo
  (INT64 a) -> INT64 rejects_collation=TRUE
argument names (a)
FunctionCall(GoogleSQL:$add(INT64, INT64) -> INT64)
+-ArgumentRef(type=INT64, name="a")
+-Literal(type=INT64, value=1)
)";
    EXPECT_EQ(expected_full_debug_string, simple_function->FullDebugString());
    EXPECT_THAT(simple_function->function_options().module_name_from_import,
                ElementsAre("a", "B", "cc"));

    // Function <bar> was successfully created.
    GOOGLESQL_ASSERT_OK(module_catalog()->FindFunction({"bar"}, &function));
    EXPECT_EQ(
        "Lazy_resolution_function:bar\n  (INT32 b) -> INT64 "
        "rejects_collation=TRUE",
        function->DebugString(/*verbose=*/true));

    ASSERT_TRUE(function->Is<SQLFunction>());
    simple_function = function->GetAs<SQLFunction>();

    expected_full_debug_string = R"(Lazy_resolution_function:bar
  (INT32 b) -> INT64 rejects_collation=TRUE
argument names (b)
FunctionCall(GoogleSQL:$add(INT64, INT64) -> INT64)
+-Cast(INT32 -> INT64)
| +-ArgumentRef(type=INT32, name="b")
+-Literal(type=INT64, value=1)
)";
    EXPECT_EQ(expected_full_debug_string, simple_function->FullDebugString());
    EXPECT_THAT(simple_function->function_options().module_name_from_import,
                ElementsAre("a", "B", "cc"));

    // Function <baz> was successfully created.
    GOOGLESQL_ASSERT_OK(module_catalog()->FindFunction({"baz"}, &function));
    EXPECT_EQ(
        "Lazy_resolution_function:baz\n  (UINT32 c) -> UINT64 "
        "rejects_collation=TRUE",
        function->DebugString(/*verbose=*/true));

    ASSERT_TRUE(function->Is<SQLFunction>());
    simple_function = function->GetAs<SQLFunction>();

    expected_full_debug_string = R"(Lazy_resolution_function:baz
  (UINT32 c) -> UINT64 rejects_collation=TRUE
argument names (c)
FunctionCall(GoogleSQL:$add(UINT64, UINT64) -> UINT64)
+-Cast(UINT32 -> UINT64)
| +-ArgumentRef(type=UINT32, name="c")
+-Literal(type=UINT64, value=3)
)";
    EXPECT_EQ(expected_full_debug_string, simple_function->FullDebugString());
    EXPECT_THAT(simple_function->function_options().module_name_from_import,
                ElementsAre("a", "B", "cc"));
  }

  // For test coverage.
  ABSL_LOG(INFO) << module_catalog()->DebugString(/*include_module_contents=*/true);
}

static std::string StatusVectorToString(absl::Span<const absl::Status> errors) {
  std::string out_string;
  for (const absl::Status& error : errors) {
    if (!out_string.empty()) {
      absl::StrAppend(&out_string, "\n");
    }
    absl::StrAppend(&out_string, internal::StatusToString(error));
  }
  return out_string;
}

// A ConstantEvaluator that returns an error for a specific constant value.
// Otherwise returns a NULL value with the same Type as <constant_expression>.
class FakeConstantEvaluator : public ConstantEvaluator {
 public:
  absl::StatusOr<Value> Evaluate(
      const ResolvedExpr& constant_expression) override {
    if (constant_expression.DebugString() ==
        R"(FunctionCall(GoogleSQL:$divide(DOUBLE, DOUBLE) -> DOUBLE)
+-Literal(type=DOUBLE, value=1)
+-Literal(type=DOUBLE, value=0)
)") {
      return absl::Status(absl::StatusCode::kInternal, "evaluation error");
    }
    return Value::Null(constant_expression.type());
  }
};

TEST_F(ModuleTest, append_module_errors_test) {
  // Create a fake ConstantEvaluator that returns an error for a specific
  // constant value.
  FakeConstantEvaluator fake_constant_evaluator;

  absl::Status create_status = CreateModuleCatalog(
      {"module_test_errors_main"}, "module_alias",
      /*module_contents=*/nullptr,
      ModuleFactoryOptions{.constant_evaluator = &fake_constant_evaluator});
  GOOGLESQL_EXPECT_OK(create_status);
  ASSERT_NE(nullptr, module_catalog());
  std::vector<absl::Status> all_errors;

  // Test AppendModuleErrors() after catalog initialization, without resolving
  // any statements.
  module_catalog()->AppendModuleErrors(/*include_nested_module_errors=*/false,
                                       /*include_catalog_object_errors=*/true,
                                       &all_errors);
  // While only looking at this module's local errors (not nested errors),
  // without resolving the statements we expect 9 errors during initialization.
  EXPECT_EQ(10, all_errors.size()) << StatusVectorToString(all_errors);

  // The expected initialization errors are:
  //   1) invalid module import for 'foo'
  //   2) invalid SELECT statement
  //   3) parse error
  //   4-10) Seven other initialization errors, for templated/non-templated
  //        UDFs/UDAs/TVFs
  std::string all_errors_string = module_catalog()->ModuleErrorsDebugString(
      /*include_nested_module_errors=*/false,
      /*include_catalog_object_errors=*/true);
  std::vector<std::string> expected_top_level_init_errors = {
      "Module foo not found",
      "Unsupported statement kind: QueryStatement",
      "Unexpected identifier \"FUNCTINO\"",  // (broken link)
      "Function udf_init_error is invalid",
      "Function templated_udf_init_error is invalid",
      "Function uda_init_error is invalid",
      "Function templated_uda_init_error is invalid",
      "Table function tvf_init_error is invalid",
      "Table function templated_tvf_init_error is invalid",
      "Constant constant_init_error is invalid"};
  for (const std::string& expected_error : expected_top_level_init_errors) {
    EXPECT_THAT(all_errors_string, HasSubstr(expected_error))
        << "expected_error: '" << expected_error << "'"
        << "\nall errors:\n"
        << all_errors_string;
  }

  all_errors.clear();
  module_catalog()->AppendModuleErrors(/*include_nested_module_errors=*/true,
                                       /*include_catalog_object_errors=*/true,
                                       &all_errors);
  // While looking at this module's local errors and imported module errors
  // *without* resolving the statements we expect 27 errors during
  // initialization.
  //
  // The errors break down as follows:
  // 1) Top level module module_test_errors_main      (10 errors)
  //    - 9 initialization errors
  //    - 1 parser error
  // 2) imported module module_test_errors_imported_a (0 errors)
  //    - This module has no errors
  // 3) imported module module_test_errors_imported_b (1 error)
  //    - 1 parser error
  // 4) imported module module_test_errors_imported_c (8 errors)
  //    - 8 initialization errors
  // 5) imported module module_test_errors_imported_d (0 errors)
  //    - This module only has resolution errors, which are not found unless
  //    - the statements are resolved
  // 6) imported module module_test_errors_imported_e (2 errors)
  //    - 2 initialization errors
  // 7) module module_test_errors_imported_e imports all the other
  //    already-imported modules (except the top level module_test_errors_main).
  //    Since we only add errors for each module once, there are no
  //    additional errors added.                      (0 errors)
  // 8) imported module module_test_errors_imported_f (0 errors)
  //    - This module only has evaluation errors, which are not found unless
  //    - the statements are evaluated
  EXPECT_EQ(21, all_errors.size()) << StatusVectorToString(all_errors);
  all_errors_string = module_catalog()->ModuleErrorsDebugString(
      /*include_nested_module_errors=*/true,
      /*include_catalog_object_errors=*/true);
  // The top level errors are all included.
  for (const std::string& expected_error : expected_top_level_init_errors) {
    EXPECT_THAT(all_errors_string, HasSubstr(expected_error))
        << "expected_error: '" << expected_error << "'"
        << "\nall errors:\n"
        << all_errors_string;
  }

  // Now resolve all statements, and again test AppendModuleErrors with and
  // without including nested module errors.
  module_catalog()->ResolveAllStatements();

  all_errors.clear();
  // After resolution, we expect the same 10 initialization errors, plus
  // 4 new resolution errors (1 each for UDFs, UDAs, TVFs, and constants).  Note
  // that we do not get resolution errors for templated UDFs/UDAs/TVFs since
  // the function bodies for templated functions are not resolved until
  // they are actually called in a query.
  module_catalog()->AppendModuleErrors(/*include_nested_module_errors=*/false,
                                       /*include_catalog_object_errors=*/true,
                                       &all_errors);
  EXPECT_EQ(14, all_errors.size()) << StatusVectorToString(all_errors);
  all_errors_string = module_catalog()->ModuleErrorsDebugString(
      /*include_nested_module_errors=*/false,
      /*include_catalog_object_errors=*/true);
  const std::vector<std::string> expected_top_level_resolution_errors = {
      "Function udf_resolution_error is invalid",
      "Function uda_resolution_error is invalid",
      "Table function tvf_resolution_error is invalid",
      "Constant constant_resolution_error is invalid",
  };
  std::vector<std::string> expected_top_level_errors =
      expected_top_level_init_errors;
  expected_top_level_errors.insert(expected_top_level_errors.end(),
                                   expected_top_level_resolution_errors.begin(),
                                   expected_top_level_resolution_errors.end());
  for (const std::string& expected_error : expected_top_level_errors) {
    EXPECT_THAT(all_errors_string, HasSubstr(expected_error))
        << "expected_error: '" << expected_error << "'"
        << "\nall errors:\n"
        << all_errors_string;
  }

  all_errors.clear();
  // When including nested errors, and after resolution, we get 8 more errors
  // than before resolution (where we originally got 21 errors):
  // 1) 4 new resolution errors in the top level module module_test_errors_main
  // 2) 4 new resolution errors in imported module module_test_errors_imported_d
  module_catalog()->AppendModuleErrors(/*include_nested_module_errors=*/true,
                                       /*include_catalog_object_errors=*/true,
                                       &all_errors);
  EXPECT_EQ(29, all_errors.size()) << StatusVectorToString(all_errors);
  all_errors_string = module_catalog()->ModuleErrorsDebugString(
      /*include_nested_module_errors=*/true,
      /*include_catalog_object_errors=*/true);
  // The top level errors are all included.
  for (const std::string& expected_error : expected_top_level_errors) {
    EXPECT_THAT(all_errors_string, HasSubstr(expected_error))
        << "expected_error: '" << expected_error << "'"
        << "\nall errors:\n"
        << all_errors_string;
  }

  // Now evaluate all constants, and again test AppendModuleErrors with and
  // without including nested module errors.
  module_catalog()->EvaluateAllConstants();

  all_errors.clear();
  // After evaluation, we expect the same 14 initialization and resolution
  // errors, plus 1 new evaluation error.
  module_catalog()->AppendModuleErrors(/*include_nested_module_errors=*/false,
                                       /*include_catalog_object_errors=*/true,
                                       &all_errors);
  EXPECT_EQ(15, all_errors.size()) << StatusVectorToString(all_errors);
  all_errors_string = module_catalog()->ModuleErrorsDebugString(
      /*include_nested_module_errors=*/false,
      /*include_catalog_object_errors=*/true);
  const std::vector<std::string> expected_top_level_evaluation_errors = {
      "evaluation error ",
  };
  expected_top_level_errors.insert(expected_top_level_errors.end(),
                                   expected_top_level_evaluation_errors.begin(),
                                   expected_top_level_evaluation_errors.end());
  for (const std::string& expected_error : expected_top_level_errors) {
    EXPECT_THAT(all_errors_string, HasSubstr(expected_error))
        << "expected_error: '" << expected_error << "'"
        << "\nall errors:\n"
        << all_errors_string;
  }

  all_errors.clear();
  // When including nested errors, and after evaluation, we get 2 more errors
  // than before evaluation (where we originally got 29 errors):
  // 1) 1 new evaluation error in the top level module module_test_errors_main
  // 2) 1 new evaluation error in imported module module_test_errors_imported_f
  module_catalog()->AppendModuleErrors(/*include_nested_module_errors=*/true,
                                       /*include_catalog_object_errors=*/true,
                                       &all_errors);
  EXPECT_EQ(31, all_errors.size()) << StatusVectorToString(all_errors);
  all_errors_string = module_catalog()->ModuleErrorsDebugString(
      /*include_nested_module_errors=*/true,
      /*include_catalog_object_errors=*/true);
  // The top level errors are all included.
  for (const std::string& expected_error : expected_top_level_errors) {
    EXPECT_THAT(all_errors_string, HasSubstr(expected_error))
        << "expected_error: '" << expected_error << "'"
        << "\nall errors:\n"
        << all_errors_string;
  }
}

TEST_F(ModuleTest, empty_module_has_no_imported_modules) {
  const std::string module_contents = "module a";
  GOOGLESQL_ASSERT_OK(
      CreateModuleCatalog({"a", "B", "cc"}, "module_alias", &module_contents));

  EXPECT_FALSE(module_catalog()->HasImportedModule("a"));
  EXPECT_FALSE(module_catalog()->HasImportedModule("B"));
  EXPECT_FALSE(module_catalog()->HasImportedModule("cc"));
  EXPECT_FALSE(module_catalog()->HasImportedModule("bogus"));
  EXPECT_FALSE(module_catalog()->HasImportedModule(""));
}

TEST_F(ModuleTest, module_with_imports_has_imported_modules) {
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"import_a1_a2_a3"}, "module_alias",
                                /*module_contents=*/nullptr));

  // The importing module's name and alias are not included.
  EXPECT_FALSE(module_catalog()->HasImportedModule("import_a1_a2_a3"));
  EXPECT_FALSE(module_catalog()->HasImportedModule("main_module"));

  // Imported modules cannot be found by their module names.
  EXPECT_FALSE(module_catalog()->HasImportedModule("a1_imports_b1"));
  EXPECT_FALSE(module_catalog()->HasImportedModule("a2_imports_b1_b2"));
  EXPECT_FALSE(module_catalog()->HasImportedModule("a3_imports_b2"));

  // Importing modules are found by their aliases.
  EXPECT_TRUE(module_catalog()->HasImportedModule("a1"));
  EXPECT_TRUE(module_catalog()->HasImportedModule("a2"));
  EXPECT_TRUE(module_catalog()->HasImportedModule("a3"));

  // Non-existing aliases are not included.
  EXPECT_FALSE(module_catalog()->HasImportedModule("bogus"));
  EXPECT_FALSE(module_catalog()->HasImportedModule(""));
}

TEST_F(ModuleTest, has_global_objects) {
  std::string module_contents = R"(
  MODULE x.y;
  CREATE PUBLIC FUNCTION udf_public() AS (1);
  CREATE PRIVATE FUNCTION udf_private() AS (1);
  CREATE PUBLIC TABLE FUNCTION tvf_public() AS (SELECT 1);
  CREATE PRIVATE TABLE FUNCTION tvf_private() AS (SELECT 1);
  )";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"x", "y"}, "module_alias", &module_contents));
  EXPECT_FALSE(module_catalog()->HasGlobalScopeObjects());

  module_contents = R"(
  MODULE x.y;
  CREATE PUBLIC FUNCTION udf_public()
    OPTIONS (allowed_references="GLOBAL")
    AS (1);
  )";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"x", "y"}, "module_alias", &module_contents));
  EXPECT_TRUE(module_catalog()->HasGlobalScopeObjects());

  module_contents = R"(
  MODULE x.y;
  CREATE PRIVATE FUNCTION udf_private()
    OPTIONS (allowed_references="GLOBAL")
    AS (1);
  )";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"x", "y"}, "module_alias", &module_contents));
  EXPECT_TRUE(module_catalog()->HasGlobalScopeObjects());

  module_contents = R"(
  MODULE x.y;
  CREATE PUBLIC TABLE FUNCTION tvf_public()
    OPTIONS (allowed_references="GLOBAL")
    AS (SELECT 1);
  )";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"x", "y"}, "module_alias", &module_contents));
  EXPECT_TRUE(module_catalog()->HasGlobalScopeObjects());

  module_contents = R"(
  MODULE x.y;
  CREATE PRIVATE TABLE FUNCTION tvf_private()
    OPTIONS (allowed_references="GLOBAL")
    AS (SELECT 1);
  )";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"x", "y"}, "module_alias", &module_contents));
  EXPECT_TRUE(module_catalog()->HasGlobalScopeObjects());
}

TEST_F(ModuleTest, named_constant_is_added_to_catalog) {
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"named_constant"}, "module_alias",
                                /*module_contents=*/nullptr));

  // Constant <foo> was successfully created. The constant expression has been
  // resolved by FindConstant(), but not evaluated yet. The value is
  // uninitialized because there is no evaluator defined.
  const Constant* constant;
  GOOGLESQL_ASSERT_OK(module_catalog()->FindConstant({"foo"}, &constant));
  ASSERT_THAT(constant, NotNull());
  EXPECT_EQ("foo=Uninitialized value", constant->DebugString());
  ASSERT_TRUE(constant->Is<SQLConstant>());
  const SQLConstant* sql_constant = constant->GetAs<SQLConstant>();

  const std::string expected_verbose_debug_string =
      R"(foo=Uninitialized value
CreateConstantStmt
+-name_path=foo
+-create_scope=CREATE_PUBLIC
+-expr=
  +-Literal(type=STRING, value="bar")
)";
  EXPECT_EQ(expected_verbose_debug_string, sql_constant->VerboseDebugString());
  EXPECT_TRUE(module_catalog()->module_errors().empty());
}

TEST_F(ModuleTest, named_constant_reference_is_resolved) {
  const std::string module_contents = R"(MODULE foo;
CREATE PUBLIC CONSTANT c2 = c1 + 1;
CREATE PUBLIC CONSTANT c1 = 1;
)";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"foo"}, "module_alias", &module_contents));

  // Constant <c2> was successfully created, including its reference to <c1>.
  // The constant expression has been resolved by FindConstant(), but not
  // evaluated yet. The value is uninitialized.
  const Constant* constant;
  GOOGLESQL_ASSERT_OK(module_catalog()->FindConstant({"c2"}, &constant));
  ASSERT_THAT(constant, NotNull());
  EXPECT_EQ("c2=Uninitialized value", constant->DebugString());
  ASSERT_TRUE(constant->Is<SQLConstant>());
  const SQLConstant* sql_constant = constant->GetAs<SQLConstant>();

  const std::string expected_verbose_debug_string =
      R"(c2=Uninitialized value
CreateConstantStmt
+-name_path=c2
+-create_scope=CREATE_PUBLIC
+-expr=
  +-FunctionCall(GoogleSQL:$add(INT64, INT64) -> INT64)
    +-Constant(c1, type=INT64, value=Uninitialized value)
    +-Literal(type=INT64, value=1)
)";

  EXPECT_EQ(expected_verbose_debug_string, sql_constant->VerboseDebugString());
  EXPECT_TRUE(module_catalog()->module_errors().empty());
}

TEST_F(ModuleTest, circular_named_constant_with_constant_is_rejected) {
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"named_constant_circular_constant"},
                                "module_alias",
                                /*module_contents=*/nullptr));
  const Constant* constant;

  // Constant <c3> is rejected because its definition is circular.
  const absl::Status find_constant_status =
      module_catalog()->FindConstant({"c3"}, &constant);
  EXPECT_THAT(find_constant_status,
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(internal::StatusToString(find_constant_status),
              ::testing::HasSubstr(
                  "Recursive dependencies detected when resolving constant "
                  "named_constant_circular_constant.c3, which include objects "
                  "(named_constant_circular_constant.c3, "
                  "named_constant_circular_constant.c2, "
                  "named_constant_circular_constant.c1, "
                  "named_constant_circular_constant.c3)"));
  EXPECT_THAT(constant, IsNull());
}

TEST_F(ModuleTest, circular_named_constant_with_function_is_rejected) {
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"named_constant_circular_function"},
                                "module_alias",
                                /*module_contents=*/nullptr));

  const Constant* constant;

  // Constant <c3> is rejected because its definition is circular.
  const absl::Status find_constant_status =
      module_catalog()->FindConstant({"c3"}, &constant);
  EXPECT_THAT(find_constant_status,
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(internal::StatusToString(find_constant_status),
              ::testing::HasSubstr(
                  "Recursive dependencies detected when resolving constant "
                  "named_constant_circular_function.c3, which include objects "
                  "(named_constant_circular_function.c3, "
                  "named_constant_circular_function.c2, "
                  "named_constant_circular_function.c1, "
                  "named_constant_circular_function.c3)"));
  EXPECT_THAT(constant, IsNull());
}

TEST_F(ModuleTest,
       circular_named_constant_with_templated_function_is_rejected) {
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"named_constant_circular_templated_function"},
                                "module_alias",
                                /*module_contents=*/nullptr));

  const Constant* constant;

  // Constant <c3> is rejected because its definition is circular.
  const absl::Status find_constant_status =
      module_catalog()->FindConstant({"c3"}, &constant);
  EXPECT_THAT(find_constant_status,
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(internal::StatusToString(find_constant_status),
              ::testing::HasSubstr(
                  "Recursive dependencies detected when resolving constant "
                  "named_constant_circular_templated_function.c3, which "
                  "include objects "
                  "(named_constant_circular_templated_function.c3, "
                  "Templated_SQL_Function:f1, "
                  "named_constant_circular_templated_function.c1, "
                  "named_constant_circular_templated_function.c3)"));
  EXPECT_THAT(constant, IsNull());
}

TEST_F(ModuleTest, duplicate_named_constant_is_ignored) {
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"named_constant_duplicate"}, "module_alias",
                                /*module_contents=*/nullptr));

  const Constant* constant;

  // Constant <named_constant> was successfully created. The constant expression
  // has been resolved by FindConstant(), but not evaluated yet. The value is
  // uninitialized. The second definition of <named_constant> is ignored.
  GOOGLESQL_ASSERT_OK(module_catalog()->FindConstant({"named_constant"}, &constant));
  ASSERT_THAT(constant, NotNull());
  EXPECT_EQ("named_constant=Uninitialized value", constant->DebugString());
  EXPECT_THAT(
      module_catalog()->module_errors(),
      Contains(StatusIs(absl::StatusCode::kInvalidArgument,
                        HasSubstr("Constants must have unique names, but found "
                                  "duplicate name named_constant"))));
}

TEST_F(ModuleTest, named_constant_with_resolution_error_is_rejected) {
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"named_constant_invalid_expression"},
                                "module_alias",
                                /*module_contents=*/nullptr));

  const Constant* constant;

  // Constant <resolution_error_named_constant> was successfully created,
  // including its definition that involves incompatible types. The constant
  // expression failed to resolve. The value is uninitialized.
  const absl::Status find_constant_status = module_catalog()->FindConstant(
      {"resolution_error_named_constant"}, &constant);
  EXPECT_THAT(find_constant_status,
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "Constant resolution_error_named_constant is invalid"));
  ErrorLocation error_location;
  ASSERT_TRUE(GetErrorLocation(find_constant_status, &error_location));
  ASSERT_GT(error_location.error_source_size(), 0);
  EXPECT_THAT(error_location.error_source(0).error_message(),
              AllOf(HasSubstr("No matching signature for operator /"),
                    HasSubstr("rgument types: INT64, STRING")));
  EXPECT_THAT(constant, IsNull());
}

TEST_F(ModuleTest, named_constant_with_evaluation_error_is_resolved) {
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"named_constant_invalid_expression"},
                                "module_alias",
                                /*module_contents=*/nullptr));

  // Constant <evaluation_error_named_constant> was successfully created,
  // including its definition that divides by zero. The constant expression has
  // been resolved by FindConstant(), but not evaluated yet, therefore no error
  // is thrown. The value is uninitialized.
  const Constant* constant;
  GOOGLESQL_ASSERT_OK(module_catalog()->FindConstant({"evaluation_error_named_constant"},
                                           &constant));
  ASSERT_THAT(constant, NotNull());
  EXPECT_EQ("evaluation_error_named_constant=Uninitialized value",
            constant->DebugString());
  ASSERT_TRUE(constant->Is<SQLConstant>());
  const SQLConstant* sql_constant = constant->GetAs<SQLConstant>();

  const std::string expected_verbose_debug_string =
      R"(evaluation_error_named_constant=Uninitialized value
CreateConstantStmt
+-name_path=evaluation_error_named_constant
+-create_scope=CREATE_PUBLIC
+-expr=
  +-FunctionCall(GoogleSQL:$divide(DOUBLE, DOUBLE) -> DOUBLE)
    +-Literal(type=DOUBLE, value=1)
    +-Literal(type=DOUBLE, value=0)
)";
  EXPECT_EQ(expected_verbose_debug_string, sql_constant->VerboseDebugString());
  EXPECT_TRUE(module_catalog()->module_errors().empty());
}

TEST_F(ModuleTest, named_constant_referencing_tvf_is_resolved) {
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"named_constant_referencing_tvf"},
                                "module_alias",
                                /*module_contents=*/nullptr));

  // Constant <named_constant> was successfully created, including its
  // definition that references a TVF. The constant expression has been resolved
  // by FindConstant(), but not evaluated yet. The value is uninitialized.
  const Constant* constant;
  GOOGLESQL_ASSERT_OK(module_catalog()->FindConstant({"named_constant_tvf"}, &constant));
  ASSERT_THAT(constant, NotNull());
  EXPECT_EQ("named_constant_tvf=Uninitialized value", constant->DebugString());
  ASSERT_TRUE(constant->Is<SQLConstant>());
  const SQLConstant* sql_constant = constant->GetAs<SQLConstant>();

  const std::string expected_verbose_debug_string =
      R"(named_constant_tvf=Uninitialized value
CreateConstantStmt
+-name_path=named_constant_tvf
+-create_scope=CREATE_PUBLIC
+-expr=
  +-SubqueryExpr
    +-type=DOUBLE
    +-subquery_type=SCALAR
    +-subquery=
      +-LimitOffsetScan
        +-column_list=[tvf.foo#1]
        +-input_scan=
        | +-OrderByScan
        |   +-column_list=[tvf.foo#1]
        |   +-is_ordered=TRUE
        |   +-input_scan=
        |   | +-TVFScan
        |   |   +-column_list=[tvf.foo#1]
        |   |   +-tvf=tvf((INT32 a) -> TABLE<foo DOUBLE>)
        |   |   +-signature=(literal INT32) -> TABLE<foo DOUBLE>
        |   |   +-argument_list=
        |   |   | +-FunctionArgument
        |   |   |   +-expr=
        |   |   |     +-Literal(type=INT32, value=1234)
        |   |   +-column_index_list=[0]
        |   |   +-function_call_signature=(INT32 a) -> TABLE<foo DOUBLE>
        |   +-order_by_item_list=
        |     +-OrderByItem
        |       +-column_ref=
        |         +-ColumnRef(type=DOUBLE, column=tvf.foo#1)
        +-limit=
          +-Literal(type=INT64, value=1)
)";
  EXPECT_EQ(expected_verbose_debug_string, sql_constant->VerboseDebugString());
  EXPECT_TRUE(module_catalog()->module_errors().empty());
}

TEST_F(ModuleTest, named_constant_referencing_uda_is_resolved) {
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"named_constant_referencing_uda"},
                                "module_alias",
                                /*module_contents=*/nullptr));

  // Constant <named_constant_uda> was successfully created, including its
  // definition that references a function. The constant expression has been
  // resolved by FindConstant(), but not evaluated yet. The value is
  // uninitialized.
  const Constant* constant;
  GOOGLESQL_ASSERT_OK(module_catalog()->FindConstant({"named_constant_uda"}, &constant));
  ASSERT_THAT(constant, NotNull());
  EXPECT_EQ("named_constant_uda=Uninitialized value", constant->DebugString());
  ASSERT_TRUE(constant->Is<SQLConstant>());
  const SQLConstant* sql_constant = constant->GetAs<SQLConstant>();

  const std::string expected_verbose_debug_string =
      R"(named_constant_uda=Uninitialized value
CreateConstantStmt
+-name_path=named_constant_uda
+-create_scope=CREATE_PUBLIC
+-expr=
  +-SubqueryExpr
    +-type=INT64
    +-subquery_type=SCALAR
    +-subquery=
      +-ProjectScan
        +-column_list=[$aggregate.$agg1#2]
        +-input_scan=
          +-AggregateScan
            +-column_list=[$aggregate.$agg1#2]
            +-input_scan=
            | +-ArrayScan
            |   +-column_list=[$array.$unnest1#1]
            |   +-array_expr_list=
            |   | +-Literal(type=ARRAY<INT64>, value=[1])
            |   +-element_column_list=[$array.$unnest1#1]
            +-aggregate_list=
              +-$agg1#2 :=
                +-AggregateFunctionCall(Lazy_resolution_function:uda(INT32 a) -> INT64)
                  +-Literal(type=INT32, value=1234)
)";
  EXPECT_EQ(expected_verbose_debug_string, sql_constant->VerboseDebugString());
  EXPECT_TRUE(module_catalog()->module_errors().empty());
}

TEST_F(ModuleTest, named_constant_referencing_udf_is_resolved) {
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"named_constant_referencing_udf"},
                                "module_alias",
                                /*module_contents=*/nullptr));

  // Constant <named_constant_udf> was successfully created, including its
  // definition that references a function. The constant expression has been
  // resolved by FindConstant(), but not evaluated yet. The value is
  // uninitialized.
  const Constant* constant;
  GOOGLESQL_ASSERT_OK(module_catalog()->FindConstant({"named_constant_udf"}, &constant));
  ASSERT_THAT(constant, NotNull());
  EXPECT_EQ("named_constant_udf=Uninitialized value", constant->DebugString());
  ASSERT_TRUE(constant->Is<SQLConstant>());
  const SQLConstant* sql_constant = constant->GetAs<SQLConstant>();

  const std::string expected_verbose_debug_string =
      R"(named_constant_udf=Uninitialized value
CreateConstantStmt
+-name_path=named_constant_udf
+-create_scope=CREATE_PUBLIC
+-expr=
  +-FunctionCall(Lazy_resolution_function:udf(INT32 a) -> INT64)
    +-Literal(type=INT32, value=1234)
)";
  EXPECT_EQ(expected_verbose_debug_string, sql_constant->VerboseDebugString());
  EXPECT_TRUE(module_catalog()->module_errors().empty());
}

TEST_F(ModuleTest, self_referencing_named_constant_is_rejected) {
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"named_constant_self_referencing"},
                                "module_alias",
                                /*module_contents=*/nullptr));

  const Constant* constant;

  // Constant <bar> is rejected because its definition is circular.
  const absl::Status find_constant_status =
      module_catalog()->FindConstant({"public_bar"}, &constant);
  EXPECT_THAT(find_constant_status,
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(
      internal::StatusToString(find_constant_status),
      ::testing::HasSubstr(
          "constant named_constant_self_referencing.public_bar is recursive"));
  EXPECT_THAT(constant, IsNull());
}

TEST_F(ModuleTest, named_constant_overrides_builtin_named_constant) {
  std::unique_ptr<SimpleConstant> builtin_constant;
  GOOGLESQL_CHECK_OK(
      SimpleConstant::Create({"foo"}, Value::Int64(9999), &builtin_constant));
  builtin_function_catalog()->AddOwnedConstant(builtin_constant.release());

  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"named_constant"}, "module_alias",
                                /*module_contents=*/nullptr));

  // Constant <foo> was successfully created. It overrides the built-in constant
  // with the same name but a different type. The constant expression has been
  // resolved by FindConstant(), but not evaluated yet. The value is
  // uninitialized.
  const Constant* constant;
  GOOGLESQL_ASSERT_OK(module_catalog()->FindConstant({"foo"}, &constant));
  ASSERT_THAT(constant, NotNull());
  EXPECT_EQ("foo=Uninitialized value", constant->DebugString());
  ASSERT_TRUE(constant->Is<SQLConstant>());
  const SQLConstant* sql_constant = constant->GetAs<SQLConstant>();

  const std::string expected_verbose_debug_string =
      R"(foo=Uninitialized value
CreateConstantStmt
+-name_path=foo
+-create_scope=CREATE_PUBLIC
+-expr=
  +-Literal(type=STRING, value="bar")
)";

  EXPECT_EQ(expected_verbose_debug_string, sql_constant->VerboseDebugString());
  EXPECT_TRUE(module_catalog()->module_errors().empty());
}

TEST_F(ModuleTest, module_error_message_string_combines_multiple_errors) {
  std::string module_contents = "module";

  absl::Status create_status =
      CreateModuleCatalog({"a", "b", "cc"}, "module_alias", &module_contents);
  GOOGLESQL_EXPECT_OK(create_status);
  ASSERT_EQ(module_catalog()->ModuleErrorMessageString(),
            "Syntax error: Unexpected end of statement\n\n"
            "A module must contain exactly one MODULE statement");
}

TEST_F(ModuleTest, module_error_message_string_makes_message_with_no_errors) {
  std::string module_contents = "module a";

  absl::Status create_status =
      CreateModuleCatalog({"a", "b", "cc"}, "module_alias", &module_contents);
  GOOGLESQL_EXPECT_OK(create_status);
  ASSERT_EQ(module_catalog()->ModuleErrorMessageString(), "no errors");
}

TEST_F(ModuleTest, resolve_all_statements_skip_global) {
  const std::string module_contents = R"(MODULE a;
CREATE PUBLIC FUNCTION public_global_func() OPTIONS (allowed_references='global') AS (CONCAT(error));
CREATE PRIVATE FUNCTION private_global_func() OPTIONS (allowed_references='global') AS (CONCAT(error));
CREATE PUBLIC TABLE FUNCTION public_global_tvf() OPTIONS (allowed_references='global') AS (SELECT CONCAT(error));
CREATE PRIVATE TABLE FUNCTION private_global_tvf() OPTIONS (allowed_references='global') AS (SELECT CONCAT(error));
)";
  absl::Status create_status =
      CreateModuleCatalog({"a"}, "module_alias", &module_contents);
  GOOGLESQL_EXPECT_OK(create_status);

  // No errors before attempting to resolve the module statements.
  std::vector<absl::Status> all_errors;
  module_catalog()->AppendModuleErrors(/*include_nested_module_errors=*/true,
                                       /*include_catalog_object_errors=*/true,
                                       &all_errors);
  EXPECT_TRUE(all_errors.empty()) << StatusVectorToString(all_errors);

  // Resolve all statements, skipping global-scope objects; shouldn't add any
  // errors because all objects are global-scope.
  module_catalog()->ResolveAllStatements(
      /*include_global_scope_objects=*/false);
  module_catalog()->AppendModuleErrors(/*include_nested_module_errors=*/true,
                                       /*include_catalog_object_errors=*/true,
                                       &all_errors);
  EXPECT_TRUE(all_errors.empty()) << StatusVectorToString(all_errors);

  // Resolve global-scope objects, now we should have errors.
  module_catalog()->ResolveAllStatements();
  module_catalog()->AppendModuleErrors(/*include_nested_module_errors=*/true,
                                       /*include_catalog_object_errors=*/true,
                                       &all_errors);
  EXPECT_EQ(4, all_errors.size()) << StatusVectorToString(all_errors);
  EXPECT_THAT(
      all_errors,
      UnorderedElementsAreArray(
          {StatusIs(absl::StatusCode::kInvalidArgument,
                    HasSubstr("Function public_global_func is invalid")),
           StatusIs(absl::StatusCode::kInvalidArgument,
                    HasSubstr("Function private_global_func is invalid")),
           StatusIs(absl::StatusCode::kInvalidArgument,
                    HasSubstr("Table function public_global_tvf is invalid")),
           StatusIs(
               absl::StatusCode::kInvalidArgument,
               HasSubstr("Table function private_global_tvf is invalid"))}));
}

// Test case for b/372326459.
TEST_F(ModuleTest, prefer_tvf_arg_over_catalog_table) {
  const std::string module_contents = R"(MODULE path.to.my.module;

# The TVF we call with syntax `TABLE x`.
CREATE PRIVATE TABLE FUNCTION tvf1(input ANY TABLE) AS (
  SELECT * FROM input
);

# A non-templated TVF (`SQLTableValuedFunction`) with an ambiguous `TABLE x`.
CREATE PRIVATE TABLE FUNCTION tvf2(test_table TABLE<a INT64, b INT64, c INT64>)
  OPTIONS (allowed_references='global')
AS (
  SELECT * FROM tvf1(TABLE test_table)
);

# A templated TVF (`TemplatedSQLTVF`) with an ambiguous `TABLE x`.
CREATE PRIVATE TABLE FUNCTION tvf3(test_table ANY TABLE)
  OPTIONS (allowed_references='global')
AS (
  SELECT * FROM tvf1(TABLE test_table)
);

# Referencing the catalog table with `TABLE x`. This acts as a baseline.
CREATE PUBLIC TABLE FUNCTION test1()
  OPTIONS (allowed_references='global')
AS (
  SELECT * FROM tvf1(TABLE test_table)
);

# Calling the non-templated TVF. We intentionally pass a subquery whose output
# schema is different from the catalog table.
CREATE PUBLIC TABLE FUNCTION test2()
  OPTIONS (allowed_references='global')
AS (
  SELECT * FROM tvf2((SELECT 1 AS a, 2 AS b, 3 AS c))
);

# Calling the templated TVF. We intentionally pass a subquery whose output
# schema is different from the catalog table. Note that templated functions are
# only resolved when being called.
CREATE PUBLIC TABLE FUNCTION test3()
  OPTIONS (allowed_references='global')
AS (
  SELECT * FROM tvf3((SELECT 1 AS a, 2 AS b, 3 AS c))
);
)";
  // TODO: b/372326459 - We can remove this feature once the new resolution
  // order is rolled out. In particular, F1 should also stop setting this.
  mutable_analyzer_options()->mutable_language()->EnableLanguageFeature(
      FEATURE_TABLE_SYNTAX_RESOLVE_ARGUMENT_LAST);
  GOOGLESQL_EXPECT_OK(CreateModuleCatalog({"path", "to", "my", "module"}, "alias",
                                &module_contents));
  module_catalog()->ResolveAllStatements();
  std::vector<absl::Status> errors;
  module_catalog()->AppendModuleErrors(/*include_nested_module_errors=*/true,
                                       /*include_catalog_object_errors=*/true,
                                       &errors);
  EXPECT_TRUE(errors.empty()) << StatusVectorToString(errors);

  auto get_output_column_debug_strings = [](const SQLTableValuedFunction* tvf) {
    std::vector<std::string> result;
    for (const auto& output_column :
         tvf->ResolvedStatement()->output_column_list()) {
      result.push_back(output_column->DebugString());
    }
    return result;
  };

  const TableValuedFunction* tvf = nullptr;

  GOOGLESQL_ASSERT_OK(module_catalog()->FindTableValuedFunction({"test1"}, &tvf));
  ASSERT_TRUE(tvf->Is<SQLTableValuedFunction>());
  EXPECT_THAT(
      get_output_column_debug_strings(tvf->GetAs<SQLTableValuedFunction>()),
      ElementsAre("tvf1.col#2 AS col [INT64]\n"));

  GOOGLESQL_ASSERT_OK(module_catalog()->FindTableValuedFunction({"test2"}, &tvf));
  ASSERT_TRUE(tvf->Is<SQLTableValuedFunction>());
  EXPECT_THAT(
      get_output_column_debug_strings(tvf->GetAs<SQLTableValuedFunction>()),
      ElementsAre("tvf2.a#4 AS a [INT64]\n", "tvf2.b#5 AS b [INT64]\n",
                  "tvf2.c#6 AS c [INT64]\n"));

  GOOGLESQL_ASSERT_OK(module_catalog()->FindTableValuedFunction({"test3"}, &tvf));
  ASSERT_TRUE(tvf->Is<SQLTableValuedFunction>());
  EXPECT_THAT(
      get_output_column_debug_strings(tvf->GetAs<SQLTableValuedFunction>()),
      ElementsAre("tvf3.a#4 AS a [INT64]\n", "tvf3.b#5 AS b [INT64]\n",
                  "tvf3.c#6 AS c [INT64]\n"));
}

TEST_F(ModuleTest, ViewsInModulesLanguageFeatureEnabled) {
  const std::string module_contents = R"(MODULE m;
  CREATE PUBLIC VIEW foo AS select 1 as bar, "2" as baz)";

  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"m"}, "module_alias", &module_contents));
  EXPECT_TRUE(module_catalog()->module_errors().empty());

  const Table* view;
  GOOGLESQL_ASSERT_OK(module_catalog()->FindTable({"foo"}, &view));
  ASSERT_TRUE(view->Is<SQLView>());
  const SQLView* sql_view = view->GetAs<SQLView>();
  EXPECT_EQ(sql_view->Name(), "foo");
  ASSERT_EQ(sql_view->NumColumns(), 2);
  EXPECT_EQ(sql_view->GetColumn(0)->Name(), "bar");
  EXPECT_TRUE(sql_view->GetColumn(0)->GetType()->IsInt64());
  EXPECT_EQ(sql_view->GetColumn(1)->Name(), "baz");
  EXPECT_TRUE(sql_view->GetColumn(1)->GetType()->IsString());
  EXPECT_EQ(sql_view->sql_security(), SQLView::kSecurityInvoker);
}

TEST_F(ModuleTest, ProceduresInModulesLanguageFeatureEnabled) {
  const std::string module_contents =
      R"(MODULE m; CREATE PUBLIC PROCEDURE my_proc(INOUT x INT64, y STRING) OPTIONS(allowed_references='GLOBAL') BEGIN END;)";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"m"}, "module_alias", &module_contents));
  EXPECT_TRUE(module_catalog()->module_errors().empty());
  const Procedure* procedure;

  GOOGLESQL_ASSERT_OK(module_catalog()->FindProcedure({"my_proc"}, &procedure));

  ASSERT_THAT(procedure, NotNull());
  EXPECT_EQ(procedure->Name(), "my_proc");
  ASSERT_EQ(procedure->signature().arguments().size(), 2);
  EXPECT_EQ(procedure->signature().DebugString(/*function_name=*/"",
                                               /*verbose=*/false),
            "(INT64 x, STRING y) -> <void>");
}

TEST_F(ModuleTest, PrivateProcedureIsNotFound) {
  const std::string module_contents =
      R"(MODULE m; CREATE PRIVATE PROCEDURE my_proc() OPTIONS(allowed_references='GLOBAL') BEGIN END;)";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"m"}, "module_alias", &module_contents));
  EXPECT_TRUE(module_catalog()->module_errors().empty());
  const Procedure* procedure;

  absl::Status status =
      module_catalog()->FindProcedure({"my_proc"}, &procedure);

  EXPECT_THAT(status,
              StatusIs(absl::StatusCode::kNotFound,
                       "Procedure not found: my_proc not found in catalog m"));
}

TEST_F(ModuleTest, DuplicateProcedureCausesError) {
  const std::string module_contents = R"(MODULE m;
  CREATE PUBLIC PROCEDURE my_proc(x INT64) OPTIONS(allowed_references='GLOBAL') BEGIN SELECT 1; END;
  CREATE PUBLIC PROCEDURE my_proc(y STRING) OPTIONS(allowed_references='GLOBAL') BEGIN SELECT 2; END;)";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"m"}, "module_alias", &module_contents));
  const Procedure* procedure;

  // The first procedure definition is kept, and the second one results in an
  // error.
  GOOGLESQL_ASSERT_OK(module_catalog()->FindProcedure({"my_proc"}, &procedure));
  ASSERT_THAT(procedure, NotNull());
  ASSERT_EQ(procedure->signature().arguments().size(), 1);
  EXPECT_EQ(procedure->signature().argument(0).argument_name(), "x");
  EXPECT_EQ(procedure->signature().argument(0).type()->DebugString(), "INT64");

  EXPECT_THAT(
      module_catalog()->module_errors(),
      Contains(StatusIs(absl::StatusCode::kInvalidArgument,
                        HasSubstr("Procedures must have unique names, but "
                                  "found duplicate name my_proc"))));
}

TEST_F(ModuleTest, GetOrderedPublicAndPrivateProcedureMap) {
  const std::string module_contents = R"(MODULE m;
  CREATE PUBLIC PROCEDURE pub_proc() OPTIONS(allowed_references='GLOBAL') BEGIN END;
  CREATE PRIVATE PROCEDURE priv_proc() OPTIONS(allowed_references='GLOBAL') BEGIN END;
)";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"m"}, "module_alias", &module_contents));
  EXPECT_TRUE(module_catalog()->module_errors().empty());
  OrderedLazyResolutionProcedureMap procedure_map;

  module_catalog()->GetOrderedPublicAndPrivateProcedureMap(&procedure_map);

  ASSERT_EQ(2, procedure_map.size());
  EXPECT_TRUE(procedure_map.contains("pub_proc"));
  EXPECT_TRUE(procedure_map.contains("priv_proc"));
}

TEST_F(ModuleTest, ProcedureWithResolutionErrorIsRejected) {
  const std::string module_contents = R"(MODULE m;
  CREATE PUBLIC PROCEDURE my_proc(x y) OPTIONS(allowed_references='GLOBAL') BEGIN END;)";

  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"m"}, "module_alias", &module_contents));
  EXPECT_TRUE(module_catalog()->module_errors().empty());

  const Procedure* procedure;
  absl::Status status =
      module_catalog()->FindProcedure({"my_proc"}, &procedure);
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kInvalidArgument,
                               HasSubstr("Procedure my_proc is invalid")));
  EXPECT_THAT(procedure, IsNull());
}

TEST_F(ModuleTest, ViewsInModulesLanguageFeatureDisabled) {
  DisableLanguageFeature(FEATURE_VIEWS_IN_MODULES);
  const std::string module_contents = R"(MODULE m;
  CREATE PUBLIC VIEW foo AS select 1 as bar, 2 as baz)";

  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"m"}, "module_alias", &module_contents));
  EXPECT_TRUE(module_catalog()->module_errors().empty());

  // Error is surfaced only when the view is referenced.
  const Table* view;
  absl::Status status = module_catalog()->FindTable({"foo"}, &view);
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kInvalidArgument,
                               HasSubstr("View foo is invalid")));
  EXPECT_THAT(internal::StatusToString(status),
              HasSubstr("CREATE VIEW statements are not supported in modules"));
}

TEST_F(ModuleTest, ProceduresInModulesLanguageFeatureDisabled) {
  DisableLanguageFeature(FEATURE_PROCEDURES_IN_MODULES);
  const std::string module_contents =
      R"(MODULE m; CREATE PUBLIC PROCEDURE my_proc() OPTIONS(allowed_references='GLOBAL') BEGIN END)";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"m"}, "module_alias", &module_contents));
  EXPECT_TRUE(module_catalog()->module_errors().empty());
  const Procedure* procedure;

  absl::Status status =
      module_catalog()->FindProcedure({"my_proc"}, &procedure);

  EXPECT_THAT(status, StatusIs(absl::StatusCode::kInvalidArgument,
                               HasSubstr("Procedure my_proc is invalid")));
  EXPECT_THAT(
      internal::StatusToString(status),
      HasSubstr("CREATE PROCEDURE statements are not supported in modules"));
}

TEST_F(ModuleTest, ProcedureWithLanguageRemoteIsSupported) {
  const std::string module_contents =
      R"(MODULE m; CREATE PUBLIC PROCEDURE my_proc() OPTIONS(allowed_references='GLOBAL') LANGUAGE REMOTE;)";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"m"}, "module_alias", &module_contents));
  EXPECT_TRUE(module_catalog()->module_errors().empty());
  const Procedure* procedure;

  GOOGLESQL_ASSERT_OK(module_catalog()->FindProcedure({"my_proc"}, &procedure));

  EXPECT_THAT(procedure, NotNull());
}

TEST_F(ModuleTest, ProcedureWithUnsupportedLanguage) {
  const std::string module_contents =
      R"(MODULE m; CREATE PUBLIC PROCEDURE my_proc() OPTIONS(allowed_references='GLOBAL') LANGUAGE js;)";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"m"}, "module_alias", &module_contents));
  EXPECT_THAT(module_catalog()->module_errors(), IsEmpty());
  const Procedure* procedure;

  absl::Status status =
      module_catalog()->FindProcedure({"my_proc"}, &procedure);
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kInvalidArgument,
                               HasSubstr("Procedure my_proc is invalid")));
  EXPECT_THAT(internal::StatusToString(status),
              HasSubstr("Invalid LANGUAGE: js. Only LANGUAGE REMOTE is "
                        "supported for procedures inside modules"));
}

TEST_F(ModuleTest, ProcedureWithConnectionClauseIsRejected) {
  const std::string module_contents =
      R"(MODULE m;
      CREATE PUBLIC PROCEDURE my_proc()
        WITH CONNECTION dummy_conn
        OPTIONS(allowed_references='GLOBAL')
      BEGIN END;)";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"m"}, "module_alias", &module_contents));
  EXPECT_THAT(module_catalog()->module_errors(), IsEmpty());
  const Procedure* procedure;

  absl::Status status =
      module_catalog()->FindProcedure({"my_proc"}, &procedure);
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kInvalidArgument,
                               HasSubstr("Procedure my_proc is invalid")));
  EXPECT_THAT(internal::StatusToString(status),
              HasSubstr("Procedures inside modules must not have a WITH "
                        "CONNECTION clause"));
}

TEST_F(ModuleTest, ProcedureWithUnsupportedOptionsIsAllowed) {
  const std::string module_contents = R"(MODULE m;
  CREATE PUBLIC PROCEDURE my_proc()
  OPTIONS(unsupported_opt = 1, allowed_references='GLOBAL')
  BEGIN END;)";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"m"}, "module_alias", &module_contents));
  EXPECT_THAT(module_catalog()->module_errors(), IsEmpty());
  const Procedure* procedure;

  GOOGLESQL_EXPECT_OK(module_catalog()->FindProcedure({"my_proc"}, &procedure));
}

TEST_F(ModuleTest, ProcedureWithMultipleErrors) {
  const std::string module_contents =
      R"(MODULE m;
      CREATE PUBLIC PROCEDURE my_proc()
      WITH CONNECTION dummy_conn
      OPTIONS(unsupported_opt = 1)
      LANGUAGE js;)";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"m"}, "module_alias", &module_contents));
  EXPECT_THAT(module_catalog()->module_errors(), IsEmpty());
  const Procedure* procedure;

  EXPECT_THAT(module_catalog()->FindProcedure({"my_proc"}, &procedure),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Procedure my_proc is invalid")));
}

TEST_F(ModuleTest, ProcedureWithMultipleAllowedReferencesErrors) {
  const std::string module_contents =
      R"(MODULE m;
      CREATE PUBLIC PROCEDURE my_proc()
      OPTIONS(allowed_references = 'GLOBAL', allowed_references = 'GLOBAL')
      BEGIN END;)";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"m"}, "module_alias", &module_contents));
  EXPECT_THAT(module_catalog()->module_errors(), IsEmpty());
  const Procedure* procedure;

  EXPECT_THAT(module_catalog()->FindProcedure({"my_proc"}, &procedure),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Procedure my_proc is invalid")));
}

TEST_F(ModuleTest, ProcedureWithoutGlobalAllowedReferencesIsRejected) {
  const std::string module_contents = R"(MODULE m;
  CREATE PUBLIC PROCEDURE my_proc() BEGIN END;)";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"m"}, "module_alias", &module_contents));
  EXPECT_THAT(module_catalog()->module_errors(), IsEmpty());
  const Procedure* procedure;

  EXPECT_THAT(module_catalog()->FindProcedure({"my_proc"}, &procedure),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Procedure my_proc is invalid")));
}

TEST_F(ModuleTest, ProcedureWithBuiltinAllowedReferencesIsRejected) {
  const std::string module_contents = R"(MODULE m;
  CREATE PUBLIC PROCEDURE my_proc()
  OPTIONS(allowed_references = 'BUILTIN')
  BEGIN END;)";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"m"}, "module_alias", &module_contents));
  EXPECT_THAT(module_catalog()->module_errors(), IsEmpty());
  const Procedure* procedure;

  EXPECT_THAT(module_catalog()->FindProcedure({"my_proc"}, &procedure),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Procedure my_proc is invalid")));
}

TEST_F(ModuleTest, ProcedureInheritsGlobalAllowedReferencesFromModule) {
  const std::string module_contents =
      R"(MODULE m OPTIONS(allowed_references = 'GLOBAL');
  CREATE PUBLIC PROCEDURE my_proc() BEGIN END;)";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"m"}, "module_alias", &module_contents));
  EXPECT_TRUE(module_catalog()->module_errors().empty());
  const Procedure* procedure;

  GOOGLESQL_ASSERT_OK(module_catalog()->FindProcedure({"my_proc"}, &procedure));

  EXPECT_THAT(procedure, NotNull());
}

TEST_F(ModuleTest, ProcedureOverridesDefaultAllowedReferencesFromModule) {
  const std::string module_contents =
      R"(
        MODULE m OPTIONS(allowed_references = 'BUILTIN');
        CREATE PUBLIC PROCEDURE my_proc()
          OPTIONS(allowed_references = 'GLOBAL')
        BEGIN END;)";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"m"}, "module_alias", &module_contents));
  EXPECT_TRUE(module_catalog()->module_errors().empty());
  const Procedure* procedure;

  GOOGLESQL_ASSERT_OK(module_catalog()->FindProcedure({"my_proc"}, &procedure));

  EXPECT_THAT(procedure, NotNull());
}

TEST_F(ModuleTest, ModuleCatalogDebugStringTest) {
  const std::string module_contents = R"(MODULE m;
CREATE PRIVATE VIEW foo as (select x from unnest([3, 1, 4, 1, 5]) as x);
CREATE VIEW pi as (select 3.14);
CREATE PUBLIC FUNCTION bar (a int64) as (a * (select max(x) from foo));
CREATE PUBLIC TABLE FUNCTION baz() as (select x, count(x) as y from foo group by x);
CREATE PUBLIC PROCEDURE qux() OPTIONS(allowed_references='GLOBAL') BEGIN END;
CREATE PUBLIC CONSTANT lorem = 'ipsum';
Select * from foo;
)";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"m"}, "alias", &module_contents));

  // TODO: Fix ZetaSQL error location output.
  const std::string unsupported_statement_error =
      "INVALID_ARGUMENT: Unsupported statement kind: QueryStatement "
      "[type.googleapis.com/googlesql.ErrorLocation='\\x08\\x08\\x10\\x01\\x1a"
      "\\x01m(\\x00\\x30\\x00']";

  EXPECT_EQ(
      module_catalog()->DebugString(/*include_module_contents=*/true),
      absl::StrCat("Module 'm'\nModule contents:\n", module_contents,
                   R"(
Initialized functions:
Lazy_resolution_function:bar

Initialized table functions:
TVF:baz

Initialized views:
PRIVATE VIEW foo

VIEW pi
ERROR: View pi is invalid [at m:3:13]; CREATE statements within modules require the PUBLIC or PRIVATE modifier [at m:3:1]

Initialized procedures:
PROCEDURE qux

Initialized constants:
CONSTANT lorem=Uninitialized value (unknown type)

)",
                   "Initialization errors:\n", unsupported_statement_error));
}

//  A mock callback for evaluating named constants.
class MockConstantEvaluator : public ConstantEvaluator {
 public:
  MOCK_METHOD(absl::StatusOr<Value>, Evaluate,
              (const ResolvedExpr& constant_expression), (override));
};

// Tests for evaluating named constants during the catalog lookup.
// Tests are parameterized by whether to create the ConstantEvaluator in
// ModuleFactoryOptions (old way) or AnalyzerOptions (new way).
// TODO: b/277365877 - Simplify once `ModuleFactoryOptions::constant_evaluator`
// is deprecated and removed.
class ConstantEvaluatorTest : public ModuleTest,
                              public ::testing::WithParamInterface<bool> {
 protected:
  ConstantEvaluatorTest() = default;

  ~ConstantEvaluatorTest() override = default;

  std::string Iteration(size_t iteration) const {
    return absl::StrCat(" in iteration #", iteration);
  }

  std::string Iteration(size_t iteration, const Constant* constant) const {
    return absl::StrCat(constant->DebugString(), " in iteration #", iteration);
  }
};

INSTANTIATE_TEST_SUITE_P(ConstantEvaluatorTest, ConstantEvaluatorTest,
                         ::testing::Bool());

TEST_F(ConstantEvaluatorTest,
       named_constant_is_resolved_but_not_evaluated_without_evaluator) {
  // Create a module catalog with no evaluator.
  const std::string module_contents = R"(MODULE named_constant;
CREATE PUBLIC CONSTANT foo = 'bar';
)";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"foo"}, "module_alias", &module_contents));

  // Constant <foo> was successfully created. The constant expression has been
  // resolved by FindConstant(), but not evaluated yet. The value is
  // uninitialized.
  const Constant* constant;
  GOOGLESQL_ASSERT_OK(module_catalog()->FindConstant({"foo"}, &constant));
  ASSERT_THAT(constant, NotNull());
  EXPECT_EQ("foo=Uninitialized value", constant->DebugString());

  ASSERT_TRUE(constant->Is<SQLConstant>());
  EXPECT_FALSE(constant->GetAs<SQLConstant>()->evaluation_result().ok());
  EXPECT_TRUE(module_catalog()->module_errors().empty());
}

TEST_P(ConstantEvaluatorTest,
       named_constant_is_resolved_and_evaluated_once_with_evaluator) {
  // Create a mock ConstantEvaluator that returns a string value.
  ::testing::StrictMock<MockConstantEvaluator> mock_constant_evaluator;
  EXPECT_CALL(mock_constant_evaluator, Evaluate(_))
      .Times(1)
      .WillOnce(Return(Value::String("bar")));

  // Either create the mock evaluator in ModuleFactoryOptions or AnalyzerOptions
  // depending on the test parameter.
  // TODO: b/277365877 - Remove after
  // `ModuleFactoryOptions::constant_evaluator` is deprecated and removed.
  bool constant_evaluator_in_analyzer_options = GetParam();
  ModuleFactoryOptions module_factory_options =
      constant_evaluator_in_analyzer_options
          ? ModuleFactoryOptions{}
          : ModuleFactoryOptions{.constant_evaluator =
                                     &mock_constant_evaluator};
  if (constant_evaluator_in_analyzer_options) {
    mutable_analyzer_options()->set_constant_evaluator(
        &mock_constant_evaluator);
  }

  // Create a module catalog using the mock evaluator.
  const std::string module_contents = R"(MODULE named_constant;
CREATE PUBLIC CONSTANT foo = 'bar';
)";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"foo"}, "module_alias", &module_contents,
                                module_factory_options));

  // Call FindConstant() multiple times to check that evaluation is performed
  // only once.
  const Constant* constant;
  for (size_t i : {0, 1, 2}) {
    // Constant <foo> was successfully created. The constant expression has been
    // resolved and evaluated by FindConstant(). The value has been set.
    GOOGLESQL_ASSERT_OK(module_catalog()->FindConstant({"foo"}, &constant))
        << Iteration(i);
    ASSERT_THAT(constant, NotNull()) << Iteration(i);
    EXPECT_EQ(R"(foo="bar")", constant->DebugString()) << Iteration(i);

    ASSERT_TRUE(constant->Is<SQLConstant>()) << Iteration(i);
    GOOGLESQL_ASSERT_OK(constant->GetAs<SQLConstant>()->evaluation_result())
        << Iteration(i);
    EXPECT_EQ(constant->GetAs<SQLConstant>()
                  ->evaluation_result()
                  .value()
                  .string_value(),
              "bar")
        << Iteration(i, constant);
    EXPECT_TRUE(module_catalog()->module_errors().empty()) << Iteration(i);
  }
}

TEST_P(ConstantEvaluatorTest,
       find_constant_returns_evaluation_error_repeatedly) {
  // Create a mock ConstantEvaluator that returns an error.
  ::testing::StrictMock<MockConstantEvaluator> mock_constant_evaluator;
  EXPECT_CALL(mock_constant_evaluator, Evaluate(_))
      .Times(1)
      .WillRepeatedly(Return(
          absl::Status(absl::StatusCode::kInternal, "division by zero")));

  // Either create the mock evaluator in ModuleFactoryOptions or AnalyzerOptions
  // depending on the test parameter.
  // TODO: b/277365877 - Remove after
  // `ModuleFactoryOptions::constant_evaluator` is deprecated and removed.
  bool constant_evaluator_in_analyzer_options = GetParam();
  ModuleFactoryOptions module_factory_options =
      constant_evaluator_in_analyzer_options
          ? ModuleFactoryOptions{}
          : ModuleFactoryOptions{.constant_evaluator =
                                     &mock_constant_evaluator};
  if (constant_evaluator_in_analyzer_options) {
    mutable_analyzer_options()->set_constant_evaluator(
        &mock_constant_evaluator);
  }

  // Create a module catalog using the mock evaluator.
  const std::string module_contents = R"(MODULE named_constant;
CREATE PUBLIC CONSTANT foo = 1 / 0;
)";
  GOOGLESQL_ASSERT_OK(CreateModuleCatalog({"foo"}, "module_alias", &module_contents,
                                module_factory_options));

  const Constant* constant;

  // Call FindConstant() multiple times to check if the evaluation error sticks.
  for (size_t i : {0, 1, 2}) {
    // Constant <foo> was successfully created. The constant expression has been
    // resolved and evaluated by FindConstant(). The evaluation resulted in an
    // error, therefore an error is returned.
    EXPECT_THAT(module_catalog()->FindConstant({"foo"}, &constant),
                StatusIs(absl::StatusCode::kInternal, "division by zero"))
        << Iteration(i);
    EXPECT_THAT(constant, IsNull()) << Iteration(i);

    // Evaluation errors are added to the Constant, not to the module.
    EXPECT_TRUE(module_catalog()->module_errors().empty()) << Iteration(i);
  }
}

}  // namespace googlesql
