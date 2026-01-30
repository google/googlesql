//
// Copyright 2020 Google LLC
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

#include "googlesql/base/file_util_oss.h"

#include <sys/stat.h>
#include <cstdlib>
#include <filesystem>  // NOLINT
#include <string>
#include <system_error>  // NOLINT
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/testing/status_matchers.h"

namespace googlesql::internal {

using ::googlesql_base::testing::StatusIs;

TEST(FileUtilTest, MatchNoWildcard) {
  const std::string filespec = absl::StrCat(
      TestSrcRootDir(), "/googlesql/base/file_util_oss_test.input_file");
  std::vector<std::string> files;
  GOOGLESQL_EXPECT_OK(Match(filespec, &files));
  EXPECT_THAT(files, testing::UnorderedElementsAre(filespec));
}

TEST(FileUtilTest, MatchEndingWithWildcard) {
  const std::string filespec =
      absl::StrCat(TestSrcRootDir(), "/googlesql/base/file_util_oss_test*");
  std::vector<std::string> files;
  GOOGLESQL_EXPECT_OK(Match(filespec, &files));
  EXPECT_THAT(
      files,
      testing::UnorderedElementsAre(
          absl::StrCat(TestSrcRootDir(), "/googlesql/base/file_util_oss_test"),
          absl::StrCat(TestSrcRootDir(),
                       "/googlesql/base/file_util_oss_test.input_file")));
}

TEST(FileUtilTest, MatchMiddleWildcard) {
  const std::string filespec =
      absl::StrCat(TestSrcRootDir(), "/googlesql/base/file_util*.input_file");
  std::vector<std::string> files;
  GOOGLESQL_EXPECT_OK(Match(filespec, &files));
  EXPECT_THAT(files, testing::UnorderedElementsAre(absl::StrCat(
                         TestSrcRootDir(),
                         "/googlesql/base/file_util_oss_test.input_file")));
}

TEST(FileUtilTest, MatchStartWildcard) {
  const std::string filespec =
      absl::StrCat(TestSrcRootDir(), "/googlesql/base/*.input_file");
  std::vector<std::string> files;
  GOOGLESQL_EXPECT_OK(Match(filespec, &files));
  EXPECT_THAT(files, testing::UnorderedElementsAre(absl::StrCat(
                         TestSrcRootDir(),
                         "/googlesql/base/file_util_oss_test.input_file")));
}

TEST(FileUtilTest, MatchFullWildcard) {
  const std::string filespec =
      absl::StrCat(TestSrcRootDir(), "/googlesql/base/*");
  std::vector<std::string> files;
  GOOGLESQL_EXPECT_OK(Match(filespec, &files));
  EXPECT_THAT(
      files,
      testing::UnorderedElementsAre(
          absl::StrCat(TestSrcRootDir(), "/googlesql/base/file_util_oss_test"),
          absl::StrCat(TestSrcRootDir(),
                       "/googlesql/base/file_util_oss_test.input_file")));
}

TEST(FileUtilTest, MatchNoMatch) {
  const std::string filespec =
      absl::StrCat(TestSrcRootDir(), "/googlesql/base/*.no_match");
  std::vector<std::string> files;
  GOOGLESQL_EXPECT_OK(Match(filespec, &files));
  EXPECT_THAT(files, testing::IsEmpty());
}

TEST(FileUtilTest, MatchBadDirectory) {
  const std::string filespec =
      absl::StrCat(TestSrcRootDir(), "/path/does/not/exist/*.input_file");
  std::vector<std::string> files;
  EXPECT_THAT(Match(filespec, &files), StatusIs(absl::StatusCode::kNotFound));
}

TEST(FileUtilTest, NullFreeString) {
  std::string str;
  GOOGLESQL_EXPECT_OK(NullFreeString("abcd0", &str));
  EXPECT_EQ(str, "abcd0");
  constexpr absl::string_view v("\0123\0", 5);
  EXPECT_EQ(v.size(), 5);
  str.clear();
  EXPECT_THAT(NullFreeString(v, &str),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_EQ(str, "");
}

TEST(FileUtilTest, GetContentsFailsOnMissingFile) {
  std::string contents;
  EXPECT_THAT(GetContents("/file/does/not/exist", &contents),
              StatusIs(absl::StatusCode::kNotFound));
  EXPECT_TRUE(contents.empty());
}

TEST(FileUtilTest, GetContentsFailsOnWrongTypeOfFile) {
  // Directory exists, but is not a regular file.
  std::string contents;
  EXPECT_THAT(
      GetContents(absl::NullSafeStringView(getenv("TEST_SRCDIR")), &contents),
      StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_TRUE(contents.empty());
}

TEST(FileUtilTest, GetContentsSucceed) {
  const std::string filespec = absl::StrCat(
      TestSrcRootDir(), "/googlesql/base/file_util_oss_test.input_file");

  std::string contents;
  GOOGLESQL_EXPECT_OK(GetContents(filespec, &contents));
  EXPECT_FALSE(contents.empty());
  EXPECT_EQ(contents, R"(This file
loads super

great!
)");
}

TEST(FileUtilTest, SetContentsSucceed) {
  const std::string filespec =
      absl::StrCat(TestTmpDir(), "/SetContentsSucceed.file");

  const std::string contents = R"(great multi
  line file. Well
  read!!

  )";
  GOOGLESQL_EXPECT_OK(SetContents(filespec, contents));
  std::string actual_contents;
  GOOGLESQL_EXPECT_OK(GetContents(filespec, &actual_contents));
  EXPECT_EQ(contents, actual_contents);
}

TEST(FileUtilTest, RecursivelyCreateDir) {
  const std::string directory = absl::StrCat(TestTmpDir(), "/test_dir");
  const std::string file = absl::StrCat(directory, "/test_file");

  // Create file fails before parent directory is created.
  absl::Status s = SetContents(file, "test");
  EXPECT_FALSE(s.ok());

  // Success after directory created.
  GOOGLESQL_ASSERT_OK(RecursivelyCreateDir(directory));
  GOOGLESQL_EXPECT_OK(SetContents(file, "test"));
}

TEST(FileUtilTest, RecursivelyCreateDirDeep) {
  const std::string directory = absl::StrCat(TestTmpDir(), "/a/b/c/d");
  const std::string file = absl::StrCat(directory, "/test_file");

  // Create file fails before parent directory is created.
  absl::Status s = SetContents(file, "test");
  EXPECT_FALSE(s.ok());

  // Success after directory created.
  GOOGLESQL_ASSERT_OK(RecursivelyCreateDir(directory));
  GOOGLESQL_EXPECT_OK(SetContents(file, "test"));
}

TEST(FileUtilTest, RecursivelyCreateDirError) {
  const std::string path = absl::StrCat(TestTmpDir(), "/test_thing");
  GOOGLESQL_ASSERT_OK(SetContents(path, "test"));
  // File with the same name exists, should fail.
  absl::Status s = RecursivelyCreateDir(path);
  EXPECT_FALSE(s.ok());
}

TEST(FileUtilTest, CopySuccess) {
  const std::string file_a = absl::StrCat(TestTmpDir(), "/file_a");
  const std::string file_b = absl::StrCat(TestTmpDir(), "/file_b");
  GOOGLESQL_ASSERT_OK(SetContents(file_a, "test"));
  GOOGLESQL_ASSERT_OK(Copy(file_a, file_b));
  std::string file_b_content;
  GOOGLESQL_ASSERT_OK(GetContents(file_b, &file_b_content));
  EXPECT_EQ("test", file_b_content);
}

TEST(FileUtilTest, CopyError) {
  // Parent directory doesn't exist, cannot copy file.
  const std::string file_a = absl::StrCat(TestTmpDir(), "/file_a");
  const std::string file_b = absl::StrCat(TestTmpDir(), "/no_exist_dir/file_b");
  GOOGLESQL_ASSERT_OK(SetContents(file_a, "test"));
  absl::Status s = Copy(file_a, file_b);
  EXPECT_FALSE(s.ok());
}

}  // namespace googlesql::internal
