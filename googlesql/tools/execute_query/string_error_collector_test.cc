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

#include "googlesql/tools/execute_query/string_error_collector.h"

#include <memory>
#include <string>

#include "gtest/gtest.h"
#include "absl/strings/string_view.h"

namespace googlesql {
namespace {

class StringErrorCollectorTest : public ::testing::Test {
 protected:
  StringErrorCollectorTest()
      : test_collector_(
            std::make_unique<StringErrorCollector>(&error_string_)) {
    error_string_.clear();
  }

  void RecordError(int line, int column, absl::string_view message) {
    test_collector_->RecordError(line, column, message);
  }

  void RecordWarning(int line, int column, absl::string_view message) {
    test_collector_->RecordWarning(line, column, message);
  }

  std::string error_string_;
  std::unique_ptr<StringErrorCollector> test_collector_;
};

TEST_F(StringErrorCollectorTest, AppendsError) {
  RecordError(1, 2, "foo");
  EXPECT_EQ("1(2): foo\n", error_string_);
}

TEST_F(StringErrorCollectorTest, AppendsWarning) {
  RecordWarning(1, 2, "foo");
  EXPECT_EQ("1(2): foo\n", error_string_);
}

TEST_F(StringErrorCollectorTest, AppendsMultipleError) {
  RecordError(1, 2, "foo");
  RecordError(3, 4, "bar");
  EXPECT_EQ("1(2): foo\n3(4): bar\n", error_string_);
}

TEST_F(StringErrorCollectorTest, AppendsMultipleWarning) {
  RecordWarning(1, 2, "foo");
  RecordWarning(3, 4, "bar");
  EXPECT_EQ("1(2): foo\n3(4): bar\n", error_string_);
}

TEST_F(StringErrorCollectorTest, OffsetWorks) {
  test_collector_ =
      std::make_unique<StringErrorCollector>(&error_string_, true);
  RecordError(1, 2, "foo");
  RecordWarning(3, 4, "bar");
  EXPECT_EQ("2(3): foo\n4(5): bar\n", error_string_);
}

}  // namespace
}  // namespace googlesql
