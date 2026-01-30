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

#include "googlesql/public/functions/string.h"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "googlesql/base/logging.h"
#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/compliance/functions_testlib.h"
#include "googlesql/public/functions/normalize_mode.pb.h"
#include "googlesql/public/type.h"
#include "googlesql/public/type.pb.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"

namespace googlesql {
namespace functions {

using ::testing::_;
using testing::HasSubstr;
using absl_testing::StatusIs;

namespace {

template <typename OutType, typename FunctionType, typename... Args>
bool EvaluateFunction(FunctionType function, Args&&... args, OutType* out,
                      absl::Status* status) {
  if constexpr (std::is_invocable_v<decltype(function), Args..., OutType*,
                                    absl::Status*>) {
    return function(args..., out, status);
  } else {
    *status = function(args..., out);
    return status->ok();
  }
}

template <typename OutType, typename FunctionType, class... Args>
void TestFunction(FunctionType function, const QueryParamsWithResult& param,
                  Args... args) {
  OutType out;
  absl::Status status;
  bool evaluation_result = EvaluateFunction<OutType, FunctionType, Args...>(
      function, std::forward<Args>(args)..., &out, &status);
  EXPECT_EQ(evaluation_result, param.status().ok());
  if (param.status().ok()) {
    EXPECT_EQ(absl::OkStatus(), status);
    EXPECT_TRUE(param.result().Equals(Value::Make<OutType>(out)))
        << "Expected: " << param.result() << "\n"
        << "Actual: " << out << "\n";
  } else if (!param.status().message().empty()) {
    // If an error message is provided, it means we want to test the exact
    // message instead of a binary success/failure.
    EXPECT_EQ(param.status(), status);
  } else {
    EXPECT_NE(absl::OkStatus(), status) << "Unexpected value: " << out;
  }
}

template <typename ElementType, typename FunctionType, class... Args>
void TestArrayFunction(FunctionType function,
                       const QueryParamsWithResult& param, Args... args) {
  std::vector<ElementType> out;
  absl::Status status;
  bool evaluation_result =
      EvaluateFunction<std::vector<ElementType>, FunctionType, Args...>(
          function, std::forward<Args>(args)..., &out, &status);
  EXPECT_EQ(evaluation_result, param.status().ok());
  Value array_value;
  if (status.ok()) {
    std::vector<Value> elements;
    for (const auto& v : out) {
      elements.push_back(Value::Make(v));
    }
    array_value = values::Array(param.result().type()->AsArray(), elements);
  }
  if (param.status().ok()) {
    GOOGLESQL_ASSERT_OK(status);
    EXPECT_TRUE(param.result().Equals(array_value))
        << "Expected: " << param.result() << "\n"
        << "Actual: " << array_value << "\n";
  } else {
    EXPECT_NE(absl::OkStatus(), status) << "Unexpected value: " << array_value;
  }
}

template <typename OutType, typename FunctionType, class... Args>
void TestStringFunction(FunctionType function,
                        const QueryParamsWithResult& param, Args... args) {
  OutType out;
  absl::Status status;
  bool evaluation_result = EvaluateFunction<OutType, FunctionType, Args...>(
      function, std::forward<Args>(args)..., &out, &status);
  EXPECT_EQ(evaluation_result, param.status().ok());
  if (param.status().ok()) {
    EXPECT_EQ(absl::OkStatus(), status);
    EXPECT_TRUE(param.result().Equals(Value::String(out)))
        << "Expected: " << param.result() << "\n"
        << "Actual: '" << out << "'\n";
  } else if (!param.status().message().empty()) {
    // If an error message is provided, it means we want to test the exact
    // message instead of a binary success/failure.
    EXPECT_EQ(param.status(), status);
  } else {
    EXPECT_NE(absl::OkStatus(), status) << "Unexpected value: " << out;
  }
}

template <typename OutType, typename FunctionType, class... Args>
void TestBytesFunction(FunctionType function,
                       const QueryParamsWithResult& param, Args... args) {
  OutType out;
  absl::Status status;
  bool evaluation_result = EvaluateFunction<OutType, FunctionType, Args...>(
      function, std::forward<Args>(args)..., &out, &status);
  EXPECT_EQ(evaluation_result, param.status().ok());
  if (param.status().ok()) {
    EXPECT_EQ(absl::OkStatus(), status);
    EXPECT_TRUE(param.result().Equals(Value::Bytes(out)))
        << "Expected: " << param.result() << "\n"
        << "Actual: " << out << "\n";
  } else {
    EXPECT_NE(absl::OkStatus(), status) << "Unexpected value: " << out;
  }
}

typedef testing::TestWithParam<FunctionTestCall> StringTemplateTest;
TEST_P(StringTemplateTest, Testlib) {
  const FunctionTestCall& param = GetParam();
  const std::string& function = param.function_name;
  const std::vector<Value>& args = param.params.params();
  for (const Value& arg : args) {
    // Ignore tests with null arguments.
    if (arg.is_null()) return;
  }
  if (function == "strpos") {
    if (args[0].type_kind() == TYPE_STRING) {
      TestFunction<int64_t>(&StrposUtf8, param.params, args[0].string_value(),
                            args[1].string_value());
    } else {
      TestFunction<int64_t>(&StrposBytes, param.params, args[0].bytes_value(),
                            args[1].bytes_value());
    }
  } else if (function == "length") {
    if (args[0].type_kind() == TYPE_STRING) {
      TestFunction<int64_t>(&LengthUtf8, param.params, args[0].string_value());
    } else {
      TestFunction<int64_t>(&LengthBytes, param.params, args[0].bytes_value());
    }
  } else if (function == "starts_with") {
    if (args[0].type_kind() == TYPE_STRING) {
      TestFunction<bool>(&StartsWithUtf8, param.params, args[0].string_value(),
                         args[1].string_value());
    } else {
      TestFunction<bool>(&StartsWithBytes, param.params, args[0].bytes_value(),
                         args[1].bytes_value());
    }
  } else if (function == "ends_with") {
    if (args[0].type_kind() == TYPE_STRING) {
      TestFunction<bool>(&EndsWithUtf8, param.params, args[0].string_value(),
                         args[1].string_value());
    } else {
      TestFunction<bool>(&EndsWithBytes, param.params, args[0].bytes_value(),
                         args[1].bytes_value());
    }
  } else if (function == "trim") {
    if (args[0].type_kind() == TYPE_BYTES) {
      TestBytesFunction<absl::string_view>(&TrimBytes, param.params,
                                           args[0].bytes_value(),
                                           args[1].bytes_value());
    } else if (args.size() == 2) {
      TestStringFunction<absl::string_view>(&TrimUtf8, param.params,
                                            args[0].string_value(),
                                            args[1].string_value());
    } else {
      TestStringFunction<absl::string_view>(&TrimSpacesUtf8, param.params,
                                            args[0].string_value());
    }
  } else if (function == "ltrim") {
    if (args[0].type_kind() == TYPE_BYTES) {
      TestBytesFunction<absl::string_view>(&LeftTrimBytes, param.params,
                                           args[0].bytes_value(),
                                           args[1].bytes_value());
    } else if (args.size() == 2) {
      TestStringFunction<absl::string_view>(&LeftTrimUtf8, param.params,
                                            args[0].string_value(),
                                            args[1].string_value());
    } else {
      TestStringFunction<absl::string_view>(&LeftTrimSpacesUtf8, param.params,
                                            args[0].string_value());
    }
  } else if (function == "rtrim") {
    if (args[0].type_kind() == TYPE_BYTES) {
      TestBytesFunction<absl::string_view>(&RightTrimBytes, param.params,
                                           args[0].bytes_value(),
                                           args[1].bytes_value());
    } else if (args.size() == 2) {
      TestStringFunction<absl::string_view>(&RightTrimUtf8, param.params,
                                            args[0].string_value(),
                                            args[1].string_value());
    } else {
      TestStringFunction<absl::string_view>(&RightTrimSpacesUtf8, param.params,
                                            args[0].string_value());
    }
  } else if (function == "left") {
    if (args[0].type_kind() == TYPE_BYTES) {
      TestBytesFunction<absl::string_view>(&LeftBytes, param.params,
                                           args[0].bytes_value(),
                                           args[1].int64_value());
    } else {
      TestStringFunction<absl::string_view>(&LeftUtf8, param.params,
                                            args[0].string_value(),
                                            args[1].int64_value());
    }
  } else if (function == "right") {
    if (args[0].type_kind() == TYPE_BYTES) {
      TestBytesFunction<absl::string_view>(&RightBytes, param.params,
                                           args[0].bytes_value(),
                                           args[1].int64_value());
    } else {
      TestStringFunction<absl::string_view>(&RightUtf8, param.params,
                                            args[0].string_value(),
                                            args[1].int64_value());
    }
  } else if (function == "substr" || function == "substring") {
    if (args[0].type_kind() == TYPE_BYTES) {
      if (args.size() == 2) {
        TestBytesFunction<absl::string_view>(&SubstrBytes, param.params,
                                             args[0].bytes_value(),
                                             args[1].int64_value());
      } else {
        TestBytesFunction<absl::string_view>(
            &SubstrWithLengthBytes, param.params, args[0].bytes_value(),
            args[1].int64_value(), args[2].int64_value());
      }
    } else {
      if (args.size() == 2) {
        TestStringFunction<absl::string_view>(&SubstrUtf8, param.params,
                                              args[0].string_value(),
                                              args[1].int64_value());
      } else {
        TestStringFunction<absl::string_view>(
            &SubstrWithLengthUtf8, param.params, args[0].string_value(),
            args[1].int64_value(), args[2].int64_value());
      }
    }
  } else if (function == "upper" || function == "ucase") {
    if (args[0].type_kind() == TYPE_BYTES) {
      TestBytesFunction<std::string>(&UpperBytes, param.params,
                                     args[0].bytes_value());
    } else {
      auto upper_function = [](absl::string_view str, std::string* out,
                               absl::Status* error) {
        return UpperUtf8(str, out, error);
      };
      TestStringFunction<std::string>(upper_function, param.params,
                                      args[0].string_value());
    }
  } else if (function == "lower" || function == "lcase") {
    if (args[0].type_kind() == TYPE_BYTES) {
      TestBytesFunction<std::string>(&LowerBytes, param.params,
                                     args[0].bytes_value());
    } else {
      auto lower_function = [](absl::string_view str, std::string* out,
                               absl::Status* error) {
        return LowerUtf8(str, out, error);
      };
      TestStringFunction<std::string>(lower_function, param.params,
                                      args[0].string_value());
    }
  } else if (function == "replace") {
    if (args[0].type_kind() == TYPE_BYTES) {
      TestBytesFunction<std::string>(
          &ReplaceBytes, param.params, args[0].bytes_value(),
          args[1].bytes_value(), args[2].bytes_value());
    } else {
      TestStringFunction<std::string>(
          &ReplaceUtf8, param.params, args[0].string_value(),
          args[1].string_value(), args[2].string_value());
    }
  } else if (function == "safe_convert_bytes_to_string") {
    TestStringFunction<std::string>(&SafeConvertBytes, param.params,
                                    args[0].bytes_value());
  }
}

TEST(FromHex, Bytes) {
  std::string out;
  absl::Status error;
  EXPECT_TRUE(FromHex("aaa", &out, &error));
  EXPECT_EQ(out, "\x0a\xaa");
  GOOGLESQL_EXPECT_OK(error);
}

TEST(TestSoundex, Raw) {
  std::string out;
  absl::Status error;
  EXPECT_TRUE(Soundex("\x80", &out, &error));
  EXPECT_EQ(out, "");
  GOOGLESQL_EXPECT_OK(error);
}

TEST(TestFirstCharOfStringToAscii, Raw) {
  int64_t out;
  absl::Status error;
  EXPECT_FALSE(FirstCharOfStringToASCII("\uFFFF", &out, &error));
  EXPECT_TRUE(FirstByteOfBytesToASCII("\uFFFF", &out, &error));
  EXPECT_EQ(out, int64_t{239});
}

TEST(SafeConvertBytes, Capacity) {
  std::string out;
  absl::Status error;

  // 20 valid characters + trailing invalid one.
  EXPECT_TRUE(SafeConvertBytes("abcde12345abcde12345\xff", &out, &error));
  GOOGLESQL_EXPECT_OK(error);
  const int required_capacity = 20 + 3;
  EXPECT_GE(out.capacity(), required_capacity);
  EXPECT_LT(out.capacity(), 2 * required_capacity);
}

INSTANTIATE_TEST_SUITE_P(String, StringTemplateTest,
                         testing::ValuesIn(GetFunctionTestsString()));

typedef testing::TestWithParam<FunctionTestCall> StringOctetLengthTest;
TEST_P(StringOctetLengthTest, Testlib) {
  const FunctionTestCall& param = GetParam();
  const std::vector<Value>& args = param.params.params();
  if (args[0].is_null()) return;
  if (args[0].type_kind() == TYPE_STRING) {
    TestFunction<int64_t>(&LengthBytes, param.params, args[0].string_value());
  } else {
    TestFunction<int64_t>(&LengthBytes, param.params, args[0].bytes_value());
  }
}

INSTANTIATE_TEST_SUITE_P(String, StringOctetLengthTest,
                         testing::ValuesIn(GetFunctionTestsOctetLength()));

typedef testing::TestWithParam<FunctionTestCall> StringASCIITest;
TEST_P(StringASCIITest, Testlib) {
  const FunctionTestCall& param = GetParam();
  const std::vector<Value>& args = param.params.params();
  if (args[0].is_null()) return;
  if (args[0].type_kind() == TYPE_STRING) {
    TestFunction<int64_t>(&FirstCharOfStringToASCII, param.params,
                          args[0].string_value());
  } else {
    TestFunction<int64_t>(&FirstByteOfBytesToASCII, param.params,
                          args[0].bytes_value());
  }
}

INSTANTIATE_TEST_SUITE_P(String, StringASCIITest,
                         testing::ValuesIn(GetFunctionTestsAscii()));

typedef testing::TestWithParam<FunctionTestCall> StringUnicodeTest;
TEST_P(StringUnicodeTest, Testlib) {
  const FunctionTestCall& param = GetParam();
  const std::vector<Value>& args = param.params.params();
  if (args[0].is_null()) return;
  TestFunction<int64_t>(&FirstCharToCodePoint, param.params,
                        args[0].string_value());
}

INSTANTIATE_TEST_SUITE_P(String, StringUnicodeTest,
                         testing::ValuesIn(GetFunctionTestsUnicode()));

typedef testing::TestWithParam<FunctionTestCall> StringCHRTest;
TEST_P(StringCHRTest, Testlib) {
  const FunctionTestCall& param = GetParam();
  const std::vector<Value>& args = param.params.params();
  if (args[0].is_null()) return;
  TestStringFunction<std::string>(&CodePointToString, param.params,
                                  args[0].int64_value());
}

INSTANTIATE_TEST_SUITE_P(String, StringCHRTest,
                         testing::ValuesIn(GetFunctionTestsChr()));

typedef testing::TestWithParam<FunctionTestCall> InstrTemplateTest;
TEST_P(InstrTemplateTest, Testlib) {
  const FunctionTestCall& param = GetParam();
  const std::vector<Value>& args = param.params.params();
  for (const Value& arg : args) {
    // Ignore tests with null arguments.
    if (arg.is_null()) return;
  }
  int64_t pos = 1;
  int64_t occurrence = 1;
  if (args.size() >= 3) pos = args[2].int64_value();
  if (args.size() == 4) occurrence = args[3].int64_value();

  if (args[0].type_kind() == TYPE_STRING) {
    TestFunction<int64_t>(&StrPosOccurrenceUtf8, param.params,
                          args[0].string_value(), args[1].string_value(), pos,
                          occurrence);
  } else {
    TestFunction<int64_t>(&StrPosOccurrenceBytes, param.params,
                          args[0].bytes_value(), args[1].bytes_value(), pos,
                          occurrence);
  }
}

INSTANTIATE_TEST_SUITE_P(String1, InstrTemplateTest,
                         testing::ValuesIn(GetFunctionTestsInstr1()));

INSTANTIATE_TEST_SUITE_P(String2, InstrTemplateTest,
                         testing::ValuesIn(GetFunctionTestsInstr2()));

INSTANTIATE_TEST_SUITE_P(String3, InstrTemplateTest,
                         testing::ValuesIn(GetFunctionTestsInstr3()));

INSTANTIATE_TEST_SUITE_P(String4, InstrTemplateTest,
                         testing::ValuesIn(GetFunctionTestsInstrNoCollator()));

typedef testing::TestWithParam<FunctionTestCall> StringSoundexTemplateTest;
TEST_P(StringSoundexTemplateTest, Testlib) {
  const FunctionTestCall& param = GetParam();
  const std::vector<Value>& args = param.params.params();
  // Ignore tests with null arguments.
  if (args[0].is_null()) return;
  TestStringFunction<std::string>(&Soundex, param.params,
                                  args[0].string_value());
}

INSTANTIATE_TEST_SUITE_P(String, StringSoundexTemplateTest,
                         testing::ValuesIn(GetFunctionTestsSoundex()));

typedef testing::TestWithParam<FunctionTestCall> StringTranslateTemplateTest;
TEST_P(StringTranslateTemplateTest, Testlib) {
  const FunctionTestCall& param = GetParam();
  const std::vector<Value>& args = param.params.params();
  for (const Value& arg : args) {
    // Ignore tests with null arguments.
    if (arg.is_null()) return;
  }
  if (args[0].type_kind() == TYPE_STRING) {
    TestStringFunction<std::string>(
        &TranslateUtf8, param.params, args[0].string_value(),
        args[1].string_value(), args[2].string_value());
  } else {
    TestBytesFunction<std::string>(
        &TranslateBytes, param.params, args[0].bytes_value(),
        args[1].bytes_value(), args[2].bytes_value());
  }
}

INSTANTIATE_TEST_SUITE_P(String, StringTranslateTemplateTest,
                         testing::ValuesIn(GetFunctionTestsTranslate()));

typedef testing::TestWithParam<FunctionTestCall> StringInitCapTemplateTest;
TEST_P(StringInitCapTemplateTest, Testlib) {
  const FunctionTestCall& param = GetParam();
  const std::vector<Value>& args = param.params.params();
  for (const Value& arg : args) {
    // Ignore tests with null arguments.
    if (arg.is_null()) return;
  }
  if (args.size() == 2) {
    TestStringFunction<std::string>(&InitialCapitalize, param.params,
                                    args[0].string_value(),
                                    args[1].string_value());
  } else {
    TestStringFunction<std::string>(&InitialCapitalizeDefault, param.params,
                                    args[0].string_value());
  }
}

INSTANTIATE_TEST_SUITE_P(String, StringInitCapTemplateTest,
                         testing::ValuesIn(GetFunctionTestsInitCap()));

TEST(SubstrWithLength, Utf8) {
  absl::Status error;
  absl::string_view out;
  EXPECT_TRUE(SubstrWithLengthUtf8("ЩФБШ", -5, 3, &out, &error));
  EXPECT_EQ(out, "ЩФБ");
}

TEST(Replace, HandleExplodingStringLength) {
  absl::Status error;
  std::string generation0 = "22222222";
  // Generation 1: 8 -> 8^2 (64)
  std::string generation1;
  EXPECT_TRUE(ReplaceUtf8(generation0, "2", generation0, &generation1, &error));
  // Generation 2: 64 -> 64^2 (4k)
  std::string generation2;
  EXPECT_TRUE(ReplaceUtf8(generation1, "2", generation1, &generation2, &error));
  // Generation 2: 4k -> 4k^2 (16m) TOO BIG
  std::string generation3;
  EXPECT_FALSE(
      ReplaceUtf8(generation2, "2", generation2, &generation3, &error));
}

// While Substr (and friends) make no guarantees on behavior in the face of
// invalid UTF, there isn't really a 'correct' answer for doing position
// substrings of invalid characters (does each invalid byte count as a bad
// codepoints? Do we respect whatever byte-encoding is in there? Do we just
// let ICU make a determination?).
TEST(SubstrWithLength, BadUtf8) {
  absl::Status error;
  absl::string_view out;
  // There is bad utf, but we don't need to traverse it, so it should work.
  EXPECT_TRUE(SubstrWithLengthUtf8("𐍈\xf0\x90\x8d𐍈", 1, 1, &out, &error));
  EXPECT_EQ(out, "𐍈");

  // There is bad utf, but we don't need to traverse it, so it should work.
  EXPECT_TRUE(SubstrWithLengthUtf8("𐍈\xf0\x90\x8d𐍈", -1, 1, &out, &error));
  EXPECT_EQ(out, "𐍈");

  // We must traverse bad utf to return the result
  // (indeed, the bad-utf is in the result).
  EXPECT_FALSE(SubstrWithLengthUtf8("𐍈\xf0\x90\x8d𐍈", 1, 2, &out, &error));
  EXPECT_FALSE(SubstrWithLengthUtf8("𐍈\xf0\x90\x8d𐍈", 2, 2, &out, &error));

  // We must traverse bad utf to return the result
  // (indeed, the bad-utf is in the result).
  EXPECT_FALSE(SubstrWithLengthUtf8("𐍈\xf0\x90\x8d𐍈", -2, 1, &out, &error));
  EXPECT_FALSE(SubstrWithLengthUtf8("𐍈\xf0\x90\x8d𐍈", -2, 2, &out, &error));

  // Although the expected substring is valid utf, we cannot figure that out
  // without traversing invalid utf-8.
  EXPECT_FALSE(SubstrWithLengthUtf8("𐍈\xf0\x90\x8d𐍈", 5, 1, &out, &error));
  EXPECT_FALSE(SubstrWithLengthUtf8("𐍈\xf0\x90\x8d𐍈", -5, 1, &out, &error));
}

void TestUtf8Trimmer(const Utf8Trimmer& trimmer, absl::string_view input,
                     absl::string_view expected_ltrim,
                     absl::string_view expected_rtrim,
                     absl::string_view expected_trim) {
  absl::Status error;
  absl::string_view ltrim_out;
  EXPECT_TRUE(trimmer.TrimLeft(input, &ltrim_out, &error));
  GOOGLESQL_EXPECT_OK(error);
  EXPECT_EQ(expected_ltrim, ltrim_out);

  absl::string_view rtrim_out;
  EXPECT_TRUE(trimmer.TrimRight(input, &rtrim_out, &error));
  GOOGLESQL_EXPECT_OK(error);
  EXPECT_EQ(expected_rtrim, rtrim_out);

  absl::string_view trim_out;
  EXPECT_TRUE(trimmer.Trim(input, &trim_out, &error));
  GOOGLESQL_EXPECT_OK(error);
  EXPECT_EQ(expected_trim, trim_out);
}

void ExpectUtf8TrimmerError(const Utf8Trimmer& trimmer,
                            absl::string_view input) {
  absl::string_view out;

  absl::Status error;
  EXPECT_FALSE(trimmer.TrimLeft(input, &out, &error));
  EXPECT_THAT(error, StatusIs(absl::StatusCode::kOutOfRange));
  error = absl::OkStatus();

  EXPECT_FALSE(trimmer.TrimRight(input, &out, &error));
  EXPECT_THAT(error, StatusIs(absl::StatusCode::kOutOfRange));
  error = absl::OkStatus();

  EXPECT_FALSE(trimmer.Trim(input, &out, &error));
  EXPECT_THAT(error, StatusIs(absl::StatusCode::kOutOfRange));
}

TEST(Trim, Utf8) {
  // Ill formed strings is undefined behavior, so this can change, however, we
  // want to test current behavior to validate it is the current 'expected'
  // behavior (i.e. we don't investigate the string at all in this case).
  constexpr absl::string_view kIllFormed = "\xA4";

  // Use of trimmer uninitialized, should trim nothing.
  Utf8Trimmer trimmer;
  absl::Status error;

  TestUtf8Trimmer(trimmer, "", "", "", "");
  TestUtf8Trimmer(trimmer, "abc", "abc", "abc", "abc");
  TestUtf8Trimmer(trimmer, "бч", "бч", "бч", "бч");
  TestUtf8Trimmer(trimmer, "\ufffd", "\ufffd", "\ufffd", "\ufffd");
  TestUtf8Trimmer(trimmer, kIllFormed, kIllFormed, kIllFormed, kIllFormed);

  // Use of trimmer with nothing in the character set.
  EXPECT_TRUE(trimmer.Initialize("", &error));
  GOOGLESQL_EXPECT_OK(error);
  TestUtf8Trimmer(trimmer, "", "", "", "");
  TestUtf8Trimmer(trimmer, "abc", "abc", "abc", "abc");
  TestUtf8Trimmer(trimmer, "бч", "бч", "бч", "бч");
  TestUtf8Trimmer(trimmer, "\ufffd", "\ufffd", "\ufffd", "\ufffd");
  TestUtf8Trimmer(trimmer, kIllFormed, kIllFormed, kIllFormed, kIllFormed);

  // Use of trimmer explicitly initialized with the unicode replacement
  // character.
  EXPECT_TRUE(trimmer.Initialize("\ufffd", &error));
  GOOGLESQL_EXPECT_OK(error);
  TestUtf8Trimmer(trimmer, "", "", "", "");
  TestUtf8Trimmer(trimmer, "abc", "abc", "abc", "abc");
  TestUtf8Trimmer(trimmer, "\ufffdб\ufffdч\ufffd", "б\ufffdч\ufffd",
                  "\ufffdб\ufffdч", "б\ufffdч");
  TestUtf8Trimmer(trimmer, "\ufffda\ufffda\ufffd", "a\ufffda\ufffd",
                  "\ufffda\ufffda", "a\ufffda");
  ExpectUtf8TrimmerError(trimmer, kIllFormed);

  // Make sure duplicates work.
  EXPECT_TRUE(trimmer.Initialize("бб", &error));
  GOOGLESQL_EXPECT_OK(error);
  TestUtf8Trimmer(trimmer, "", "", "", "");
  TestUtf8Trimmer(trimmer, "abc", "abc", "abc", "abc");
  TestUtf8Trimmer(trimmer, "бч", "ч", "бч", "ч");

  // Test complex repeats
  EXPECT_TRUE(trimmer.Initialize("abcabc", &error));
  GOOGLESQL_EXPECT_OK(error);
  TestUtf8Trimmer(trimmer, "", "", "", "");
  TestUtf8Trimmer(trimmer, "abc", "", "", "");
  TestUtf8Trimmer(trimmer, "бч", "бч", "бч", "бч");
  // It is undefined behavior what we chose to do on ill formed strings, in this
  // case, we don't detect that we have an ill formed string.
  TestUtf8Trimmer(trimmer, kIllFormed, kIllFormed, kIllFormed, kIllFormed);
}

TEST(Split, Utf8) {
  absl::Status error;
  std::vector<std::string> result;

  // Only ASCII characters.
  EXPECT_TRUE(SplitUtf8("a,b", ",", &result, &error));
  EXPECT_THAT(result, ::testing::ElementsAre("a", "b"));
  EXPECT_TRUE(SplitBytes("a,b", ",", &result, &error));
  EXPECT_THAT(result, ::testing::ElementsAre("a", "b"));

  // Empty delimiter.
  EXPECT_TRUE(SplitUtf8("ab", "", &result, &error));
  EXPECT_THAT(result, ::testing::ElementsAre("a", "b"));
  EXPECT_TRUE(SplitBytes("ab", "", &result, &error));
  EXPECT_THAT(result, ::testing::ElementsAre("a", "b"));

  // Empty input with any delimiter.
  EXPECT_TRUE(SplitUtf8("", "a", &result, &error));
  EXPECT_THAT(result, ::testing::ElementsAre(""));
  EXPECT_TRUE(SplitBytes("", "a", &result, &error));
  EXPECT_THAT(result, ::testing::ElementsAre(""));

  // Empty string represented as string_view which is different from
  // string_view("", 0)
  EXPECT_TRUE(SplitUtf8(absl::string_view(), "a", &result, &error));
  EXPECT_THAT(result, ::testing::ElementsAre(""));
  EXPECT_TRUE(SplitBytes(absl::string_view(), "a", &result, &error));
  EXPECT_THAT(result, ::testing::ElementsAre(""));

  // Empty delimiter and empty input produces single element.
  EXPECT_TRUE(SplitUtf8("", "", &result, &error));
  EXPECT_THAT(result, ::testing::ElementsAre(""));
  EXPECT_TRUE(SplitBytes("", "", &result, &error));
  EXPECT_THAT(result, ::testing::ElementsAre(""));

  // UTF8 string, ASCII delimiter.
  EXPECT_TRUE(SplitUtf8("чижик пыжик", " ", &result, &error));
  EXPECT_THAT(result, ::testing::ElementsAre("чижик", "пыжик"));
  EXPECT_TRUE(SplitBytes("чижик пыжик", " ", &result, &error));
  EXPECT_THAT(result, ::testing::ElementsAre("чижик", "пыжик"));

  // UTF8 string, UTF8 delimiter.
  EXPECT_TRUE(SplitUtf8("абракадабра", "абр", &result, &error));
  EXPECT_THAT(result, ::testing::ElementsAre("", "акад", "а"));
  EXPECT_TRUE(SplitBytes("абракадабра", "абр", &result, &error));
  EXPECT_THAT(result, ::testing::ElementsAre("", "акад", "а"));

  // UTF8 string with Hindi matras, empty delimiter. Matra looks like a single
  // character, but is combination of consonant and vowel - 2 UTF8 characters.
  EXPECT_TRUE(SplitUtf8("विभि", "", &result, &error));
  EXPECT_THAT(result, ::testing::ElementsAre("व", "ि", "भ", "ि"));
  // Bytes version just breaks into bytes.
  EXPECT_TRUE(SplitBytes("विभि", "", &result, &error));
  EXPECT_THAT(result, ::testing::ElementsAreArray(
                          {"\xE0", "\xA4", "\xB5", "\xE0", "\xA4", "\xBF",
                           "\xE0", "\xA4", "\xAD", "\xE0", "\xA4", "\xBF"}));

  // UTF8 string with Hindi vowel qualifiers same in delimiter.
  EXPECT_TRUE(SplitUtf8("विभिवि", "भि", &result, &error));
  EXPECT_THAT(result, ::testing::ElementsAre("वि", "वि"));
  EXPECT_TRUE(SplitBytes("विभिवि", "भि", &result, &error));
  EXPECT_THAT(result, ::testing::ElementsAre("वि", "वि"));

  // UTF8 string with invalid UTF8 delimiter fails.
  EXPECT_FALSE(SplitUtf8("विभि", "\xA4", &result, &error));
  EXPECT_THAT(
      error,
      StatusIs(_,
               HasSubstr(
                   "Delimiter in SPLIT function is not a valid UTF-8 string")));
  // But bytes version doesn't care.
  EXPECT_TRUE(SplitBytes("विभि", "\xA4", &result, &error));
  EXPECT_THAT(result, ::testing::ElementsAre("\xE0", "\xB5\xE0", "\xBF\xE0",
                                             "\xAD\xE0", "\xBF"));
}

typedef testing::TestWithParam<FunctionTestCall> NormalizeStringTemplateTest;
TEST_P(NormalizeStringTemplateTest, Testlib) {
  const FunctionTestCall& param = GetParam();
  const std::string& function = param.function_name;
  const std::vector<Value>& args = param.params.params();
  for (const Value& arg : args) {
    // Ignore tests with null arguments.
    if (arg.is_null()) return;
  }
  NormalizeMode mode = NormalizeMode::NFC;
  if (args.size() == 2) {
    ABSL_CHECK_EQ(googlesql::TypeKind::TYPE_ENUM, args[1].type_kind());
    mode = static_cast<NormalizeMode>(args[1].enum_value());
  }
  if (function == "normalize") {
    TestStringFunction<std::string>(&Normalize, param.params,
                                    args[0].string_value(), mode,
                                    false /* is_casefold */);
  } else if (function == "normalize_and_casefold") {
    TestStringFunction<std::string>(&Normalize, param.params,
                                    args[0].string_value(), mode,
                                    true /* is_casefold */);
  }
}

INSTANTIATE_TEST_SUITE_P(String, NormalizeStringTemplateTest,
                         testing::ValuesIn(GetFunctionTestsNormalize()));

TEST(Normalize, InvalidNormalizeMode) {
  std::string out;
  absl::Status error;
  EXPECT_FALSE(
      Normalize("abc", static_cast<NormalizeMode>(10), true, &out, &error));
  EXPECT_THAT(error,
              StatusIs(_, HasSubstr("A valid normalize mode is required")));
}

TEST(Normalize, InvalidUtf8Input) {
  std::string out;
  absl::Status error;
// Verifies that invalid UTF-8 chars in the input are replaced with U+FFFD
// during normalization.
#define U_FFFD "\xef\xbf\xbd"
  for (int mode = NormalizeMode::NFC; mode < NormalizeMode_ARRAYSIZE; ++mode) {
    out.clear();
    EXPECT_TRUE(Normalize("abcABC\xc2", static_cast<NormalizeMode>(mode),
                          false /* is_casefold */, &out, &error));
    EXPECT_EQ("abcABC" U_FFFD, out);
    out.clear();
    EXPECT_TRUE(Normalize("abcABC\xc2", static_cast<NormalizeMode>(mode),
                          true /* is_casefold */, &out, &error));
    EXPECT_EQ("abcabc" U_FFFD, out);
  }
#undef U_FFFD
}

typedef testing::TestWithParam<FunctionTestCall> Base64StringTemplateTest;
TEST_P(Base64StringTemplateTest, Testlib) {
  const FunctionTestCall& param = GetParam();
  const std::string& function = param.function_name;
  const Value arg = param.params.param(0);
  // Ignore tests with null input.
  if (arg.is_null()) return;
  if (function == "to_base64") {
    TestStringFunction<std::string>(&ToBase64, param.params, arg.bytes_value());
  } else if (function == "from_base64") {
    TestBytesFunction<std::string>(&FromBase64, param.params,
                                   arg.string_value());
  }
}

INSTANTIATE_TEST_SUITE_P(String, Base64StringTemplateTest,
                         testing::ValuesIn(GetFunctionTestsBase64()));

typedef testing::TestWithParam<FunctionTestCall> HexStringTemplateTest;
TEST_P(HexStringTemplateTest, Testlib) {
  const FunctionTestCall& param = GetParam();
  const std::string& function = param.function_name;
  const Value arg = param.params.param(0);
  // Ignore tests with null input.
  if (arg.is_null()) return;
  if (function == "to_hex") {
    TestStringFunction<std::string>(&ToHex, param.params, arg.bytes_value());
  } else if (function == "from_hex") {
    TestBytesFunction<std::string>(&FromHex, param.params, arg.string_value());
  }
}

INSTANTIATE_TEST_SUITE_P(String, HexStringTemplateTest,
                         testing::ValuesIn(GetFunctionTestsHex()));

typedef testing::TestWithParam<FunctionTestCall> PaddingTest;
TEST_P(PaddingTest, Testlib) {
  const FunctionTestCall& param = GetParam();
  const std::string& function = param.function_name;
  ASSERT_TRUE(function == "lpad_bytes" || function == "lpad_string" ||
              function == "rpad_bytes" || function == "rpad_string");
  ASSERT_LE(param.params.num_params(), 3);

  if (function == "lpad_bytes") {
    if (param.params.num_params() == 3) {
      TestStringFunction<std::string>(&LeftPadBytes, param.params,
                                      param.params.param(0).bytes_value(),
                                      param.params.param(1).int64_value(),
                                      param.params.param(2).bytes_value());

    } else {
      TestStringFunction<std::string>(&LeftPadBytesDefault, param.params,
                                      param.params.param(0).bytes_value(),
                                      param.params.param(1).int64_value());
    }
  } else if (function == "lpad_string") {
    if (param.params.num_params() == 3) {
      TestStringFunction<std::string>(&LeftPadUtf8, param.params,
                                      param.params.param(0).string_value(),
                                      param.params.param(1).int64_value(),
                                      param.params.param(2).string_value());
    } else {
      TestStringFunction<std::string>(&LeftPadUtf8Default, param.params,
                                      param.params.param(0).string_value(),
                                      param.params.param(1).int64_value());
    }
  } else if (function == "rpad_bytes") {
    if (param.params.num_params() == 3) {
      TestStringFunction<std::string>(&RightPadBytes, param.params,
                                      param.params.param(0).bytes_value(),
                                      param.params.param(1).int64_value(),
                                      param.params.param(2).bytes_value());
    } else {
      TestStringFunction<std::string>(&RightPadBytesDefault, param.params,
                                      param.params.param(0).bytes_value(),
                                      param.params.param(1).int64_value());
    }
  } else if (function == "rpad_string") {
    if (param.params.num_params() == 3) {
      TestStringFunction<std::string>(&RightPadUtf8, param.params,
                                      param.params.param(0).string_value(),
                                      param.params.param(1).int64_value(),
                                      param.params.param(2).string_value());

    } else {
      TestStringFunction<std::string>(&RightPadUtf8Default, param.params,
                                      param.params.param(0).string_value(),
                                      param.params.param(1).int64_value());
    }
  }
}
INSTANTIATE_TEST_SUITE_P(String, PaddingTest,
                         testing::ValuesIn(GetFunctionTestsPadding()));

typedef testing::TestWithParam<FunctionTestCall> RepeatTest;
TEST_P(RepeatTest, Testlib) {
  const FunctionTestCall& param = GetParam();
  const std::string& function = param.function_name;
  ASSERT_EQ(function, "repeat");

  TestStringFunction<std::string>(&Repeat, param.params,
                                  param.params.param(0).type()->IsBytes()
                                      ? param.params.param(0).bytes_value()
                                      : param.params.param(0).string_value(),
                                  param.params.param(1).int64_value());
}
INSTANTIATE_TEST_SUITE_P(String, RepeatTest,
                         testing::ValuesIn(GetFunctionTestsRepeat()));

typedef testing::TestWithParam<FunctionTestCall> ReverseTest;
TEST_P(ReverseTest, Testlib) {
  const FunctionTestCall& param = GetParam();
  const std::string& function = param.function_name;
  ASSERT_EQ(function, "reverse");

  const Value& value = param.params.param(0);
  if (value.is_null()) {
    return;
  }
  const Type* input_type = value.type();
  if (input_type->IsBytes()) {
    TestBytesFunction<std::string>(&ReverseBytes, param.params,
                                   value.bytes_value());
  } else if (input_type->IsString()) {
    TestStringFunction<std::string>(&ReverseUtf8, param.params,
                                    value.string_value());
  }  // else it's an array.
}
INSTANTIATE_TEST_SUITE_P(String, ReverseTest,
                         testing::ValuesIn(GetFunctionTestsReverse()));

TEST(RepeatTest, OutputSizeTest) {
  absl::Status status;
  std::string output;
  absl::string_view simulate_large_text("a",
                                        std::numeric_limits<int64_t>::max());

  // large text and small rep_size
  EXPECT_FALSE(Repeat(simulate_large_text, 1, &output, &status));
  EXPECT_THAT(
      status,
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr(
                   "Output of REPEAT exceeds max allowed output size of 1MB")));
  status = absl::OkStatus();

  // small text and large rep_size
  EXPECT_FALSE(
      Repeat("a", std::numeric_limits<int64_t>::max(), &output, &status));
  EXPECT_THAT(
      status,
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr(
                   "Output of REPEAT exceeds max allowed output size of 1MB")));
  status = absl::OkStatus();

  // large text and large rep_size
  EXPECT_FALSE(Repeat(simulate_large_text, std::numeric_limits<int64_t>::max(),
                      &output, &status));
  EXPECT_THAT(
      status,
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr(
                   "Output of REPEAT exceeds max allowed output size of 1MB")));

  status = absl::OkStatus();

  // small text small rep_size but still exceeds output.
  int64_t rep_size = 1 << 18;
  EXPECT_FALSE(Repeat("¼¼¼a", rep_size, &output, &status));
  EXPECT_THAT(
      status,
      StatusIs(absl::StatusCode::kOutOfRange,
               HasSubstr(
                   "Output of REPEAT exceeds max allowed output size of 1MB")));
}

TEST(PaddingTest, OutputSizeTest) {
  absl::Status status;
  std::string output;
  int64_t large_pad_size = std::numeric_limits<int64_t>::max();

  // LeftPadBytes: large pads size.
  EXPECT_FALSE(LeftPadBytes("a", large_pad_size, "b", &output, &status));
  EXPECT_THAT(
      status,
      StatusIs(
          absl::StatusCode::kOutOfRange,
          HasSubstr(
              "Output of LPAD/RPAD exceeds max allowed output size of 1MB")));
  status = absl::OkStatus();

  // RightPadBytes: large pads size.
  EXPECT_FALSE(RightPadBytes("a", large_pad_size, "b", &output, &status));
  EXPECT_THAT(
      status,
      StatusIs(
          absl::StatusCode::kOutOfRange,
          HasSubstr(
              "Output of LPAD/RPAD exceeds max allowed output size of 1MB")));
  status = absl::OkStatus();

  // LeftPadUtf8:

  // large pad size and small text and small pattern
  EXPECT_FALSE(LeftPadUtf8("a", large_pad_size, "b", &output, &status));
  EXPECT_THAT(
      status,
      StatusIs(
          absl::StatusCode::kOutOfRange,
          HasSubstr(
              "Output of LPAD/RPAD exceeds max allowed output size of 1MB")));
  status = absl::OkStatus();

  int64_t pad_size = (1 << 18) + 3;

  // small text and small pad_size but still output_size exceeds limit.
  EXPECT_FALSE(LeftPadUtf8("a", pad_size, "𠈓𠈓𠈓𠈓", &output, &status));
  EXPECT_THAT(
      status,
      StatusIs(
          absl::StatusCode::kOutOfRange,
          HasSubstr(
              "Output of LPAD/RPAD exceeds max allowed output size of 1MB")));
  status = absl::OkStatus();

  // RightPadUtf8:

  EXPECT_FALSE(RightPadUtf8("a", large_pad_size, "b", &output, &status));
  EXPECT_THAT(
      status,
      StatusIs(
          absl::StatusCode::kOutOfRange,
          HasSubstr(
              "Output of LPAD/RPAD exceeds max allowed output size of 1MB")));
  status = absl::OkStatus();

  // small text and small pad_size but still output_size exceeds limit.
  EXPECT_FALSE(RightPadUtf8("a", pad_size, "𠈓𠈓𠈓𠈓", &output, &status));
  EXPECT_THAT(
      status,
      StatusIs(
          absl::StatusCode::kOutOfRange,
          HasSubstr(
              "Output of LPAD/RPAD exceeds max allowed output size of 1MB")));
}

typedef testing::TestWithParam<FunctionTestCall> CodePointsTemplateTest;
TEST_P(CodePointsTemplateTest, Testlib) {
  const FunctionTestCall& param = GetParam();
  const std::string& function = param.function_name;
  const Value arg = param.params.param(0);
  // Ignore tests with null input.
  if (arg.is_null()) return;
  if (function == "to_code_points") {
    if (arg.type()->IsBytes()) {
      TestArrayFunction<int64_t>(&BytesToCodePoints, param.params,
                                 arg.bytes_value());
    } else if (arg.type()->IsString()) {
      TestArrayFunction<int64_t>(&StringToCodePoints, param.params,
                                 arg.string_value());
    }
  } else {
    std::vector<int64_t> codepoints;
    for (const auto& element : arg.elements()) {
      codepoints.push_back(element.int64_value());
    }
    if (function == "code_points_to_bytes") {
      TestBytesFunction<std::string>(&CodePointsToBytes, param.params,
                                     codepoints);
    } else if (function == "code_points_to_string") {
      TestStringFunction<std::string>(&CodePointsToString, param.params,
                                      codepoints);
    }
  }
}

INSTANTIATE_TEST_SUITE_P(String, CodePointsTemplateTest,
                         testing::ValuesIn(GetFunctionTestsCodePoints()));

TEST(LikeRewriteTest, GetRewriteForLikePattern) {
  struct TestCase {
    static TestCase NoRewriteBytes(absl::string_view pattern) {
      return TestCase{false, std::string(pattern), "",
                      LikeRewriteType::kNoRewrite};
    }
    static TestCase NoRewriteString(absl::string_view pattern) {
      return TestCase{true, std::string(pattern), "",
                      LikeRewriteType::kNoRewrite};
    }

    static TestCase StartsWithBytes(absl::string_view pattern,
                                    absl::string_view substring) {
      return TestCase{false, std::string(pattern), std::string(substring),
                      LikeRewriteType::kStartsWith};
    }
    static TestCase StartsWithString(absl::string_view pattern,
                                     absl::string_view substring) {
      return TestCase{true, std::string(pattern), std::string(substring),
                      LikeRewriteType::kStartsWith};
    }

    static TestCase EndsWithBytes(absl::string_view pattern,
                                  absl::string_view substring) {
      return TestCase{false, std::string(pattern), std::string(substring),
                      LikeRewriteType::kEndsWith};
    }
    static TestCase EndsWithString(absl::string_view pattern,
                                   absl::string_view substring) {
      return TestCase{true, std::string(pattern), std::string(substring),
                      LikeRewriteType::kEndsWith};
    }

    static TestCase ContainsBytes(absl::string_view pattern,
                                  absl::string_view substring) {
      return TestCase{false, std::string(pattern), std::string(substring),
                      LikeRewriteType::kContains};
    }
    static TestCase ContainsString(absl::string_view pattern,
                                   absl::string_view substring) {
      return TestCase{true, std::string(pattern), std::string(substring),
                      LikeRewriteType::kContains};
    }

    static TestCase EqualsBytes(absl::string_view pattern) {
      return TestCase{false, std::string(pattern), std::string(pattern),
                      LikeRewriteType::kEquals};
    }
    static TestCase EqualsString(absl::string_view pattern) {
      return TestCase{true, std::string(pattern), std::string(pattern),
                      LikeRewriteType::kEquals};
    }

    static TestCase NotNullBytes(absl::string_view pattern) {
      return TestCase{false, std::string(pattern), "",
                      LikeRewriteType::kNotNull};
    }
    static TestCase NotNullString(absl::string_view pattern) {
      return TestCase{true, std::string(pattern), "",
                      LikeRewriteType::kNotNull};
    }

    bool is_string;
    std::string pattern;
    std::string substring;
    LikeRewriteType expected_rewrite_type;
  };

  std::string all_bytes_but_special_characters;
  for (char c = std::numeric_limits<char>::min();
       c < std::numeric_limits<char>::max(); ++c) {
    switch (c) {
      case '_':
      case '\\':
      case '%':
        continue;
      default:
        break;
    }
    all_bytes_but_special_characters.push_back(c);
  }
  all_bytes_but_special_characters.push_back(std::numeric_limits<char>::max());

  const std::vector<TestCase> test_cases = {
      TestCase::NoRewriteBytes("f%oo"),
      TestCase::NoRewriteBytes("%f%oo"),
      TestCase::NoRewriteBytes("%fo%o%"),
      TestCase::NoRewriteBytes("f_oo"),
      TestCase::NoRewriteBytes("foo\\%"),
      TestCase::NoRewriteBytes("%fo%o%"),
      TestCase::NoRewriteString("f%oo"),
      TestCase::NoRewriteString("%f%oo"),
      TestCase::NoRewriteString("%fo%o%"),
      TestCase::NoRewriteString("f_oo"),
      TestCase::NoRewriteString("foo\\%"),
      TestCase::NoRewriteString("%fo%o%"),
      TestCase::NoRewriteString("foo_"),
      TestCase::NoRewriteString("_foo%"),
      TestCase::NoRewriteBytes("foo_"),
      TestCase::NoRewriteBytes("_foo%"),
      TestCase::NoRewriteString("foo_bar%"),
      TestCase::NoRewriteBytes("foo_bar%"),

      TestCase::EqualsBytes(""),
      TestCase::EqualsBytes("foo"),
      TestCase::EqualsString(""),

      TestCase::NotNullBytes("%"),
      TestCase::NotNullString("%"),
      TestCase::NotNullBytes("%%"),
      TestCase::NotNullString("%%"),
      TestCase::NotNullBytes("%%%%%"),
      TestCase::NotNullString("%%%%%"),

      TestCase::StartsWithString("foo%", "foo"),
      TestCase::StartsWithString("foo%%", "foo"),
      TestCase::EndsWithString("%foo", "foo"),
      TestCase::EndsWithString("%%foo", "foo"),
      TestCase::ContainsString("%foo%", "foo"),
      TestCase::ContainsString("%%%%foo%%", "foo"),
      TestCase::ContainsString("%foo%%%", "foo"),
      TestCase::StartsWithBytes("foo%", "foo"),
      TestCase::StartsWithBytes("foo%%", "foo"),
      TestCase::EndsWithBytes("%foo", "foo"),
      TestCase::EndsWithBytes("%%foo", "foo"),
      TestCase::ContainsBytes("%foo%", "foo"),
      TestCase::ContainsBytes("%%%%foo%%", "foo"),
      TestCase::ContainsBytes("%foo%%%", "foo"),

      // Multi-byte code points.
      TestCase::EqualsString("abcABCжщфЖЩФ"),
      TestCase::StartsWithString("abcABCжщфЖЩФ%%%", "abcABCжщфЖЩФ"),
      TestCase::EndsWithString("%%abcABCжщфЖЩФ", "abcABCжщфЖЩФ"),
      TestCase::ContainsString("%abcABCжщфЖЩФ%", "abcABCжщфЖЩФ"),
      // _ wildcard.
      TestCase::NoRewriteString("abcABC_жщфЖЩФ%"),
      TestCase::NoRewriteString("%abcABCжщф_ЖЩФ"),
      TestCase::NoRewriteString("%ab_cABCжщфЖЩФ%"),
      // % near the middle of the string.
      TestCase::NoRewriteString("abcABC%жщфЖЩФ%"),
      TestCase::NoRewriteString("%abcABCжщф%ЖЩФ"),
      TestCase::NoRewriteString("%ab%cABCжщфЖЩФ%"),

      // % with all possible bytes except \, _, and %.
      TestCase::ContainsBytes(
          absl::StrCat("%", all_bytes_but_special_characters, "%"),
          all_bytes_but_special_characters),
      TestCase::EndsWithBytes(
          absl::StrCat("%", all_bytes_but_special_characters),
          all_bytes_but_special_characters),
      TestCase::StartsWithBytes(
          absl::StrCat(all_bytes_but_special_characters, "%"),
          all_bytes_but_special_characters),
      TestCase::EqualsBytes(all_bytes_but_special_characters),
  };

  for (const auto& test_case : test_cases) {
    SCOPED_TRACE(absl::Substitute("is_string: $0, pattern: $1",
                                  test_case.is_string, test_case.pattern));
    absl::string_view substring;
    EXPECT_EQ(test_case.expected_rewrite_type,
              GetRewriteForLikePattern(test_case.is_string, test_case.pattern,
                                       &substring));
    EXPECT_EQ(test_case.substring, substring);
  }

  absl::string_view substring;
  EXPECT_EQ(LikeRewriteType::kEquals,
            GetRewriteForLikePattern(true /* is_string */, absl::string_view(),
                                     &substring));
  EXPECT_EQ(LikeRewriteType::kEquals,
            GetRewriteForLikePattern(false /* is_string */, absl::string_view(),
                                     &substring));
}

bool BytesToStringWrapper(absl::string_view str, absl::string_view format,
                          std::string* out, absl::Status* error) {
  *error = BytesToString(str, format, out);
  return error->ok();
}

bool StringToBytesWrapper(absl::string_view str, absl::string_view format,
                          std::string* out, absl::Status* error) {
  *error = StringToBytes(str, format, out);
  return error->ok();
}

typedef testing::TestWithParam<FunctionTestCall>
    BytesStringConversionTemplateTest;
TEST_P(BytesStringConversionTemplateTest, Testlib) {
  const FunctionTestCall& param = GetParam();
  const std::string& function = param.function_name;
  if (param.params.param(0).is_null() || param.params.param(1).is_null()) {
    return;
  }
  if (function == "bytes_to_string") {
    TestStringFunction<std::string>(&BytesToStringWrapper, param.params,
                                    param.params.param(0).bytes_value(),
                                    param.params.param(1).string_value());
  } else if (function == "string_to_bytes") {
    TestBytesFunction<std::string>(&StringToBytesWrapper, param.params,
                                   param.params.param(0).string_value(),
                                   param.params.param(1).string_value());
  }
}

INSTANTIATE_TEST_SUITE_P(
    String, BytesStringConversionTemplateTest,
    testing::ValuesIn(GetFunctionTestsBytesStringConversion()));

typedef testing::TestWithParam<FunctionTestCall> SplitSubstrTemplateTest;
TEST_P(SplitSubstrTemplateTest, Testlib) {
  const FunctionTestCall& param = GetParam();
  int64_t num_params = param.params.num_params();

  for (int i = 0; i < num_params; ++i) {
    if (param.params.param(i).is_null()) {
      return;
    }
  }

  if (num_params == 3) {
    TestStringFunction<std::string>(&SplitSubstr, param.params,
                                    param.params.param(0).string_value(),
                                    param.params.param(1).string_value(),
                                    param.params.param(2).int64_value());
  } else {
    TestStringFunction<std::string>(&SplitSubstrWithCount, param.params,
                                    param.params.param(0).string_value(),
                                    param.params.param(1).string_value(),
                                    param.params.param(2).int64_value(),
                                    param.params.param(3).int64_value());
  }
}

INSTANTIATE_TEST_SUITE_P(
    String, SplitSubstrTemplateTest,
    testing::ValuesIn(GetFunctionTestsSplitSubstr(/*skip_collation=*/true)));

}  // anonymous namespace
}  // namespace functions
}  // namespace googlesql
