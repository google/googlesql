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

#include "googlesql/public/functions/convert_string.h"

#include <math.h>

#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/compliance/functions_testlib.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace functions {

using absl_testing::StatusIs;

template <typename T>
void TestRoundtripValueHex(T value) {}

template <typename UIntT>
void TestRoundtripValueHex(
    typename std::enable_if<std::is_integral<UIntT>::value &&
                                !std::is_signed<UIntT>::value &&
                                !std::is_same<UIntT, bool>::value,
                            UIntT>::type value) {
  std::string hexstr = absl::StrCat("0x", absl::Hex(value));
  UIntT hexout;
  absl::Status error;
  EXPECT_TRUE(StringToNumeric(hexstr, &hexout, &error))
      << " hexstr: " << hexstr;
  GOOGLESQL_EXPECT_OK(error) << "hexstr: " << hexstr << " hexout:" << hexout;
  EXPECT_EQ(value, hexout) << "hexstr: " << hexstr << " hexout:" << hexout;
}

template <typename IntT>
void TestRoundtripValueHex(
    typename std::enable_if<std::is_integral<IntT>::value &&
                                std::is_signed<IntT>::value,
                            IntT>::type value) {
  std::string sign = value < 0 ? "-" : "";
  uint64_t abs_value;
  if (value == std::numeric_limits<IntT>::min()) {
    abs_value = std::numeric_limits<IntT>::max() + 1ULL;
  } else {
    abs_value = std::abs(value);
  }
  std::string hexstr = absl::StrCat(sign, "0x", absl::Hex(abs_value));
  IntT hexout;
  absl::Status error;
  EXPECT_TRUE(StringToNumeric(hexstr, &hexout, &error))
      << " hexstr: " << hexstr;
  GOOGLESQL_EXPECT_OK(error) << "hexstr: " << hexstr << " hexout:" << hexout;
  EXPECT_EQ(value, hexout) << "hexstr: " << hexstr << " hexout:" << hexout;
}

// Converting any numeric value to a string and then back must always return
// the same value. This is guaranteed by NumericToString() implementation.
template <typename T>
void TestRoundtripValue(T value) {
  std::string str;
  absl::Status error;
  EXPECT_TRUE(
      NumericToString<T>(value, &str, &error, /*canonicalize_zero=*/true));
  GOOGLESQL_EXPECT_OK(error);
  EXPECT_GT(str.size(), 0);

  T out;
  EXPECT_TRUE(StringToNumeric<T>(str, &out, &error));
  EXPECT_TRUE(error.ok());

  TestRoundtripValueHex<T>(value);

  if (!std::numeric_limits<T>::is_integer && std::isnan(value)) {
    EXPECT_TRUE(std::isnan(out));
  } else {
    EXPECT_EQ(value, out);
  }
}

template <typename T>
void TestRoundtrip() {
  TestRoundtripValue<T>(0);
  TestRoundtripValue<T>(1);
  if (std::numeric_limits<T>::is_signed) {
    TestRoundtripValue<T>(-1);
  }
  TestRoundtripValue(std::numeric_limits<T>::min());
  TestRoundtripValue(std::numeric_limits<T>::max());
  if (std::numeric_limits<T>::is_integer || !std::numeric_limits<T>::is_exact) {
    TestRoundtripValue(std::numeric_limits<T>::max() - 1);
    TestRoundtripValue(std::numeric_limits<T>::lowest() + 1);
  }
  TestRoundtripValue(std::numeric_limits<T>::lowest());
  if (std::numeric_limits<T>::has_denorm) {
    TestRoundtripValue(std::numeric_limits<T>::denorm_min());
  }
  if (std::numeric_limits<T>::has_infinity) {
    TestRoundtripValue(std::numeric_limits<T>::infinity());
    TestRoundtripValue(-std::numeric_limits<T>::infinity());
  }
  if (std::numeric_limits<T>::has_quiet_NaN) {
    TestRoundtripValue(std::numeric_limits<T>::quiet_NaN());
  }
}

template <typename T>
void TestSingleChar() {
  for (int i = 0; i < 256; ++i) {
    char buf = i;
    T out;
    absl::Status error;
    if (absl::ascii_isdigit(i) && !std::is_same<bool, T>::value) {
      EXPECT_TRUE(StringToNumeric<T>(absl::string_view(&buf, 1), &out, &error));
      EXPECT_TRUE(error.ok());
    } else {
      EXPECT_FALSE(
          StringToNumeric<T>(absl::string_view(&buf, 1), &out, &error));
      EXPECT_FALSE(error.ok());
    }
  }
}

template <typename T>
void TestLongString() {
  char buf[256] = {0, };
  T out;
  absl::Status error;
  EXPECT_FALSE(
      StringToNumeric<T>(absl::string_view(buf, sizeof(buf)), &out, &error));
  EXPECT_FALSE(error.ok());
  EXPECT_TRUE(absl::EndsWith(error.message(), "\0\0\0..."));
}

template <typename T>
void TestAll() {
  TestRoundtrip<T>();
  TestSingleChar<T>();
  TestLongString<T>();
}

TEST(Convert, TestBool) {
  TestAll<bool>();
}

TEST(Convert, TestInt32) { TestAll<int32_t>(); }

TEST(Convert, TestInt64) { TestAll<int64_t>(); }

TEST(Convert, TestUint32) { TestAll<uint32_t>(); }

TEST(Convert, TestUint64) { TestAll<uint64_t>(); }

TEST(Convert, TestFloat) {
  TestAll<float>();
}

TEST(Convert, TestDouble) {
  TestAll<double>();
}

template <typename T>
void TestNumericToString(const QueryParamsWithResult& test) {
  if (test.param(0).is_null()) return;
  std::string str;
  absl::Status error;
  if (test.status().ok()) {
    EXPECT_TRUE(NumericToString<T>(test.param(0).Get<T>(), &str, &error,
                                   /*canonicalize_zero=*/true));
    GOOGLESQL_EXPECT_OK(error);
    EXPECT_EQ(test.result().string_value(), str);
  } else {
    EXPECT_FALSE(NumericToString<T>(test.param(0).Get<T>(), &str, &error,
                                    /*canonicalize_zero=*/true));
    EXPECT_FALSE(error.ok());
    EXPECT_EQ(error.code(), test.status().code());
  }
}

template <typename T>
void TestStringToNumeric(const QueryParamsWithResult& test) {
  if (test.param(0).is_null()) return;
  T out;
  absl::Status error;
  if (test.status().ok()) {
    EXPECT_TRUE(StringToNumeric<T>(test.param(0).string_value(),
                                   &out, &error));
    EXPECT_TRUE(error.ok());
    T expected = test.result().Get<T>();
    if (std::numeric_limits<T>::is_integer || isinf(expected)) {
      EXPECT_EQ(expected, out);
    } else if (isnan(expected)) {
      EXPECT_TRUE(isnan(out));
    } else {
      EXPECT_TRUE(test.float_margin().Equal(expected, out))
          << test.float_margin().PrintError(expected, out);
    }
  } else {
    EXPECT_FALSE(StringToNumeric<T>(test.param(0).string_value(),
                                    &out, &error));
    EXPECT_FALSE(error.ok());
    EXPECT_EQ(error.code(), test.status().code());
  }
}

template <>
void TestStringToNumeric<NumericValue>(const QueryParamsWithResult& test) {
  if (test.param(0).is_null()) return;
  NumericValue out;
  absl::Status error;
  if (test.status().ok()) {
    EXPECT_TRUE(StringToNumeric<NumericValue>(test.param(0).string_value(),
                                              &out, &error));
    GOOGLESQL_EXPECT_OK(error);
    EXPECT_EQ(test.result().numeric_value(), out);
  } else {
    EXPECT_FALSE(StringToNumeric<NumericValue>(test.param(0).string_value(),
                                               &out, &error));
    EXPECT_THAT(error, StatusIs(test.status().code()));
  }
}

template <>
void TestStringToNumeric<BigNumericValue>(const QueryParamsWithResult& test) {
  if (test.param(0).is_null()) return;
  BigNumericValue out;
  absl::Status error;
  if (test.status().ok()) {
    EXPECT_TRUE(StringToNumeric<BigNumericValue>(test.param(0).string_value(),
                                                 &out, &error));
    GOOGLESQL_EXPECT_OK(error);
    EXPECT_EQ(test.result().bignumeric_value(), out);
  } else {
    EXPECT_FALSE(StringToNumeric<BigNumericValue>(test.param(0).string_value(),
                                                  &out, &error));
    EXPECT_THAT(error, StatusIs(test.status().code()));
  }
}

template <>
void TestStringToNumeric<bool>(const QueryParamsWithResult& test) {
  if (test.param(0).is_null()) return;
  bool out;
  absl::Status error;
  if (test.status().ok()) {
    EXPECT_TRUE(
        StringToNumeric<bool>(test.param(0).string_value(), &out, &error));
    EXPECT_TRUE(error.ok());
    bool expected = test.result().Get<bool>();
    EXPECT_EQ(expected, out);
  } else {
    EXPECT_FALSE(
        StringToNumeric<bool>(test.param(0).string_value(), &out, &error));
    EXPECT_FALSE(error.ok());
    EXPECT_EQ(error.code(), test.status().code());
  }
}

TEST(Convert, Compliance) {
  std::vector<QueryParamsWithResult> tests =
      GetFunctionTestsCastNumericString();
  for (const QueryParamsWithResult& test : tests) {
    const TypeKind result_kind =
        test.results().begin()->second.result.type_kind();
    if (result_kind == TYPE_STRING) {
      switch (test.param(0).type_kind()) {
        case TYPE_BOOL:
          TestNumericToString<bool>(test);
          break;
        case TYPE_INT32:
          TestNumericToString<int32_t>(test);
          break;
        case TYPE_INT64:
          TestNumericToString<int64_t>(test);
          break;
        case TYPE_UINT32:
          TestNumericToString<uint32_t>(test);
          break;
        case TYPE_UINT64:
          TestNumericToString<uint64_t>(test);
          break;
        case TYPE_FLOAT:
          TestNumericToString<float>(test);
          break;
        case TYPE_DOUBLE:
          TestNumericToString<double>(test);
          break;
        case TYPE_NUMERIC:
          TestNumericToString<NumericValue>(test);
          break;
        case TYPE_BIGNUMERIC:
          TestNumericToString<BigNumericValue>(test);
          break;
        default:
          FAIL() << "unexpected type: " << test.param(0).type_kind();
      }
    } else {
      switch (result_kind) {
        case TYPE_BOOL:
          TestStringToNumeric<bool>(test);
          break;
        case TYPE_INT32:
          TestStringToNumeric<int32_t>(test);
          break;
        case TYPE_INT64:
          TestStringToNumeric<int64_t>(test);
          break;
        case TYPE_UINT32:
          TestStringToNumeric<uint32_t>(test);
          break;
        case TYPE_UINT64:
          TestStringToNumeric<uint64_t>(test);
          break;
        case TYPE_FLOAT:
          TestStringToNumeric<float>(test);
          break;
        case TYPE_DOUBLE:
          TestStringToNumeric<double>(test);
          break;
        case TYPE_NUMERIC:
          TestStringToNumeric<NumericValue>(test);
          break;
        case TYPE_BIGNUMERIC:
          TestStringToNumeric<BigNumericValue>(test);
          break;
        default:
          FAIL() << "unexpected type: " << test.result().type_kind();
      }
    }
  }
}

TEST(Convert, TestCanonicalizeZeroDouble) {
  std::string out;
  absl::Status status;
  EXPECT_TRUE(
      NumericToString<double>(-0.0, &out, &status, /*canonicalize_zero=*/true));
  GOOGLESQL_ASSERT_OK(status);
  EXPECT_THAT(out, testing::Eq("0"));

  EXPECT_TRUE(NumericToString<double>(-0.0, &out, &status,
                                      /*canonicalize_zero=*/false));
  GOOGLESQL_ASSERT_OK(status);
  EXPECT_THAT(out, testing::Eq("-0"));

  for (const bool canonicalize_zero : {true, false}) {
    EXPECT_TRUE(
        NumericToString<double>(-0.5, &out, &status, canonicalize_zero));
    GOOGLESQL_ASSERT_OK(status);
    EXPECT_THAT(out, testing::Eq("-0.5"));
  }
}

TEST(Convert, TestCanonicalizeZeroFloat) {
  std::string out;
  absl::Status status;
  EXPECT_TRUE(
      NumericToString<float>(-0.0f, &out, &status, /*canonicalize_zero=*/true));
  GOOGLESQL_ASSERT_OK(status);
  EXPECT_THAT(out, testing::Eq("0"));

  EXPECT_TRUE(NumericToString<float>(-0.0f, &out, &status,
                                     /*canonicalize_zero=*/false));
  GOOGLESQL_ASSERT_OK(status);
  EXPECT_THAT(out, testing::Eq("-0"));

  for (const bool canonicalize_zero : {true, false}) {
    EXPECT_TRUE(NumericToString<float>(-0.5, &out, &status, canonicalize_zero));
    GOOGLESQL_ASSERT_OK(status);
    EXPECT_THAT(out, testing::Eq("-0.5"));
  }
}

}  // namespace functions
}  // namespace googlesql
