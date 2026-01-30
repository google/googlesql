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

#include "googlesql/public/functions/generate_array.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/common/float_margin.h"
#include "googlesql/common/internal_value.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/compliance/functions_testlib.h"
#include "googlesql/public/numeric_value.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/substitute.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace functions {

using ::absl_testing::StatusIs;

TEST(GenerateArrayTest, TooManyElementsInt) {
  std::vector<int64_t> values;
  absl::Status status = GenerateArray<int64_t, int64_t>(
      int64_t{1}, int64_t{1000000000}, int64_t{1}, &values);
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kOutOfRange));
}

TEST(GenerateArrayTest, TooManyElementsDouble) {
  std::vector<double> values;
  absl::Status status = GenerateArray<double, double>(
      double{0}, double{1}, std::numeric_limits<double>::min(), &values);
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kOutOfRange));
}

TEST(GenerateArrayTest, ComplianceTests) {
  const std::vector<FunctionTestCall> tests = GetFunctionTestsGenerateArray();
  for (const auto& test : tests) {
    const std::vector<Value>& params = test.params.params();
    // Skip test cases with any null inputs. The actual compliance tests
    // exercise these cases still.
    if (params.size() != 3 ||
        std::find_if(params.begin(), params.end(), [](const Value& param) {
          return param.is_null();
        }) != params.end()) {
      continue;
    }

    const std::string params_string =
        absl::StrJoin(params, ", ", [](std::string* out, const Value& param) {
          absl::StrAppend(out, param.DebugString());
        });
    SCOPED_TRACE(absl::Substitute("GENERATE_ARRAY($0)", params_string));

    const TypeKind input_type = params[0].type_kind();
    Value result;
    absl::Status status;
    switch (input_type) {
      case TYPE_INT64: {
        std::vector<int64_t> output;
        status = GenerateArray<int64_t>(params[0].int64_value(),
                                        params[1].int64_value(),
                                        params[2].int64_value(), &output);
        if (status.ok()) {
          result = values::Int64Array(output);
        }
        break;
      }
      case TYPE_UINT64: {
        std::vector<uint64_t> output;
        status = GenerateArray<uint64_t>(params[0].uint64_value(),
                                         params[1].uint64_value(),
                                         params[2].uint64_value(), &output);
        if (status.ok()) {
          result = values::Uint64Array(output);
        }
        break;
      }
      case TYPE_FLOAT: {
        std::vector<float> output;
        status = GenerateArray<float>(params[0].float_value(),
                                      params[1].float_value(),
                                      params[2].float_value(), &output);
        if (status.ok()) {
          result = values::FloatArray(output);
        }
        break;
      }
      case TYPE_DOUBLE: {
        std::vector<double> output;
        status = GenerateArray<double>(params[0].double_value(),
                                       params[1].double_value(),
                                       params[2].double_value(), &output);
        if (status.ok()) {
          result = values::DoubleArray(output);
        }
        break;
      }
      case TYPE_NUMERIC: {
        std::vector<NumericValue> output;
        status = GenerateArray<NumericValue>(
            params[0].numeric_value(), params[1].numeric_value(),
            params[2].numeric_value(), &output);
        if (status.ok()) {
          result = values::NumericArray(output);
        }
        break;
      }
      case TYPE_BIGNUMERIC: {
        std::vector<BigNumericValue> output;
        status = GenerateArray<BigNumericValue>(
            params[0].bignumeric_value(), params[1].bignumeric_value(),
            params[2].bignumeric_value(), &output);
        if (status.ok()) {
          result = values::BigNumericArray(output);
        }
        break;
      }
      default:
        FAIL() << "Unexpected type kind: " << TypeKind_Name(input_type);
        break;
    }

    if (test.params.status().ok()) {
      GOOGLESQL_EXPECT_OK(status);
      if (status.ok()) {
        std::string reason;
        EXPECT_TRUE(InternalValue::Equals(
            test.params.result(), result,
            ValueEqualityCheckOptions{.float_margin = kDefaultFloatMargin,
                                      .reason = &reason}))
            << "Expected: " << test.params.result() << "\nActual:   " << result
            << "\nReason: " << reason;
      }
    } else {
      EXPECT_THAT(status, StatusIs(test.params.status().code()));
    }
  }
}

}  // namespace functions
}  // namespace googlesql
