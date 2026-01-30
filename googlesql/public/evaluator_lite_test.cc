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

#include "googlesql/public/evaluator_lite.h"

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/public/value.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::googlesql::values::Int64;
using ::testing::HasSubstr;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;

namespace googlesql {
namespace {

TEST(EvaluatorLiteTest, SimpleExpression) {
  PreparedExpressionLite expr("1 + 2");
  EXPECT_THAT(expr.Execute(), IsOkAndHolds(Int64(3)));
}

TEST(EvaluatorLiteTest, IncludedFunction) {
  PreparedExpressionLite expr("ARRAY_LENGTH([1,2,3])");
  EXPECT_THAT(expr.Execute(), IsOkAndHolds(Int64(3)));
}

}  // namespace
}  // namespace googlesql
