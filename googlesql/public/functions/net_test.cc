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

#include "googlesql/public/functions/net.h"

#include <cstdint>
#include <string>
#include <vector>

#include "googlesql/base/testing/status_matchers.h"
#include "googlesql/compliance/functions_testlib.h"
#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "re2/re2.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace functions {
namespace net {
namespace {

template <typename T>
struct BaseType {
  typedef T Primitive;
  static Value MakeValue(const Primitive& primitive) {
    return Value::Make<T>(primitive);
  }
};

struct StringType {
  typedef std::string Primitive;
  static Value MakeValue(const Primitive& primitive) {
    return Value::String(primitive);
  }
};

struct BytesType {
  typedef std::string Primitive;
  static Value MakeValue(const Primitive& primitive) {
    return Value::Bytes(primitive);
  }
};

template <typename OutType, typename FunctionType, class... Args>
void TestFunction(FunctionType function, const QueryParamsWithResult& param,
                  Args... args) {
  typename OutType::Primitive out;
  absl::Status status;
  EXPECT_EQ(param.status().ok(), function(args..., &out, &status));
  if (param.status().ok()) {
    GOOGLESQL_EXPECT_OK(status);
    EXPECT_TRUE(param.result().Equals(OutType::MakeValue(out)))
      << "Expected: " << param.result() << "\n"
      << "Actual: " << out << "\n";
  } else {
    EXPECT_NE(absl::OkStatus(), status) << "Unexpected value: " << out;
  }
}

template <typename OutType>
void TestNullableFunction(absl::Status (*function)(absl::string_view, OutType*,
                                                   bool*),
                          const QueryParamsWithResult& param,
                          absl::string_view url, OutType* out) {
  bool is_null = true;
  GOOGLESQL_EXPECT_OK(function(url, out, &is_null));
  const Value out_value = is_null ? Value::Null(param.result().type()) :
      param.result().type()->IsBytes() ? Value::Bytes(*out) :
          Value::String(*out);
  EXPECT_TRUE(param.result().Equals(out_value))
    << "Expected: " << param.result() << "\n"
    << "Actual: " << out_value << "\n";
}

void TestUrlFunction(absl::Status (*function)(absl::string_view,
                                              absl::string_view*, bool*),
                     const QueryParamsWithResult& param,
                     absl::string_view url) {
  absl::string_view out;
  TestNullableFunction(function, param, url, &out);
  // Verify that function(function(url)) == function(url).
  TestNullableFunction(function, param, out, &out);
}

void TestHost(const QueryParamsWithResult& param, absl::string_view url) {
  absl::string_view host;
  TestNullableFunction(&Host, param, url, &host);

  // This is the regular expression given at
  // https://tools.ietf.org/html/rfc3986#appendix-A.
  static RE2* re = new RE2(
      "^(([^:/?#]+):)?(//([^/?#]*))?([^?#]*)(\\?([^#]*))?(#(.*))?");

  absl::string_view parts[9];
  ASSERT_TRUE(
      RE2::PartialMatch(url, *re, &parts[0], &parts[1], &parts[2], &parts[3],
                        &parts[4], &parts[5], &parts[6], &parts[7], &parts[8]));
  if (!parts[3].empty()) {
    // The host should be a substring of the authority component, if not empty.
    EXPECT_THAT(parts[3], testing::HasSubstr(std::string(host)));
  }
  // The regular expression yields empty authority for some malformed urls
  // (e.g., those with backslashes), but Host might return a non-empty string.

  // Verify that HOST(HOST(url)) == HOST(url).
  TestNullableFunction(&Host, param, host, &host);
}

typedef testing::TestWithParam<FunctionTestCall> NetTest;
TEST_P(NetTest, Testlib) {
  const FunctionTestCall& param = GetParam();
  const std::string& function = param.function_name;
  const std::vector<Value>& args = param.params.params();
  for (const Value& arg : args) {
    // Ignore tests with null arguments.
    if (arg.is_null()) return;
  }
  std::string out;
  if (function == "net.host") {
    TestHost(param.params, args[0].string_value());
  } else if (function == "net.reg_domain") {
    TestUrlFunction(&RegDomain, param.params, args[0].string_value());
  } else if (function == "net.public_suffix") {
    TestUrlFunction(&PublicSuffix, param.params, args[0].string_value());
  } else if (function == "net.format_ip") {
    TestFunction<StringType>(&FormatIP, param.params, args[0].int64_value());
  } else if (function == "net.ipv4_from_int64") {
    TestFunction<BytesType>(&IPv4FromInt64, param.params,
                            args[0].int64_value());
  } else if (function == "net.parse_ip") {
    TestFunction<BaseType<int64_t>>(&ParseIP, param.params,
                                    args[0].string_value());
  } else if (function == "net.ipv4_to_int64") {
    TestFunction<BaseType<int64_t>>(&IPv4ToInt64, param.params,
                                    args[0].bytes_value());
  } else if (function == "net.format_packed_ip") {
    TestFunction<StringType>(&FormatPackedIP, param.params,
                             args[0].bytes_value());
  } else if (function == "net.ip_to_string") {
    TestFunction<StringType>(&IPToString, param.params, args[0].bytes_value());
  } else if (function == "net.parse_packed_ip") {
    TestFunction<BytesType>(&ParsePackedIP, param.params,
                            args[0].string_value());
  } else if (function == "net.ip_from_string") {
    TestFunction<BytesType>(&IPFromString, param.params,
                            args[0].string_value());
  } else if (function == "net.safe_ip_from_string") {
    TestNullableFunction(&SafeIPFromString, param.params,
                         args[0].string_value(), &out);
  } else if (function == "net.ip_net_mask") {
    TestFunction<BytesType>(&IPNetMask, param.params,
                            args[0].int64_value(), args[1].int64_value());
  } else if (function == "net.ip_trunc") {
    TestFunction<BytesType>(&IPTrunc, param.params,
                            args[0].bytes_value(), args[1].int64_value());
  } else if (function == "net.ip_in_net") {
    TestFunction<BaseType<bool>>(&IPInNet, param.params,
                                 args[0].string_value(),
                                 args[1].string_value());
  } else if (function == "net.make_net") {
    TestFunction<StringType>(&MakeNet, param.params,
                             args[0].string_value(),
                             args[1].int32_value());
  } else {
    FAIL() << "The function " << function << " is not tested here.";
  }
}

INSTANTIATE_TEST_SUITE_P(String, NetTest,
                         testing::ValuesIn(GetFunctionTestsNet()));

}  // anonymous namespace
}  // namespace net
}  // namespace functions
}  // namespace googlesql
