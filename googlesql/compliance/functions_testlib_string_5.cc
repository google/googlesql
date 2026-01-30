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

#include <string>
#include <utility>
#include <vector>

#include "googlesql/compliance/functions_testlib_common.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "googlesql/testing/test_value.h"
#include "googlesql/testing/using_test_value.cc"  // NOLINT
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace googlesql {

static constexpr absl::StatusCode OUT_OF_RANGE = absl::StatusCode::kOutOfRange;

std::vector<FunctionTestCall> GetFunctionTestsInstr3() {
  return {
      // instr(bytes, bytes, int64, int64) -> int64
      {"instr", {NullBytes(), Bytes(""), 1ll, 1ll}, NullInt64()},
      {"instr", {NullBytes(), Bytes("x"), 1ll, 1ll}, NullInt64()},
      {"instr", {Bytes(""), NullBytes(), 1ll, 1ll}, NullInt64()},
      {"instr", {NullBytes(), NullBytes(), 1ll, 1ll}, NullInt64()},
      {"instr", {Bytes(""), Bytes("x"), NullInt64(), 1ll}, NullInt64()},
      {"instr", {Bytes(""), Bytes(""), NullInt64(), 1ll}, NullInt64()},
      {"instr", {NullBytes(), Bytes("x"), NullInt64(), 1ll}, NullInt64()},
      {"instr", {NullBytes(), Bytes(""), NullInt64(), 1ll}, NullInt64()},
      {"instr", {Bytes(""), NullBytes(), NullInt64(), 1ll}, NullInt64()},
      {"instr", {Bytes("x"), NullBytes(), NullInt64(), 1ll}, NullInt64()},
      {"instr", {NullBytes(), NullBytes(), NullInt64(), 1ll}, NullInt64()},
      {"instr", {Bytes(""), Bytes("x"), 0ll, 1ll}, NullInt64(), OUT_OF_RANGE},
      {"instr", {NullBytes(), Bytes(""), 1ll, NullInt64()}, NullInt64()},
      {"instr", {NullBytes(), Bytes("x"), 1ll, NullInt64()}, NullInt64()},
      {"instr", {Bytes(""), NullBytes(), 1ll, NullInt64()}, NullInt64()},
      {"instr", {NullBytes(), NullBytes(), 1ll, NullInt64()}, NullInt64()},
      {"instr", {Bytes(""), Bytes("x"), NullInt64(), NullInt64()}, NullInt64()},
      {"instr", {Bytes(""), Bytes(""), NullInt64(), NullInt64()}, NullInt64()},
      {"instr",
       {NullBytes(), Bytes("x"), NullInt64(), NullInt64()},
       NullInt64()},
      {"instr",
       {NullBytes(), Bytes(""), NullInt64(), NullInt64()},
       NullInt64()},
      {"instr",
       {Bytes(""), NullBytes(), NullInt64(), NullInt64()},
       NullInt64()},
      {"instr",
       {Bytes("x"), NullBytes(), NullInt64(), NullInt64()},
       NullInt64()},
      {"instr",
       {NullBytes(), NullBytes(), NullInt64(), NullInt64()},
       NullInt64()},
      {"instr", {Bytes("x"), Bytes("x"), 1ll, NullInt64()}, NullInt64()},
      {"instr", {Bytes(""), Bytes("x"), 1ll, 0ll}, NullInt64(), OUT_OF_RANGE},
      {"instr", {Bytes(""), Bytes("x"), 1ll, 1ll}, 0ll},
      {"instr", {Bytes(""), Bytes("x"), 1ll, 2ll}, 0ll},
      {"instr", {Bytes("x"), Bytes(""), 1ll, 1ll}, 1ll},
      {"instr", {Bytes("x"), Bytes(""), 1ll, 2ll}, 2ll},
      {"instr", {Bytes("x"), Bytes(""), 1ll, 3ll}, 0ll},
      {"instr", {Bytes("x"), Bytes(""), 2ll, 1ll}, 2ll},
      {"instr", {Bytes("x"), Bytes(""), 2ll, 2ll}, 0ll},
      {"instr", {Bytes("xx"), Bytes(""), 1ll, 1ll}, 1ll},
      {"instr", {Bytes("xx"), Bytes(""), 1ll, 2ll}, 2ll},
      {"instr", {Bytes("xx"), Bytes(""), 1ll, 3ll}, 3ll},
      {"instr", {Bytes("xx"), Bytes(""), 1ll, 4ll}, 0ll},
      {"instr", {Bytes("xx"), Bytes(""), 2ll, 1ll}, 2ll},
      {"instr", {Bytes("xx"), Bytes(""), 2ll, 2ll}, 3ll},
      {"instr", {Bytes("xx"), Bytes(""), 2ll, 3ll}, 0ll},
      {"instr", {Bytes("xx"), Bytes(""), 3ll, 1ll}, 3ll},
      {"instr", {Bytes("xx"), Bytes(""), 3ll, 2ll}, 0ll},
      {"instr", {Bytes("zгl"), Bytes(""), 1ll, 1ll}, 1ll},
      {"instr", {Bytes("zгl"), Bytes(""), 1ll, 2ll}, 2ll},
      {"instr", {Bytes("zгl"), Bytes(""), 1ll, 3ll}, 3ll},
      {"instr", {Bytes("zгl"), Bytes(""), 1ll, 4ll}, 4ll},
      {"instr", {Bytes("zгl"), Bytes(""), 1ll, 5ll}, 5ll},
      {"instr", {Bytes("zгl"), Bytes(""), 1ll, 6ll}, 0ll},
      {"instr", {Bytes(""), Bytes(""), 1ll, 1ll}, 1ll},
      {"instr", {Bytes(""), Bytes(""), 1ll, 2ll}, 0ll},
      {"instr", {Bytes("xxx"), Bytes("x"), 1ll, 1ll}, 1ll},
      {"instr", {Bytes("xxx"), Bytes("x"), 1ll, 2ll}, 2ll},
      {"instr", {Bytes("xxx"), Bytes("x"), 1ll, 3ll}, 3ll},
      {"instr", {Bytes("xxx"), Bytes("x"), 1ll, 4ll}, 0ll},
      {"instr", {Bytes("xxx"), Bytes("x"), 2ll, 1ll}, 2ll},
      {"instr", {Bytes("xxx"), Bytes("x"), 2ll, 2ll}, 3ll},
      {"instr", {Bytes("xxx"), Bytes("x"), 2ll, 3ll}, 0ll},
      {"instr", {Bytes("xxx"), Bytes("x"), 3ll, 1ll}, 3ll},
      {"instr", {Bytes("xxx"), Bytes("x"), 3ll, 2ll}, 0ll},
      {"instr", {Bytes("xxx"), Bytes("x"), 1ll, int64max}, 0ll},
      {"instr", {Bytes("xxx"), Bytes("x"), 2ll, int64max}, 0ll},
      {"instr", {Bytes("abcdef"), Bytes("cd"), 1ll, 1ll}, 3ll},
      {"instr", {Bytes("abcdef"), Bytes("cd"), 1ll, 2ll}, 0ll},
      {"instr", {Bytes("abcdef"), Bytes("cd"), 3ll, 1ll}, 3ll},
      {"instr", {Bytes("abcdef"), Bytes("cd"), 3ll, 2ll}, 0ll},
      {"instr", {Bytes("abcdef"), Bytes("cd"), 3ll, 5ll}, 0ll},
      {"instr", {Bytes("abcdef"), Bytes("cd"), int64max, int64max}, 0ll},
      {"instr", {Bytes("abcdefabcdef"), Bytes("xz"), 1ll, 1ll}, 0ll},
      {"instr", {Bytes("abcdefabcdef"), Bytes("xz"), 1ll, 5ll}, 0ll},
      {"instr", {Bytes("abcdefabcdef"), Bytes("cd"), 1ll, 1ll}, 3ll},
      {"instr", {Bytes("abcdefabcdef"), Bytes("cd"), 1ll, 2ll}, 9ll},
      {"instr", {Bytes("abcdefabcdef"), Bytes("cd"), 4ll, 1ll}, 9ll},
      {"instr", {Bytes("abcdefabcdef"), Bytes("cd"), 4ll, 2ll}, 0ll},
      {"instr", {Bytes("aaabbaaa"), Bytes("aa"), 1ll, 1ll}, 1ll},
      {"instr", {Bytes("aaabbaaa"), Bytes("aa"), 1ll, 2ll}, 2ll},
      {"instr", {Bytes("aaabbaaa"), Bytes("aa"), 1ll, 3ll}, 6ll},
      {"instr", {Bytes("aaabbaaa"), Bytes("aa"), 1ll, 4ll}, 7ll},
      {"instr", {Bytes("aaabbaaa"), Bytes("aa"), 1ll, 5ll}, 0ll},
      {"instr", {Bytes("aaabbaaa"), Bytes("aa"), 2ll, 1ll}, 2ll},
      {"instr", {Bytes("aaabbaaa"), Bytes("aa"), 2ll, 2ll}, 6ll},
      {"instr", {Bytes("aaabbaaa"), Bytes("aa"), 7ll, 1ll}, 7ll},
      {"instr", {Bytes("aaabbaaa"), Bytes("aa"), 7ll, 2ll}, 0ll},
      {"instr", {Bytes("\0abce\0dabcf"), Bytes("abc"), 1ll, 1ll}, 2ll},
      {"instr", {Bytes("\0abce\0dabcf"), Bytes("abc"), 1ll, 2ll}, 8ll},
      {"instr", {Bytes("\0abce\0dabcf"), Bytes("abc"), 2ll, 1ll}, 2ll},
      {"instr", {Bytes("\0abce\0dabcf"), Bytes("abc"), 2ll, 2ll}, 8ll},
      {"instr", {Bytes("\0abce\0dabcf"), Bytes("abc"), 5ll, 1ll}, 8ll},
      {"instr", {Bytes("\0abce\0dabcf"), Bytes("abc"), 5ll, 2ll}, 0ll},
      {"instr", {Bytes("abca\0b\0c\0"), Bytes("a\0b\0c"), 3ll, 1ll}, 4ll},
      {"instr", {Bytes("zгдlгдl"), Bytes("дl"), 1ll, 1ll}, 4ll},
      {"instr", {Bytes("zгдlгдl"), Bytes("дl"), 1ll, 2ll}, 9ll},
      {"instr", {Bytes("zгдlгдl"), Bytes("дl"), 1ll, 3ll}, 0ll},
      {"instr", {Bytes("zгдlгдl"), Bytes("дl"), 5ll, 1ll}, 9ll},
      {"instr", {Bytes("zгдlгдl"), Bytes("дl"), 5ll, 2ll}, 0ll},

      {"instr", {NullBytes(), Bytes(""), -1ll, 1ll}, NullInt64()},
      {"instr", {NullBytes(), Bytes("x"), -1ll, 1ll}, NullInt64()},
      {"instr", {Bytes(""), NullBytes(), -1ll, 1ll}, NullInt64()},
      {"instr", {NullBytes(), NullBytes(), -1ll, 1ll}, NullInt64()},
      {"instr", {Bytes(""), Bytes("x"), -1ll, 1ll}, 0ll},
      {"instr", {Bytes(""), Bytes("x"), -1ll, 2ll}, 0ll},
      {"instr", {Bytes(""), Bytes("x"), -2ll, 1ll}, 0ll},
      {"instr", {Bytes(""), Bytes("x"), -2ll, 2ll}, 0ll},
      {"instr", {Bytes("x"), Bytes(""), -1ll, 1ll}, 2ll},
      {"instr", {Bytes("x"), Bytes(""), -1ll, 2ll}, 1ll},
      {"instr", {Bytes("x"), Bytes(""), -2ll, 1ll}, 1ll},
      {"instr", {Bytes("x"), Bytes(""), -2ll, 2ll}, 0ll},
      {"instr", {Bytes("xx"), Bytes(""), -1ll, 1ll}, 3ll},
      {"instr", {Bytes("xx"), Bytes(""), -1ll, 2ll}, 2ll},
      {"instr", {Bytes("xx"), Bytes(""), -1ll, 3ll}, 1ll},
      {"instr", {Bytes("xx"), Bytes(""), -1ll, 4ll}, 0ll},
      {"instr", {Bytes("xx"), Bytes(""), -2ll, 1ll}, 2ll},
      {"instr", {Bytes("xx"), Bytes(""), -2ll, 2ll}, 1ll},
      {"instr", {Bytes("xx"), Bytes(""), -2ll, 3ll}, 0ll},
      {"instr", {Bytes("xx"), Bytes(""), -3ll, 1ll}, 1ll},
      {"instr", {Bytes("xx"), Bytes(""), -3ll, 2ll}, 0ll},
      {"instr", {Bytes(""), Bytes(""), -1ll, 1ll}, 1ll},
      {"instr", {Bytes(""), Bytes(""), -1ll, 2ll}, 0ll},
      {"instr", {Bytes("xxx"), Bytes("x"), -1ll, 1ll}, 3ll},
      {"instr", {Bytes("xxx"), Bytes("x"), -1ll, 2ll}, 2ll},
      {"instr", {Bytes("xxx"), Bytes("x"), -1ll, 3ll}, 1ll},
      {"instr", {Bytes("xxx"), Bytes("x"), -1ll, 4ll}, 0ll},
      {"instr", {Bytes("xxx"), Bytes("x"), -2ll, 1ll}, 2ll},
      {"instr", {Bytes("xxx"), Bytes("x"), -2ll, 2ll}, 1ll},
      {"instr", {Bytes("xxx"), Bytes("x"), -2ll, 3ll}, 0ll},
      {"instr", {Bytes("xxx"), Bytes("x"), -3ll, 1ll}, 1ll},
      {"instr", {Bytes("xxx"), Bytes("x"), -3ll, 2ll}, 0ll},
      {"instr", {Bytes("xxx"), Bytes("x"), -4ll, 1ll}, 0ll},
      {"instr", {Bytes("xxx"), Bytes("x"), int64min, 1ll}, 0ll},
      {"instr", {Bytes("abcdef"), Bytes("cd"), -1ll, 1ll}, 3ll},
      {"instr", {Bytes("abcdef"), Bytes("cd"), -1ll, 2ll}, 0ll},
      {"instr", {Bytes("abcdef"), Bytes("cd"), -3ll, 1ll}, 3ll},
      {"instr", {Bytes("abcdef"), Bytes("cd"), -3ll, 2ll}, 0ll},
      {"instr", {Bytes("abcdef"), Bytes("cd"), -4ll, 1ll}, 3ll},
      {"instr", {Bytes("abcdef"), Bytes("cd"), -4ll, 2ll}, 0ll},
      {"instr", {Bytes("abcdef"), Bytes("cd"), -5ll, 1ll}, 0ll},
      {"instr", {Bytes("abcdef"), Bytes("cd"), -5ll, 5ll}, 0ll},
      {"instr", {Bytes("abcdefabcdef"), Bytes("xz"), -1ll, 1ll}, 0ll},
      {"instr", {Bytes("abcdefabcdef"), Bytes("xz"), -1ll, 2ll}, 0ll},
      {"instr", {Bytes("abcdefabcdef"), Bytes("xz"), -30ll, 1ll}, 0ll},
      {"instr", {Bytes("abcdefabcdef"), Bytes("xz"), -30ll, 2ll}, 0ll},
      {"instr", {Bytes("abcdefabcdef"), Bytes("cd"), -1ll, 1ll}, 9ll},
      {"instr", {Bytes("abcdefabcdef"), Bytes("cd"), -1ll, 2ll}, 3ll},
      {"instr", {Bytes("abcdefabcdef"), Bytes("cd"), -4ll, 1ll}, 9ll},
      {"instr", {Bytes("abcdefabcdef"), Bytes("cd"), -4ll, 2ll}, 3ll},
      {"instr", {Bytes("abcdefabcdef"), Bytes("cd"), -5ll, 1ll}, 3ll},
      {"instr", {Bytes("abcdefabcdef"), Bytes("cd"), -5ll, 2ll}, 0ll},
      {"instr", {Bytes("aaabbaaa"), Bytes("aa"), -1ll, 1ll}, 7ll},
      {"instr", {Bytes("aaabbaaa"), Bytes("aa"), -1ll, 2ll}, 6ll},
      {"instr", {Bytes("aaabbaaa"), Bytes("aa"), -1ll, 3ll}, 2ll},
      {"instr", {Bytes("aaabbaaa"), Bytes("aa"), -1ll, 4ll}, 1ll},
      {"instr", {Bytes("aaabbaaa"), Bytes("aa"), -1ll, 5ll}, 0ll},
      {"instr", {Bytes("aaabbaaa"), Bytes("aa"), -2ll, 1ll}, 7ll},
      {"instr", {Bytes("aaabbaaa"), Bytes("aa"), -2ll, 2ll}, 6ll},
      {"instr", {Bytes("aaabbaaa"), Bytes("aa"), -7ll, 1ll}, 2ll},
      {"instr", {Bytes("aaabbaaa"), Bytes("aa"), -7ll, 2ll}, 1ll},
      {"instr", {Bytes("aaabbaaa"), Bytes("aa"), -8ll, 1ll}, 1ll},
      {"instr", {Bytes("aaabbaaa"), Bytes("aa"), -8ll, 2ll}, 0ll},
      {"instr", {Bytes("\0abce\0dabcf"), Bytes("abc"), -1ll, 1ll}, 8ll},
      {"instr", {Bytes("\0abce\0dabcf"), Bytes("abc"), -1ll, 2ll}, 2ll},
      {"instr", {Bytes("\0abce\0dabcf"), Bytes("abc"), -2ll, 1ll}, 8ll},
      {"instr", {Bytes("\0abce\0dabcf"), Bytes("abc"), -2ll, 2ll}, 2ll},
      {"instr", {Bytes("\0abce\0dabcf"), Bytes("abc"), -3ll, 1ll}, 8ll},
      {"instr", {Bytes("\0abce\0dabcf"), Bytes("abc"), -3ll, 2ll}, 2ll},
      {"instr", {Bytes("\0abce\0dabcf"), Bytes("abc"), -5ll, 1ll}, 2ll},
      {"instr", {Bytes("\0abce\0dabcf"), Bytes("abc"), -5ll, 2ll}, 0ll},
      {"instr", {Bytes("abca\0b\0c\0"), Bytes("a\0b\0c"), -1ll, 1ll}, 4ll},
      {"instr", {Bytes("abca\0b\0c\0"), Bytes("a\0b\0c"), -1ll, 2ll}, 0ll},
      {"instr", {Bytes("abca\0b\0c\0"), Bytes("a\0b\0c"), -2ll, 1ll}, 4ll},
      {"instr", {Bytes("abca\0b\0c\0"), Bytes("a\0b\0c"), -2ll, 2ll}, 0ll},
      {"instr", {Bytes("abca\0b\0c\0"), Bytes("a\0b\0c"), -4ll, 1ll}, 4ll},
      {"instr", {Bytes("abca\0b\0c\0"), Bytes("a\0b\0c"), -4ll, 2ll}, 0ll},
      {"instr", {Bytes("abca\0b\0c\0"), Bytes("a\0b\0c"), -7ll, 1ll}, 0ll},
      {"instr", {Bytes("zгдlгдl"), Bytes("дl"), -1ll, 1ll}, 9ll},
      {"instr", {Bytes("zгдlгдl"), Bytes("дl"), -1ll, 2ll}, 4ll},
      {"instr", {Bytes("zгдlгдl"), Bytes("дl"), -1ll, 3ll}, 0ll},
      {"instr", {Bytes("zгдlгдl"), Bytes("дl"), -2ll, 1ll}, 9ll},
      {"instr", {Bytes("zгдlгдl"), Bytes("дl"), -3ll, 1ll}, 9ll},
      {"instr", {Bytes("zгдlгдl"), Bytes("дl"), -4ll, 1ll}, 4ll},
      {"instr", {Bytes("zгдlгдl"), Bytes("дl"), -6ll, 1ll}, 4ll},
      {"instr", {Bytes("zгдlгдl"), Bytes("дl"), -6ll, 2ll}, 0ll}};
}

// Test cases where the behaviors of INSTR and INSTR with collation differ.
// Collation ignores certain code points (such as the null byte), while regular
// INSTR takes them into account. Also, INSTR with collation always returns
// 0 if the substring is empty or contains only ignorable collation elements.
// Do not add cases here unless necessary.
std::vector<FunctionTestCall> GetFunctionTestsInstrNoCollator() {
  return {
      // instr(bytes, bytes, int64, int64) -> int64
      {"instr", {"abca\0b\0c\0", "a\0b\0c"}, 4ll},
      {"instr", {"abca\0b\0c\0", "a\0b\0c", -7ll}, 0ll},
      {"instr", {"abca\0b\0c\0", "a\0b\0c", -1ll, 2ll}, 0ll},
      {"instr", {"abca\0b\0c\0", "a\0b\0c", -6ll, 2ll}, 0ll},
      {"instr", {"abca\0b\0c\0", "a\0b\0c", -7ll, 1ll}, 0ll},
      {"instr", {"", ""}, 1ll},
      {"instr", {"x", ""}, 1ll},
      {"instr", {"", "", 1ll}, 1ll},
      {"instr", {"x", "", 1ll}, 1ll},
      // Since INSTR("", "") = 1 (position after the string's end), then
      // INSTR("x", "", 2) = 2 is valid.
      {"instr", {"x", "", 2ll}, 2ll},
      {"instr", {"x", "", 3ll}, 0ll},
      {"instr", {"xx", "", 1ll}, 1ll},
      {"instr", {"xx", "", 2ll}, 2ll},
      // Similarly, INSTR("xx", "", 3) = 3.
      {"instr", {"xx", "", 3ll}, 3ll},
      {"instr", {"xx", "", 4ll}, 0ll},
      {"instr", {"zгl", "", 1ll}, 1ll},
      {"instr", {"zгl", "", 2ll}, 2ll},
      {"instr", {"zгl", "", 3ll}, 3ll},
      {"instr", {"zгl", "", 4ll}, 4ll},
      {"instr", {"zгl", "", 5ll}, 0ll},
      // When counting from the end, the first occurrence of "" is after the
      // string's end.
      {"instr", {"x", "", -1ll}, 2ll},
      {"instr", {"x", "", -2ll}, 1ll},
      {"instr", {"x", "", -3ll}, 0ll},
      {"instr", {"xx", "", -1ll}, 3ll},
      {"instr", {"xx", "", -2ll}, 2ll},
      {"instr", {"xx", "", -3ll}, 1ll},
      {"instr", {"xx", "", -4ll}, 0ll},
      {"instr", {"zгl", "", -1ll}, 4ll},
      {"instr", {"zгl", "", -2ll}, 3ll},
      {"instr", {"zгl", "", -3ll}, 2ll},
      {"instr", {"zгl", "", -4ll}, 1ll},
      {"instr", {"zгl", "", -5ll}, 0ll},
      {"instr", {"", "", -1ll}, 1ll},
      {"instr", {"", "", -2ll}, 0ll},
      {"instr", {"x", "", 1ll, 1ll}, 1ll},
      {"instr", {"x", "", 1ll, 2ll}, 2ll},
      {"instr", {"x", "", 1ll, 3ll}, 0ll},
      {"instr", {"x", "", 2ll, 1ll}, 2ll},
      {"instr", {"x", "", 2ll, 2ll}, 0ll},
      {"instr", {"xx", "", 1ll, 1ll}, 1ll},
      {"instr", {"xx", "", 1ll, 2ll}, 2ll},
      {"instr", {"xx", "", 1ll, 3ll}, 3ll},
      {"instr", {"xx", "", 1ll, 4ll}, 0ll},
      {"instr", {"xx", "", 2ll, 1ll}, 2ll},
      {"instr", {"xx", "", 2ll, 2ll}, 3ll},
      {"instr", {"xx", "", 2ll, 3ll}, 0ll},
      {"instr", {"xx", "", 3ll, 1ll}, 3ll},
      {"instr", {"xx", "", 3ll, 2ll}, 0ll},
      {"instr", {"zгl", "", 1ll, 1ll}, 1ll},
      {"instr", {"zгl", "", 1ll, 2ll}, 2ll},
      {"instr", {"zгl", "", 1ll, 3ll}, 3ll},
      {"instr", {"zгl", "", 1ll, 4ll}, 4ll},
      {"instr", {"zгl", "", 1ll, 5ll}, 0ll},
      {"instr", {"", "", 1ll, 1ll}, 1ll},
      {"instr", {"", "", 1ll, 2ll}, 0ll},
      {"instr", {"x", "", -1ll, 1ll}, 2ll},
      {"instr", {"x", "", -1ll, 2ll}, 1ll},
      {"instr", {"x", "", -2ll, 1ll}, 1ll},
      {"instr", {"x", "", -2ll, 2ll}, 0ll},
      {"instr", {"xx", "", -1ll, 1ll}, 3ll},
      {"instr", {"xx", "", -1ll, 2ll}, 2ll},
      {"instr", {"xx", "", -1ll, 3ll}, 1ll},
      {"instr", {"xx", "", -1ll, 4ll}, 0ll},
      {"instr", {"xx", "", -2ll, 1ll}, 2ll},
      {"instr", {"xx", "", -2ll, 2ll}, 1ll},
      {"instr", {"xx", "", -2ll, 3ll}, 0ll},
      {"instr", {"xx", "", -3ll, 1ll}, 1ll},
      {"instr", {"xx", "", -3ll, 2ll}, 0ll},
      {"instr", {"", "", -1ll, 1ll}, 1ll},
      {"instr", {"", "", -1ll, 2ll}, 0ll},
  };
}

static void CreateStringFunctionWithCollation(
    absl::string_view collation_function_name,
    std::vector<ValueConstructor> arguments, ValueConstructor binary_result,
    ValueConstructor und_result, ValueConstructor und_ci_result,
    std::vector<FunctionTestCall>& tests) {
  std::vector<ValueConstructor> collation_args;

  collation_args.push_back("binary");
  for (const ValueConstructor& arg : arguments) {
    collation_args.emplace_back(arg);
  }
  tests.emplace_back(collation_function_name, std::move(collation_args),
                     std::move(binary_result));

  collation_args.clear();
  collation_args.push_back("und");
  for (const ValueConstructor& arg : arguments) {
    collation_args.emplace_back(arg);
  }
  tests.emplace_back(collation_function_name, std::move(collation_args),
                     std::move(und_result));

  collation_args.clear();
  collation_args.push_back("und:ci");
  for (const ValueConstructor& arg : arguments) {
    collation_args.emplace_back(arg);
  }
  tests.emplace_back(collation_function_name, std::move(collation_args),
                     std::move(und_ci_result));
}

static void GetReplaceFunctions(std::vector<FunctionTestCall>& tests) {
  // replace_with_collator(collator, string, string, string) -> string
  // This function creates the equivalent of
  // {"replace_with_collator", {"binary", args...}, <binary_result>}
  // {"replace_with_collator", {"und", args...}, <und_result>}
  // {"replace_with_collator", {"und:ci", args...}, <und_ci_result>}
  auto AddTests = [&tests](const std::vector<ValueConstructor>& args,
                           ValueConstructor binary_result,
                           ValueConstructor und_result,
                           ValueConstructor und_ci_result) {
    CreateStringFunctionWithCollation(
        "replace_with_collator", args, std::move(binary_result),
        std::move(und_result), std::move(und_ci_result), tests);
  };
  // binary should have the same behavior as regular REPLACE.

  // Corner Cases: Empty
  AddTests({"", "", ""}, "", "", "");
  AddTests({"", "a", ""}, "", "", "");
  AddTests({"", "", "a"}, "", "", "");
  AddTests({"abc", "", "xyz"}, "abc", "abc", "abc");
  AddTests({"abc", "", ""}, "abc", "abc", "abc");

  // Basic functionality
  AddTests({"abc", "b", "xyz"}, "axyzc", "axyzc", "axyzc");
  AddTests({"abc", "b", ""}, "ac", "ac", "ac");
  AddTests({"abcabc", "bc", "xyz"}, "axyzaxyz", "axyzaxyz", "axyzaxyz");
  AddTests({"abc", "abc", "xyz"}, "xyz", "xyz", "xyz");
  AddTests({"abc", "abcde", "xyz"}, "abc", "abc", "abc");

  // First match replacement only when multiple overlapping matches
  AddTests({"banana", "ana", "xyz"}, "bxyzna", "bxyzna", "bxyzna");
  AddTests({"banana", "ana", ""}, "bna", "bna", "bna");
  AddTests({"banana", "a", "z"}, "bznznz", "bznznz", "bznznz");

  // Basic functionality with non-ascii
  AddTests({"щцфшцф", "ф", "ы"}, "щцышцы", "щцышцы", "щцышцы");

  // Basic functionality with upper case
  AddTests({"AbC", "", "xyz"}, "AbC", "AbC", "AbC");
  AddTests({"AbC", "", ""}, "AbC", "AbC", "AbC");
  AddTests({"ABC", "b", "xyz"}, "ABC", "ABC", "AxyzC");
  AddTests({"ABCaBC", "bC", "xyz"}, "ABCaBC", "ABCaBC", "Axyzaxyz");
  AddTests({"AbC", "AbC", "xyz"}, "xyz", "xyz", "xyz");
  AddTests({"ABC", "abcde", "xyz"}, "ABC", "ABC", "ABC");

  // First match replacement * upper case
  AddTests({"BaNaNA", "AnA", "xYz"}, "BaNaNA", "BaNaNA", "BxYzNA");
  AddTests({"bANANA", "ana", ""}, "bANANA", "bANANA", "bNA");
  AddTests({"BANANA", "a", "z"}, "BANANA", "BANANA", "BzNzNz");

  // Case insensitivity does not include accent sensitivity
  AddTests({"bänänä", "ä", "X"}, "bXnXnX", "bXnXnX", "bXnXnX");
  AddTests({"bäNäNä", "n", "ä"}, "bäNäNä", "bäNäNä", "bäääää");
  AddTests({"bäNäÑä", "ñ", "ä"}, "bäNäÑä", "bäNäÑä", "bäNäää");
  AddTests({"bäNäÑä", "n", "ä"}, "bäNäÑä", "bäNäÑä", "bäääÑä");

  // Punctuation works
  AddTests({"a! #a!#na !#n", "a!#n", ""}, "a! #a !#n", "a! #a !#n",
           "a! #a !#n");

  // Weird collator corner cases.

  // Ignorable characters - internal
  AddTests({"bA\0N\0A\0NA", "ANA", "X"}, "bA\0N\0A\0NA", "bX\0NA", "bX\0NA");
  AddTests({"bA\0N\0A\0NA", "ana", "X"}, "bA\0N\0A\0NA", "bA\0N\0A\0NA",
           "bX\0NA");

  // Ignorable characters - postfix
  AddTests({"bA\0N\0A\0", "ANA", "X"}, "bA\0N\0A\0", "bX\0", "bX\0");
  AddTests({"bA\0N\0A\0", "ana", "X"}, "bA\0N\0A\0", "bA\0N\0A\0", "bX\0");

  // Ignorable characters - postfix and prefix
  AddTests({"\0A\0N\0A\0", "ANA", "X"}, "\0A\0N\0A\0", "\0X\0", "\0X\0");
  AddTests({"\0A\0N\0A\0", "ana", "X"}, "\0A\0N\0A\0", "\0A\0N\0A\0", "\0X\0");

  // Ignorable characters - in match, postfix
  AddTests({"bA\0N\0A\0", "ANA\0", "X"}, "bA\0N\0A\0", "bX\0", "bX\0");
  AddTests({"bA\0N\0A\0", "ana\0", "X"}, "bA\0N\0A\0", "bA\0N\0A\0", "bX\0");

  AddTests({"\0A\0N\0A\0", "ANA\0", "bX\4"}, "\0A\0N\0A\0", "\0bX\4\0",
           "\0bX\4\0");
  AddTests({"\0A\0N\0A\0", "ana\0", "bX\4"}, "\0A\0N\0A\0", "\0A\0N\0A\0",
           "\0bX\4\0");

  // Ignorable characters in match, prefix
  AddTests({"bA\0N\0A\0", "\0ANA", "X"}, "bA\0N\0A\0", "bX\0", "bX\0");
  AddTests({"bA\0N\0A\0", "\0ana", "X"}, "bA\0N\0A\0", "bA\0N\0A\0", "bX\0");

  // Ignorable characters are interchangeable on match; preserved on replace.
  AddTests({"\1A\2N\3A\4", "\0ANA", "X"}, "\1A\2N\3A\4", "\1X\4", "\1X\4");
  AddTests({"\1A\2N\3A\4", "\0ana", "X"}, "\1A\2N\3A\4", "\1A\2N\3A\4",
           "\1X\4");

  // Test contractions. In Slovak, "ch" is one collation element.
  AddTests({"chlieb chlieb", "ch", "X"}, "Xlieb Xlieb", "Xlieb Xlieb",
           "Xlieb Xlieb");
  tests.push_back({"replace_with_collator",
                   {"sk", "chlieb chlieb", "ch", "X"},
                   "Xlieb Xlieb"});

  // ... with case insensitive
  AddTests({"chlieb chlieb", "CH", "X"}, "chlieb chlieb", "chlieb chlieb",
           "Xlieb Xlieb");
  tests.push_back({"replace_with_collator",
                   {"sk", "chlieb chlieb", "CH", "X"},
                   "chlieb chlieb"});
  tests.push_back({"replace_with_collator",
                   {"sk:ci", "chlieb chlieb", "CH", "X"},
                   "Xlieb Xlieb"});

  AddTests({"chlieb", "c", "X"}, "Xhlieb", "Xhlieb", "Xhlieb");
  tests.push_back(
      {"replace_with_collator", {"sk", "chlieb", "c", "X"}, "chlieb"});

  AddTests({"chlieb", "h", "X"}, "cXlieb", "cXlieb", "cXlieb");
  tests.push_back(
      {"replace_with_collator", {"sk", "chlieb", "h", "X"}, "chlieb"});
}

static std::vector<FunctionTestCall>
GetFunctionTestsStringWithCollatorInitializerList() {
  // Define the array as a vector of Values since values::StringArray strips the
  // "\0".
  std::vector<Value> split_out_values = {
      Value::String("\0"), Value::String("n\0"), Value::String("\0"),
      Value::String("\0n\0"), Value::String("\0")};
  auto split_out_array = Value::Array(
      googlesql::types::StringArrayType()->AsArray(), split_out_values);
  return {
      // split_with_collator(collator, string, string) -> array<string>
      // binary should have the same behavior as regular SPLIT.
      {"split_with_collator",
       {"binary", "banana", "a"},
       values::StringArray({"b", "n", "n", ""})},
      {"split_with_collator",
       {"binary", "banana", "A"},
       values::StringArray({"banana"})},
      {"split_with_collator",
       {"binary", "banaana", "a"},
       values::StringArray({"b", "n", "", "n", ""})},
      {"split_with_collator",
       {"binary", "a,b", ","},
       values::StringArray({"a", "b"})},
      {"split_with_collator",
       {"binary", "a,b", ""},
       values::StringArray({"a", ",", "b"})},
      {"split_with_collator", {"binary", "", "a"}, values::StringArray({""})},
      {"split_with_collator",
       {"binary", absl::string_view(), "a"},
       values::StringArray({""})},
      {"split_with_collator", {"binary", "", ""}, values::StringArray({""})},
      {"split_with_collator",
       {"binary", "чижик пыжик", " "},
       values::StringArray({"чижик", "пыжик"})},
      {"split_with_collator",
       {"binary", "абракадабра", "абр"},
       values::StringArray({"", "акад", "а"})},
      {"split_with_collator",
       {"binary", "विभि", ""},
       values::StringArray({"व", "ि", "भ", "ि"})},
      {"split_with_collator",
       {"binary", "विभिवि", "भि"},
       values::StringArray({"वि", "वि"})},
      // split_with_collator(collator, string, string) -> array<string>
      // und:ci test cases.
      {"split_with_collator",
       {"und:ci", "banAna", "a"},
       values::StringArray({"b", "n", "n", ""})},
      {"split_with_collator",
       {"und:ci", "banAanA", "a"},
       values::StringArray({"b", "n", "", "n", ""})},
      {"split_with_collator",
       {"und:ci", "banAnA", "aN"},
       values::StringArray({"b", "", "A"})},
      {"split_with_collator",
       {"und:ci", "a,b", ","},
       values::StringArray({"a", "b"})},
      {"split_with_collator",
       {"und:ci", "a,b", ""},
       values::StringArray({"a", ",", "b"})},
      {"split_with_collator",
       {"und:ci", absl::string_view(), "a"},
       values::StringArray({""})},
      {"split_with_collator",
       {"und:ci", absl::string_view(), "a"},
       values::StringArray({""})},
      {"split_with_collator", {"und:ci", "", ""}, values::StringArray({""})},
      {"split_with_collator",
       {"und:ci", "чижик пыжик", " "},
       values::StringArray({"чижик", "пыжик"})},
      {"split_with_collator",
       {"und:ci", "аБракадабра", "Абр"},
       values::StringArray({"", "акад", "а"})},
      {"split_with_collator",
       {"und:ci", "विभि", ""},
       values::StringArray({"व", "ि", "भ", "ि"})},
      {"split_with_collator",
       {"und:ci", "विभिवि", "भि"},
       values::StringArray({"वि", "वि"})},
      {"split_with_collator",
       {"und:ci", "banAnA", "\0a\0"},
       values::StringArray({"b", "n", "n", ""})},
      {"split_with_collator",
       {"und:ci", "\0an\0A\0a\0n\0A\0", "\0a\0"},
       split_out_array},
      {"split_with_collator",
       {"und:ci", "whät", ""},
       values::StringArray({"w", "h", "ä", "t"})},
      {"split_with_collator",
       {"und:ci", "äxäx", "x"},
       values::StringArray({"ä", "ä", ""})},
      {"split_with_collator",
       {"und:ci", "äxäx", "ä"},
       values::StringArray({"", "x", "x"})},
      // In Slovak ("sk" locale), "ch" is treated as a single collation element.
      // In unicode, "ch" is two separate collation elements.
      {"split_with_collator",
       {"sk:ci", "chlieb", ""},
       values::StringArray({"ch", "l", "i", "e", "b"})},
      {"split_with_collator",
       {"und:ci", "chlieb", ""},
       values::StringArray({"c", "h", "l", "i", "e", "b"})},
      // instr_with_collator(collator, string, [int64, [int64]]) -> int64
      // instr_with_collator also runs test cases for instr which cover all the
      // logic for regular instr for both binary and und:ci. These test
      // cases cover only case sensitivity and treatment of "ignorable collation
      // elements".
      // Check that binary is case sensitive.
      {"instr_with_collator", {"binary", "AaA", "a", 1l, 1l}, 2l},
      {"instr_with_collator", {"binary", "AaA", "a", 3l, 1l}, 0l},
      {"instr_with_collator", {"binary", "AaA", "a", -1l, 1l}, 2l},
      {"instr_with_collator", {"binary", "AaA", "a", -3l, 1l}, 0l},
      // Check that binary does not ignore null bytes.
      {"instr_with_collator", {"binary", "abca\0b\0c\0", "a\0b\0c"}, 4ll},
      {"instr_with_collator", {"binary", "abca\0b\0c\0", "a\0b\0c", -7ll}, 0ll},
      {"instr_with_collator",
       {"binary", "abca\0b\0c\0", "a\0b\0c", -1ll, 2ll},
       0ll},
      {"instr_with_collator",
       {"binary", "abca\0b\0c\0", "a\0b\0c", -6ll, 2ll},
       0ll},
      {"instr_with_collator",
       {"binary", "abca\0b\0c\0", "a\0b\0c", -7ll, 1ll},
       0ll},
      // Check that und:ci is case insensitive
      {"instr_with_collator", {"und:ci", "AaA", "a", 1l, 1l}, 1l},
      {"instr_with_collator", {"und:ci", "AaA", "a", 2l, 1l}, 2l},
      {"instr_with_collator", {"und:ci", "AaA", "a", -1l, 1l}, 3l},
      {"instr_with_collator", {"und:ci", "AaA", "a", -2l, 1l}, 2l},
      {"instr_with_collator", {"und:ci", "AbAbA", "Ba", 1l, 1l}, 2l},
      {"instr_with_collator", {"und:ci", "AbAbA", "Ba", 1l, 2l}, 4l},
      {"instr_with_collator", {"und:ci", "AbAbA", "Ba", -1l, 1l}, 4l},
      {"instr_with_collator", {"und:ci", "AbAbA", "Ba", -1l, 2l}, 2l},
      {"instr_with_collator", {"und:ci", "aaa", "", 1l, 1l}, 0l},
      {"instr_with_collator", {"und:ci", "aaa", "", -1l, 1l}, 0l},
      {"instr_with_collator", {"und:ci", "aaa", "\0", 1l, 1l}, 0l},
      {"instr_with_collator", {"und:ci", "", "", 1l, 1l}, 0l},
      {"instr_with_collator", {"und:ci", "", "", -1l, 1l}, 0l},
      {"instr_with_collator", {"und:ci", "", "\0", 1l, 1l}, 0l},
      {"instr_with_collator", {"und:ci", "", "\0", -1l, 1l}, 0l},
      // Check that collation based instr ignores null bytes when
      // matching but preserves them in unmatched part of the input.
      {"instr_with_collator", {"und:ci", "abca\0b\0c\0", "a\0b\0c"}, 1l},
      {"instr_with_collator", {"und:ci", "aBca\0b\0c\0", "A\0b\0c", -7l}, 1l},
      {"instr_with_collator",
       {"und:ci", "abCa\0b\0C\0", "a\0B\0c", -1l, 2l},
       1l},
      {"instr_with_collator",
       {"und:ci", "AbcA\0b\0c\0", "a\0b\0C", -6l, 2l},
       1l},
      {"instr_with_collator",
       {"und:ci", "AbcA\0b\0c\0", "a\0b\0c", -7l, 1l},
       1l},
      {"instr_with_collator", {"und:ci", "\0a\0b\0", "ab"}, 2l},
      {"instr_with_collator", {"und:ci", "abcd", "\0\0b\0\0c\0\0"}, 2l},
      {"instr_with_collator", {"und:ci", "\0a\0b\0", "ab", -1l}, 2l},
      // icu::StringSearch ignores the null byte in both the needle and the
      // haystack so it reports a match at index 1 (0-based). The result here is
      // 2 because INSTR is 1-based.
      {"instr_with_collator", {"und:ci", "\0a\0b\0", "\0a\0b"}, 2l},
      // icu::StringSearch ignores the null byte in the needle and reports no
      // match for an empty string.
      {"instr_with_collator", {"und:ci", "\0\0\0a", "\0"}, 0ll},
      // Test contractions. In Slovak, "ch" is one collation element.
      {"instr_with_collator", {"sk", "chlieb", "ch", 1l, 1l}, 1l},
      {"instr_with_collator", {"sk", "chlieb", "c", 1l, 1l}, 0l},
      {"instr_with_collator", {"und:ci", "chlieb", "c", 1l, 1l}, 1l},
      {"instr_with_collator", {"sk", "chlieb", "h", 1l, 1l}, 0l},
      {"instr_with_collator", {"sk", "chlieb", "h", 2l, 1l}, 0l},
      {"instr_with_collator", {"sk", "chlieb", "l", 1l, 1l}, 3l},
      {"instr_with_collator", {"und:ci", "chlieb", "c", 1l, 1l}, 1l},
      // strpos_with_collator(collator, string) -> int64
      // binary cases.
      {"strpos_with_collator", {"binary", "", "x"}, 0ll},
      {"strpos_with_collator", {"binary", "x", ""}, 1ll},
      {"strpos_with_collator", {"binary", "", ""}, 1ll},
      {"strpos_with_collator", {"binary", "xxx", "x"}, 1ll},
      {"strpos_with_collator", {"binary", "abcdef", "cd"}, 3ll},
      {"strpos_with_collator", {"binary", "abcdefabcdef", "de"}, 4ll},
      {"strpos_with_collator", {"binary", "abcdefabcdef", "xz"}, 0ll},
      {"strpos_with_collator", {"binary", "\0abcedf", "abc"}, 2ll},
      {"strpos_with_collator", {"binary", "abca\0b\0c\0", "a\0b\0c"}, 4ll},
      {"strpos_with_collator", {"binary", "zгдl", "дl"}, 3ll},
      {"strpos_with_collator", {"binary", "AaA", "A"}, 1l},
      {"strpos_with_collator", {"binary", "AaA", "a"}, 2l},
      // und:ci cases
      {"strpos_with_collator", {"und:ci", "AaA", "a"}, 1l},
      {"strpos_with_collator", {"und:ci", "AbAbA", "Ba", 1l, 1l}, 2l},
      {"strpos_with_collator", {"und:ci", "aaa", "", 1l, 1l}, 0l},
      {"strpos_with_collator", {"und:ci", "aaa", "\0", 1l, 1l}, 0l},
      {"strpos_with_collator", {"und:ci", "", "", 1l, 1l}, 0l},
      {"strpos_with_collator", {"und:ci", "", "\0", 1l, 1l}, 0l},
      // Check that collation based instr ignores null bytes when
      // matching but preserves them in unmatched part of the input.
      {"strpos_with_collator", {"und:ci", "abca\0b\0c\0", "a\0b\0c"}, 1l},
      {"strpos_with_collator", {"und:ci", "\0a\0b\0", "ab"}, 2l},
      {"strpos_with_collator", {"und:ci", "abcd", "\0\0b\0\0c\0\0"}, 2l},
      // icu::StringSearch ignores the null byte in both the needle and the
      // haystack so it reports a match at index 1 (0-based). The result here is
      // 2 because INSTR is 1-based.
      {"strpos_with_collator", {"und:ci", "\0a\0b\0", "\0a\0b"}, 2l},
      // icu::StringSearch ignores the null byte in the needle and reports no
      // match for an empty string.
      {"strpos_with_collator", {"und:ci", "\0\0\0a", "\0"}, 0l},
      // Test contractions. In Slovak, "ch" is one collation element.
      {"strpos_with_collator", {"sk", "chlieb", "ch"}, 1l},
      {"strpos_with_collator", {"sk", "chlieb", "c"}, 0l},
      {"strpos_with_collator", {"und:ci", "chlieb", "c"}, 1l},
      {"strpos_with_collator", {"sk", "chlieb", "h"}, 0l},
      {"strpos_with_collator", {"sk", "chlieb", "l"}, 3l},
      {"strpos_with_collator", {"und:ci", "chlieb", "c"}, 1l},
      // starts_with_collator(collator, string, string) -> bool
      // binary should behave same as non-collation version of STARTS_WITH
      {"starts_with_collator", {"binary", "", ""}, true},
      {"starts_with_collator", {"binary", "", "a"}, false},
      {"starts_with_collator", {"binary", "starts_with", "starts"}, true},
      {"starts_with_collator", {"binary", "starts_with", "starts_with"}, true},
      {"starts_with_collator", {"binary", "starts_with", "ends"}, false},
      {"starts_with_collator", {"binary", "starts_with", "with"}, false},
      {"starts_with_collator",
       {"binary", "starts_with", "starts_with_extra"},
       false},
      {"starts_with_collator", {"binary", "starts_with", ""}, true},
      {"starts_with_collator", {"binary", "абвгд", "абв"}, true},
      {"starts_with_collator", {"binary", "абв", "абвг"}, false},
      // STARTS_WITH with und:ci
      {"starts_with_collator", {"und:ci", "", ""}, false},
      {"starts_with_collator", {"und:ci", "starts_with", ""}, false},
      {"starts_with_collator", {"und:ci", "", "\0"}, false},
      {"starts_with_collator", {"und:ci", "starts_with", "\0"}, false},
      {"starts_with_collator", {"und:ci", "\0", ""}, false},
      {"starts_with_collator", {"und:ci", "\0", "\0"}, false},
      {"starts_with_collator", {"und:ci", "a\0bc\1\0\1\0", "abc"}, true},
      {"starts_with_collator", {"und:ci", "", "a"}, false},
      {"starts_with_collator", {"und:ci", "starts_with", ""}, false},
      {"starts_with_collator", {"und:ci", "starts!_with", "sTaRts!"}, true},
      {"starts_with_collator", {"und:ci", "StArTs_WiTh", "sTarTs_wItH"}, true},
      {"starts_with_collator", {"und:ci", "starts_with", "ends"}, false},
      {"starts_with_collator", {"und:ci", "starts_with", "with"}, false},
      {"starts_with_collator",
       {"und:ci", "starts_with", "starts_with_extra"},
       false},
      {"starts_with_collator", {"und:ci", "абв#гд", "абв#"}, true},
      {"starts_with_collator", {"und:ci", "абвгд", "аБв"}, true},
      {"starts_with_collator", {"und:ci", "абв", "абвг"}, false},
      {"starts_with_collator", {"und:ci", "аБв", "абвг"}, false},
      // In the sk locale, ch is one collation element.
      {"starts_with_collator", {"sk", "chlieb", "c"}, false},
      {"starts_with_collator", {"sk", "chlieb", "ch"}, true},
      // ends_with_collator(collator, string, string) -> bool
      // binary should behave same as non-collation version of ENDS_WITH
      {"ends_with_collator", {"binary", "", ""}, true},
      {"ends_with_collator", {"binary", "", "a"}, false},
      {"ends_with_collator", {"binary", "ends_with", "with"}, true},
      {"ends_with_collator", {"binary", "ends_with", "extra_ends_with"}, false},
      {"ends_with_collator", {"binary", "ends_with", "ends"}, false},
      {"ends_with_collator", {"binary", "ends_with", "with"}, true},
      {"ends_with_collator", {"binary", "ends_with", ""}, true},
      {"ends_with_collator", {"binary", "абвгд", "вгд"}, true},
      {"ends_with_collator", {"binary", "бв", "aбв"}, false},
      // ENDS_WITH with und:ci
      {"ends_with_collator", {"und:ci", "", ""}, false},
      {"ends_with_collator", {"und:ci", "ends_with", ""}, false},
      {"ends_with_collator", {"und:ci", "", "\0"}, false},
      {"ends_with_collator", {"und:ci", "\0", ""}, false},
      {"ends_with_collator", {"und:ci", "", "a"}, false},
      {"ends_with_collator", {"und:ci", "aaa\0b", "ab"}, true},
      {"ends_with_collator", {"und:ci", "ends_with", "WItH"}, true},
      {"ends_with_collator", {"und:ci", "eNDs_wITh", "extra_ends_with"}, false},
      {"ends_with_collator", {"und:ci", "ends_with", "ends"}, false},
      {"ends_with_collator", {"und:ci", "ends_wITh", "WitH"}, true},
      {"ends_with_collator", {"und:ci", "ends_with", ""}, false},
      {"ends_with_collator", {"und:ci", "абвгд", "вгД"}, true},
      {"ends_with_collator", {"und:ci", "бв", "aбв"}, false},
      // In the sk locale, ch is one collation element.
      {"ends_with_collator", {"sk", "prach", "h"}, false},
  };
}

std::vector<FunctionTestCall> GetFunctionTestsStringWithCollator() {
  std::vector<FunctionTestCall> tests =
      GetFunctionTestsStringWithCollatorInitializerList();
  GetReplaceFunctions(tests);
  return tests;
}

struct CollatedResult {
  std::string collation;
  const ValueConstructor result;
  absl::StatusCode status;

  CollatedResult(absl::string_view collation, const ValueConstructor& result,
                 absl::StatusCode status = absl::StatusCode::kOk)
      : collation(collation), result(result), status(status) {}
};

struct FunctionTestWithCollator {
  std::string function_name;
  std::vector<ValueConstructor> params;
  std::vector<CollatedResult> results;
};

std::vector<FunctionTestCall> GetFunctionTestCalls(
    absl::Span<const FunctionTestWithCollator> tests, bool skip_collation) {
  std::vector<FunctionTestCall> function_test_calls;
  for (const auto& test : tests) {
    for (const auto& result : test.results) {
      if (skip_collation) {
        // only push test-cases with binary collation
        if (result.collation == "binary") {
          QueryParamsWithResult query_params_with_result(
              test.params, result.result, result.status);
          function_test_calls.push_back(
              {test.function_name, query_params_with_result});
        }
      } else {
        std::vector<ValueConstructor> params_with_collator;
        params_with_collator.reserve(test.params.size() + 1);
        params_with_collator.push_back(result.collation);

        for (const auto& param : test.params) {
          params_with_collator.push_back(param);
        }

        QueryParamsWithResult query_params_with_result(
            params_with_collator, result.result, result.status);
        function_test_calls.push_back(
            {test.function_name + "_with_collator", query_params_with_result});
      }
    }
  }
  return function_test_calls;
}

std::vector<FunctionTestCall> GetFunctionTestsSplitSubstr(bool skip_collation) {
  std::vector<FunctionTestWithCollator> tests{
      // Empty Delimiter
      {"split_substr",
       {"www.abc.com", "", 1l, 1l},
       {{"binary", "", OUT_OF_RANGE}, {"und:ci", "", OUT_OF_RANGE}}},
      // Non-UTF-8 Delimiter
      {"split_substr",
       {"www.abc.com", "\xc2", 1l, 1l},
       {{"binary", "", OUT_OF_RANGE}, {"und:ci", "", OUT_OF_RANGE}}},
      // Non-UTF-8 Text
      {"split_substr",
       {"www.abc.\xc2om", ".", 1l, 1l},
       {{"binary", "", OUT_OF_RANGE}, {"und:ci", "", OUT_OF_RANGE}}},
      // Negative Count
      {"split_substr",
       {"www.abc.com", ".", 1l, -1l},
       {{"binary", "", OUT_OF_RANGE}, {"und:ci", "", OUT_OF_RANGE}}},
      // Empty Text.
      {"split_substr", {"", ".", 1l, 1l}, {{"binary", ""}, {"und:ci", ""}}},
      // Delimiter not present
      {"split_substr",
       {"www.abc.com", "#", 1l, 1l},
       {{"binary", "www.abc.com"}, {"und:ci", "www.abc.com"}}},
      {"split_substr",
       {"www.abc.com", ".", 1l, 0l},
       {{"binary", ""}, {"und:ci", ""}}},
      {"split_substr",
       {"www.abc.com", ".", 1l, 2l},
       {{"binary", "www.abc"}, {"und:ci", "www.abc"}}},
      {"split_substr",
       {"www.abc.com", ".", 1l, 1l},
       {{"binary", "www"}, {"und:ci", "www"}}},
      {"split_substr",
       {"www.abc.com", ".", 1l, 2l},
       {{"binary", "www.abc"}, {"und:ci", "www.abc"}}},
      {"split_substr",
       {"www.abc.com", ".", 1l, 3l},
       {{"binary", "www.abc.com"}, {"und:ci", "www.abc.com"}}},
      {"split_substr",
       {"www.abc.com", ".", 1l, 4l},
       {{"binary", "www.abc.com"}, {"und:ci", "www.abc.com"}}},
      {"split_substr",
       {"www.abc.com", ".", 0l, 4l},
       {{"binary", "www.abc.com"}, {"und:ci", "www.abc.com"}}},
      {"split_substr",
       {"www.abc.com", ".", 2l, 3l},
       {{"binary", "abc.com"}, {"und:ci", "abc.com"}}},
      {"split_substr",
       {"www.abc.com", ".", 3l, 3l},
       {{"binary", "com"}, {"und:ci", "com"}}},
      {"split_substr",
       {"www.abc.com", ".", 4l, 1l},
       {{"binary", ""}, {"und:ci", ""}}},
      {"split_substr",
       {"www.abc.com", ".", 5l, 1l},
       {{"binary", ""}, {"und:ci", ""}}},
      {"split_substr",
       {"www.abc.com", ".", -1l, 1l},
       {{"binary", "com"}, {"und:ci", "com"}}},
      {"split_substr",
       {"www.abc.com", ".", -2l, 3l},
       {{"binary", "abc.com"}, {"und:ci", "abc.com"}}},
      {"split_substr",
       {"www.abc.com", ".", -3l, 4l},
       {{"binary", "www.abc.com"}, {"und:ci", "www.abc.com"}}},
      {"split_substr",
       {"www.abc.com", ".", -4l, 1l},
       {{"binary", "www"}, {"und:ci", "www"}}},
      {"split_substr",
       {"www.abc.com", ".", -5l, 1l},
       {{"binary", "www"}, {"und:ci", "www"}}},
      {"split_substr",
       {"www.abc.com", ".", -5l, 2l},
       {{"binary", "www.abc"}, {"und:ci", "www.abc"}}},
      {"split_substr",
       {"aaa***bb***cc", "**", 0l, 1l},
       {{"binary", "aaa"}, {"und:ci", "aaa"}}},
      {"split_substr",
       {"aaa***bb***cc", "**", 1l, 1l},
       {{"binary", "aaa"}, {"und:ci", "aaa"}}},
      {"split_substr",
       {"aaa***bb***cc", "**", 2l, 1l},
       {{"binary", "*bb"}, {"und:ci", "*bb"}}},
      {"split_substr",
       {"aaa***bb***cc", "**", 3l, 1l},
       {{"binary", "*cc"}, {"und:ci", "*cc"}}},
      {"split_substr",
       {"aaa***bb***cc", "**", 4l, 1l},
       {{"binary", ""}, {"und:ci", ""}}},
      {"split_substr",
       {"aaa***bb***cc", "**", -1l, 2l},
       {{"binary", "*cc"}, {"und:ci", "*cc"}}},
      {"split_substr",
       {"aaa***bb***cc", "**", -2l, 2l},
       {{"binary", "*bb***cc"}, {"und:ci", "*bb***cc"}}},
      {"split_substr",
       {"aaa***bb***cc", "**", -3l, 2l},
       {{"binary", "aaa***bb"}, {"und:ci", "aaa***bb"}}},
      {"split_substr",
       {"aaa***bb***cc", "**", -4l, 2l},
       {{"binary", "aaa***bb"}, {"und:ci", "aaa***bb"}}},
      {"split_substr",
       {"aaa***bb***cc", "**", -5l, 2l},
       {{"binary", "aaa***bb"}, {"und:ci", "aaa***bb"}}},
      {"split_substr",
       {"aaa***bb***cc", "**", 100l, 2l},
       {{"binary", ""}, {"und:ci", ""}}},
      {"split_substr",
       {"aaa***bb***cc", "**", -100l, 2l},
       {{"binary", "aaa***bb"}, {"und:ci", "aaa***bb"}}},
      // Delimiter as prefix.
      {"split_substr",
       {"**aaa***bb***cc", "**", 1l, 1l},
       {{"binary", ""}, {"und:ci", ""}}},
      // Delimiter as suffix.
      {"split_substr",
       {"aaa***bb***cc**", "**", -1l, 1l},
       {{"binary", ""}, {"und:ci", ""}}},
      {"split_substr",
       {"aaaXXXbbXXXcc", "Xx", 0l, 1l},
       {{"und:ci", "aaa"}, {"binary", "aaaXXXbbXXXcc"}}},
      {"split_substr", {"aaaXXXbbXXXcc", "Xx", 1l, 1l}, {{"und:ci", "aaa"}}},
      {"split_substr", {"aaaXXXbbXXXcc", "Xx", 2l, 1l}, {{"und:ci", "Xbb"}}},
      {"split_substr", {"aaaXXXbbXXXcc", "Xx", 3l, 1l}, {{"und:ci", "Xcc"}}},
      {"split_substr", {"aaaXXXbbXXXcc", "Xx", 4l, 1l}, {{"und:ci", ""}}},
      {"split_substr", {"aaaXXXbbXXXcc", "Xx", -1l, 2l}, {{"und:ci", "Xcc"}}},
      {"split_substr",
       {"aaaXXXbbXXXcc", "Xx", -2l, 2l},
       {{"und:ci", "XbbXXXcc"}}},
      {"split_substr",
       {"aaaXXXbbXXXcc", "Xx", -3l, 2l},
       {{"und:ci", "aaaXXXbb"}}},
      {"split_substr",
       {"aaaXXXbbXXXcc", "Xx", -4l, 2l},
       {{"und:ci", "aaaXXXbb"}}},
      {"split_substr",
       {"aaaXXXbbXXXcc", "Xx", -5l, 2l},
       {{"und:ci", "aaaXXXbb"}}},
      {"split_substr", {"aaaXXXbbXXXcc", "Xx", 100l, 2l}, {{"und:ci", ""}}},
      {"split_substr",
       {"aaaXXXbbXXXcc", "Xx", -100l, 2l},
       {{"und:ci", "aaaXXXbb"}}},
      {"split_substr",
       {"аБракадабра", "абр", 1l, 1l},
       {{"und:ci", ""}, {"binary", "аБракад"}}},
      {"split_substr",
       {"аБракадабра", "абр", 2l, 1l},
       {{"und:ci", "акад"}, {"binary", "а"}}},
      {"split_substr",
       {"aaa\0*\0*bb***cc", "**", 0l, 1l},
       {{"binary", "aaa\0*\0*bb"}, {"und:ci", "aaa\0"}}},
      // Delimiter as prefix.
      {"split_substr", {"XXaaaXXXbbXXXcc", "Xx", 1l, 1l}, {{"und:ci", ""}}},
      // Delimiter as suffix.
      {"split_substr", {"aaaXXXbbXXXccXX", "Xx", -1l, 1l}, {{"und:ci", ""}}},
      // without count argument
      //  // Empty Delimiter
      {"split_substr",
       {"www.abc.com", "", 1l},
       {{"binary", "", OUT_OF_RANGE}, {"und:ci", "", OUT_OF_RANGE}}},
      // Non-UTF-8 Delimiter
      {"split_substr",
       {"www.abc.com", "\xc2", 1l},
       {{"binary", "", OUT_OF_RANGE}, {"und:ci", "", OUT_OF_RANGE}}},
      // Non-UTF-8 Text
      {"split_substr",
       {"www.abc.\xc2om", ".", 1l},
       {{"binary", "", OUT_OF_RANGE}, {"und:ci", "", OUT_OF_RANGE}}},
      {"split_substr",
       {"www.abc.com", "#", 1l},
       {{"binary", "www.abc.com"}, {"und:ci", "www.abc.com"}}},
      {"split_substr",
       {"www.abc.com", ".", 0l},
       {{"binary", "www.abc.com"}, {"und:ci", "www.abc.com"}}},
      {"split_substr",
       {"www.abc.com", ".", 1l},
       {{"binary", "www.abc.com"}, {"und:ci", "www.abc.com"}}},
      {"split_substr",
       {"www.abc.com", ".", 2l},
       {{"binary", "abc.com"}, {"und:ci", "abc.com"}}},
      {"split_substr",
       {"www.abc.com", ".", 3l},
       {{"binary", "com"}, {"und:ci", "com"}}},
      {"split_substr",
       {"www.abc.com", ".", 4l},
       {{"binary", ""}, {"und:ci", ""}}},
      {"split_substr",
       {"www.abc.com", ".", 5l},
       {{"binary", ""}, {"und:ci", ""}}},
      {"split_substr",
       {"www.abc.com", ".", -1l},
       {{"binary", "com"}, {"und:ci", "com"}}},
      {"split_substr",
       {"www.abc.com", ".", -2l},
       {{"binary", "abc.com"}, {"und:ci", "abc.com"}}},
      {"split_substr",
       {"www.abc.com", ".", -3l},
       {{"binary", "www.abc.com"}, {"und:ci", "www.abc.com"}}},
      {"split_substr",
       {"www.abc.com", ".", -4l},
       {{"binary", "www.abc.com"}, {"und:ci", "www.abc.com"}}},
      {"split_substr",
       {"www.abc.com", ".", -5l},
       {{"binary", "www.abc.com"}, {"und:ci", "www.abc.com"}}},
      {"split_substr",
       {"aaa***bb***cc", "**", 0l},
       {{"binary", "aaa***bb***cc"}, {"und:ci", "aaa***bb***cc"}}},
      {"split_substr",
       {"aaa***bb***cc", "**", 1l},
       {{"binary", "aaa***bb***cc"}, {"und:ci", "aaa***bb***cc"}}},
      {"split_substr",
       {"aaa***bb***cc", "**", 2l},
       {{"binary", "*bb***cc"}, {"und:ci", "*bb***cc"}}},
      {"split_substr",
       {"aaa***bb***cc", "**", 3l},
       {{"binary", "*cc"}, {"und:ci", "*cc"}}},
      {"split_substr",
       {"aaa***bb***cc", "**", 4l},
       {{"binary", ""}, {"und:ci", ""}}},
      {"split_substr",
       {"aaa***bb***cc", "**", -1l},
       {{"binary", "*cc"}, {"und:ci", "*cc"}}},
      {"split_substr",
       {"aaa***bb***cc", "**", -2l},
       {{"binary", "*bb***cc"}, {"und:ci", "*bb***cc"}}},
      {"split_substr",
       {"aaa***bb***cc", "**", -3l},
       {{"binary", "aaa***bb***cc"}, {"und:ci", "aaa***bb***cc"}}},
      {"split_substr",
       {"aaa***bb***cc", "**", -4l},
       {{"binary", "aaa***bb***cc"}, {"und:ci", "aaa***bb***cc"}}},
      {"split_substr",
       {"aaa***bb***cc", "**", -5l},
       {{"binary", "aaa***bb***cc"}, {"und:ci", "aaa***bb***cc"}}},
      {"split_substr",
       {"aaa***bb***cc", "**", 100l},
       {{"binary", ""}, {"und:ci", ""}}},
      {"split_substr",
       {"aaa***bb***cc", "**", -100l},
       {{"binary", "aaa***bb***cc"}, {"und:ci", "aaa***bb***cc"}}},
      // Delimiter as prefix.
      {"split_substr",
       {"**aaa***bb***cc", "**", 1l},
       {{"binary", "**aaa***bb***cc"}, {"und:ci", "**aaa***bb***cc"}}},
      // Delimiter as suffix.
      {"split_substr",
       {"aaa***bb***cc**", "**", -1l},
       {{"binary", ""}, {"und:ci", ""}}},
      {"split_substr",
       {"aaaXXXbbXXXcc", "xX", 0l},
       {{"und:ci", "aaaXXXbbXXXcc"}}},
      {"split_substr",
       {"aaaXXXbbXXXcc", "xX", 1l},
       {{"und:ci", "aaaXXXbbXXXcc"}}},
      {"split_substr", {"aaaXXXbbXXXcc", "xX", 2l}, {{"und:ci", "XbbXXXcc"}}},
      {"split_substr", {"aaaXXXbbXXXcc", "xX", -1l}, {{"und:ci", "Xcc"}}},
      {"split_substr", {"aaaXXXbbXXXcc", "xX", -2l}, {{"und:ci", "XbbXXXcc"}}},
      {"split_substr", {"aaaXXXbbXXXcc", "xX", 100l}, {{"und:ci", ""}}},
      {"split_substr",
       {"aaaXXXbbXXXcc", "xX", -100l},
       {{"und:ci", "aaaXXXbbXXXcc"}}},
      {"split_substr",
       {"аБракадабра", "Абр", 2l},
       {{"und:ci", "акадабра"}, {"binary", ""}}},
      {"split_substr",
       {"аБракадабра", "Абр", 2l},
       {{"und:ci", "акадабра"}, {"binary", ""}}},
      {"split_substr",
       {"аБракадабра", "Абр", 1l},
       {{"und:ci", "аБракадабра"}, {"binary", "аБракадабра"}}},
      {"split_substr",
       {"aaa\0*\0*bb***cc", "**", 2l},
       {{"binary", "*cc"}, {"und:ci", "bb***cc"}}},
  };
  return GetFunctionTestCalls(tests, skip_collation);
}

}  // namespace googlesql
