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

#include "googlesql/tools/execute_query/homedir.h"

#include <cstdlib>
#include <optional>
#include <string>
#include <type_traits>

#include "gtest/gtest.h"
#include "absl/types/optional.h"

namespace googlesql {

class GetHomedirTest : public ::testing::Test {
 protected:
  GetHomedirTest() {
    if (const char *home = ::getenv("HOME")) {
      original_ = home;
    }
  }

  ~GetHomedirTest() override {
    // Restore original value
    SetHome(original_);
  }

  void SetHome(std::optional<std::string> value) {
    if (value.has_value()) {
      EXPECT_EQ(::setenv("HOME", value->c_str(), 1), 0);
    } else {
      ::unsetenv("HOME");
    }
  }

 private:
  std::optional<std::string> original_;
};

TEST_F(GetHomedirTest, EnvVarUnset) {
  SetHome(std::nullopt);

  const std::optional<std::string> got = GetHomedir();

  EXPECT_NE(got, std::nullopt);
  EXPECT_FALSE(got->empty());
}

TEST_F(GetHomedirTest, EnvVarCustom) {
  SetHome("/tmp/custom/home");

  const std::optional<std::string> got = GetHomedir();

  EXPECT_EQ(*got, "/tmp/custom/home");
}

}  // namespace googlesql
