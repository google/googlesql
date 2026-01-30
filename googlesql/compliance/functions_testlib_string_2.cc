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

#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "googlesql/compliance/functions_testlib_common.h"
#include "googlesql/public/type.h"
#include "googlesql/public/value.h"
#include "googlesql/testing/test_function.h"
#include "googlesql/testing/test_value.h"
#include "googlesql/testing/using_test_value.cc"  // NOLINT
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "googlesql/base/status.h"

namespace googlesql {
namespace {
constexpr absl::StatusCode OUT_OF_RANGE = absl::StatusCode::kOutOfRange;
}  // namespace

std::vector<FunctionTestCall> GetStringConcatTests() {
  return {
      // concat(string...) -> string
      {"concat", {NullString()}, NullString()},
      {"concat", {NullString(), NullString()}, NullString()},
      {"concat", {"a", NullString()}, NullString()},
      {"concat", {NullString(), "b"}, NullString()},
      {"concat", {"a", "b"}, "ab"},
      {"concat", {"", ""}, ""},
      {"concat", {"", NullString()}, NullString()},
      {"concat", {"", "A"}, "A"},
      {"concat", {"A", ""}, "A"},
      {"concat", {"abc", "def", "xyz"}, "abcdefxyz"},

      // concat(bytes...) -> bytes
      {"concat", {NullBytes()}, NullBytes()},
      {"concat", {NullBytes(), NullBytes()}, NullBytes()},
      {"concat", {Bytes("a"), NullBytes()}, NullBytes()},
      {"concat", {NullBytes(), Bytes("b")}, NullBytes()},
      {"concat", {Bytes("a"), Bytes("b")}, Bytes("ab")},
      {"concat", {Bytes(""), Bytes("")}, Bytes("")},
      {"concat", {Bytes(""), NullBytes()}, NullBytes()},
      {"concat", {Bytes(""), Bytes("A")}, Bytes("A")},
      {"concat", {Bytes("A"), Bytes("")}, Bytes("A")},
      {"concat", {Bytes("abc"), Bytes("def"), Bytes("xyz")},
       Bytes("abcdefxyz")},
  };
}

// Tests or SUBSTR() function.
std::vector<FunctionTestCall> GetFunctionTestsSubstr(
    absl::string_view function_name) {
  std::vector<FunctionTestCall> results = {
      // substr(string, pos) -> string
      {function_name, {NullString(), 0ll}, NullString()},
      {function_name, {NullString(), NullInt64()}, NullString()},
      {function_name, {"", NullInt64()}, NullString()},
      {function_name, {"", 0ll}, ""},
      {function_name, {"", 1ll}, ""},
      {function_name, {"", -1ll}, ""},
      {function_name, {"abc", 1ll}, "abc"},
      {function_name, {"abc", 3ll}, "c"},
      {function_name, {"abc", 4ll}, ""},
      {function_name, {"abc", 0ll}, "abc"},
      {function_name, {"abc", -2ll}, "bc"},
      {function_name, {"abc", -5ll}, "abc"},
      {function_name, {"abc", int64min}, "abc"},
      {function_name, {"abc", int64max}, ""},
      {function_name, {"ЩФБШ", 0ll}, "ЩФБШ"},
      {function_name, {"ЩФБШ", 1ll}, "ЩФБШ"},
      {function_name, {"ЩФБШ", 3ll}, "БШ"},
      {function_name, {"ЩФБШ", 5ll}, ""},
      {function_name, {"ЩФБШ", -2ll}, "БШ"},
      {function_name, {"ЩФБШ", -4ll}, "ЩФБШ"},
      {function_name, {"ЩФБШ", -5ll}, "ЩФБШ"},
      {function_name, {"ЩФБШ", int64min}, "ЩФБШ"},
      {function_name, {"ЩФБШ", int64max}, ""},

      // substr(string, pos, length) -> string
      {function_name, {NullString(), 0ll, 1ll}, NullString()},
      {function_name, {NullString(), 0ll, 0ll}, NullString()},
      {function_name, {NullString(), 0ll, -1ll}, NullString()},
      {function_name, {NullString(), 0ll, NullInt64()}, NullString()},
      {function_name, {NullString(), 1ll, 1ll}, NullString()},
      {function_name, {NullString(), 1ll, 0ll}, NullString()},
      {function_name, {NullString(), 1ll, -1ll}, NullString()},
      {function_name, {NullString(), 1ll, NullInt64()}, NullString()},
      {function_name, {NullString(), -1ll, 1ll}, NullString()},
      {function_name, {NullString(), -1ll, 0ll}, NullString()},
      {function_name, {NullString(), -1ll, -1ll}, NullString()},
      {function_name, {NullString(), -1ll, NullInt64()}, NullString()},
      {function_name, {NullString(), NullInt64(), 1ll}, NullString()},
      {function_name, {NullString(), NullInt64(), 0ll}, NullString()},
      {function_name, {NullString(), NullInt64(), -1ll}, NullString()},
      {function_name, {NullString(), NullInt64(), NullInt64()}, NullString()},
      {function_name, {"", NullInt64(), 1ll}, NullString()},
      {function_name, {"", NullInt64(), 0ll}, NullString()},
      {function_name, {"", NullInt64(), -1ll}, NullString()},
      {function_name, {"", NullInt64(), NullInt64()}, NullString()},
      {function_name, {"", 0ll, 1ll}, ""},
      {function_name, {"", 0ll, 0ll}, ""},
      {function_name, {"", 0ll, -1ll}, NullString(), OUT_OF_RANGE},
      {function_name, {"", 0ll, NullInt64()}, NullString()},
      {function_name, {"", 1ll, 1ll}, ""},
      {function_name, {"", 1ll, 0ll}, ""},
      {function_name, {"", 1ll, -1ll}, NullString(), OUT_OF_RANGE},
      {function_name, {"", 1ll, NullInt64()}, NullString()},
      {function_name, {"", -1ll, 1ll}, ""},
      {function_name, {"", -1ll, 0ll}, ""},
      {function_name, {"", -1ll, -1ll}, NullString(), OUT_OF_RANGE},
      {function_name, {"", -1ll, NullInt64()}, NullString()},
      {function_name, {"abc", 0ll, 0ll}, ""},
      {function_name, {"abc", 0ll, 1ll}, "a"},
      {function_name, {"abc", 0ll, -1ll}, NullString(), OUT_OF_RANGE},
      {function_name, {"abc", 0ll, NullInt64()}, NullString()},
      {function_name, {"abc", -1ll, 0ll}, ""},
      {function_name, {"abc", -1ll, 1ll}, "c"},
      {function_name, {"abc", -1ll, -1ll}, NullString(), OUT_OF_RANGE},
      {function_name, {"abc", -1ll, NullInt64()}, NullString()},
      {function_name, {"abc", NullInt64(), 0ll}, NullString()},
      {function_name, {"abc", NullInt64(), 1ll}, NullString()},
      {function_name, {"abc", NullInt64(), -1ll}, NullString()},
      {function_name, {"abc", NullInt64(), NullInt64()}, NullString()},
      {function_name, {"abc", 1ll, 2ll}, "ab"},
      {function_name, {"abc", 3ll, 1ll}, "c"},
      {function_name, {"abc", 3ll, 0ll}, ""},
      {function_name, {"abc", 2ll, 1ll}, "b"},
      {function_name, {"abc", 2ll, NullInt64()}, NullString()},
      {function_name, {"abc", 4ll, 5ll}, ""},
      {function_name, {"abc", 1ll, int64max}, "abc"},
      {function_name, {"abc", 3ll, -1ll}, NullString(), OUT_OF_RANGE},
      {function_name, {"abc", -2ll, 1ll}, "b"},
      {function_name, {"abc", -5ll, 2ll}, "ab"},
      {function_name, {"abc", -5ll, 0ll}, ""},
      {function_name, {"abc", -5ll, -1ll}, NullString(), OUT_OF_RANGE},
      {function_name, {"abc", int64min, 1ll}, "a"},
      {function_name, {"abc", int64max, int64max}, ""},
      {function_name, {"abc", 1ll, -1ll}, NullString(), OUT_OF_RANGE},
      {function_name, {"abc", 1ll, int64min}, NullString(), OUT_OF_RANGE},
      {function_name, {"ЩФБШ", 0ll, 2ll}, "ЩФ"},
      {function_name, {"ЩФБШ", 0ll, 5ll}, "ЩФБШ"},
      {function_name, {"ЩФБШ", 2ll, 0ll}, ""},
      {function_name, {"ЩФБШ", 2ll, 1ll}, "Ф"},
      {function_name, {"ЩФБШ", 2ll, 5ll}, "ФБШ"},
      {function_name, {"ЩФБШ", 3ll, 2ll}, "БШ"},
      {function_name, {"ЩФБШ", 5ll, 2ll}, ""},
      {function_name, {"ЩФБШ", -2ll, 3ll}, "БШ"},
      {function_name, {"ЩФБШ", -2ll, 0ll}, ""},
      {function_name, {"ЩФБШ", -2ll, 1ll}, "Б"},
      {function_name, {"ЩФБШ", -4ll, 5ll}, "ЩФБШ"},
      {function_name, {"ЩФБШ", -3ll, 2ll}, "ФБ"},
      {function_name, {"ЩФБШ", -5ll, 3ll}, "ЩФБ"},
      {function_name, {"ЩФБШ", int64min, 1ll}, "Щ"},
      {function_name, {"ЩФБШ", int64max, 1ll}, ""},

      // substr(bytes, pos) -> bytes
      {function_name, {NullBytes(), 1ll}, NullBytes()},
      {function_name, {NullBytes(), 0ll}, NullBytes()},
      {function_name, {NullBytes(), -1ll}, NullBytes()},
      {function_name, {Bytes(""), 0ll}, Bytes("")},
      {function_name, {Bytes(""), 1ll}, Bytes("")},
      {function_name, {Bytes(""), -1ll}, Bytes("")},
      {function_name, {Bytes(""), NullInt64()}, NullBytes()},
      {function_name, {NullBytes(), NullInt64()}, NullBytes()},
      {function_name, {Bytes("abc"), 1ll}, Bytes("abc")},
      {function_name, {Bytes("abc"), 3ll}, Bytes("c")},
      {function_name, {Bytes("abc"), 4ll}, Bytes("")},
      {function_name, {Bytes("abc"), 0ll}, Bytes("abc")},
      {function_name, {Bytes("abc"), -2ll}, Bytes("bc")},
      {function_name, {Bytes("abc"), -5ll}, Bytes("abc")},
      {function_name, {Bytes("abc"), int64min}, Bytes("abc")},
      {function_name, {Bytes("abc"), int64max}, Bytes("")},
      {function_name, {Bytes("abc"), NullInt64()}, NullBytes()},
      {function_name, {Bytes("abc\0xyz"), 4ll}, Bytes("\0xyz")},
      {function_name, {Bytes("\xff\x80\xa0"), 2ll}, Bytes("\x80\xa0")},

      // substr(bytes, pos, length) -> bytes
      {function_name, {NullBytes(), NullInt64(), 1ll}, NullBytes()},
      {function_name, {NullBytes(), NullInt64(), 0ll}, NullBytes()},
      {function_name, {NullBytes(), NullInt64(), -1ll}, NullBytes()},
      {function_name, {NullBytes(), NullInt64(), NullInt64()}, NullBytes()},
      {function_name, {NullBytes(), 0ll, 1ll}, NullBytes()},
      {function_name, {NullBytes(), 0ll, 0ll}, NullBytes()},
      {function_name, {NullBytes(), 0ll, -1ll}, NullBytes()},
      {function_name, {NullBytes(), 0ll, NullInt64()}, NullBytes()},
      {function_name, {NullBytes(), 1ll, 1ll}, NullBytes()},
      {function_name, {NullBytes(), 1ll, 0ll}, NullBytes()},
      {function_name, {NullBytes(), 1ll, -1ll}, NullBytes()},
      {function_name, {NullBytes(), 1ll, NullInt64()}, NullBytes()},
      {function_name, {NullBytes(), -1ll, 1ll}, NullBytes()},
      {function_name, {NullBytes(), -1ll, 0ll}, NullBytes()},
      {function_name, {NullBytes(), -1ll, -1ll}, NullBytes()},
      {function_name, {NullBytes(), -1ll, NullInt64()}, NullBytes()},
      {function_name, {Bytes(""), NullInt64(), 1ll}, NullBytes()},
      {function_name, {Bytes(""), NullInt64(), 0ll}, NullBytes()},
      {function_name, {Bytes(""), NullInt64(), -1ll}, NullBytes()},
      {function_name, {Bytes(""), NullInt64(), NullInt64()}, NullBytes()},
      {function_name, {Bytes(""), 0ll, 1ll}, Bytes("")},
      {function_name, {Bytes(""), 0ll, 0ll}, Bytes("")},
      {function_name, {Bytes(""), 0ll, -1ll}, NullBytes(), OUT_OF_RANGE},
      {function_name, {Bytes(""), 0ll, NullInt64()}, NullBytes()},
      {function_name, {Bytes(""), 1ll, 1ll}, Bytes("")},
      {function_name, {Bytes(""), 1ll, 0ll}, Bytes("")},
      {function_name, {Bytes(""), 1ll, -1ll}, NullBytes(), OUT_OF_RANGE},
      {function_name, {Bytes(""), 1ll, NullInt64()}, NullBytes()},
      {function_name, {Bytes(""), -1ll, 1ll}, Bytes("")},
      {function_name, {Bytes(""), -1ll, 0ll}, Bytes("")},
      {function_name, {Bytes(""), -1ll, -1ll}, NullBytes(), OUT_OF_RANGE},
      {function_name, {Bytes(""), -1ll, NullInt64()}, NullBytes()},
      {function_name, {Bytes("abc"), 1ll, 2ll}, Bytes("ab")},
      {function_name, {Bytes("abc"), 3ll, 1ll}, Bytes("c")},
      {function_name, {Bytes("abc"), 3ll, 0ll}, Bytes("")},
      {function_name, {Bytes("abc"), 2ll, 1ll}, Bytes("b")},
      {function_name, {Bytes("abc"), 4ll, 5ll}, Bytes("")},
      {function_name, {Bytes("abc"), 1ll, int64max}, Bytes("abc")},
      {function_name, {Bytes("abc"), -2ll, 1ll}, Bytes("b")},
      {function_name, {Bytes("abc"), -5ll, 2ll}, Bytes("ab")},
      {function_name, {Bytes("abc"), -5ll, 0ll}, Bytes("")},
      {function_name, {Bytes("abc"), -5ll, -1ll}, NullBytes(), OUT_OF_RANGE},
      {function_name, {Bytes("abc"), int64min, 1ll}, Bytes("a")},
      {function_name, {Bytes("abc"), int64max, int64max}, Bytes("")},
      {function_name, {Bytes("abc"), NullInt64(), 1ll}, NullBytes()},
      {function_name, {Bytes("abc"), NullInt64(), 0ll}, NullBytes()},
      {function_name, {Bytes("abc"), NullInt64(), -1ll}, NullBytes()},
      {function_name, {Bytes("abc"), NullInt64(), NullInt64()}, NullBytes()},
      {function_name, {Bytes("abc"), 1ll, -1ll}, NullBytes(), OUT_OF_RANGE},
      {function_name, {Bytes("abc"), 1ll, int64min}, NullBytes(), OUT_OF_RANGE},
      {function_name, {Bytes("\xff\x80\xa0"), 2ll, 1ll}, Bytes("\x80")}};
  return results;
}

std::vector<FunctionTestCall> GetFunctionTestsLower(
    absl::string_view function_name) {
  absl::string_view all_bytes_str(
      "\x00\x01\x02\x03\x04\x05\x06\x07\x08\t\n\x0b\x0c\r\x0e\x0f\x10\x11\x12"
      "\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f !\" #$ %"
      "&\'()*+,-./"
      "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
      "abcdefghijklmnopqrstuvwxyz{|}~"
      "\x7f\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f\x90"
      "\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f\xa0\xa1\xa2"
      "\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xab\xac\xad\xae\xaf\xb0\xb1\xb2\xb3\xb4"
      "\xb5\xb6\xb7\xb8\xb9\xba\xbb\xbc\xbd\xbe\xbf\xc0\xc1\xc2\xc3\xc4\xc5\xc6"
      "\xc7\xc8\xc9\xca\xcb\xcc\xcd\xce\xcf\xd0\xd1\xd2\xd3\xd4\xd5\xd6\xd7\xd8"
      "\xd9\xda\xdb\xdc\xdd\xde\xdf\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea"
      "\xeb\xec\xed\xee\xef\xf0\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9\xfa\xfb\xfc"
      "\xfd\xfe\xff",
      256);
  absl::string_view all_bytes_lowercase(
      "\x00\x01\x02\x03\x04\x05\x06\x07\x08\t\n\x0b\x0c\r\x0e\x0f\x10\x11\x12"
      "\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f !\" #$ %"
      "&\'()*+,-./"
      "0123456789:;<=>?@abcdefghijklmnopqrstuvwxyz[\\]^_`"
      "abcdefghijklmnopqrstuvwxyz{|}~"
      "\x7f\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f\x90"
      "\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f\xa0\xa1\xa2"
      "\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xab\xac\xad\xae\xaf\xb0\xb1\xb2\xb3\xb4"
      "\xb5\xb6\xb7\xb8\xb9\xba\xbb\xbc\xbd\xbe\xbf\xc0\xc1\xc2\xc3\xc4\xc5\xc6"
      "\xc7\xc8\xc9\xca\xcb\xcc\xcd\xce\xcf\xd0\xd1\xd2\xd3\xd4\xd5\xd6\xd7\xd8"
      "\xd9\xda\xdb\xdc\xdd\xde\xdf\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea"
      "\xeb\xec\xed\xee\xef\xf0\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9\xfa\xfb\xfc"
      "\xfd\xfe\xff",
      256);
  return {
      // lower(string) -> string
      {function_name, {NullString()}, NullString()},
      {function_name, {""}, ""},
      // Ⱥ -> ⱥ (from 2 bytes to 3 bytes)
      {function_name, {"Ⱥ"}, "ⱥ"},  // Returns a longer string.

      {function_name, {"abcABCжщфЖЩФ"}, "abcabcжщфжщф"},
      // LOWER() on STRING works on both composed and decomposed forms,
      // i.e. lower("réSumÉ") == "résumé".
      {function_name, {"re\xCC\x81SumE\xCC\x81"}, "re\xCC\x81sume\xCC\x81"},
      {function_name, {"r\xc3\xa9SuM\xC3\x89"}, "r\xc3\xa9sum\xc3\xa9"},
      {function_name,
       {all_bytes_str.substr(0, 128)},
       all_bytes_lowercase.substr(0, 128)},

      // Σ has two lower-case forms: σ and ς. ς is used when it is at the end
      // of a word.
      {function_name, {"aBcΣ"}, "abcς"},
      {function_name, {"ΣaBc"}, "σabc"},
      {function_name, {"aΣBc"}, "aσbc"},
      {function_name, {"Σ"}, "σ"},
      // lower(bytes) -> bytes
      {function_name, {NullBytes()}, NullBytes()},
      {function_name, {Bytes("")}, Bytes("")},
      {function_name, {Bytes("abcABCжщфЖЩФ")}, Bytes("abcabcжщфЖЩФ")},
      {function_name, {Bytes(all_bytes_str)}, Bytes(all_bytes_lowercase)},
      // The composed forms for 'é' and 'É' stay unchanged if passed into
      // LOWER() on BYTES. The decomposed forms become upper case.
      {function_name,
       {Bytes("re\xCC\x81SumE\xCC\x81")},
       Bytes("re\xCC\x81sume\xCC\x81")},
      {function_name,
       {Bytes("r\xc3\xa9SuM\xC3\x89")},
       Bytes("r\xc3\xa9sum\xc3\x89")},
  };
}

std::vector<FunctionTestCall> GetFunctionTestsUpper(
    absl::string_view function_name) {
  absl::string_view all_bytes_str(
      "\x00\x01\x02\x03\x04\x05\x06\x07\x08\t\n\x0b\x0c\r\x0e\x0f\x10\x11\x12"
      "\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f !\" #$ %"
      "&\'()*+,-./"
      "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
      "abcdefghijklmnopqrstuvwxyz{|}~"
      "\x7f\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f\x90"
      "\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f\xa0\xa1\xa2"
      "\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xab\xac\xad\xae\xaf\xb0\xb1\xb2\xb3\xb4"
      "\xb5\xb6\xb7\xb8\xb9\xba\xbb\xbc\xbd\xbe\xbf\xc0\xc1\xc2\xc3\xc4\xc5\xc6"
      "\xc7\xc8\xc9\xca\xcb\xcc\xcd\xce\xcf\xd0\xd1\xd2\xd3\xd4\xd5\xd6\xd7\xd8"
      "\xd9\xda\xdb\xdc\xdd\xde\xdf\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea"
      "\xeb\xec\xed\xee\xef\xf0\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9\xfa\xfb\xfc"
      "\xfd\xfe\xff",
      256);
  absl::string_view all_bytes_uppercase(
      "\x00\x01\x02\x03\x04\x05\x06\x07\x08\t\n\x0b\x0c\r\x0e\x0f\x10\x11\x12"
      "\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f !\" #$ %"
      "&\'()*+,-./"
      "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ{|}~"
      "\x7f\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f\x90"
      "\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f\xa0\xa1\xa2"
      "\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xab\xac\xad\xae\xaf\xb0\xb1\xb2\xb3\xb4"
      "\xb5\xb6\xb7\xb8\xb9\xba\xbb\xbc\xbd\xbe\xbf\xc0\xc1\xc2\xc3\xc4\xc5\xc6"
      "\xc7\xc8\xc9\xca\xcb\xcc\xcd\xce\xcf\xd0\xd1\xd2\xd3\xd4\xd5\xd6\xd7\xd8"
      "\xd9\xda\xdb\xdc\xdd\xde\xdf\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea"
      "\xeb\xec\xed\xee\xef\xf0\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9\xfa\xfb\xfc"
      "\xfd\xfe\xff",
      256);
  return {
      // upper(string) -> string
      {function_name, {NullString()}, NullString()},
      {function_name, {""}, ""},
      {function_name, {"ß"}, "SS"},  // Returns a longer string.
      // ⱥ -> Ⱥ (from 3 bytes to 2 bytes)
      {function_name, {"ⱥ"}, "Ⱥ"},  // Returns a shorter string.
      {function_name, {"abcABCжщфЖЩФ"}, "ABCABCЖЩФЖЩФ"},
      // 'é' has two Unicode representations: composed and decomposed. The
      // encodings for these representations are 'C3 A9' and '65 CC 81',
      // respectively. Similarly, the encodings for 'É' are 'C3 89' and
      // '45 CC 81', respectively.
      // UPPER() on STRING works on both composed and decomposed forms,
      // i.e. upper("réSumÉ") == "RÉSUMÉ".
      {function_name, {"re\xCC\x81SumE\xCC\x81"}, "RE\xCC\x81SUME\xCC\x81"},
      {function_name, {"r\xc3\xa9Sum\xC3\x89"}, "R\xc3\x89SUM\xc3\x89"},
      {function_name,
       {all_bytes_str.substr(0, 128)},
       all_bytes_uppercase.substr(0, 128)},

      // upper(bytes) -> bytes
      {function_name, {NullBytes()}, NullBytes()},
      {function_name, {Bytes("")}, Bytes("")},
      {function_name, {Bytes("abcABCжщфЖЩФ")}, Bytes("ABCABCжщфЖЩФ")},
      {function_name, {Bytes(all_bytes_str)}, Bytes(all_bytes_uppercase)},
      // The composed forms for 'é' and 'É' stay unchanged if passed into
      // UPPER() on BYTES. The decomposed forms become upper case.
      {function_name,
       {Bytes("re\xCC\x81SumE\xCC\x81")},
       Bytes("RE\xCC\x81SUME\xCC\x81")},
      {function_name,
       {Bytes("r\xc3\xa9Sum\xC3\x89")},
       Bytes("R\xc3\xa9SUM\xc3\x89")},
  };
}

std::vector<FunctionTestCall> GetFunctionTestsSubstring() {
  return GetFunctionTestsSubstr("substring");
}

std::vector<FunctionTestCall> GetFunctionTestsString() {
  std::vector<FunctionTestCall> results = {
      // strpos(string, string) -> int64
      {"strpos", {NullString(), ""}, NullInt64()},
      {"strpos", {NullString(), "x"}, NullInt64()},
      {"strpos", {"", NullString()}, NullInt64()},
      {"strpos", {NullString(), NullString()}, NullInt64()},
      {"strpos", {"", "x"}, 0ll},
      {"strpos", {"x", ""}, 1ll},
      {"strpos", {"x", NullString()}, NullInt64()},
      {"strpos", {"", ""}, 1ll},
      {"strpos", {"xxx", "x"}, 1ll},
      {"strpos", {"abcdef", "cd"}, 3ll},
      {"strpos", {"abcdefabcdef", "de"}, 4ll},
      {"strpos", {"abcdefabcdef", "xz"}, 0ll},
      {"strpos", {"\0abcedf", "abc"}, 2ll},
      {"strpos", {"abca\0b\0c\0", "a\0b\0c"}, 4ll},
      {"strpos", {"zгдl", "дl"}, 3ll},

      // strpos(bytes, bytes) -> int64
      {"strpos", {NullBytes(), Bytes("")}, NullInt64()},
      {"strpos", {NullBytes(), Bytes("x")}, NullInt64()},
      {"strpos", {NullBytes(), NullBytes()}, NullInt64()},
      {"strpos", {Bytes(""), NullBytes()}, NullInt64()},
      {"strpos", {Bytes(""), Bytes("x")}, 0ll},
      {"strpos", {Bytes(""), Bytes("")}, 1ll},
      {"strpos", {Bytes("x"), Bytes("")}, 1ll},
      {"strpos", {Bytes("x"), NullBytes()}, NullInt64()},
      {"strpos", {Bytes("xxx"), Bytes("x")}, 1ll},
      {"strpos", {Bytes("abcdef"), Bytes("cd")}, 3ll},
      {"strpos", {Bytes("abcdefabcdef"), Bytes("de")}, 4ll},
      {"strpos", {Bytes("abcdefabcdef"), Bytes("xz")}, 0ll},
      {"strpos", {Bytes("\0abcedf"), Bytes("abc")}, 2ll},
      {"strpos", {Bytes("abca\0b\0c\0"), Bytes("a\0b\0c")}, 4ll},
      {"strpos", {Bytes("zгдl"), Bytes("дl")}, 4ll},

      // starts_with(string, string) -> bool
      {"starts_with", {NullString(), ""}, NullBool()},
      {"starts_with", {NullString(), "a"}, NullBool()},
      {"starts_with", {"", NullString()}, NullBool()},
      {"starts_with", {"starts_with", NullString()}, NullBool()},
      {"starts_with", {NullString(), NullString()}, NullBool()},
      {"starts_with", {"", ""}, true},
      {"starts_with", {"", "a"}, false},
      {"starts_with", {"starts_with", "starts"}, true},
      {"starts_with", {"starts_with", "starts_with"}, true},
      {"starts_with", {"starts_with", "ends"}, false},
      {"starts_with", {"starts_with", "with"}, false},
      {"starts_with", {"starts_with", "starts_with_extra"}, false},
      {"starts_with", {"starts_with", ""}, true},
      {"starts_with", {"абвгд", "абв"}, true},
      {"starts_with", {"абв", "абвг"}, false},

      // starts_with(bytes, bytes) -> bool
      {"starts_with", {NullBytes(), Bytes("")}, NullBool()},
      {"starts_with", {Bytes(""), NullBytes()}, NullBool()},
      {"starts_with", {Bytes("starts_with"), NullBytes()}, NullBool()},
      {"starts_with", {NullBytes(), NullBytes()}, NullBool()},
      {"starts_with", {Bytes(""), Bytes("")}, true},
      {"starts_with", {Bytes(""), Bytes("a")}, false},
      {"starts_with", {Bytes("starts_with"), Bytes("starts")}, true},
      {"starts_with", {Bytes("starts_with"), Bytes("starts_with")}, true},
      {"starts_with",
       {Bytes("starts_with"), Bytes("starts_with_extra")},
       false},
      {"starts_with", {Bytes("starts_with"), Bytes("ends")}, false},
      {"starts_with", {Bytes("starts_with"), Bytes("with")}, false},
      {"starts_with", {Bytes("starts_with"), Bytes("")}, true},

      // ends_with(string, string) -> bool
      {"ends_with", {NullString(), ""}, NullBool()},
      {"ends_with", {"", NullString()}, NullBool()},
      {"ends_with", {"ends_with", NullString()}, NullBool()},
      {"ends_with", {NullString(), NullString()}, NullBool()},
      {"ends_with", {"ends_with", "with"}, true},
      {"ends_with", {"ends_with", "ends_with"}, true},
      {"ends_with", {"ends_with", "extra_ends_with"}, false},
      {"ends_with", {"ends_with", "ends"}, false},
      {"ends_with", {"ends_with", "with"}, true},
      {"ends_with", {"ends_with", ""}, true},
      {"ends_with", {"абвгд", "вгд"}, true},
      {"ends_with", {"бв", "aбв"}, false},

      // ends_with(bytes, bytes) -> bool
      {"ends_with", {NullBytes(), Bytes("")}, NullBool()},
      {"ends_with", {Bytes("ends_with"), NullBytes()}, NullBool()},
      {"ends_with", {Bytes(""), NullBytes()}, NullBool()},
      {"ends_with", {NullBytes(), NullBytes()}, NullBool()},
      {"ends_with", {Bytes("ends_with"), Bytes("with")}, true},
      {"ends_with", {Bytes("ends_with"), Bytes("ends_with")}, true},
      {"ends_with", {Bytes("ends_with"), Bytes("extra_ends_with")}, false},
      {"ends_with", {Bytes("ends_with"), Bytes("ends")}, false},
      {"ends_with", {Bytes("ends_with"), Bytes("with")}, true},
      {"ends_with", {Bytes("ends_with"), Bytes("")}, true},

      // length(string) -> int64
      {"length", {NullString()}, NullInt64()},
      {"length", {""}, 0ll},
      {"length", {"abcde"}, 5ll},
      {"length", {"абвгд"}, 5ll},
      {"length", {"\0\0"}, 2ll},

      // length(bytes) -> int64
      {"length", {NullBytes()}, NullInt64()},
      {"length", {Bytes("")}, 0ll},
      {"length", {Bytes("abcde")}, 5ll},
      {"length", {Bytes("абвгд")}, 10ll},
      {"length", {Bytes("\0\0")}, 2ll},

      // byte_length(string) -> int64
      {"byte_length", {NullString()}, NullInt64()},
      {"byte_length", {""}, 0ll},
      {"byte_length", {"abcde"}, 5ll},
      {"byte_length", {"абвгд"}, 10ll},
      {"byte_length", {"\0\0"}, 2ll},

      // byte_length(bytes) -> int64
      {"byte_length", {NullBytes()}, NullInt64()},
      {"byte_length", {Bytes("")}, 0ll},
      {"byte_length", {Bytes("abcde")}, 5ll},
      {"byte_length", {Bytes("абвгд")}, 10ll},
      {"byte_length", {Bytes("\0\0")}, 2ll},

      // char_length(string) -> int64
      {"char_length", {NullString()}, NullInt64()},
      {"character_length", {""}, 0ll},
      {"char_length", {"abcde"}, 5ll},
      {"character_length", {"абвгд"}, 5ll},
      {"char_length", {"\0\0"}, 2ll},

      // trim(bytes, bytes) -> bytes
      {"trim", {NullBytes(), Bytes("")}, NullBytes()},
      {"trim", {NullBytes(), Bytes("a")}, NullBytes()},
      {"trim", {Bytes(""), NullBytes()}, NullBytes()},
      {"trim", {Bytes("a"), NullBytes()}, NullBytes()},
      {"trim", {NullBytes(), NullBytes()}, NullBytes()},
      {"trim", {Bytes(""), Bytes("")}, Bytes("")},
      {"trim", {Bytes(""), Bytes("a")}, Bytes("")},
      {"trim", {Bytes("  abc  "), Bytes("")}, Bytes("  abc  ")},
      {"trim", {Bytes("  abc  "), Bytes(" ")}, Bytes("abc")},
      {"trim", {Bytes("  a b c  "), Bytes(" ")}, Bytes("a b c")},
      {"trim", {Bytes("  abc  "), Bytes(" cba")}, Bytes("")},
      {"trim", {Bytes("  abc  "), Bytes(" edcba")}, Bytes("")},
      {"trim", {Bytes("a \0xyza \0"), Bytes("\0a ")}, Bytes("xyz")},
      {"trim",
       {Bytes("\xFF\x80\x81xyz\x81\x80\xFF"), Bytes("\x80\xFF")},
       Bytes("\x81xyz\x81")},

      // ltrim(bytes, bytes) -> bytes
      {"ltrim", {NullBytes(), Bytes("")}, NullBytes()},
      {"ltrim", {NullBytes(), Bytes("a")}, NullBytes()},
      {"ltrim", {Bytes(""), NullBytes()}, NullBytes()},
      {"ltrim", {Bytes("a"), NullBytes()}, NullBytes()},
      {"ltrim", {NullBytes(), NullBytes()}, NullBytes()},
      {"ltrim", {Bytes(""), Bytes("")}, Bytes("")},
      {"ltrim", {Bytes(""), Bytes("a")}, Bytes("")},
      {"ltrim", {Bytes("  abc  "), Bytes("")}, Bytes("  abc  ")},
      {"ltrim", {Bytes("  abc  "), Bytes(" ")}, Bytes("abc  ")},
      {"ltrim", {Bytes("  a b c  "), Bytes(" ")}, Bytes("a b c  ")},
      {"ltrim", {Bytes("  abc  "), Bytes(" cba")}, Bytes("")},
      {"ltrim", {Bytes("  abc  "), Bytes(" edcba")}, Bytes("")},
      {"ltrim", {Bytes("a \0xyza \0"), Bytes("\0a ")}, Bytes("xyza \0")},
      {"ltrim",
       {Bytes("\xFF\x80\x81xyz\x81\x80\xFF"), Bytes("\x80\xFF")},
       Bytes("\x81xyz\x81\x80\xFF")},

      // rtrim(bytes, bytes) -> bytes
      {"rtrim", {NullBytes(), Bytes("")}, NullBytes()},
      {"rtrim", {NullBytes(), Bytes("a")}, NullBytes()},
      {"rtrim", {Bytes(""), NullBytes()}, NullBytes()},
      {"rtrim", {Bytes("a"), NullBytes()}, NullBytes()},
      {"rtrim", {NullBytes(), NullBytes()}, NullBytes()},
      {"rtrim", {Bytes(""), Bytes("")}, Bytes("")},
      {"rtrim", {Bytes(""), Bytes("a")}, Bytes("")},
      {"rtrim", {Bytes("  abc  "), Bytes("")}, Bytes("  abc  ")},
      {"rtrim", {Bytes("  abc  "), Bytes(" ")}, Bytes("  abc")},
      {"rtrim", {Bytes("  a b c  "), Bytes(" ")}, Bytes("  a b c")},
      {"rtrim", {Bytes("  abc  "), Bytes(" cba")}, Bytes("")},
      {"rtrim", {Bytes("  abc  "), Bytes(" edcba")}, Bytes("")},
      {"rtrim", {Bytes("a \0xyza \0"), Bytes("\0a ")}, Bytes("a \0xyz")},
      {"rtrim",
       {Bytes("\xFF\x80\x81xyz\x81\x80\xFF"), Bytes("\x80\xFF")},
       Bytes("\xFF\x80\x81xyz\x81")},

      // trim(string) -> string
      {"trim", {NullString()}, NullString()},
      {"trim", {""}, ""},
      {"trim", {"   "}, ""},
      {"trim", {"   abc   "}, "abc"},
      {"trim", {"   a b c   "}, "a b c"},
      {"trim", {"   ыфщ   "}, "ыфщ"},
      {"trim", {"\t\r\n abc\t\n\r "}, "abc"},
      // The following string encodes all 25 space characters defined by
      // Unicode.
      {"trim",
       {"\u0009\u000A\u000B\u000C\u000D\u0020\u0085\u00A0\u1680\u2000"
        "\u2001\u2002\u2003\u2004\u2005\u2006\u2007\u2008\u2009\u200A"
        "\u2028\u2029\u202F\u205Fabc\u0009\u000A\u000B\u000C\u000D\u0020"
        "\u0085\u00A0\u1680\u2000\u2001\u2002\u2003\u2004\u2005\u2006"
        "\u2007\u2008\u2009\u200A\u2028\u2029\u202F\u205F"},
       "abc"},

      // ltrim(string) -> string
      {"ltrim", {NullString()}, NullString()},
      {"ltrim", {""}, ""},
      {"ltrim", {"   "}, ""},
      {"ltrim", {"   abc   "}, "abc   "},
      {"ltrim", {"   a b c   "}, "a b c   "},
      {"ltrim", {"   ыфщ   "}, "ыфщ   "},
      {"ltrim", {"\t\r\n abc\t\n\r "}, "abc\t\n\r "},
      {"ltrim",
       {"\u0009\u000A\u000B\u000C\u000D\u0020\u0085\u00A0\u1680\u2000"
        "\u2001\u2002\u2003\u2004\u2005\u2006\u2007\u2008\u2009\u200A"
        "\u2028\u2029\u202F\u205Fabc"},
       "abc"},

      // rtrim(string) -> string
      {"rtrim", {NullString()}, NullString()},
      {"rtrim", {""}, ""},
      {"rtrim", {"   "}, ""},
      {"rtrim", {"   abc   "}, "   abc"},
      {"rtrim", {"   a b c   "}, "   a b c"},
      {"rtrim", {"   ыфщ   "}, "   ыфщ"},
      {"rtrim", {"\t\r\n abc\t\n\r "}, "\t\r\n abc"},
      {"rtrim",
       {"abc\u0009\u000A\u000B\u000C\u000D\u0020\u0085\u00A0\u1680\u2000"
        "\u2001\u2002\u2003\u2004\u2005\u2006\u2007\u2008\u2009\u200A"
        "\u2028\u2029\u202F\u205F"},
       "abc"},

      // trim(string, string) -> string
      {"trim", {NullString(), ""}, NullString()},
      {"trim", {NullString(), "a"}, NullString()},
      {"trim", {"", NullString()}, NullString()},
      {"trim", {"a", NullString()}, NullString()},
      {"trim", {NullString(), NullString()}, NullString()},
      {"trim", {"", ""}, ""},
      {"trim", {"", "a"}, ""},
      {"trim", {"  abc  ", ""}, "  abc  "},
      {"trim", {"", " "}, ""},
      {"trim", {"   ", " "}, ""},
      {"trim", {"\0   \0", "\0"}, "   "},
      {"trim", {"   abc   ", " "}, "abc"},
      {"trim", {"\nabc\n", " "}, "\nabc\n"},
      {"trim", {"\nabc\n", "\n"}, "abc"},
      {"trim", {"\tabc\t", " "}, "\tabc\t"},
      {"trim", {"\tabc\t", "\t"}, "abc"},
      {"trim", {"   abc   ", " cba"}, ""},
      {"trim", {"   a b c   ", " cba"}, ""},
      {"trim", {"   ыфщ   ", " "}, "ыфщ"},
      {"trim", {"   ыфabcщ   ", " ыфщ"}, "abc"},
      // The explicitly specified REPLACEMENT CHARACTER is allowed as a trim
      // pattern and should be handled as any other unicode character.
      {"trim", {"a\ufffdb\ufffdxyz\ufffdb\ufffda", "ab\ufffd"}, "xyz"},

      // ltrim(string, string) -> string
      {"ltrim", {NullString(), ""}, NullString()},
      {"ltrim", {"", " "}, ""},
      {"ltrim", {"", ""}, ""},
      {"ltrim", {"  abc  ", ""}, "  abc  "},
      {"ltrim", {"\0   \0", "\0"}, "   \0"},
      {"ltrim", {"   ", " "}, ""},
      {"ltrim", {"   abc   ", " "}, "abc   "},
      {"ltrim", {"\nabc\n", " "}, "\nabc\n"},
      {"ltrim", {"\nabc\n", "\n"}, "abc\n"},
      {"ltrim", {"\tabc\t", " "}, "\tabc\t"},
      {"ltrim", {"\tabc\t", "\t"}, "abc\t"},
      {"ltrim", {"   abc   ", " cba"}, ""},
      {"ltrim", {"   a b c   ", " cba"}, ""},
      {"ltrim", {"   ыфщ   ", " "}, "ыфщ   "},
      {"ltrim", {"   ыфabcщ   ", " ыфщ"}, "abcщ   "},
      // The explicitly specified REPLACEMENT CHARACTER is allowed as a trim
      // input
      // and should be handled as any other unicode character.
      {"ltrim",
       {"a\ufffdb\ufffdxyz\ufffdb\ufffda", "ab\ufffd"},
       "xyz\ufffdb\ufffda"},

      // rtrim(string, string) -> string
      {"rtrim", {NullString(), ""}, NullString()},
      {"rtrim", {NullString(), "a"}, NullString()},
      {"rtrim", {"", NullString()}, NullString()},
      {"rtrim", {"a", NullString()}, NullString()},
      {"rtrim", {NullString(), NullString()}, NullString()},
      {"rtrim", {"", ""}, ""},
      {"rtrim", {"", " "}, ""},
      {"rtrim", {"  abc  ", ""}, "  abc  "},
      {"rtrim", {"\0   \0", "\0"}, "\0   "},
      {"rtrim", {"   ", " "}, ""},
      {"rtrim", {"   abc   ", " "}, "   abc"},
      {"rtrim", {"\nabc\n", " "}, "\nabc\n"},
      {"rtrim", {"\nabc\n", "\n"}, "\nabc"},
      {"rtrim", {"\tabc\t", " "}, "\tabc\t"},
      {"rtrim", {"\tabc\t", "\t"}, "\tabc"},
      {"rtrim", {"   abc   ", " cba"}, ""},
      {"rtrim", {"   a b c   ", " cba"}, ""},
      {"rtrim", {"   ыфщ   ", " "}, "   ыфщ"},
      {"rtrim", {"   ыфabcщ   ", " ыфщ"}, "   ыфabc"},
      // The explicitly specified REPLACEMENT CHARACTER is allowed as a trim
      // input
      // and should be handled as any other unicode character.
      {"rtrim",
       {"a\ufffdb\ufffdxyz\ufffdb\ufffda", "ab\ufffd"},
       "a\ufffdb\ufffdxyz"},

      // left(string, length) -> string
      {"left", {NullString(), 1ll}, NullString()},
      {"left", {NullString(), 0ll}, NullString()},
      {"left", {NullString(), -1ll}, NullString()},
      {"left", {NullString(), NullInt64()}, NullString()},
      {"left", {"", 1ll}, ""},
      {"left", {"", 0ll}, ""},
      {"left", {"", -1ll}, NullString(), OUT_OF_RANGE},
      {"left", {"", NullInt64()}, NullString()},
      {"left", {"abc", 0ll}, ""},
      {"left", {"abc", 1ll}, "a"},
      {"left", {"abc", -1ll}, NullString(), OUT_OF_RANGE},
      {"left", {"abc", NullInt64()}, NullString()},
      {"left", {"abc", 2ll}, "ab"},
      {"left", {"abc", 3ll}, "abc"},
      {"left", {"abc", int64max}, "abc"},
      {"left", {"abc", -5ll}, NullString(), OUT_OF_RANGE},
      {"left", {"abc", int64min}, NullString(), OUT_OF_RANGE},
      {"left", {"ЩФБШ", NullInt64()}, NullString()},
      {"left", {"ЩФБШ", 0ll}, ""},
      {"left", {"ЩФБШ", 2ll}, "ЩФ"},
      {"left", {"ЩФБШ", 5ll}, "ЩФБШ"},
      {"left", {"ЩФБШ", int64max}, "ЩФБШ"},

      // left(bytes, length) -> bytes
      {"left", {NullBytes(), 1ll}, NullBytes()},
      {"left", {NullBytes(), 0ll}, NullBytes()},
      {"left", {NullBytes(), -1ll}, NullBytes()},
      {"left", {NullBytes(), NullInt64()}, NullBytes()},
      {"left", {Bytes(""), 1ll}, Bytes("")},
      {"left", {Bytes(""), 0ll}, Bytes("")},
      {"left", {Bytes(""), -1ll}, NullBytes(), OUT_OF_RANGE},
      {"left", {Bytes(""), NullInt64()}, NullBytes()},
      {"left", {Bytes("abc"), 0ll}, Bytes("")},
      {"left", {Bytes("abc"), 1ll}, Bytes("a")},
      {"left", {Bytes("abc"), -1ll}, NullBytes(), OUT_OF_RANGE},
      {"left", {Bytes("abc"), NullInt64()}, NullBytes()},
      {"left", {Bytes("abc"), 2ll}, Bytes("ab")},
      {"left", {Bytes("abc"), 3ll}, Bytes("abc")},
      {"left", {Bytes("abc"), int64max}, Bytes("abc")},
      {"left", {Bytes("abc"), -5ll}, NullBytes(), OUT_OF_RANGE},
      {"left", {Bytes("abc"), int64min}, NullBytes(), OUT_OF_RANGE},
      {"left", {Bytes("\xff\x80\xa0"), 2ll}, Bytes("\xff\x80")},

      // right(string, length) -> string
      {"right", {NullString(), 1ll}, NullString()},
      {"right", {NullString(), 0ll}, NullString()},
      {"right", {NullString(), -1ll}, NullString()},
      {"right", {NullString(), NullInt64()}, NullString()},
      {"right", {"", 1ll}, ""},
      {"right", {"", 0ll}, ""},
      {"right", {"", -1ll}, NullString(), OUT_OF_RANGE},
      {"right", {"", NullInt64()}, NullString()},
      {"right", {"abc", 0ll}, ""},
      {"right", {"abc", 1ll}, "c"},
      {"right", {"abc", -1ll}, NullString(), OUT_OF_RANGE},
      {"right", {"abc", NullInt64()}, NullString()},
      {"right", {"abc", 2ll}, "bc"},
      {"right", {"abc", 3ll}, "abc"},
      {"right", {"abc", int64max}, "abc"},
      {"right", {"abc", -5ll}, NullString(), OUT_OF_RANGE},
      {"right", {"abc", int64min}, NullString(), OUT_OF_RANGE},
      {"right", {"ЩФБШ", NullInt64()}, NullString()},
      {"right", {"ЩФБШ", 0ll}, ""},
      {"right", {"ЩФБШ", 2ll}, "БШ"},
      {"right", {"ЩФБШ", 5ll}, "ЩФБШ"},
      {"right", {"ЩФБШ", int64max}, "ЩФБШ"},

      // right(bytes, length) -> bytes
      {"right", {NullBytes(), 1ll}, NullBytes()},
      {"right", {NullBytes(), 0ll}, NullBytes()},
      {"right", {NullBytes(), -1ll}, NullBytes()},
      {"right", {NullBytes(), NullInt64()}, NullBytes()},
      {"right", {Bytes(""), 1ll}, Bytes("")},
      {"right", {Bytes(""), 0ll}, Bytes("")},
      {"right", {Bytes(""), -1ll}, NullBytes(), OUT_OF_RANGE},
      {"right", {Bytes(""), NullInt64()}, NullBytes()},
      {"right", {Bytes("abc"), 0ll}, Bytes("")},
      {"right", {Bytes("abc"), 1ll}, Bytes("c")},
      {"right", {Bytes("abc"), -1ll}, NullBytes(), OUT_OF_RANGE},
      {"right", {Bytes("abc"), NullInt64()}, NullBytes()},
      {"right", {Bytes("abc"), 2ll}, Bytes("bc")},
      {"right", {Bytes("abc"), 3ll}, Bytes("abc")},
      {"right", {Bytes("abc"), int64max}, Bytes("abc")},
      {"right", {Bytes("abc"), -5ll}, NullBytes(), OUT_OF_RANGE},
      {"right", {Bytes("abc"), int64min}, NullBytes(), OUT_OF_RANGE},
      {"right", {Bytes("\xff\x80\xa0"), 2ll}, Bytes("\x80\xa0")},

      // replace(string, string, string) -> string
      {"replace", {NullString(), "", ""}, NullString()},
      {"replace", {NullString(), NullString(), ""}, NullString()},
      {"replace", {NullString(), "", NullString()}, NullString()},
      {"replace", {NullString(), NullString(), NullString()}, NullString()},
      {"replace", {"", NullString(), ""}, NullString()},
      {"replace", {"", "", NullString()}, NullString()},
      {"replace", {"", NullString(), NullString()}, NullString()},
      {"replace", {"", "", ""}, ""},
      {"replace", {"", "a", ""}, ""},
      {"replace", {"", "", "a"}, ""},
      {"replace", {"abc", "", "xyz"}, "abc"},
      {"replace", {"abc", "", ""}, "abc"},
      {"replace", {"abc", "b", "xyz"}, "axyzc"},
      {"replace", {"abc", "b", ""}, "ac"},
      {"replace", {"abcabc", "bc", "xyz"}, "axyzaxyz"},
      {"replace", {"abc", "abc", "xyz"}, "xyz"},
      {"replace", {"abc", "abcde", "xyz"}, "abc"},
      {"replace", {"banana", "ana", "xyz"}, "bxyzna"},
      {"replace", {"banana", "ana", ""}, "bna"},
      {"replace", {"banana", "a", "z"}, "bznznz"},
      {"replace", {"щцфшцф", "ф", "ы"}, "щцышцы"},

      // replace(bytes, bytes, bytes) -> bytes
      {"replace", {NullBytes(), Bytes(""), Bytes("")}, NullBytes()},
      {"replace", {NullBytes(), Bytes(""), NullBytes()}, NullBytes()},
      {"replace", {NullBytes(), NullBytes(), Bytes("")}, NullBytes()},
      {"replace", {NullBytes(), NullBytes(), NullBytes()}, NullBytes()},
      {"replace", {Bytes(""), NullBytes(), Bytes("")}, NullBytes()},
      {"replace", {Bytes(""), Bytes(""), NullBytes()}, NullBytes()},
      {"replace", {Bytes(""), NullBytes(), NullBytes()}, NullBytes()},
      {"replace", {Bytes(""), Bytes(""), Bytes("")}, Bytes("")},
      {"replace", {Bytes(""), Bytes("a"), Bytes("")}, Bytes("")},
      {"replace", {Bytes(""), Bytes(""), Bytes("a")}, Bytes("")},
      {"replace", {Bytes("abc"), Bytes(""), Bytes("xyz")}, Bytes("abc")},
      {"replace", {Bytes("abc"), Bytes(""), Bytes("")}, Bytes("abc")},
      {"replace", {Bytes("abc"), Bytes("b"), Bytes("xyz")}, Bytes("axyzc")},
      {"replace", {Bytes("abc"), Bytes("b"), Bytes("")}, Bytes("ac")},
      {"replace",
       {Bytes("abcabc"), Bytes("bc"), Bytes("xyz")},
       Bytes("axyzaxyz")},
      {"replace", {Bytes("abc"), Bytes("abc"), Bytes("xyz")}, Bytes("xyz")},
      {"replace",
       {Bytes("banana"), Bytes("ana"), Bytes("xyz")},
       Bytes("bxyzna")},
      {"replace", {Bytes("banana"), Bytes("a"), Bytes("z")}, Bytes("bznznz")},
      {"replace", {Bytes("banana"), Bytes("ana"), Bytes("")}, Bytes("bna")},

      // safe_convert_bytes_to_string(bytes) -> string
      // Valid UTF-8 string remains unchanged.
      {"safe_convert_bytes_to_string", {NullBytes()}, NullString()},
      {"safe_convert_bytes_to_string", {Bytes("")}, String("")},
      {"safe_convert_bytes_to_string", {Bytes("abcABC")}, "abcABC"},
      {"safe_convert_bytes_to_string", {Bytes("abcABCжщфЖЩФ")}, "abcABCжщфЖЩФ"},
      {"safe_convert_bytes_to_string", {Bytes("абвгд")}, "абвгд"},
      {"safe_convert_bytes_to_string", {Bytes("₡")}, "₡"},
      {"safe_convert_bytes_to_string", {Bytes("𐌼")}, "𐌼"},
      {"safe_convert_bytes_to_string",
       {Bytes("𐌼₡𐌼₡𐌼₡𐌼₡𐌼₡𐌼₡𐌼₡𐌼₡𐌼₡")},
       "𐌼₡𐌼₡𐌼₡𐌼₡𐌼₡𐌼₡𐌼₡𐌼₡𐌼₡"},

  // Invalid UTF-8 characters are replaced with U+FFFD.
#define U_FFFD "\xef\xbf\xbd"
      // Invalid 1 octet.
      {"safe_convert_bytes_to_string", {Bytes("\xc2")}, U_FFFD},
      {"safe_convert_bytes_to_string", {Bytes("\xd0")}, U_FFFD},
      // Invalid 2 octet sequence.
      {"safe_convert_bytes_to_string", {Bytes("\xc3\x28")}, U_FFFD "("},
      {"safe_convert_bytes_to_string", {Bytes("\xa0\xa1")}, U_FFFD U_FFFD},
      // Invalid 3 octet sequence.
      {"safe_convert_bytes_to_string",
       {Bytes("\xe2\x28\xa1")},
       U_FFFD "(" U_FFFD},
      {"safe_convert_bytes_to_string",
       {Bytes("\xe2\x82\x28")},
       U_FFFD U_FFFD "("},
      // Valid 3 octet sequence followed by invalid continuation bytes.
      {"safe_convert_bytes_to_string",
       {Bytes("\xe0\xa4\x88\xe2\x28")},
       "ई" U_FFFD "("},
      // Invalid 4 octet sequence.
      {"safe_convert_bytes_to_string",
       {Bytes("\xf0\x28\x8c\xbc")},
       U_FFFD "(" U_FFFD U_FFFD},
      {"safe_convert_bytes_to_string",
       {Bytes("\xf0\x90\x28\xbc")},
       U_FFFD U_FFFD "(" U_FFFD},
      {"safe_convert_bytes_to_string",
       {Bytes("\xf0\x28\x8c\x28")},
       U_FFFD "(" U_FFFD "("},
      // Valid 5, 6 octet sequence but not Unicode (UTF-8 was restricted by
      // RFC 3629 to end at U+10FFFF).
      {"safe_convert_bytes_to_string",
       {Bytes("\xf8\xa1\xa1\xa1\xa1")},
       U_FFFD U_FFFD U_FFFD U_FFFD U_FFFD},
      {"safe_convert_bytes_to_string",
       {Bytes("\xfc\xa1\xa1\xa1\xa1\xa1")},
       U_FFFD U_FFFD U_FFFD U_FFFD U_FFFD U_FFFD},
      // Mix of valid and invalid chars.
      {"safe_convert_bytes_to_string",
       {Bytes("𐌼₡\xc2𐌼₡\xa0\xa1𐌼₡\xf0\x28\x8c\x28𐌼₡")},
       "𐌼₡" U_FFFD "𐌼₡" U_FFFD U_FFFD "𐌼₡" U_FFFD "(" U_FFFD "(𐌼₡"},
#undef U_FFFD
  };
  std::vector<FunctionTestCall> tests_substr = GetFunctionTestsSubstr("substr");
  results.insert(results.end(), std::make_move_iterator(tests_substr.begin()),
                 std::make_move_iterator(tests_substr.end()));
  std::vector<FunctionTestCall> tests_concat = GetStringConcatTests();
  results.insert(results.end(), std::make_move_iterator(tests_concat.begin()),
                 std::make_move_iterator(tests_concat.end()));
  std::vector<FunctionTestCall> tests_lcase = GetFunctionTestsLower("lower");
  results.insert(results.end(), std::make_move_iterator(tests_lcase.begin()),
                 std::make_move_iterator(tests_lcase.end()));
  std::vector<FunctionTestCall> tests_ucase = GetFunctionTestsUpper("upper");
  results.insert(results.end(), std::make_move_iterator(tests_ucase.begin()),
                 std::make_move_iterator(tests_ucase.end()));
  return results;
}

std::vector<QueryParamsWithResult> GetFunctionTestsStringConcatOperator() {
  std::vector<QueryParamsWithResult> results;
  // string || string -> string
  // bytes || bytes -> bytes
  // Test the test cases of concat function.
  for (const FunctionTestCall& function_call : GetStringConcatTests()) {
    if (function_call.params.params().size() == 2) {
      results.push_back(function_call.params);
    }
  }
  return results;
}

std::vector<FunctionTestCall> GetFunctionTestsLcase() {
  return GetFunctionTestsLower("lcase");
}
std::vector<FunctionTestCall> GetFunctionTestsUcase() {
  return GetFunctionTestsUpper("ucase");
}

}  // namespace googlesql
