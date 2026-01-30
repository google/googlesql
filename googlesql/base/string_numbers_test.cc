//
// Copyright 2018 Google LLC
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

// This file tests string processing functions related to numeric values.

#include "googlesql/base/string_numbers.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace {

using testing::Eq;
using testing::MatchesRegex;

template <typename IntType>
inline bool Itoa(IntType value, int base, std::string* destination) {
  destination->clear();
  if (base <= 1 || base > 36) {
    return false;
  }

  if (value == 0) {
    destination->push_back('0');
    return true;
  }

  bool negative = value < 0;
  while (value != 0) {
    const IntType next_value = value / base;
    // Can't use std::abs here because of problems when IntType is unsigned.
    int remainder = value > next_value * base ? value - next_value * base
                                              : next_value * base - value;
    char c = remainder < 10 ? '0' + remainder : 'A' + remainder - 10;
    destination->insert(0, 1, c);
    value = next_value;
  }

  if (negative) {
    destination->insert(0, 1, '-');
  }
  return true;
}

struct uint32_test_case {
  const char* str;
  bool expect_ok;
  int base;  // base to pass to the conversion function
  uint32_t expected;
};

inline const std::array<uint32_test_case, 27>& strtouint32_test_cases() {
  static const std::array<uint32_test_case, 27> test_cases{{
      {"0xffffffff", true, 16, std::numeric_limits<uint32_t>::max()},
      {"0x34234324", true, 16, 0x34234324},
      {"34234324", true, 16, 0x34234324},
      {"0", true, 16, 0},
      {" \t\n 0xffffffff", true, 16, std::numeric_limits<uint32_t>::max()},
      {" \f\v 46", true, 10, 46},  // must accept weird whitespace
      {" \t\n 72717222", true, 8, 072717222},
      {" \t\n 072717222", true, 8, 072717222},
      {" \t\n 072717228", false, 8, 07271722},
      {"0", true, 0, 0},

      // Base-10 version.
      {"34234324", true, 0, 34234324},
      {"4294967295", true, 0, std::numeric_limits<uint32_t>::max()},
      {"34234324 \n\t", true, 10, 34234324},

      // Unusual base
      {"0", true, 3, 0},
      {"2", true, 3, 2},
      {"11", true, 3, 4},

      // Invalid uints.
      {"", false, 0, 0},
      {"  ", false, 0, 0},
      {"abc", false, 0, 0},  // would be valid hex, but prefix is missing
      {"34234324a", false, 0, 34234324},
      {"34234.3", false, 0, 34234},
      {"-1", false, 0, 0},
      {"   -123", false, 0, 0},
      {" \t\n -123", false, 0, 0},

      // Out of bounds.
      {"4294967296", false, 0, std::numeric_limits<uint32_t>::max()},
      {"0x100000000", false, 0, std::numeric_limits<uint32_t>::max()},
      {nullptr, false, 0, 0},
  }};
  return test_cases;
}

struct uint64_test_case {
  const char* str;
  bool expect_ok;
  int base;
  uint64_t expected;
};

inline const std::array<uint64_test_case, 34>& strtouint64_test_cases() {
  static const std::array<uint64_test_case, 34> test_cases{{
      {"0x3423432448783446", true, 16, int64_t{0x3423432448783446}},
      {"3423432448783446", true, 16, int64_t{0x3423432448783446}},

      {"0", true, 16, 0},
      {"000", true, 0, 0},
      {"0", true, 0, 0},
      {" \t\n 0xffffffffffffffff", true, 16,
       std::numeric_limits<uint64_t>::max()},

      {"012345670123456701234", true, 8, int64_t{012345670123456701234}},
      {"12345670123456701234", true, 8, int64_t{012345670123456701234}},

      {"12845670123456701234", false, 8, 0},

      // Base-10 version.
      {"34234324487834466", true, 0, int64_t{34234324487834466}},

      {" \t\n 18446744073709551615", true, 0,
       std::numeric_limits<uint64_t>::max()},

      {"34234324487834466 \n\t ", true, 0, int64_t{34234324487834466}},

      {" \f\v 46", true, 10, 46},  // must accept weird whitespace

      // Unusual base
      {"0", true, 3, 0},
      {"2", true, 3, 2},
      {"11", true, 3, 4},

      {"0", true, 0, 0},

      // Invalid uints.
      {"", false, 0, 0},
      {"  ", false, 0, 0},
      {"abc", false, 0, 0},
      {"34234324487834466a", false, 0, 0},
      {"34234487834466.3", false, 0, 0},
      {"-1", false, 0, 0},
      {"   -123", false, 0, 0},
      {" \t\n -123", false, 0, 0},

      // Out of bounds.
      {"18446744073709551616", false, 10, 0},
      {"18446744073709551616", false, 0, 0},
      {"0x10000000000000000", false, 16, std::numeric_limits<uint64_t>::max()},
      {"0X10000000000000000", false, 16,
       std::numeric_limits<uint64_t>::max()},  // 0X versus 0x.
      {"0x10000000000000000", false, 0, std::numeric_limits<uint64_t>::max()},
      {"0X10000000000000000", false, 0,
       std::numeric_limits<uint64_t>::max()},  // 0X versus 0x.

      {"0x1234", true, 16, 0x1234},

      // Base-10 string version.
      {"1234", true, 0, 1234},
      {nullptr, false, 0, 0},
  }};
  return test_cases;
}

TEST(stringtest, safe_strto32_base) {
  int32_t value;
  EXPECT_TRUE(googlesql_base::safe_strto32_base("0x34234324", &value, 16));
  EXPECT_EQ(0x34234324, value);

  EXPECT_TRUE(googlesql_base::safe_strto32_base("0X34234324", &value, 16));
  EXPECT_EQ(0x34234324, value);

  EXPECT_TRUE(googlesql_base::safe_strto32_base("34234324", &value, 16));
  EXPECT_EQ(0x34234324, value);

  EXPECT_TRUE(googlesql_base::safe_strto32_base("0", &value, 16));
  EXPECT_EQ(0, value);

  EXPECT_TRUE(
      googlesql_base::safe_strto32_base(" \t\n -0x34234324", &value, 16));
  EXPECT_EQ(-0x34234324, value);

  EXPECT_TRUE(googlesql_base::safe_strto32_base(" \t\n -34234324", &value, 16));
  EXPECT_EQ(-0x34234324, value);

  EXPECT_TRUE(googlesql_base::safe_strto32_base("7654321", &value, 8));
  EXPECT_EQ(07654321, value);

  EXPECT_TRUE(googlesql_base::safe_strto32_base("-01234", &value, 8));
  EXPECT_EQ(-01234, value);

  EXPECT_FALSE(googlesql_base::safe_strto32_base("1834", &value, 8));

  // Autodetect base.
  EXPECT_TRUE(googlesql_base::safe_strto32_base("0", &value, 0));
  EXPECT_EQ(0, value);

  EXPECT_TRUE(googlesql_base::safe_strto32_base("077", &value, 0));
  EXPECT_EQ(077, value);  // Octal interpretation

  // Leading zero indicates octal, but then followed by invalid digit.
  EXPECT_FALSE(googlesql_base::safe_strto32_base("088", &value, 0));

  // Leading 0x indicated hex, but then followed by invalid digit.
  EXPECT_FALSE(googlesql_base::safe_strto32_base("0xG", &value, 0));

  // Base-10 version.
  EXPECT_TRUE(googlesql_base::safe_strto32_base("34234324", &value, 10));
  EXPECT_EQ(34234324, value);

  EXPECT_TRUE(googlesql_base::safe_strto32_base("0", &value, 10));
  EXPECT_EQ(0, value);

  EXPECT_TRUE(googlesql_base::safe_strto32_base(" \t\n -34234324", &value, 10));
  EXPECT_EQ(-34234324, value);

  EXPECT_TRUE(googlesql_base::safe_strto32_base("34234324 \n\t ", &value, 10));
  EXPECT_EQ(34234324, value);

  // Invalid ints.
  EXPECT_FALSE(googlesql_base::safe_strto32_base("", &value, 10));
  EXPECT_FALSE(googlesql_base::safe_strto32_base("  ", &value, 10));
  EXPECT_FALSE(googlesql_base::safe_strto32_base("abc", &value, 10));
  EXPECT_FALSE(googlesql_base::safe_strto32_base("34234324a", &value, 10));
  EXPECT_FALSE(googlesql_base::safe_strto32_base("34234.3", &value, 10));

  // Out of bounds.
  EXPECT_FALSE(googlesql_base::safe_strto32_base("2147483648", &value, 10));
  EXPECT_FALSE(googlesql_base::safe_strto32_base("-2147483649", &value, 10));

  // String version.
  EXPECT_TRUE(
      googlesql_base::safe_strto32_base(std::string("0x1234"), &value, 16));
  EXPECT_EQ(0x1234, value);

  // Base-10 string version.
  EXPECT_TRUE(googlesql_base::safe_strto32_base("1234", &value, 10));
  EXPECT_EQ(1234, value);
}

TEST(stringtest, safe_strto32_range) {
  // These tests verify underflow/overflow behaviour.
  int32_t value;
  EXPECT_FALSE(googlesql_base::safe_strto32_base("2147483648", &value, 10));
  EXPECT_EQ(std::numeric_limits<int32_t>::max(), value);

  EXPECT_TRUE(googlesql_base::safe_strto32_base("-2147483648", &value, 10));
  EXPECT_EQ(std::numeric_limits<int32_t>::min(), value);

  EXPECT_FALSE(googlesql_base::safe_strto32_base("-2147483649", &value, 10));
  EXPECT_EQ(std::numeric_limits<int32_t>::min(), value);
}

TEST(stringtest, safe_strto64_range) {
  // These tests verify underflow/overflow behaviour.
  int64_t value;
  EXPECT_FALSE(
      googlesql_base::safe_strto64_base("9223372036854775808", &value, 10));
  EXPECT_EQ(std::numeric_limits<int64_t>::max(), value);

  EXPECT_TRUE(
      googlesql_base::safe_strto64_base("-9223372036854775808", &value, 10));
  EXPECT_EQ(std::numeric_limits<int64_t>::min(), value);

  EXPECT_FALSE(
      googlesql_base::safe_strto64_base("-9223372036854775809", &value, 10));
  EXPECT_EQ(std::numeric_limits<int64_t>::min(), value);
}

TEST(stringtest, safe_strto32_leading_substring) {
  // These tests verify this comment in numbers.h:
  // On error, returns false, and sets *value to: [...]
  //   conversion of leading substring if available ("123@@@" -> 123)
  //   0 if no leading substring available
  int32_t value;
  EXPECT_FALSE(googlesql_base::safe_strto32_base("04069@@@", &value, 10));
  EXPECT_EQ(4069, value);

  EXPECT_FALSE(googlesql_base::safe_strto32_base("04069@@@", &value, 8));
  EXPECT_EQ(0406, value);

  EXPECT_FALSE(googlesql_base::safe_strto32_base("04069balloons", &value, 10));
  EXPECT_EQ(4069, value);

  EXPECT_FALSE(googlesql_base::safe_strto32_base("04069balloons", &value, 16));
  EXPECT_EQ(0x4069ba, value);

  EXPECT_FALSE(googlesql_base::safe_strto32_base("@@@", &value, 10));
  EXPECT_EQ(0, value);  // there was no leading substring
}

TEST(stringtest, safe_strto64_leading_substring) {
  // These tests verify this comment in numbers.h:
  // On error, returns false, and sets *value to: [...]
  //   conversion of leading substring if available ("123@@@" -> 123)
  //   0 if no leading substring available
  int64_t value;
  EXPECT_FALSE(googlesql_base::safe_strto64_base("04069@@@", &value, 10));
  EXPECT_EQ(4069, value);

  EXPECT_FALSE(googlesql_base::safe_strto64_base("04069@@@", &value, 8));
  EXPECT_EQ(0406, value);

  EXPECT_FALSE(googlesql_base::safe_strto64_base("04069balloons", &value, 10));
  EXPECT_EQ(4069, value);

  EXPECT_FALSE(googlesql_base::safe_strto64_base("04069balloons", &value, 16));
  EXPECT_EQ(0x4069ba, value);

  EXPECT_FALSE(googlesql_base::safe_strto64_base("@@@", &value, 10));
  EXPECT_EQ(0, value);  // there was no leading substring
}

TEST(stringtest, safe_strto64_base) {
  int64_t value;
  EXPECT_TRUE(
      googlesql_base::safe_strto64_base("0x3423432448783446", &value, 16));
  EXPECT_EQ(int64_t{0x3423432448783446}, value);

  EXPECT_TRUE(
      googlesql_base::safe_strto64_base("3423432448783446", &value, 16));
  EXPECT_EQ(int64_t{0x3423432448783446}, value);

  EXPECT_TRUE(googlesql_base::safe_strto64_base("0", &value, 16));
  EXPECT_EQ(0, value);

  EXPECT_TRUE(googlesql_base::safe_strto64_base(" \t\n -0x3423432448783446",
                                                &value, 16));
  EXPECT_EQ(int64_t{-0x3423432448783446}, value);

  EXPECT_TRUE(
      googlesql_base::safe_strto64_base(" \t\n -3423432448783446", &value, 16));
  EXPECT_EQ(int64_t{-0x3423432448783446}, value);

  EXPECT_TRUE(
      googlesql_base::safe_strto64_base("123456701234567012", &value, 8));
  EXPECT_EQ(int64_t{0123456701234567012}, value);

  EXPECT_TRUE(googlesql_base::safe_strto64_base("-017777777777777", &value, 8));
  EXPECT_EQ(int64_t{-017777777777777}, value);

  EXPECT_FALSE(googlesql_base::safe_strto64_base("19777777777777", &value, 8));

  // Autodetect base.
  EXPECT_TRUE(googlesql_base::safe_strto64_base("0", &value, 0));
  EXPECT_EQ(0, value);

  EXPECT_TRUE(googlesql_base::safe_strto64_base("077", &value, 0));
  EXPECT_EQ(077, value);  // Octal interpretation

  // Leading zero indicates octal, but then followed by invalid digit.
  EXPECT_FALSE(googlesql_base::safe_strto64_base("088", &value, 0));

  // Leading 0x indicated hex, but then followed by invalid digit.
  EXPECT_FALSE(googlesql_base::safe_strto64_base("0xG", &value, 0));

  // Base-10 version.
  EXPECT_TRUE(
      googlesql_base::safe_strto64_base("34234324487834466", &value, 10));
  EXPECT_EQ(int64_t{34234324487834466}, value);

  EXPECT_TRUE(googlesql_base::safe_strto64_base("0", &value, 10));
  EXPECT_EQ(0, value);

  EXPECT_TRUE(googlesql_base::safe_strto64_base(" \t\n -34234324487834466",
                                                &value, 10));
  EXPECT_EQ(int64_t{-34234324487834466}, value);

  EXPECT_TRUE(
      googlesql_base::safe_strto64_base("34234324487834466 \n\t ", &value, 10));
  EXPECT_EQ(int64_t{34234324487834466}, value);

  // Invalid ints.
  EXPECT_FALSE(googlesql_base::safe_strto64_base("", &value, 10));
  EXPECT_FALSE(googlesql_base::safe_strto64_base("  ", &value, 10));
  EXPECT_FALSE(googlesql_base::safe_strto64_base("abc", &value, 10));
  EXPECT_FALSE(
      googlesql_base::safe_strto64_base("34234324487834466a", &value, 10));
  EXPECT_FALSE(
      googlesql_base::safe_strto64_base("34234487834466.3", &value, 10));

  // Out of bounds.
  EXPECT_FALSE(
      googlesql_base::safe_strto64_base("9223372036854775808", &value, 10));
  EXPECT_FALSE(
      googlesql_base::safe_strto64_base("-9223372036854775809", &value, 10));

  // String version.
  EXPECT_TRUE(
      googlesql_base::safe_strto64_base(std::string("0x1234"), &value, 16));
  EXPECT_EQ(0x1234, value);

  // Base-10 string version.
  EXPECT_TRUE(googlesql_base::safe_strto64_base("1234", &value, 10));
  EXPECT_EQ(1234, value);
}

const size_t kNumRandomTests = 10000;

template <typename IntType>
void test_random_integer_parse_base(bool (*parse_func)(absl::string_view,
                                                       IntType* value,
                                                       int base)) {
  using RandomEngine = std::minstd_rand0;
  std::random_device rd;
  RandomEngine rng(rd());
  std::uniform_int_distribution<IntType> random_int(
      std::numeric_limits<IntType>::min());
  std::uniform_int_distribution<int> random_base(2, 35);
  for (size_t i = 0; i < kNumRandomTests; i++) {
    IntType value = random_int(rng);
    int base = random_base(rng);
    std::string str_value;
    EXPECT_TRUE(Itoa<IntType>(value, base, &str_value));
    IntType parsed_value;

    // Test successful parse
    EXPECT_TRUE(parse_func(str_value, &parsed_value, base));
    EXPECT_EQ(parsed_value, value);

    // Test overflow
    EXPECT_FALSE(
        parse_func(absl::StrCat(std::numeric_limits<IntType>::max(), value),
                   &parsed_value, base));

    // Test underflow
    if (std::numeric_limits<IntType>::min() < 0) {
      EXPECT_FALSE(
          parse_func(absl::StrCat(std::numeric_limits<IntType>::min(), value),
                     &parsed_value, base));
    } else {
      EXPECT_FALSE(parse_func(absl::StrCat("-", value), &parsed_value, base));
    }
  }
}

TEST(stringtest, safe_strto32_random) {
  test_random_integer_parse_base<int32_t>(&googlesql_base::safe_strto32_base);
}
TEST(stringtest, safe_strto64_random) {
  test_random_integer_parse_base<int64_t>(&googlesql_base::safe_strto64_base);
}
TEST(stringtest, safe_strtou32_random) {
  test_random_integer_parse_base<uint32_t>(&googlesql_base::safe_strtou32_base);
}
TEST(stringtest, safe_strtou64_random) {
  test_random_integer_parse_base<uint64_t>(&googlesql_base::safe_strtou64_base);
}

TEST(stringtest, safe_strtou32_base) {
  for (int i = 0; strtouint32_test_cases()[i].str != nullptr; ++i) {
    const auto& e = strtouint32_test_cases()[i];
    uint32_t value;
    EXPECT_EQ(e.expect_ok,
              googlesql_base::safe_strtou32_base(e.str, &value, e.base))
        << "str=\"" << e.str << "\" base=" << e.base;
    if (e.expect_ok) {
      EXPECT_EQ(e.expected, value) << "i=" << i << " str=\"" << e.str
                                   << "\" base=" << e.base;
    }
  }
}

TEST(stringtest, safe_strtou32_base_length_delimited) {
  for (int i = 0; strtouint32_test_cases()[i].str != nullptr; ++i) {
    const auto& e = strtouint32_test_cases()[i];
    std::string tmp(e.str);
    tmp.append("12");  // Adds garbage at the end.

    uint32_t value;
    EXPECT_EQ(e.expect_ok,
              googlesql_base::safe_strtou32_base(
                  absl::string_view(tmp.data(), strlen(e.str)), &value, e.base))
        << "str=\"" << e.str << "\" base=" << e.base;
    if (e.expect_ok) {
      EXPECT_EQ(e.expected, value) << "i=" << i << " str=" << e.str
                                   << " base=" << e.base;
    }
  }
}

TEST(stringtest, safe_strtou64_base) {
  for (int i = 0; strtouint64_test_cases()[i].str != nullptr; ++i) {
    const auto& e = strtouint64_test_cases()[i];
    uint64_t value;
    EXPECT_EQ(e.expect_ok,
              googlesql_base::safe_strtou64_base(e.str, &value, e.base))
        << "str=\"" << e.str << "\" base=" << e.base;
    if (e.expect_ok) {
      EXPECT_EQ(e.expected, value) << "str=" << e.str << " base=" << e.base;
    }
  }
}

TEST(stringtest, safe_strtou64_base_length_delimited) {
  for (int i = 0; strtouint64_test_cases()[i].str != nullptr; ++i) {
    const auto& e = strtouint64_test_cases()[i];
    std::string tmp(e.str);
    tmp.append("12");  // Adds garbage at the end.

    uint64_t value;
    EXPECT_EQ(e.expect_ok,
              googlesql_base::safe_strtou64_base(
                  absl::string_view(tmp.data(), strlen(e.str)), &value, e.base))
        << "str=\"" << e.str << "\" base=" << e.base;
    if (e.expect_ok) {
      EXPECT_EQ(e.expected, value) << "str=\"" << e.str << "\" base=" << e.base;
    }
  }
}

TEST(StrToInt32, Partial) {
  struct Int32TestLine {
    std::string input;
    bool status;
    int32_t value;
  };
  const int32_t int32_min = std::numeric_limits<int32_t>::min();
  const int32_t int32_max = std::numeric_limits<int32_t>::max();
  Int32TestLine int32_test_line[] = {
      {"", false, 0},
      {" ", false, 0},
      {"-", false, 0},
      {"123@@@", false, 123},
      {absl::StrCat(int32_min, int32_max), false, int32_min},
      {absl::StrCat(int32_max, int32_max), false, int32_max},
  };

  for (const Int32TestLine& test_line : int32_test_line) {
    int32_t value = -2;
    bool status =
        googlesql_base::safe_strto32_base(test_line.input, &value, 10);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
    value = -2;
    status = googlesql_base::safe_strto32_base(test_line.input, &value, 10);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
    value = -2;
    status = googlesql_base::safe_strto32_base(
        absl::string_view(test_line.input), &value, 10);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
  }
}

TEST(StrToUint32, Partial) {
  struct Uint32TestLine {
    std::string input;
    bool status;
    uint32_t value;
  };
  const uint32_t uint32_max = std::numeric_limits<uint32_t>::max();
  Uint32TestLine uint32_test_line[] = {
      {"", false, 0},
      {" ", false, 0},
      {"-", false, 0},
      {"123@@@", false, 123},
      {absl::StrCat(uint32_max, uint32_max), false, uint32_max},
  };

  for (const Uint32TestLine& test_line : uint32_test_line) {
    uint32_t value = 2;
    bool status =
        googlesql_base::safe_strtou32_base(test_line.input, &value, 10);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
    value = 2;
    status = googlesql_base::safe_strtou32_base(test_line.input, &value, 10);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
    value = 2;
    status = googlesql_base::safe_strtou32_base(
        absl::string_view(test_line.input), &value, 10);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
  }
}

TEST(StrToInt64, Partial) {
  struct Int64TestLine {
    std::string input;
    bool status;
    int64_t value;
  };
  const int64_t int64_min = std::numeric_limits<int64_t>::min();
  const int64_t int64_max = std::numeric_limits<int64_t>::max();
  Int64TestLine int64_test_line[] = {
      {"", false, 0},
      {" ", false, 0},
      {"-", false, 0},
      {"123@@@", false, 123},
      {absl::StrCat(int64_min, int64_max), false, int64_min},
      {absl::StrCat(int64_max, int64_max), false, int64_max},
  };

  for (const Int64TestLine& test_line : int64_test_line) {
    int64_t value = -2;
    bool status =
        googlesql_base::safe_strto64_base(test_line.input, &value, 10);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
    value = -2;
    status = googlesql_base::safe_strto64_base(test_line.input, &value, 10);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
    value = -2;
    status = googlesql_base::safe_strto64_base(
        absl::string_view(test_line.input), &value, 10);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
  }
}

TEST(StrToUint64, Partial) {
  struct Uint64TestLine {
    std::string input;
    bool status;
    uint64_t value;
  };
  const uint64_t uint64_max = std::numeric_limits<uint64_t>::max();
  Uint64TestLine uint64_test_line[] = {
      {"", false, 0},
      {" ", false, 0},
      {"-", false, 0},
      {"123@@@", false, 123},
      {absl::StrCat(uint64_max, uint64_max), false, uint64_max},
  };

  for (const Uint64TestLine& test_line : uint64_test_line) {
    uint64_t value = 2;
    bool status =
        googlesql_base::safe_strtou64_base(test_line.input, &value, 10);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
    value = 2;
    status = googlesql_base::safe_strtou64_base(test_line.input, &value, 10);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
    value = 2;
    status = googlesql_base::safe_strtou64_base(
        absl::string_view(test_line.input), &value, 10);
    EXPECT_EQ(test_line.status, status) << test_line.input;
    EXPECT_EQ(test_line.value, value) << test_line.input;
  }
}

TEST(StrToInt32Base, PrefixOnly) {
  struct Int32TestLine {
    std::string input;
    bool status;
    int32_t value;
  };
  Int32TestLine int32_test_line[] = {
    { "", false, 0 },
    { "-", false, 0 },
    { "-0", true, 0 },
    { "0", true, 0 },
    { "0x", false, 0 },
    { "-0x", false, 0 },
  };
  const int base_array[] = { 0, 2, 8, 10, 16 };

  for (const Int32TestLine& line : int32_test_line) {
    for (const int base : base_array) {
      int32_t value = 2;
      bool status =
          googlesql_base::safe_strto32_base(line.input.c_str(), &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
      value = 2;
      status = googlesql_base::safe_strto32_base(line.input, &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
      value = 2;
      status = googlesql_base::safe_strto32_base(absl::string_view(line.input),
                                                 &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
    }
  }
}

TEST(StrToUint32Base, PrefixOnly) {
  struct Uint32TestLine {
    std::string input;
    bool status;
    uint32_t value;
  };
  Uint32TestLine uint32_test_line[] = {
    { "", false, 0 },
    { "0", true, 0 },
    { "0x", false, 0 },
  };
  const int base_array[] = { 0, 2, 8, 10, 16 };

  for (const Uint32TestLine& line : uint32_test_line) {
    for (const int base : base_array) {
      uint32_t value = 2;
      bool status =
          googlesql_base::safe_strtou32_base(line.input.c_str(), &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
      value = 2;
      status = googlesql_base::safe_strtou32_base(line.input, &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
      value = 2;
      status = googlesql_base::safe_strtou32_base(absl::string_view(line.input),
                                                  &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
    }
  }
}

TEST(StrToInt64Base, PrefixOnly) {
  struct Int64TestLine {
    std::string input;
    bool status;
    int64_t value;
  };
  Int64TestLine int64_test_line[] = {
    { "", false, 0 },
    { "-", false, 0 },
    { "-0", true, 0 },
    { "0", true, 0 },
    { "0x", false, 0 },
    { "-0x", false, 0 },
  };
  const int base_array[] = { 0, 2, 8, 10, 16 };

  for (const Int64TestLine& line : int64_test_line) {
    for (const int base : base_array) {
      int64_t value = 2;
      bool status =
          googlesql_base::safe_strto64_base(line.input.c_str(), &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
      value = 2;
      status = googlesql_base::safe_strto64_base(line.input, &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
      value = 2;
      status = googlesql_base::safe_strto64_base(absl::string_view(line.input),
                                                 &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
    }
  }
}

TEST(StrToUint64Base, PrefixOnly) {
  struct Uint64TestLine {
    std::string input;
    bool status;
    uint64_t value;
  };
  Uint64TestLine uint64_test_line[] = {
    { "", false, 0 },
    { "0", true, 0 },
    { "0x", false, 0 },
  };
  const int base_array[] = { 0, 2, 8, 10, 16 };

  for (const Uint64TestLine& line : uint64_test_line) {
    for (const int base : base_array) {
      uint64_t value = 2;
      bool status =
          googlesql_base::safe_strtou64_base(line.input.c_str(), &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
      value = 2;
      status = googlesql_base::safe_strtou64_base(line.input, &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
      value = 2;
      status = googlesql_base::safe_strtou64_base(absl::string_view(line.input),
                                                  &value, base);
      EXPECT_EQ(line.status, status) << line.input << " " << base;
      EXPECT_EQ(line.value, value) << line.input << " " << base;
    }
  }
}

}  // namespace
