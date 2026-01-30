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

#include "googlesql/public/functions/bitwise.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/compliance/functions_testlib.h"
#include "googlesql/public/functions/bitwise_agg_mode.pb.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "gtest/gtest.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace functions {
namespace {

template <typename T>
inline T GetDummyValue() {
  return 0xDEADBEEF;
}

template <>
inline std::string GetDummyValue<std::string>() {
  return "\xDE\xAD\xBE\xEF";
}

template <typename T>
inline T GetValue(const Value& in) {
  return in.Get<T>();
}

template <>
inline absl::string_view GetValue<absl::string_view>(const Value& in) {
  return in.bytes_value();
}

template <typename T, typename Result>
// Note: defining fn as std::function would confuse the compiler in template
// argument inference.
void TestBitwiseNot(bool (*fn)(T, Result*, absl::Status*),
                    const Value& in, const Value& expected,
                    const absl::Status& expected_status) {
  Result out = GetDummyValue<Result>();
  absl::Status status;  // actual status
  fn(GetValue<T>(in), &out, &status);
  if (expected_status.ok()) {
    EXPECT_EQ(absl::OkStatus(), status);
    EXPECT_EQ(GetValue<T>(expected), out);
  } else {
    EXPECT_NE(absl::OkStatus(), status) << "Unexpected value: " << out;
  }
}

typedef testing::TestWithParam<QueryParamsWithResult> BitwiseNotTemplateTest;
TEST_P(BitwiseNotTemplateTest, Testlib) {
  const QueryParamsWithResult& param = GetParam();
  ABSL_CHECK_EQ(1, param.num_params());
  const Value& input = param.param(0);
  const Value& expected = param.result();
  if (input.is_null()) {  // NULLs arguments are ignored by this test.
    return;
  }
  switch (input.type_kind()) {
    case TYPE_INT32:
      TestBitwiseNot(&BitwiseNot<int32_t>, input, expected, param.status());
      break;
    case TYPE_INT64:
      TestBitwiseNot(&BitwiseNot<int64_t>, input, expected, param.status());
      break;
    case TYPE_UINT32:
      TestBitwiseNot(&BitwiseNot<uint32_t>, input, expected, param.status());
      break;
    case TYPE_UINT64:
      TestBitwiseNot(&BitwiseNot<uint64_t>, input, expected, param.status());
      break;
    case TYPE_BYTES:
      TestBitwiseNot(&BitwiseNotBytes, input, expected, param.status());
      break;
    default:
      FAIL() << "This op is not tested here: " << param;
  }
}

INSTANTIATE_TEST_SUITE_P(BitwiseNot, BitwiseNotTemplateTest,
                         testing::ValuesIn(GetFunctionTestsBitwiseNot()));

template <typename T1, typename T2, typename Result>
using BinaryFunc = bool (*)(T1, T2, Result*, absl::Status*);

template <typename T1, typename T2, typename Result>
using BinaryFuncWithMode = bool (*)(T1, T2, BitwiseAggEnums::BitwiseAggMode,
                                    Result*, absl::Status*);

template <typename T1, typename T2, typename Result>
// Note: defining fn as std::function would confuse the compiler in template
// argument inference.
void TestBitwiseBinaryOp(BinaryFunc<T1, T2, Result> fn, const Value& in1,
                         const Value& in2, const Value& expected,
                         const absl::Status& expected_status) {
  Result out = GetDummyValue<Result>();
  absl::Status status;  // actual status
  fn(GetValue<T1>(in1), GetValue<T2>(in2), &out, &status);
  if (expected_status.ok()) {
    EXPECT_EQ(absl::OkStatus(), status);
    EXPECT_EQ(GetValue<T1>(expected), out);
  } else {
    EXPECT_NE(absl::OkStatus(), status) << "Unexpected value: " << out;
  }
}

// Same as TestBitwiseBinaryOp, but with a mode parameter.
template <typename T1, typename T2, typename Result>
void TestBitwiseBinaryOpWithMode(BinaryFuncWithMode<T1, T2, Result> fn,
                                 const Value& in1, const Value& in2,
                                 const BitwiseAggEnums::BitwiseAggMode& mode,
                                 const Value& expected,
                                 const absl::Status& expected_status) {
  Result out = GetDummyValue<Result>();
  absl::Status status;  // actual status
  fn(GetValue<T1>(in1), GetValue<T2>(in2), mode, &out, &status);
  if (expected_status.ok()) {
    EXPECT_EQ(absl::OkStatus(), status);
    EXPECT_EQ(GetValue<T1>(expected), out);
  } else {
    EXPECT_NE(absl::OkStatus(), status) << "Unexpected value: " << out;
  }
}

struct BitwiseOrTraits {
  template <typename T>
  static BinaryFunc<T, T, T> GetIntFunction() {
    return &BitwiseOr<T>;
  }
  static BinaryFunc<absl::string_view, absl::string_view, std::string>
  GetBytesFunction() {
    return &BitwiseBinaryOpBytes<std::bit_or>;
  }
  static BinaryFuncWithMode<absl::string_view, absl::string_view, std::string>
  GetBytesWithModeFunction() {
    return &BitwiseBinaryOpBytesWithMode<std::bit_or>;
  }
  static std::vector<QueryParamsWithResult> GetTests() {
    return GetFunctionTestsBitwiseOr(/*with_mode_tests=*/true);
  }
};

struct BitwiseXorTraits {
  template <typename T>
  static BinaryFunc<T, T, T> GetIntFunction() {
    return &BitwiseXor<T>;
  }
  static BinaryFunc<absl::string_view, absl::string_view, std::string>
  GetBytesFunction() {
    return &BitwiseBinaryOpBytes<std::bit_xor>;
  }
  static BinaryFuncWithMode<absl::string_view, absl::string_view, std::string>
  GetBytesWithModeFunction() {
    return &BitwiseBinaryOpBytesWithMode<std::bit_xor>;
  }
  static std::vector<QueryParamsWithResult> GetTests() {
    return GetFunctionTestsBitwiseXor(/*with_mode_tests=*/true);
  }
};

struct BitwiseAndTraits {
  template <typename T>
  static BinaryFunc<T, T, T> GetIntFunction() {
    return &BitwiseAnd<T>;
  }
  static BinaryFunc<absl::string_view, absl::string_view, std::string>
  GetBytesFunction() {
    return &BitwiseBinaryOpBytes<std::bit_and>;
  }
  static BinaryFuncWithMode<absl::string_view, absl::string_view, std::string>
  GetBytesWithModeFunction() {
    return &BitwiseBinaryOpBytesWithMode<std::bit_and>;
  }
  static std::vector<QueryParamsWithResult> GetTests() {
    return GetFunctionTestsBitwiseAnd(/*with_mode_tests=*/true);
  }
};

struct BitwiseLeftShiftTraits {
  template <typename T>
  static BinaryFunc<T, int64_t, T> GetIntFunction() {
    return &BitwiseLeftShift<T>;
  }
  static BinaryFunc<absl::string_view, int64_t, std::string>
  GetBytesFunction() {
    return &BitwiseLeftShiftBytes;
  }
  static BinaryFuncWithMode<absl::string_view, int64_t, std::string>
  GetBytesWithModeFunction() {
    return nullptr;  // Not implemented
  }
  static std::vector<QueryParamsWithResult> GetTests() {
    return GetFunctionTestsBitwiseLeftShift();
  }
};

struct BitwiseRightShiftTraits {
  template <typename T>
  static BinaryFunc<T, int64_t, T> GetIntFunction() {
    return &BitwiseRightShift<T>;
  }
  static BinaryFunc<absl::string_view, int64_t, std::string>
  GetBytesFunction() {
    return &BitwiseRightShiftBytes;
  }
  static BinaryFuncWithMode<absl::string_view, int64_t, std::string>
  GetBytesWithModeFunction() {
    return nullptr;  // Not implemented
  }
  static std::vector<QueryParamsWithResult> GetTests() {
    return GetFunctionTestsBitwiseRightShift();
  }
};

template <typename Traits>
class BitwiseBinaryTest : public ::testing::Test {
};

typedef ::testing::Types<BitwiseOrTraits, BitwiseXorTraits, BitwiseAndTraits,
                         BitwiseLeftShiftTraits, BitwiseRightShiftTraits>
BitwiseBinaryTestTypes;
TYPED_TEST_SUITE(BitwiseBinaryTest, BitwiseBinaryTestTypes);

TYPED_TEST(BitwiseBinaryTest, Testlib) {
  for (const QueryParamsWithResult& param : TypeParam::GetTests()) {
    SCOPED_TRACE(param);
    ASSERT_LE(param.num_params(), 3);
    const Value& input1 = param.param(0);
    const Value& input2 = param.param(1);
    const Value& expected = param.result();
    if (input1.is_null() || input2.is_null()) {
      continue;
    }
    switch (input1.type_kind()) {
      case TYPE_INT32:
        TestBitwiseBinaryOp(TypeParam::template GetIntFunction<int32_t>(),
                            input1, input2, expected, param.status());
        break;
      case TYPE_INT64:
        TestBitwiseBinaryOp(TypeParam::template GetIntFunction<int64_t>(),
                            input1, input2, expected, param.status());
        break;
      case TYPE_UINT32:
        TestBitwiseBinaryOp(TypeParam::template GetIntFunction<uint32_t>(),
                            input1, input2, expected, param.status());
        break;
      case TYPE_UINT64:
        TestBitwiseBinaryOp(TypeParam::template GetIntFunction<uint64_t>(),
                            input1, input2, expected, param.status());
        break;
      case TYPE_BYTES: {
        if (param.num_params() > 2) {
          const auto& fn = TypeParam::GetBytesWithModeFunction();
          // Mode argument is not implemented for BitwiseLeftShift and
          // BitwiseRightShift
          if (fn == nullptr) {
            FAIL() << "Mode argument is not implemented for this function";
          }
          const BitwiseAggEnums::BitwiseAggMode& mode =
              static_cast<BitwiseAggEnums::BitwiseAggMode>(
                  param.param(2).enum_value());
          TestBitwiseBinaryOpWithMode(fn, input1, input2, mode, expected,
                                      param.status());
        } else {
          TestBitwiseBinaryOp(TypeParam::GetBytesFunction(), input1, input2,
                              expected, param.status());
        }
      } break;
      default:
        FAIL() << "This op is not tested here: " << param;
    }
  }
}

template <typename T>
void TestBitCount(const Value& in, const Value& expected) {
  EXPECT_EQ(GetValue<int64_t>(expected), BitCount(GetValue<T>(in)));
}

typedef testing::TestWithParam<QueryParamsWithResult> BitCountTemplateTest;
TEST_P(BitCountTemplateTest, Testlib) {
  const QueryParamsWithResult& param = GetParam();
  ASSERT_EQ(1, param.num_params());
  const Value& input = param.param(0);
  const Value& expected = param.result();
  if (input.is_null()) {
    return;
  }
  switch (input.type_kind()) {
    case TYPE_INT32:
      TestBitCount<int32_t>(input, expected);
      break;
    case TYPE_UINT32:
      // There are UINT32 cases for the compliance test framework, but uint32 is
      // simply coerced to uint64.
      break;
    case TYPE_INT64:
      TestBitCount<int64_t>(input, expected);
      break;
    case TYPE_UINT64:
      TestBitCount<uint64_t>(input, expected);
      break;
    case TYPE_BYTES:
      TestBitCount<absl::string_view>(input, expected);
      break;
    default:
      FAIL() << "This op is not tested here: " << param;
  }
}

INSTANTIATE_TEST_SUITE_P(BitCount, BitCountTemplateTest,
                         testing::ValuesIn(GetFunctionTestsBitCount()));
}  // namespace
}  // namespace functions
}  // namespace googlesql
