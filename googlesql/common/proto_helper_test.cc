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

#include "googlesql/common/proto_helper.h"

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "googlesql/base/path.h"
#include "google/protobuf/compiler/importer.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/descriptor.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/common/testing/testing_proto_util.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace googlesql {

using ::testing::_;
using ::testing::HasSubstr;
using ::testing::UnorderedElementsAreArray;
using ::absl_testing::StatusIs;

// Need to implement this to catch importing errors.
class MultiFileErrorCollector
    : public google::protobuf::compiler::MultiFileErrorCollector {
 public:
  MultiFileErrorCollector() = default;
  MultiFileErrorCollector(const MultiFileErrorCollector&) = delete;
  MultiFileErrorCollector& operator=(const MultiFileErrorCollector&) = delete;
  void RecordError(absl::string_view filename, int line, int column,
                   absl::string_view message) override {
    absl::StrAppend(&error_, "Line ", line, " Column ", column, " :", message,
                    "\n");
  }
  const std::string& GetError() const { return error_; }

 private:
  std::string error_;
};

// Unittests for helper functions in proto_helper.h
class ProtoHelperTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const std::vector<std::string> test_files{
        // Order matters for these imports.
        "google/protobuf/descriptor.proto",
        "googlesql/public/proto/type_annotation.proto",
        "googlesql/testdata/test_schema.proto",
        "googlesql/testdata/referencing_schema.proto",
        "googlesql/testdata/referenced_schema.proto",
        "googlesql/testdata/bad_test_schema.proto",
        "googlesql/testdata/bad_extension_schema.proto",
    };
    source_tree_ = CreateProtoSourceTree();

    proto_importer_ = std::make_unique<google::protobuf::compiler::Importer>(
        source_tree_.get(), &error_collector_);
    for (const std::string& test_file : test_files) {
      ASSERT_THAT(proto_importer_->Import(test_file), testing::NotNull())
          << "Error importing " << test_file << ": "
          << error_collector_.GetError();
    }
    pool_ = std::make_unique<google::protobuf::DescriptorPool>(proto_importer_->pool());
  }

  absl::Status GetFileDescriptorSetContainingFile(
      absl::string_view filename,
      google::protobuf::FileDescriptorSet* file_descriptor_set) {
    file_descriptor_set->Clear();
    const google::protobuf::FileDescriptor* file = pool_->FindFileByName(filename);
    std::set<const google::protobuf::FileDescriptor*> file_descriptors;
    return PopulateFileDescriptorSet(file, std::optional<int64_t>(),
                                     file_descriptor_set, &file_descriptors);
  }

  std::unique_ptr<google::protobuf::compiler::Importer> proto_importer_;
  MultiFileErrorCollector error_collector_;
  std::unique_ptr<google::protobuf::compiler::DiskSourceTree> source_tree_;
  std::unique_ptr<google::protobuf::DescriptorPool> pool_;
};

TEST_F(ProtoHelperTest, PopulateFileDescriptorSet) {
  const google::protobuf::FileDescriptor* file = pool_->FindFileByName(
      "googlesql/public/proto/type_annotation.proto");
  google::protobuf::FileDescriptorSet file_descriptor_set;
  std::set<const google::protobuf::FileDescriptor*> file_descriptors;
  GOOGLESQL_ASSERT_OK(PopulateFileDescriptorSet(file, std::optional<int64_t>(),
                                      &file_descriptor_set, &file_descriptors));
  ASSERT_EQ(2, file_descriptor_set.file_size());
  EXPECT_THAT(file_descriptor_set.file(0).name(),
              testing::HasSubstr("/descriptor.proto"));
  EXPECT_THAT(file_descriptor_set.file(1).name(),
              testing::HasSubstr("/type_annotation.proto"));
  ASSERT_EQ(2, file_descriptors.size());

  file = pool_->FindFileByName("googlesql/testdata/test_schema.proto");
  file_descriptor_set.Clear();
  file_descriptors.clear();
  GOOGLESQL_ASSERT_OK(PopulateFileDescriptorSet(file, std::optional<int64_t>(),
                                      &file_descriptor_set, &file_descriptors));

  std::vector<testing::Matcher<const std::string&>> expected_proto_matchers = {
      HasSubstr("/descriptor.proto"),
      HasSubstr("/type_annotation.proto"),
      HasSubstr("/wire_format_annotation.proto"),
      HasSubstr("/test_schema.proto")};

  std::vector<std::string> file_names;
  file_names.reserve(file_descriptor_set.file_size());
  for (int i = 0; i < file_descriptor_set.file_size(); ++i) {
    file_names.emplace_back(file_descriptor_set.file(i).name());
  }
  ASSERT_THAT(file_names, UnorderedElementsAreArray(expected_proto_matchers));

  ASSERT_EQ(expected_proto_matchers.size(), file_descriptors.size());
}

TEST_F(ProtoHelperTest, PopulateFileDescriptorSet_TooBig) {
  const google::protobuf::FileDescriptor* file = pool_->FindFileByName(
      "googlesql/public/proto/type_annotation.proto");
  google::protobuf::FileDescriptorSet file_descriptor_set;
  std::set<const google::protobuf::FileDescriptor*> file_descriptors;
  const absl::Status status =
      PopulateFileDescriptorSet(file, /*file_descriptor_set_max_size_bytes=*/0,
                                &file_descriptor_set, &file_descriptors);
  ASSERT_FALSE(status.ok()) << status;
  EXPECT_THAT(status,
              StatusIs(_, testing::HasSubstr(
                              "Serializing proto descriptors failed due "
                              "to maximum FileDescriptorSet size exceeded")));
}

TEST_F(ProtoHelperTest, AddFileDescriptorSetToPool) {
  google::protobuf::DescriptorPool pool;
  google::protobuf::FileDescriptorSet file_descriptor_set;

  GOOGLESQL_ASSERT_OK(GetFileDescriptorSetContainingFile(
      "googlesql/public/proto/type_annotation.proto",
      &file_descriptor_set));
  GOOGLESQL_ASSERT_OK(AddFileDescriptorSetToPool(&file_descriptor_set, &pool));
  ASSERT_THAT(pool.FindFileByName(
      "googlesql/public/proto/type_annotation.proto"),
              testing::NotNull());

  GOOGLESQL_ASSERT_OK(GetFileDescriptorSetContainingFile(
      "googlesql/testdata/test_schema.proto", &file_descriptor_set));
  GOOGLESQL_ASSERT_OK(AddFileDescriptorSetToPool(&file_descriptor_set, &pool));
  ASSERT_THAT(pool.FindFileByName(
      "googlesql/testdata/test_schema.proto"), testing::NotNull());
}

TEST_F(ProtoHelperTest, AddFileDescriptorSetToPool_IncompleteData) {
  google::protobuf::DescriptorPool pool;
  google::protobuf::FileDescriptorSet file_descriptor_set;

  GOOGLESQL_ASSERT_OK(GetFileDescriptorSetContainingFile(
      "googlesql/public/proto/type_annotation.proto",
      &file_descriptor_set));

  google::protobuf::FileDescriptorSet incomplete_file_descriptor_set;
  *incomplete_file_descriptor_set.add_file() = file_descriptor_set.file(1);

  auto status =
      AddFileDescriptorSetToPool(&incomplete_file_descriptor_set, &pool);
  ASSERT_FALSE(status.ok());
}

TEST_F(ProtoHelperTest, AddFileDescriptorSetToPool_BadData) {
  google::protobuf::DescriptorPool pool;
  google::protobuf::FileDescriptorSet file_descriptor_set;

  GOOGLESQL_ASSERT_OK(GetFileDescriptorSetContainingFile(
      "googlesql/public/proto/type_annotation.proto",
      &file_descriptor_set));

  file_descriptor_set.mutable_file(1)->add_dependency("inexisting.proto");

  auto status = AddFileDescriptorSetToPool(&file_descriptor_set, &pool);
  ASSERT_FALSE(status.ok());
}

}  // namespace googlesql
