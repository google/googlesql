//
// Copyright 2018 Google LLC
// Copyright 2017 Abseil Authors
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
#ifndef THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_STRING_NUMBERS_H_
#define THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_STRING_NUMBERS_H_

// This file contains string processing functions related to
// numeric values.

#include <cassert>
#include <cstdint>
#include <string>

#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"

namespace googlesql_base {

bool safe_strto32_base(absl::string_view text, int32_t* value, int base);

bool safe_strto64_base(absl::string_view text, int64_t* value, int base);

bool safe_strtou32_base(absl::string_view text, uint32_t* value, int base);

bool safe_strtou64_base(absl::string_view text, uint64_t* value, int base);

std::string RoundTripDoubleToString(double d);
std::string RoundTripFloatToString(float d);

inline int hex_digit_to_int(char c) {
  static_assert('0' == 0x30 && 'A' == 0x41 && 'a' == 0x61,
                "Character set must be ASCII.");
  assert(absl::ascii_isxdigit(c));
  int x = static_cast<unsigned char>(c);
  if (x > '9') {
    x += 9;
  }
  return x & 0xf;
}

inline std::string SimpleBtoa(bool value) {
  return value ? std::string("true") : std::string("false");
}

}  // namespace googlesql_base

#endif  // THIRD_PARTY_GOOGLESQL_GOOGLESQL_BASE_STRING_NUMBERS_H_
