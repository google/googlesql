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

#include "googlesql/common/function_utils.h"

#include "googlesql/public/function.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace googlesql {

TEST(FunctionUtilsTest, OperatorFunctionNameTest) {
  auto make_function = [](absl::string_view name) {
    return Function(name, Function::kGoogleSQLFunctionGroupName,
                    Function::SCALAR);
  };
  EXPECT_TRUE(FunctionIsOperator(make_function("$add")));
  EXPECT_FALSE(FunctionIsOperator(make_function("count")));
  EXPECT_FALSE(FunctionIsOperator(make_function("$count_star")));
  EXPECT_FALSE(FunctionIsOperator(make_function("$extract")));
}

}  // namespace googlesql
