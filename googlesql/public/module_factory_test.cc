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


#include <cstdint>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/base/path.h"
#include "googlesql/base/testing/status_matchers.h"  
#include "googlesql/public/error_helpers.h"
#include "googlesql/public/file_module_contents_fetcher.h"
#include "googlesql/public/module_contents_fetcher.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/span.h"
#include "googlesql/base/file_util.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/base/map_util.h"
#include "googlesql/base/status.h"
#include "googlesql/base/status_macros.h"

namespace googlesql {

using google::protobuf::DescriptorPool;
using testing::HasSubstr;
using ::absl_testing::StatusIs;

absl::Status CopyFile(absl::string_view from_path, absl::string_view to_path) {
  const absl::string_view dest_dir = googlesql_base::Dirname(to_path);
  if (!dest_dir.empty()) {
    GOOGLESQL_RETURN_IF_ERROR(internal::RecursivelyCreateDir(dest_dir));
  }
  return internal::Copy(from_path, to_path);
}

// Produce a path to the test input source directory, relative to which modules
// and protos can be read.
std::string TestSrcDirBase() {
  return googlesql_base::JoinPath(::testing::SrcDir(), "_main");
}

// Produce a module name path for a module under `testdata/modules`.
std::vector<std::string> TestDataModuleNamePath(
    std::vector<std::string> name_path_suffix) {
  std::vector<std::string> out = {"googlesql", "testdata", "modules"};
  out.insert(std::end(out),
             std::make_move_iterator(std::begin(name_path_suffix)),
             std::make_move_iterator(std::end(name_path_suffix)));
  return out;
}

TEST(FileModuleContentsFetcherTest, FetchesModule) {
  FileModuleContentsFetcher module_fetcher(TestSrcDirBase());
  ModuleContentsInfo module_contents;
  GOOGLESQL_EXPECT_OK(module_fetcher.FetchModuleContents(
      TestDataModuleNamePath({"simple"}), &module_contents));
  EXPECT_THAT(module_contents.contents, HasSubstr("module simple_module;"));

  FileModuleContentsFetcher another_module_fetcher(googlesql_base::JoinPath(
      ::testing::SrcDir(), "_main/googlesql/testdata/modules"));
  GOOGLESQL_EXPECT_OK(
      another_module_fetcher.FetchModuleContents({"simple"}, &module_contents));
  EXPECT_THAT(module_contents.contents, HasSubstr("module simple_module;"));
}

TEST(FileModuleContentsFetcherTest, ModuleNotFound) {
  FileModuleContentsFetcher module_fetcher(TestSrcDirBase());
  ModuleContentsInfo module_contents;
  EXPECT_THAT(module_fetcher.FetchModuleContents({"nonexistent", "module"},
                                                 &module_contents),
              StatusIs(absl::StatusCode::kNotFound));
}

TEST(FileModuleContentsFetcherTest, FetchesModuleInMultipleRoots) {
  std::vector<std::string> source_directories = {
      googlesql_base::JoinPath(::testing::SrcDir(),
                     "_main/googlesql/testdata/modules"),
      googlesql_base::JoinPath(::testing::TempDir(), "separate_modules_root")};
  FileModuleContentsFetcher module_fetcher(source_directories);

  ModuleContentsInfo module_contents;

  // Fetch a module from the first root.
  GOOGLESQL_EXPECT_OK(module_fetcher.FetchModuleContents({"simple"}, &module_contents));
  EXPECT_THAT(module_contents.contents, HasSubstr("module simple_module;"));

  // Fetch a module from the second root.
  GOOGLESQL_ASSERT_OK(
      CopyFile(googlesql_base::JoinPath(source_directories[0], "simple.sqlm"),
               googlesql_base::JoinPath(source_directories[1], "unique_name.sqlm")));
  GOOGLESQL_EXPECT_OK(
      module_fetcher.FetchModuleContents({"unique_name"}, &module_contents));
  EXPECT_THAT(module_contents.contents, HasSubstr("module simple_module;"));

  // Fetch non-existing module.
  const auto non_existing_status = module_fetcher.FetchModuleContents(
      {"nonexistent", "module"}, &module_contents);
  // Error message contains ::testing::SrcDir(), which is different for every
  // test, so checking only for substrings.
  EXPECT_THAT(
      non_existing_status,
      StatusIs(
          absl::StatusCode::kNotFound,
          testing::AllOf(
              HasSubstr("Module nonexistent.module not found"),
              HasSubstr("modules/nonexistent/module.sqlm"),
              HasSubstr("modules/nonexistent/module/module.sqlm"),
              HasSubstr("separate_modules_root/nonexistent/module.sqlm"),
              HasSubstr(
                  "separate_modules_root/nonexistent/module/module.sqlm"))));

  // Fetch a module, which exists in both locations.
  // First found module is returned.
  GOOGLESQL_ASSERT_OK(CopyFile(
      googlesql_base::JoinPath(source_directories[0], "simple_with_comments.sqlm"),
      googlesql_base::JoinPath(source_directories[1], "simple_with_comments.sqlm")));
  GOOGLESQL_EXPECT_OK(module_fetcher.FetchModuleContents({"simple_with_comments"},
                                               &module_contents));
  EXPECT_THAT(module_contents.contents, HasSubstr("MODULE simple_module;"));
  EXPECT_THAT(module_contents.filename,
              HasSubstr("modules/simple_with_comments.sqlm"));
}

class FileModuleContentsFetcherFetchProtosTest : public ::testing::Test {
 protected:
  FileModuleContentsFetcherFetchProtosTest() {
    const std::vector<std::string> source_directories = {
        TestSrcDirBase(),
        // TODO: Remove dependency on canonical name format.
        // Directories for protos imported from Google protobuf code.
        googlesql_base::JoinPath(::testing::SrcDir(), "protobuf~", "src",
                       "google", "protobuf", "_virtual_imports",
                       "descriptor_proto"),
        ::testing::TempDir()};
    module_fetcher_ =
        std::make_unique<FileModuleContentsFetcher>(source_directories);
  }

  // Common output variable for FetchProtoFileDescriptor() calls.
  const google::protobuf::FileDescriptor* proto_file_descriptor_;

  std::unique_ptr<FileModuleContentsFetcher> module_fetcher_;
};

TEST_F(FileModuleContentsFetcherFetchProtosTest, FetchOneProto) {
  const std::string filename = "googlesql/testdata/test_schema.proto";
  GOOGLESQL_EXPECT_OK(module_fetcher_->FetchProtoFileDescriptor(filename,
                                                      &proto_file_descriptor_));
  EXPECT_NE(proto_file_descriptor_, nullptr);
  EXPECT_EQ(filename, proto_file_descriptor_->name());

  // Fetch the same proto again.  The returned FileDescriptor* should be the
  // same since it's from the same DescriptorPool.
  const google::protobuf::FileDescriptor* proto_file_descriptor_2;
  GOOGLESQL_EXPECT_OK(module_fetcher_->FetchProtoFileDescriptor(
      filename, &proto_file_descriptor_2));
  EXPECT_EQ(proto_file_descriptor_, proto_file_descriptor_2);
}

TEST_F(FileModuleContentsFetcherFetchProtosTest,
       FetchAnotherProtoWithDependencies) {
  const std::string filename =
      "googlesql/testdata/referencing_schema.proto";
  GOOGLESQL_EXPECT_OK(module_fetcher_->FetchProtoFileDescriptor(filename,
                                                      &proto_file_descriptor_));
  EXPECT_NE(proto_file_descriptor_, nullptr);
  EXPECT_EQ(filename, proto_file_descriptor_->name());
}

TEST_F(FileModuleContentsFetcherFetchProtosTest,
       FetchAnotherProtoWithDagDependenciesA) {
  const std::string filename =
      "googlesql/testdata/proto_dag_like/a.proto";
  GOOGLESQL_EXPECT_OK(module_fetcher_->FetchProtoFileDescriptor(filename,
                                                      &proto_file_descriptor_));
  ASSERT_NE(proto_file_descriptor_, nullptr);
  EXPECT_EQ(filename, proto_file_descriptor_->name());
  EXPECT_NE(proto_file_descriptor_->FindMessageTypeByName("MessageA"), nullptr);
}

TEST_F(FileModuleContentsFetcherFetchProtosTest,
       FetchAnotherProtoWithDagDependenciesB1) {
  const std::string filename =
      "googlesql/testdata/proto_dag_like/b1.proto";
  GOOGLESQL_EXPECT_OK(module_fetcher_->FetchProtoFileDescriptor(filename,
                                                      &proto_file_descriptor_));
  ASSERT_NE(proto_file_descriptor_, nullptr);
  EXPECT_EQ(filename, proto_file_descriptor_->name());
  EXPECT_NE(proto_file_descriptor_->FindMessageTypeByName("MessageB1"),
            nullptr);
}

TEST_F(FileModuleContentsFetcherFetchProtosTest,
       FetchAnotherProtoWithDagDependenciesB2) {
  const std::string filename =
      "googlesql/testdata/proto_dag_like/b2.proto";
  GOOGLESQL_EXPECT_OK(module_fetcher_->FetchProtoFileDescriptor(filename,
                                                      &proto_file_descriptor_));
  ASSERT_NE(proto_file_descriptor_, nullptr);
  EXPECT_EQ(filename, proto_file_descriptor_->name());
  EXPECT_NE(proto_file_descriptor_->FindMessageTypeByName("MessageB2"),
            nullptr);
}

TEST_F(FileModuleContentsFetcherFetchProtosTest, FetchRecursiveSchemaProto) {
  const std::string filename =
      "googlesql/testdata/recursive_schema.proto";
  GOOGLESQL_EXPECT_OK(module_fetcher_->FetchProtoFileDescriptor(filename,
                                                      &proto_file_descriptor_));
  EXPECT_NE(proto_file_descriptor_, nullptr);
  EXPECT_EQ(filename, proto_file_descriptor_->name());
  EXPECT_NE(proto_file_descriptor_->FindMessageTypeByName("TestRecursivePB"),
            nullptr);
}

TEST_F(FileModuleContentsFetcherFetchProtosTest, FetchMultipleProtos) {
  const std::string first_filename =
      "googlesql/testdata/keyword_in_package_name.proto";
  const google::protobuf::FileDescriptor* proto_file_descriptor_1;
  GOOGLESQL_EXPECT_OK(module_fetcher_->FetchProtoFileDescriptor(
      first_filename, &proto_file_descriptor_1));
  EXPECT_NE(proto_file_descriptor_1, nullptr);
  EXPECT_EQ(first_filename, proto_file_descriptor_1->name());
  EXPECT_NE(proto_file_descriptor_1->FindExtensionByName("order"), nullptr);

  std::string another_filename =
      "googlesql/testdata/referencing_schema.proto";
  GOOGLESQL_EXPECT_OK(module_fetcher_->FetchProtoFileDescriptor(another_filename,
                                                      &proto_file_descriptor_));
  EXPECT_NE(proto_file_descriptor_, nullptr);
  EXPECT_EQ(another_filename, proto_file_descriptor_->name());
  EXPECT_NE(proto_file_descriptor_->FindMessageTypeByName("TestReferencingPB"),
            nullptr);

  another_filename = "googlesql/testdata/test_schema.proto";
  GOOGLESQL_EXPECT_OK(module_fetcher_->FetchProtoFileDescriptor(another_filename,
                                                      &proto_file_descriptor_));
  EXPECT_NE(proto_file_descriptor_, nullptr);
  EXPECT_EQ(another_filename, proto_file_descriptor_->name());
  EXPECT_NE(proto_file_descriptor_->FindMessageTypeByName("TestExtraPB"),
            nullptr);

  // Fetch the same one as the first again.
  GOOGLESQL_EXPECT_OK(module_fetcher_->FetchProtoFileDescriptor(first_filename,
                                                      &proto_file_descriptor_));
  EXPECT_EQ(proto_file_descriptor_, proto_file_descriptor_1);
}

TEST_F(FileModuleContentsFetcherFetchProtosTest, ProtoNotFound) {
  const absl::Status fetch_status = module_fetcher_->FetchProtoFileDescriptor(
      "invalid/proto/name.path", &proto_file_descriptor_);
  // The error message contains the full path for the fetched proto file,
  // which for tests is a temp directory that varies from run to run.  So
  // check that the first and last parts of the error message are as expected.
  EXPECT_THAT(
      fetch_status,
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("Fetching proto file 'invalid/proto/name.path' failed")));
  EXPECT_EQ(proto_file_descriptor_, nullptr);
}

TEST_F(FileModuleContentsFetcherFetchProtosTest, ProtoWithParseErrors) {
  const absl::Status fetch_status = module_fetcher_->FetchProtoFileDescriptor(
      "googlesql/testdata/bad_parse_schema.proto",
      &proto_file_descriptor_);
  EXPECT_FALSE(fetch_status.ok());

  // There exist several errors in this proto file, with the messages
  // concatenated into a single string separated by newlines.  We check for
  // each of these expected error substrings.
  const std::vector<std::pair<int, std::string>> expected_errors = {
      {18, "Line $0 Column 0 :Expected \";\"."},
      {31,
       "Line $0 Column 2 :Expected \"required\", \"optional\", or "
       "\"repeated\"."},
      {36,
       "Line $0 Column 2 :Expected \"required\", \"optional\", or "
       "\"repeated\"."},
      {36, "Line $0 Column 19 :Missing field number."},
      {41,
       "Line $0 Column 2 :Expected \"required\", \"optional\", or "
       "\"repeated\"."},
      {41, "Line $0 Column 22 :Expected field number."},
      {46,
       "Line $0 Column 2 :Expected \"required\", \"optional\", or "
       "\"repeated\"."},
      {46, "Line $0 Column 22 :Expected field number."},
      {51,
       "Line $0 Column 2 :Expected \"required\", \"optional\", or "
       "\"repeated\"."},
      {51, "Line $0 Column 10 :Missing field number."},
      {51, "Line $0 Column 46 :Need space between number and identifier."}};
  for (const std::pair<int, std::string>& expected_error : expected_errors) {
    const int line_offset = 16;
    std::string expected_message = absl::Substitute(
        expected_error.second, expected_error.first + line_offset);
    EXPECT_TRUE(absl::StrContains(fetch_status.message(), expected_message))
        << "fetch_status.message(): " << fetch_status.message()
        << "\nexpected_message: " << expected_message;
  }

  // Fetch the proto again.  Note that the error message is the same as the
  // first time.
  const absl::Status fetch_status_2 = module_fetcher_->FetchProtoFileDescriptor(
      "googlesql/testdata/bad_parse_schema.proto",
      &proto_file_descriptor_);
  EXPECT_FALSE(fetch_status_2.ok());
  EXPECT_EQ(fetch_status.message(), fetch_status_2.message());
}

TEST_F(FileModuleContentsFetcherFetchProtosTest, ProtoWithSemanticErrors) {
  const absl::Status fetch_status = module_fetcher_->FetchProtoFileDescriptor(
      "googlesql/testdata/bad_reference.proto",
      &proto_file_descriptor_);

  EXPECT_THAT(
      fetch_status,
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr(R"("DependsOnNonExistingMessage" is not defined.)")));
  EXPECT_EQ(proto_file_descriptor_, nullptr);
}

TEST_F(FileModuleContentsFetcherFetchProtosTest, ProtoWithMissingDependency) {
  const absl::Status fetch_status = module_fetcher_->FetchProtoFileDescriptor(
      "googlesql/testdata/bad_file_dependency.proto",
      &proto_file_descriptor_);

  std::string dependency_path = "depends/on/nonexistent/file.proto";
  EXPECT_THAT(
      fetch_status,
      StatusIs(absl::StatusCode::kInvalidArgument,
               "Fetching proto file 'depends/on/nonexistent/file.proto' failed "
               "with error: Filename depends/on/nonexistent/file.proto Line -1 "
               "Column 0 :File not found.\n"));
  EXPECT_EQ(proto_file_descriptor_, nullptr);
}

TEST_F(FileModuleContentsFetcherFetchProtosTest,
       FetchMultipleProtosWithSomeParseErrors) {
  // Fetch a proto with parse errors.
  absl::Status fetch_status = module_fetcher_->FetchProtoFileDescriptor(
      "googlesql/testdata/bad_parse_schema.proto",
      &proto_file_descriptor_);
  EXPECT_THAT(fetch_status, StatusIs(absl::StatusCode::kInvalidArgument,
                                     HasSubstr("googlesql/testdata/"
                                               "bad_parse_schema.proto'")));

  // Fetch a valid proto.
  GOOGLESQL_EXPECT_OK(module_fetcher_->FetchProtoFileDescriptor(
      "googlesql/testdata/test_schema.proto", &proto_file_descriptor_));
  EXPECT_EQ(proto_file_descriptor_->name(),
            "googlesql/testdata/test_schema.proto");

  // Fetch a proto with parse errors again.
  fetch_status = module_fetcher_->FetchProtoFileDescriptor(
      "googlesql/testdata/bad_parse_schema.proto",
      &proto_file_descriptor_);
  EXPECT_THAT(fetch_status, StatusIs(absl::StatusCode::kInvalidArgument,
                                     HasSubstr("googlesql/testdata/"
                                               "bad_parse_schema.proto'")));

  fetch_status = module_fetcher_->FetchProtoFileDescriptor(
      "googlesql/testdata/bad_parse_schema_simple.proto",
      &proto_file_descriptor_);
  EXPECT_THAT(fetch_status,
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("googlesql/testdata/"
                                 "bad_parse_schema_simple.proto'")));
}

TEST_F(FileModuleContentsFetcherFetchProtosTest, FetchProtoFromSecondRoot) {
  const std::string filename = "unique_name.proto";
  GOOGLESQL_ASSERT_OK(CopyFile(
      googlesql_base::JoinPath(::testing::SrcDir(),
                     "_main/googlesql/testdata/test_schema.proto"),
      googlesql_base::JoinPath(::testing::TempDir(), filename)));

  GOOGLESQL_EXPECT_OK(module_fetcher_->FetchProtoFileDescriptor(filename,
                                                      &proto_file_descriptor_));
  ASSERT_NE(proto_file_descriptor_, nullptr);
  EXPECT_EQ(filename, proto_file_descriptor_->name());
  EXPECT_NE(proto_file_descriptor_->FindMessageTypeByName("TestExtraPB"),
            nullptr);
}

TEST_F(FileModuleContentsFetcherFetchProtosTest,
       FetchProtoThatExistsInBothRoots) {
  const std::string filename =
      "_main/googlesql/testdata/test_schema.proto";
  GOOGLESQL_ASSERT_OK(CopyFile(googlesql_base::JoinPath(::testing::SrcDir(), filename),
                     googlesql_base::JoinPath(::testing::TempDir(), filename)));

  GOOGLESQL_EXPECT_OK(module_fetcher_->FetchProtoFileDescriptor(filename,
                                                      &proto_file_descriptor_));
  ASSERT_NE(proto_file_descriptor_, nullptr);
  EXPECT_EQ(filename, proto_file_descriptor_->name());
  EXPECT_NE(proto_file_descriptor_->FindMessageTypeByName("TestExtraPB"),
            nullptr);
}

class FetchAllModuleAndProtoContentsTest : public ::testing::Test {
 protected:
  FetchAllModuleAndProtoContentsTest() {
    const std::string source_directory = TestSrcDirBase();
    module_fetcher_ =
        std::make_unique<FileModuleContentsFetcher>(source_directory);
  }

  void CheckFetchedModuleFileNames(
      const std::set<std::string>& expected_module_names) {
    std::set<std::string> map_keys;
    for (const auto& entry : module_info_map_) {
      map_keys.insert(absl::StrJoin(entry.first, "."));
    }
    EXPECT_THAT(map_keys, ::testing::ContainerEq(expected_module_names));
  }
  void CheckFetchedProtoFileNames(
      const std::set<std::string>& expected_proto_names) {
    std::set<std::string> map_keys;
    for (const auto& entry : proto_info_map_) {
      EXPECT_EQ(entry.first, entry.second.file_descriptor_proto.name());
      map_keys.insert(entry.first);
    }
    EXPECT_THAT(map_keys, ::testing::ContainerEq(expected_proto_names));
  }

  // Variables that are common output from FetchAllModuleAndProtoContents().
  ModuleContentsInfoMap module_info_map_;
  ProtoContentsInfoMap proto_info_map_;
  std::vector<absl::Status> errors_;

  // Supports proto imports.
  std::unique_ptr<FileModuleContentsFetcher> module_fetcher_;
};

TEST_F(FetchAllModuleAndProtoContentsTest, FetchTrivialModule) {
  const std::vector<std::string> module_name_path(
      TestDataModuleNamePath({"trivial"}));
  GOOGLESQL_ASSERT_OK(FetchAllModuleAndProtoContents(
      {module_name_path}, module_fetcher_.get(), &module_info_map_,
      &proto_info_map_, &errors_));
  // This module does not import other modules.
  EXPECT_EQ(module_info_map_.size(), 1);
  // This module does not import any protos.
  EXPECT_EQ(proto_info_map_.size(), 0);
  // This module does not have any errors.
  EXPECT_EQ(errors_.size(), 0);
}

TEST_F(FetchAllModuleAndProtoContentsTest, FetchSimpleModule) {
  const std::vector<std::string> module_name_path(
      TestDataModuleNamePath({"simple"}));
  // Module 'simple.sqlm' has errors, so FetchAllModuleAndProtoContents
  // returns an error.
  EXPECT_FALSE(FetchAllModuleAndProtoContents(
                   {module_name_path}, module_fetcher_.get(), &module_info_map_,
                   &proto_info_map_, &errors_)
                   .ok());
  // This module does not import other modules.
  EXPECT_EQ(module_info_map_.size(), 1);
  // This module does not import any protos.
  EXPECT_EQ(proto_info_map_.size(), 0);
  // This module has a parse error.
  EXPECT_EQ(errors_.size(), 1);
}

// Demonstrate fix for crash in b/141155550.
TEST_F(FetchAllModuleAndProtoContentsTest, InvalidProtoImport) {
  const std::vector<std::string> module_name_path(
      TestDataModuleNamePath({"invalid_proto_import"}));
  absl::Status status = FetchAllModuleAndProtoContents(
      {module_name_path}, module_fetcher_.get(), &module_info_map_,
      &proto_info_map_, &errors_);
  // This module does not import other modules.
  EXPECT_EQ(module_info_map_.size(), 1);
  // This module does not import any protos.
  EXPECT_EQ(proto_info_map_.size(), 0);
  // This module has errors.
  EXPECT_EQ(errors_.size(), 1);
  EXPECT_THAT(errors_[0].ToString(),
              HasSubstr("IMPORT PROTO must be followed by a file path"));
}

class FetchAllModuleContentsTest : public ::testing::Test {
 protected:
  FetchAllModuleContentsTest() {
    module_fetcher_ =
        std::make_unique<FileModuleContentsFetcher>(TestSrcDirBase());
  }

  void FetchContentsAndAssertErrorCount(
      const std::vector<std::string>& module_name_path,
      int64_t expected_error_count) {
    const absl::Status fetch_status = FetchAllModuleContents(
        module_name_path, module_fetcher_.get(), &module_contents_info_map_,
        &module_fetch_errors_);
    ASSERT_EQ(fetch_status.ok(), expected_error_count == 0)
        << "Got fetch_status: " << fetch_status;
    ASSERT_EQ(module_fetch_errors_.size(), expected_error_count);
  }

  void FetchMultipleContentsAndAssertErrorCount(
      absl::Span<const std::vector<std::string>> module_name_paths,
      int64_t expected_error_count) {
    const absl::Status fetch_status = FetchAllModuleContents(
        module_name_paths, module_fetcher_.get(), &module_contents_info_map_,
        &module_fetch_errors_);
    ASSERT_EQ(fetch_status.ok(), expected_error_count == 0)
        << "Got fetch_status: " << fetch_status;
    ASSERT_EQ(module_fetch_errors_.size(), expected_error_count);
  }

  ModuleContentsInfoMap module_contents_info_map_;
  std::unique_ptr<FileModuleContentsFetcher> module_fetcher_;
  std::vector<absl::Status> module_fetch_errors_;
};

TEST_F(FetchAllModuleContentsTest, FetchModuleContentsNotFound) {
  std::vector<std::string> module_name_path(
      TestDataModuleNamePath({"some bogus module name"}));
  FetchContentsAndAssertErrorCount(module_name_path, 1);
  const absl::Status fetch_status = module_fetch_errors_[0];
  EXPECT_THAT(
      fetch_status,
      StatusIs(absl::StatusCode::kNotFound,
               HasSubstr("Module googlesql.testdata.modules.some bogus "
                         "module name not found")
               ));
}

TEST_F(FetchAllModuleContentsTest, FetchModuleContents) {
  std::vector<std::string> module_name_path(
      TestDataModuleNamePath({"simple_with_comments"}));
  FetchContentsAndAssertErrorCount(module_name_path, 0);
  ASSERT_EQ(module_contents_info_map_.size(), 1);
  EXPECT_TRUE(googlesql_base::ContainsKey(module_contents_info_map_, module_name_path));
}

TEST_F(FetchAllModuleContentsTest, FetchModuleContentsWithPipes) {
  std::vector<std::string> module_name_path(
      TestDataModuleNamePath({"simple_with_pipes"}));
  FetchContentsAndAssertErrorCount(module_name_path, 0);
  ASSERT_EQ(module_contents_info_map_.size(), 1);
  EXPECT_TRUE(module_contents_info_map_.find(module_name_path) !=
              module_contents_info_map_.end());
}

TEST_F(FetchAllModuleContentsTest, FetchModuleContentsWithSyntaxError) {
  // Fetching contents tolerates syntax errors.
  std::vector<std::string> module_name_path(TestDataModuleNamePath({"simple"}));
  FetchContentsAndAssertErrorCount(module_name_path, 1);
  // We hit one syntax error.
  EXPECT_THAT(
      module_fetch_errors_[0],
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("Syntax error")));
  // We still found contents for this module file.
  ASSERT_EQ(module_contents_info_map_.size(), 1);
  EXPECT_TRUE(googlesql_base::ContainsKey(module_contents_info_map_, module_name_path));
}

TEST_F(FetchAllModuleContentsTest, FetchNestedModuleContentsWithSyntaxError) {
  // Fetching contents tolerates syntax errors, in the top level module
  // and nested modules.  Fetching contents progresses as much as it can.
  std::vector<std::string> module_name_path(
      TestDataModuleNamePath({"module_test_errors_main_2"}));
  FetchContentsAndAssertErrorCount(module_name_path, 2);
  // We hit two syntax errors.
  EXPECT_THAT(
      module_fetch_errors_[0],
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("Syntax error")));
  EXPECT_THAT(FormatError(module_fetch_errors_[0]),
              HasSubstr("module_test_errors_main_2.sqlm"));
  EXPECT_THAT(
      module_fetch_errors_[1],
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("Syntax error")));
  EXPECT_THAT(FormatError(module_fetch_errors_[1]),
              HasSubstr("module_test_errors_imported_b.sqlm"));
  // We still found contents for 6 modules.
  ASSERT_EQ(module_contents_info_map_.size(), 6);
  EXPECT_TRUE(googlesql_base::ContainsKey(module_contents_info_map_, module_name_path));
}

TEST_F(FetchAllModuleContentsTest, FetchModuleContentsWithNestedLookupError) {
  // Fetching contents tolerates syntax errors and file lookup errors.  We
  // still get module contents back for the modules we actually found.
  std::vector<std::string> module_name_path(
      TestDataModuleNamePath({"module_test_errors_main"}));
  FetchContentsAndAssertErrorCount(module_name_path, 4);
  // Note that this test is currently sensitive to the order in which the
  // errors are populated.  The order isn't guaranteed by the function
  // contract, so if this test becomes a maintenance problem then we need
  // to update the test to more accurately reflect the contract.
  EXPECT_THAT(
      module_fetch_errors_[0],
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("Syntax error")));
  EXPECT_THAT(
      module_fetch_errors_[1],
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("Module foo not found")));
  EXPECT_THAT(
      module_fetch_errors_[2],
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("Syntax error")));
  EXPECT_THAT(
      module_fetch_errors_[3],
      StatusIs(absl::StatusCode::kNotFound, HasSubstr("Module foo not found")));

  // Despite syntax errors and file not found errors, we still found contents
  // for 7 modules (module itself + 6 nested imports).
  ASSERT_EQ(module_contents_info_map_.size(), 7);
  EXPECT_TRUE(googlesql_base::ContainsKey(module_contents_info_map_, module_name_path));
}

TEST_F(FetchAllModuleContentsTest, FetchModuleContentsRecursive) {
  // Test a module that imports itself.
  std::vector<std::string> module_name_path(
      TestDataModuleNamePath({"recursive_self"}));
  FetchContentsAndAssertErrorCount(module_name_path, 0);
  EXPECT_EQ(module_contents_info_map_.size(), 1);
  EXPECT_TRUE(googlesql_base::ContainsKey(module_contents_info_map_, module_name_path));

  // Test a module that imports modules that import each other.
  std::vector<std::string> module_name_path_2(
      TestDataModuleNamePath({"recursive_3_imports_2"}));
  FetchContentsAndAssertErrorCount(module_name_path_2, 0);
  EXPECT_EQ(module_contents_info_map_.size(), 3);
  EXPECT_TRUE(googlesql_base::ContainsKey(module_contents_info_map_, module_name_path_2));
}

TEST_F(FetchAllModuleContentsTest, FetchModuleContentsWithSharedDag) {
  // Test a complex DAG import scenario:
  //
  //   Imported module:        import_a1_a2_a3
  //                             /    |   \
  //                           a1    a2    a3
  //                             \  /  \  /
  //                              b1    b2
  //                             /  \  /  \
  //                           c1    c2    c3
  //
  std::vector<std::string> module_name_path(
      TestDataModuleNamePath({"import_a1_a2_a3"}));
  FetchContentsAndAssertErrorCount(module_name_path, 0);
  ASSERT_EQ(module_contents_info_map_.size(), 9);
  EXPECT_TRUE(googlesql_base::ContainsKey(module_contents_info_map_, module_name_path));
}

TEST_F(FetchAllModuleContentsTest, FetchMultipleModuleContents) {
  // Test the api that takes a list of modules to import, and import them
  // all.  This test includes the modules from the previous test, and
  // imports the modules multiple times.  Each module contents show up only
  // once, despite being imported multiple times.
  std::vector<std::vector<std::string>> module_name_paths(
      {TestDataModuleNamePath({"a1_imports_b1"}),
       TestDataModuleNamePath({"recursive_self"}),
       TestDataModuleNamePath({"recursive_self"}),
       TestDataModuleNamePath({"import_a1_a2_a3"}),
       TestDataModuleNamePath({"import_a1_a2_a3"}),
       TestDataModuleNamePath({"recursive_3_imports_2"})});
  FetchMultipleContentsAndAssertErrorCount(module_name_paths, 0);
  // We expect 9 entries from 'import_a1_a2_a3', 3 entries from
  // 'recursive_3_imports_2', and 1 entry from 'recursive_self'.
  ASSERT_EQ(module_contents_info_map_.size(), 13);
  for (const std::vector<std::string>& module_name_path : module_name_paths) {
    EXPECT_TRUE(googlesql_base::ContainsKey(module_contents_info_map_, module_name_path));
  }
}

TEST_F(FetchAllModuleContentsTest, FetchModuleContentsWithBracedProtoCtor) {
  std::vector<std::string> module_name_path(
      TestDataModuleNamePath({"braced_proto_ctor"}));
  FetchContentsAndAssertErrorCount(module_name_path, 0);
  ASSERT_EQ(module_contents_info_map_.size(), 1);
  EXPECT_TRUE(googlesql_base::ContainsKey(module_contents_info_map_, module_name_path));
}

void WriteTestFile(absl::string_view file_name, absl::string_view content) {
  const std::string out_file =
      googlesql_base::JoinPath(googlesql::internal::TestTmpDir(), file_name);
  GOOGLESQL_ASSERT_OK(googlesql::internal::SetContents(out_file, content));
}

TEST(FileModuleContentsFetcherTest, FetchOneProto) {
  FileModuleContentsFetcher module_fetcher(
      std::vector<std::string>{googlesql::internal::TestTmpDir()});

  const std::string filename = "test.proto";
  WriteTestFile(filename, "syntax = \"proto2\";");
  const google::protobuf::FileDescriptor* proto_file_descriptor_1;
  GOOGLESQL_ASSERT_OK(module_fetcher.FetchProtoFileDescriptor(filename,
                                                    &proto_file_descriptor_1));
  EXPECT_NE(proto_file_descriptor_1, nullptr);
  EXPECT_EQ(filename, proto_file_descriptor_1->name());

  // Fetch the same proto again. The returned FileDescriptor* should be the same
  // even if the FileReader returns different content since it's from the same
  // DescriptorPool.
  WriteTestFile(filename, "ABC");
  const google::protobuf::FileDescriptor* proto_file_descriptor_2;
  GOOGLESQL_ASSERT_OK(module_fetcher.FetchProtoFileDescriptor(filename,
                                                    &proto_file_descriptor_2));
  EXPECT_EQ(proto_file_descriptor_1, proto_file_descriptor_2);
}

TEST(FileModuleContentsFetcherTest, FetchProtoWithExternalPool) {
  FileModuleContentsFetcher module_fetcher(
      std::vector<std::string>{googlesql::internal::TestTmpDir()});
  DescriptorPool descriptor_pool;

  const std::string filename = "test.proto";
  WriteTestFile(filename, "syntax = \"proto2\";");
  const google::protobuf::FileDescriptor* proto_file_descriptor_1;

  GOOGLESQL_ASSERT_OK(module_fetcher.FetchProtoFileDescriptor(filename,
                                                    &proto_file_descriptor_1));
  EXPECT_NE(proto_file_descriptor_1, nullptr);
  EXPECT_EQ(filename, proto_file_descriptor_1->name());

  // Fetch the same proto again. The returned FileDescriptor* should be the same
  // even if the FileReader returns different content since it's from the same
  // DescriptorPool.
  WriteTestFile(filename, "ABC");
  const google::protobuf::FileDescriptor* proto_file_descriptor_2;
  GOOGLESQL_ASSERT_OK(module_fetcher.FetchProtoFileDescriptor(filename,
                                                    &proto_file_descriptor_2));
  EXPECT_EQ(proto_file_descriptor_1, proto_file_descriptor_2);
}

TEST(FileModuleContentsFetcherTest, ReplacesDescriptorPool) {
  FileModuleContentsFetcher module_fetcher(
      std::vector<std::string>{googlesql::internal::TestTmpDir()});
  DescriptorPool descriptor_pool;

  const std::string filename = "test.proto";
  WriteTestFile(filename, "syntax = \"proto2\";");
  const google::protobuf::FileDescriptor* proto_file_descriptor_1;
  GOOGLESQL_ASSERT_OK(module_fetcher.FetchProtoFileDescriptor(filename,
                                                    &proto_file_descriptor_1));
  EXPECT_NE(proto_file_descriptor_1, nullptr);
  EXPECT_EQ(filename, proto_file_descriptor_1->name());

  // Replace the pool with a new one for which the file reader will read an
  // invalid proto content.
  WriteTestFile(filename, "ABC");
  DescriptorPool new_descriptor_pool;
  module_fetcher.ReplaceDescriptorPool(&new_descriptor_pool);
  const google::protobuf::FileDescriptor* proto_file_descriptor_2;
  EXPECT_THAT(module_fetcher.FetchProtoFileDescriptor(filename,
                                                      &proto_file_descriptor_2),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("'test.proto' failed")));
}

}  // namespace googlesql
