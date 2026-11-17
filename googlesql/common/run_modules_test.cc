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

#include "googlesql/base/path.h"
#include "googlesql/common/status_payload_utils.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/analyzer.h"
#include "googlesql/public/analyzer_options.h"
#include "googlesql/public/analyzer_output.h"
#include "googlesql/public/catalog.h"
#include "googlesql/public/catalog_wrapper.h"
#include "googlesql/public/constant_evaluator.h"
#include "googlesql/public/evaluator.h"
#include "googlesql/public/language_options.h"
#include "googlesql/public/module_factory.h"
#include "googlesql/public/modules.h"
#include "googlesql/public/options.pb.h"
#include "googlesql/public/prepared_expression_constant_evaluator.h"
#include "googlesql/public/type.h"
#include "googlesql/resolved_ast/resolved_ast.h"
#include "googlesql/testdata/sample_catalog.h"
#include "googlesql/testing/test_module_contents_fetcher.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/flags/flag.h"
#include "absl/functional/bind_front.h"
#include "googlesql/base/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "googlesql/base/status_macros.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "file_based_test_driver/file_based_test_driver.h"
#include "file_based_test_driver/run_test_case_result.h"
#include "file_based_test_driver/test_case_options.h"
#include "google/protobuf/compiler/importer.h"
#include "google/protobuf/descriptor_database.h"
#include "googlesql/base/ret_check.h"
#include "googlesql/base/status.h"

ABSL_FLAG(std::string, test_file_spec_pattern, "*.module_test",
          "Regex pattern for modules test data files.");

namespace {
// Produce a path to the test input source directory, relative to which modules
// and protos can be read.
std::string TestSrcDirBase() {
  return googlesql_base::JoinPath(::testing::SrcDir(), "_main");
}
}  // namespace

namespace googlesql {

// Collects google::protobuf::Importer errors.
class TestMultiFileErrorCollector
    : public google::protobuf::compiler::MultiFileErrorCollector {
 public:
  TestMultiFileErrorCollector() = default;

  TestMultiFileErrorCollector(const TestMultiFileErrorCollector&) = delete;
  TestMultiFileErrorCollector& operator=(const TestMultiFileErrorCollector&) =
      delete;

  void RecordError(absl::string_view filename, int line, int column,
                   absl::string_view message) override {
    absl::StrAppend(&error_, "File ", filename, " Line ", line, " Column ",
                    column, " :", message, "\n");
  }
  const std::string& GetError() const { return error_; }

 private:
  std::string error_;
};

class RunModulesTest : public ::testing::Test {
 public:  // Pointer-to-member-function usage requires public member functions
  // Valid options in the case cases:
  //   input_is_module_contents - Indicates whether or not the test input
  //     string is the module contents.  If 'false', then the test input
  //     string must instead be a single IMPORT statement that imports
  //     the module contents from a module file.
  const std::string kInputIsModuleContents = "input_is_module_contents";

  //   resolve_statements - Indicates whether valid module statements should
  //     be resolved before producing the output result.
  const std::string kResolveStatements = "resolve_statements";

  //   show_module_contents - Indicates whether the raw module contents
  //     should be included in the output result.
  const std::string kShowModuleContents = "show_module_contents";

  //   use_full_debugstring - Indicates whether the output of functions
  //     uses FullDebugString(), or just DebugString().
  const std::string kUseFullDebugString = "use_full_debugstring";

  //   debug_string_include_types - Indicates whether the output module debug
  //     string should should include type details. This option can be used when
  //     the point of the test is not to observe the details of the imported
  //     protos and/or when printing the type details would become a change
  //     detector test on the proto file content.
  const std::string kDebugStringIncludeTypes = "debug_string_include_types";

  //   show_imported_modules - Indicates whether the test output should include
  //     the contents of imported modules.
  const std::string kShowImportedModules = "show_imported_modules";

  //   provide_global_catalog - Indicates whether a global-scope catalog should
  //     be included when when constructing modules. If disabled, then `nullptr`
  //     is provided and global-scope module objects will produce errors. If
  //     enabled, then the sample global-scope catalog is provided.
  static constexpr absl::string_view kProvideGlobalCatalog =
      "provide_global_catalog";

  //   error_message_mode - Indicates the error message formatting style to use
  //     in the test output. Supported options: WITH_PAYLOAD,
  //     MULTI_LINE_WITH_CARET, ONE_LINE.
  static constexpr absl::string_view kErrorMessageMode = "error_message_mode";

  RunModulesTest() {
    test_case_options_.RegisterBool(kInputIsModuleContents, true);
    test_case_options_.RegisterBool(kResolveStatements, true);
    test_case_options_.RegisterBool(kShowModuleContents, false);
    test_case_options_.RegisterBool(kUseFullDebugString, false);
    test_case_options_.RegisterBool(kDebugStringIncludeTypes, true);
    test_case_options_.RegisterBool(kShowImportedModules, true);
    test_case_options_.RegisterBool(kProvideGlobalCatalog, true);
    test_case_options_.RegisterString(kErrorMessageMode,
                                      "MULTI_LINE_WITH_CARET");

    // Force a blank line at the start of every test case.
    absl::SetFlag(&FLAGS_file_based_test_driver_insert_leading_blank_lines, 1);

    LanguageOptions* language_options = analyzer_options_.mutable_language();
    language_options->EnableLanguageFeature(FEATURE_ANNOTATION_FRAMEWORK);
    language_options->EnableLanguageFeature(FEATURE_COLLATION_SUPPORT);
    language_options->EnableLanguageFeature(
        FEATURE_TYPE_ANNOTATIONS_ON_SQL_FUNCTION_ARGUMENTS);
    language_options->EnableLanguageFeature(
        FEATURE_ALL_TYPE_ANNOTATIONS_ON_SQL_FUNCTION_ARGUMENTS);

    // Set up constant evaluator.
    constant_evaluator_ = std::make_unique<PreparedExpressionConstantEvaluator>(
        /*options=*/EvaluatorOptions{.type_factory = &type_factory_},
        analyzer_options_.language());

    // Make sure that the CREATE statements found in modules are enabled.
    language_options->SetSupportsAllStatementKinds();
    language_options->EnableMaximumLanguageFeatures();
    // Manually enable any in_development features we want to test. Module tests
    // don't have the ability to toggle features per-test, so these apply to all
    // tests.
    language_options->EnableLanguageFeature(FEATURE_NON_SQL_PROCEDURE);

    sample_catalog_ = std::make_unique<SampleCatalog>(
        analyzer_options_.language(), &type_factory_);

    builtin_function_catalog_ = std::make_unique<FunctionCatalogWrapper>(
        "test_catalog", &type_factory_, sample_catalog_->catalog());
  }

  RunModulesTest(const RunModulesTest&) = delete;
  RunModulesTest& operator=(const RunModulesTest&) = delete;

  void RunTest(absl::string_view test_case_input,
               file_based_test_driver::RunTestCaseResult* test_result) {
    std::string test_case = std::string(test_case_input);
    const absl::Status options_status =
        test_case_options_.ParseTestCaseOptions(&test_case);
    if (!options_status.ok()) {
      test_result->AddTestOutput(
          absl::StrCat("ERROR: Invalid test case options: ",
                       internal::StatusToString(options_status)));
      return;
    }

    GOOGLESQL_VLOG(1) << "test case:\n" << test_case;
    GOOGLESQL_EXPECT_OK(RunModuleTest(test_case, test_result)) << test_case;
  }

  void AddSanitizedTestOutput(
      absl::string_view output_string,
      file_based_test_driver::RunTestCaseResult* test_result) {
    // Sanitize the <output_string> by scrubbing out references to the test
    // source directory from the string, since the source directory can vary
    // from one run to the next.  The only place the full source directory
    // shows up in error messages is when an IMPORT MODULE fails to find
    // the module file.
    const std::string sanitized_output_string = absl::StrReplaceAll(
        output_string, {{absl::StrCat(::testing::SrcDir(), "_main"), "..."}});
    test_result->AddTestOutput(sanitized_output_string);
  }

 private:
  google::protobuf::DescriptorPool* GetTestDescriptorPool() {
    if (descriptor_pool_ != nullptr) {
      return descriptor_pool_.get();
    }
    if (proto_importer_ == nullptr) {
      const std::vector<std::string> proto_file_names = {"test_schema.proto"};

      proto_disk_source_tree_.MapPath("", TestSrcDirBase());
      // TODO: b/455664770 - Remove dependency on canonical name format.
      // Directories for protos imported from Google protobuf code.
      proto_disk_source_tree_.MapPath(
          "", googlesql_base::JoinPath(::testing::SrcDir(), "protobuf+", "src", "google",
          "protobuf", "_virtual_imports", "descriptor_proto"));
      proto_importer_ = std::make_unique<google::protobuf::compiler::Importer>(
          &proto_disk_source_tree_, &error_collector_);
      for (const std::string& proto_file : proto_file_names) {
        ABSL_CHECK_NE(nullptr, proto_importer_->Import(googlesql_base::JoinPath(
                              "googlesql/testdata/", proto_file)))
            << "Error importing " << proto_file << ": "
            << error_collector_.GetError();
      }
    }
    ABSL_CHECK_NE(proto_importer_->pool(), nullptr);
    if (descriptor_pool_database_ == nullptr) {
      descriptor_pool_database_ =
          std::make_unique<google::protobuf::DescriptorPoolDatabase>(
              *proto_importer_->pool());
    }
    descriptor_pool_ = std::make_unique<google::protobuf::DescriptorPool>(
        descriptor_pool_database_.get());
    return descriptor_pool_.get();
  }

  absl::Status RunModuleTest(
      absl::string_view input_string,
      file_based_test_driver::RunTestCaseResult* test_result) {
    std::string error_message_mode_option =
        test_case_options_.GetString(kErrorMessageMode);
    if (error_message_mode_option == "WITH_PAYLOAD") {
      analyzer_options_.set_error_message_mode(ERROR_MESSAGE_WITH_PAYLOAD);
    } else if (error_message_mode_option == "MULTI_LINE_WITH_CARET") {
      analyzer_options_.set_error_message_mode(
          ERROR_MESSAGE_MULTI_LINE_WITH_CARET);
    } else if (error_message_mode_option == "ONE_LINE") {
      analyzer_options_.set_error_message_mode(ERROR_MESSAGE_ONE_LINE);
    } else {
      GOOGLESQL_RET_CHECK_FAIL() << "Test driver does not support error message mode: "
                       << error_message_mode_option;
    }

    std::vector<std::string> module_import_path;
    // If test input is module contents, then it will be loaded later with an
    // empty name path and TestModuleContentsFetcher will return `input_string`.
    // If test is not module contents, and for any transitive imports, modules
    // with a non-empty name-path will load from `source_directory`.
    auto module_contents_fetcher =
        std::make_unique<testing::TestModuleContentsFetcher>(
            GetTestDescriptorPool(),
            /*source_directory=*/TestSrcDirBase());
    GOOGLESQL_RETURN_IF_ERROR(module_contents_fetcher->AddInMemoryModule(
        module_import_path, input_string));
    if (!test_case_options_.GetBool(kInputIsModuleContents)) {
      // The input string is not the module contents.  The input string
      // must be a simple IMPORT statement that provides the file name of
      // the imported module, for example:
      //
      // IMPORT MODULE foo;
      //   or
      // IMPORT MODULE foo as bar;
      //
      // The module contents will be read from the file, which must reside
      // in googlesql/common/testdata/modules, with a file extension of '.sqlm'.
      std::unique_ptr<const AnalyzerOutput> analyzer_output;
      GOOGLESQL_RETURN_IF_ERROR(AnalyzeStatement(input_string, analyzer_options_,
                                       sample_catalog_->catalog(),
                                       &type_factory_, &analyzer_output));
      const ResolvedImportStmt* resolved_import_statement =
          analyzer_output->resolved_statement()->GetAs<ResolvedImportStmt>();
      module_import_path = resolved_import_statement->name_path();
    }

    // Create AnalyzerOptions with a valid ConstantEvaluator.
    analyzer_options_.set_constant_evaluator(constant_evaluator_.get());

    // Create the ModuleFactory.
    Catalog* global_catalog = nullptr;
    if (test_case_options_.GetBool(kProvideGlobalCatalog)) {
      global_catalog = sample_catalog_->catalog();
    }
    // TODO: b/406079753 - Remove constant_evaluator from ModuleFactoryOptions.
    auto module_factory = std::make_unique<ModuleFactory>(
        analyzer_options_,
        ModuleFactoryOptions{.constant_evaluator = constant_evaluator_.get()},
        std::move(module_contents_fetcher), builtin_function_catalog_.get(),
        global_catalog, &type_factory_);

    // Create a ModuleCatalog.
    ModuleCatalog* module_catalog;
    const absl::Status create_status =
        module_factory->CreateOrReturnModuleCatalog(module_import_path,
                                                    &module_catalog);
    if (!create_status.ok()) {
      AddSanitizedTestOutput(
          absl::StrCat("ModuleCatalog creation failed with status:\n",
                       internal::StatusToString(create_status)),
          test_result);
      return absl::OkStatus();
    }

    // Add the module contents to the result, if required.
    if (test_case_options_.GetBool(kShowModuleContents)) {
      AddSanitizedTestOutput(absl::StrCat("Module contents:\n",
                                          module_catalog->GetModuleContents()),
                             test_result);
    }

    // Add the object contents to the result.
    if (test_case_options_.GetBool(kResolveStatements)) {
      module_catalog->ResolveAllStatements();
    }
    module_catalog->EvaluateAllConstants();
    bool include_types = test_case_options_.GetBool(kDebugStringIncludeTypes);
    AddSanitizedTestOutput(module_catalog->ObjectsDebugString(
                               test_case_options_.GetBool(kUseFullDebugString),
                               /*verbose=*/true, include_types),
                           test_result);

    if (test_case_options_.GetBool(kShowImportedModules) &&
        module_catalog->HasImportedModules()) {
      // TODO: Change <include_module_contents> to false, to remove
      // a bunch of unnecessary output that is not particularly useful.  This
      // will require a lot of test case updates.
      AddSanitizedTestOutput(module_catalog->ImportedModulesDebugString(
                                 /* include_module_contents = */ true),
                             test_result);
    }

    // If there are errors then add the error information to the result.
    if (module_catalog->HasErrors(
            /* include_nested_module_errors = */ false,
            /* include_catalog_object_errors = */ false)) {
      // Do not include nested module errors here, they are already
      // included above via HasImportedModules().
      AddSanitizedTestOutput(
          absl::StrCat("Module errors:\n",
                       module_catalog->ModuleErrorsDebugString(
                           /* include_nested_module_errors = */ false,
                           /* include_catalog_object_errors = */ false)),
          test_result);
    }

    return absl::OkStatus();
  }

  // Analyzer options to use for the test.
  AnalyzerOptions analyzer_options_;

  // Constant evaluator to use for analysis time constant evaluation.
  std::unique_ptr<ConstantEvaluator> constant_evaluator_ = nullptr;

  // TypeFactory to use for the test.
  TypeFactory type_factory_;

  // A Catalog that contains some objects to use when resolving modules. This is
  // provided as the global catalog to the ModuleFactory, and is used to resolve
  // global-scope module statements.

  std::unique_ptr<SampleCatalog> sample_catalog_;

  // A wrapper around the sample catalog which exposes only the contained
  // functions. This more closely matches the expected production contents of a
  // builtin function catalog, which wouldn't contain tables, types, etc. which
  // are in the SampleCatalog. This is provided as the builtin function catalog
  // to the ModuleFactory, and is used to resolve module statements.
  std::unique_ptr<FunctionCatalogWrapper> builtin_function_catalog_;

  // Objects related to fetching a DescriptorDatabase to use when processing
  // IMPORT PROTO statements.
  std::unique_ptr<google::protobuf::compiler::Importer> proto_importer_;
  google::protobuf::compiler::DiskSourceTree proto_disk_source_tree_;
  TestMultiFileErrorCollector error_collector_;

  // DescriptorPoolDatabase and DescriptorPool for use for IMPORT PROTO
  // statements.  The <descriptor_pool_> is built from the
  // <descriptor_pool_database_>, which is in turn built from the
  // <proto_importer_>.
  std::unique_ptr<google::protobuf::DescriptorPoolDatabase> descriptor_pool_database_;
  std::unique_ptr<google::protobuf::DescriptorPool> descriptor_pool_;

  file_based_test_driver::TestCaseOptions test_case_options_;
};

TEST_F(RunModulesTest, ModulesTests) {
  const std::string pattern = googlesql_base::JoinPath(
      ::testing::SrcDir(), "_main/googlesql/common/testdata/",
      absl::GetFlag(FLAGS_test_file_spec_pattern));

  EXPECT_TRUE(file_based_test_driver::RunTestCasesFromFiles(
      pattern, absl::bind_front(&RunModulesTest::RunTest, this)));
}

}  // namespace googlesql
