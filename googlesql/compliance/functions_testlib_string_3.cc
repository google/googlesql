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

#include <vector>

#include "googlesql/compliance/functions_testlib_common.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "googlesql/testing/using_test_value.cc"  // NOLINT

namespace googlesql {

static constexpr absl::StatusCode OUT_OF_RANGE = absl::StatusCode::kOutOfRange;

std::vector<FunctionTestCall> GetFunctionTestsInstr1() {
  return {
      // instr(string, string) -> int64
      {"instr", {NullString(), ""}, NullInt64()},
      {"instr", {NullString(), "x"}, NullInt64()},
      {"instr", {"", NullString()}, NullInt64()},
      {"instr", {NullString(), NullString()}, NullInt64()},
      {"instr", {"", "x"}, 0ll},
      {"instr", {"x", NullString()}, NullInt64()},
      // Similar to STRPOS("", "") = 1
      {"instr", {"xxx", "x"}, 1ll},
      {"instr", {"abcdef", "cd"}, 3ll},
      {"instr", {"abcdefabcdef", "de"}, 4ll},
      {"instr", {"abcdefabcdef", "xz"}, 0ll},
      {"instr", {"\0abcedf", "abc"}, 2ll},
      {"instr", {"zгдl", "дl"}, 3ll},

      // instr(bytes, bytes) -> int64
      {"instr", {NullBytes(), Bytes("")}, NullInt64()},
      {"instr", {NullBytes(), Bytes("x")}, NullInt64()},
      {"instr", {Bytes(""), NullBytes()}, NullInt64()},
      {"instr", {NullBytes(), NullBytes()}, NullInt64()},
      {"instr", {Bytes(""), Bytes("x")}, 0ll},
      {"instr", {Bytes("x"), Bytes("")}, 1ll},
      {"instr", {Bytes("x"), NullBytes()}, NullInt64()},
      {"instr", {Bytes(""), Bytes("")}, 1ll},
      {"instr", {Bytes("xxx"), Bytes("x")}, 1ll},
      {"instr", {Bytes("abcdef"), Bytes("cd")}, 3ll},
      {"instr", {Bytes("abcdefabcdef"), Bytes("de")}, 4ll},
      {"instr", {Bytes("abcdefabcdef"), Bytes("xz")}, 0ll},
      {"instr", {Bytes("\0abcedf"), Bytes("abc")}, 2ll},
      {"instr", {Bytes("abca\0b\0c\0"), Bytes("a\0b\0c")}, 4ll},
      {"instr", {Bytes("zгдl"), Bytes("дl")}, 4ll},

      // instr(string, string, int64) -> int64
      {"instr", {NullString(), "", 1ll}, NullInt64()},
      {"instr", {NullString(), "x", 1ll}, NullInt64()},
      {"instr", {"", NullString(), 1ll}, NullInt64()},
      {"instr", {NullString(), NullString(), 1ll}, NullInt64()},
      {"instr", {"", "x", NullInt64()}, NullInt64()},
      {"instr", {"", "", NullInt64()}, NullInt64()},
      {"instr", {NullString(), "x", NullInt64()}, NullInt64()},
      {"instr", {NullString(), "", NullInt64()}, NullInt64()},
      {"instr", {"", NullString(), NullInt64()}, NullInt64()},
      {"instr", {"x", NullString(), NullInt64()}, NullInt64()},
      {"instr", {NullString(), NullString(), NullInt64()}, NullInt64()},
      {"instr", {"", "x", 0ll}, NullInt64(), OUT_OF_RANGE},
      {"instr", {"", "x", 1ll}, 0ll},
      {"instr", {"", "x", 2ll}, 0ll},
      {"instr", {"", "", 2ll}, 0ll},
      {"instr", {"xxx", "x", 1ll}, 1ll},
      {"instr", {"xxx", "x", 2ll}, 2ll},
      {"instr", {"xxx", "x", 3ll}, 3ll},
      {"instr", {"xxx", "x", 4ll}, 0ll},
      {"instr", {"xxx", "x", int64max}, 0ll},
      {"instr", {"abcdef", "cd", 1ll}, 3ll},
      {"instr", {"abcdef", "cd", 3ll}, 3ll},
      {"instr", {"abcdef", "cd", 4ll}, 0ll},
      {"instr", {"abcdef", "cd", 8ll}, 0ll},
      {"instr", {"abcdef", "cd", int64max}, 0ll},
      {"instr", {"abcdefabcdef", "xz", 1ll}, 0ll},
      {"instr", {"abcdefabcdef", "xz", 30ll}, 0ll},
      {"instr", {"\0abcedf", "abc", 1ll}, 2ll},
      {"instr", {"\0abcedf", "abc", 2ll}, 2ll},
      {"instr", {"\0abcedf", "abc", 3ll}, 0ll},
      {"instr", {"abca\0b\0c\0", "a\0b\0c", 3ll}, 4ll},
      {"instr", {"abca\0b\0c\0", "a\0b\0c", 4ll}, 4ll},
      {"instr", {"abca\0b\0c\0", "a\0b\0c", 5ll}, 0ll},
      {"instr", {"zгдl", "дl", 1ll}, 3ll},
      {"instr", {"zгдl", "дl", 3ll}, 3ll},
      {"instr", {"zгдl", "дl", 4ll}, 0ll},

      {"instr", {NullString(), "", -1ll}, NullInt64()},
      {"instr", {NullString(), "x", -1ll}, NullInt64()},
      {"instr", {"", NullString(), -1ll}, NullInt64()},
      {"instr", {NullString(), NullString(), -1ll}, NullInt64()},
      {"instr", {"", "x", -1ll}, 0ll},
      {"instr", {"", "x", -2ll}, 0ll},
      {"instr", {"xxx", "x", -1ll}, 3ll},
      {"instr", {"xxx", "x", -2ll}, 2ll},
      {"instr", {"xxx", "x", -3ll}, 1ll},
      {"instr", {"xxx", "x", -4ll}, 0ll},
      {"instr", {"xxx", "x", int64min}, 0ll},
      {"instr", {"abcdef", "cd", -1ll}, 3ll},
      {"instr", {"abcdef", "cd", -3ll}, 3ll},
      {"instr", {"abcdef", "cd", -4ll}, 3ll},
      {"instr", {"abcdef", "cd", -5ll}, 0ll},
      {"instr", {"abcdef", "cd", int64min}, 0ll},
      {"instr", {"abcdefabcdef", "xz", -1ll}, 0ll},
      {"instr", {"abcdefabcdef", "xz", -30ll}, 0ll},
      {"instr", {"\0abcedf", "abc", -1ll}, 2ll},
      {"instr", {"\0abcedf", "abc", -5ll}, 2ll},
      {"instr", {"\0abcedf", "abc", -7ll}, 0ll},
      {"instr", {"abca\0b\0c\0", "a\0b\0c", -1ll}, 4ll},
      {"instr", {"abca\0b\0c\0", "a\0b\0c", -2ll}, 4ll},
      {"instr", {"abca\0b\0c\0", "a\0b\0c", -6ll}, 4ll},
      {"instr", {"zгдlг", "дl", -1ll}, 3ll},
      {"instr", {"zгдlг", "дl", -2ll}, 3ll},
      {"instr", {"zгдlг", "дl", -3ll}, 3ll},
      {"instr", {"zгдlг", "дl", -4ll}, 0ll},
  };
}

}  // namespace googlesql
